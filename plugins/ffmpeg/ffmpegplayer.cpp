#include "ffmpegplayer.h"

FFmpegPlayer::FFmpegPlayer(QObject *parent)
    : AudioPlayer(parent)
{
    m_positionTimer.setInterval(250);
    connect(&m_positionTimer, &QTimer::timeout, this,
            &FFmpegPlayer::reportPosition);
}

FFmpegPlayer::~FFmpegPlayer()
{
    stopPlayback();
}

void FFmpegPlayer::play(const MediaItem &item)
{
    stopPlayback();

    const QString scheme = item.source.scheme();
    if (scheme != QLatin1String("file") && scheme != QLatin1String("http")
            && scheme != QLatin1String("https")) {
        emit errorOccurred(
            QStringLiteral("Unsupported source scheme “%1”").arg(scheme));
        return;
    }

    // No parent: the worker moves to its own thread (a QObject with a
    // parent cannot), and this object owns it (deleted in stopPlayback
    // once the thread is joined).
    m_worker = new DecodeWorker;
    wireWorker(m_worker);
    connect(m_worker, &DecodeWorker::decodeError, this,
            &FFmpegPlayer::onDecodeError);
    m_worker->start(item.source, &m_ring, kOutputRate, kCurrentWriterId);

    // The new current is not done: the gate closes (a queued worker
    // started meanwhile stages until this decode's eos).
    m_currentDone = false;
    // A fresh session starts from the ring's read count at the
    // stopPlayback's drain (the pump cannot advance the count in
    // between — the ring is empty and the writer down).
    m_segStartReads = m_ringDrainReads;
    m_offsetFrames = 0;
}

void FFmpegPlayer::wireWorker(DecodeWorker *worker)
{
    // The worker pointer is captured: a queued signal of a superseded
    // track (its worker was joined and deleted, but the signal was
    // already in flight) must not land on its successor.
    connect(worker, &DecodeWorker::opened, this,
            [this, worker](int sampleRate, const QVariantMap &info) {
                if (worker != m_worker)
                    return;
                onOpened(sampleRate, info);
            });
    connect(worker, &DecodeWorker::eos, this,
            [this, worker] {
                if (worker != m_worker)
                    return;
                onEos();
            });
    connect(worker, &DecodeWorker::ringDrained, this,
            &FFmpegPlayer::onRingDrained);
}

DecodeWorker *FFmpegPlayer::freshWorker(const QUrl &source, qint64 offsetMs)
{
    // The re-decode of a corpse (the play-out window): a corpse has no
    // consumer for a seek / pause — its replacement decodes the source
    // from the operation's point (the initial seek's drain re-anchors
    // the segment through ringDrained).
    retireWorker(m_worker);
    m_worker = new DecodeWorker;
    wireWorker(m_worker);
    connect(m_worker, &DecodeWorker::decodeError, this,
            &FFmpegPlayer::onDecodeError);
    m_currentDone = false;
    m_offsetFrames = 0;  // the position runs from the re-decoded stretch
    if (!m_worker->start(source, &m_ring, kOutputRate, kCurrentWriterId,
                         nullptr, 0, offsetMs)) {
        retireWorker(m_worker);
        m_worker = nullptr;
    }
    return m_worker;
}

PulseOut *FFmpegPlayer::createOutput()
{
    return new PulseOut(this);
}

void FFmpegPlayer::onOpened(int sampleRate, const QVariantMap &info)
{
    m_rate = sampleRate;  // the fixed output rate (the worker resamples)
    // What the driver actually decoded (the codec, container, the
    // source's real bitrate / rate / channels) — the player's own
    // streamInfo.
    emit streamInfo(info);
    emit durationChanged(m_worker != nullptr ? m_worker->durationMs() : 0);

    // The output is persistent (the gapless handoff's contract): it
    // comes up once per play session and survives the track boundary.
    // It is created after the source is open (the rate is known — the
    // fixed one); the ring's headroom covers the stream connection
    // time. The stream itself comes up asynchronously (ready/failed,
    // queued to this thread) — a watchdog covers a no-show.
    if (m_output == nullptr) {
        m_outputReady = false;
        m_output = createOutput();
        // A fresh output starts from the user's levels (the setVolume/
        // setMute/setBoost before the output existed only stored them;
        // the sink's volume and mute are re-applied by the output when
        // its stream comes up).
        m_output->setBoost(m_boost);
        m_output->setSinkVolume(m_volume);
        m_output->setSinkMute(m_muted);
        connect(m_output, &PulseOut::ready, this, &FFmpegPlayer::onOutputReady);
        connect(m_output, &PulseOut::failed, this,
                &FFmpegPlayer::onOutputFailed);
        // The audio tap: the output is the only place that sees the
        // frames actually played, so its levels/waveform/raw frames
        // become this driver's. Cross-thread (the mainloop → the GUI),
        // so queued with a copy.
        connect(m_output, &PulseOut::audioLevels, this,
                &AudioPlayer::audioLevels);
        connect(m_output, &PulseOut::audioWaveform, this,
                &AudioPlayer::audioWaveform);
        connect(m_output, &PulseOut::audioFrames, this,
                &AudioPlayer::audioFrames);
        // The sink's volume and mute, read back from the server (the
        // initial sync, an external client's change): re-emitted as
        // this driver's, so the manager follows the server's state.
        connect(m_output, &PulseOut::sinkVolumeChanged, this,
                &AudioPlayer::sinkVolumeChanged);
        connect(m_output, &PulseOut::sinkMuteChanged, this,
                &AudioPlayer::sinkMuteChanged);
        if (!m_output->start(&m_ring, kOutputRate)) {
            emit errorOccurred(QStringLiteral("Cannot start the audio output"));
            stopPlayback();
            return;
        }
        // The watchdog is per output: a track that ends before five
        // seconds must not be judged by its predecessor's timer.
        PulseOut *output = m_output;
        QTimer::singleShot(5000, this, [this, output] {
            if (output == m_output && !m_outputReady) {
                emit errorOccurred(QStringLiteral("The audio output did not come up"));
                stopPlayback();
            }
        });
    }
}

void FFmpegPlayer::onOutputReady()
{
    m_outputReady = true;
    m_positionTimer.start();
}

void FFmpegPlayer::onEos()
{
    // The current item's decode is done: its last frame is written
    // (the gate's ordering guarantee) — the gate opens (a parked
    // corpse's drain-wait ends, a still-decoding gated worker flows
    // into the ring), and the stretch's end is known (the ring read
    // count at which its last frame leaves the ring). The boundary
    // fires in the position timer — the ring's tail must play out
    // first (a short track decodes in milliseconds; the output keeps
    // running, the worker's thread is simply done).
    m_currentDone = true;
    noteStretchEnd();
}

void FFmpegPlayer::onDecodeError(const QString &message)
{
    stopPlayback();  // joins the worker's thread and deletes it
    emit errorOccurred(message);
}

void FFmpegPlayer::onOutputFailed(const QString &message)
{
    stopPlayback();
    emit errorOccurred(message);
}

void FFmpegPlayer::pause()
{
    if (m_worker == nullptr)
        return;
    // The play-out window (the current is a corpse — its tail is in
    // the ring): the boundary resolves at the pause (the handoff —
    // finished fires, the manager's early-armed no-op, before its
    // Paused state), and the pause applies to the promoted worker. A
    // corpse (the current with no queued worker, or the promoted one)
    // has no consumer for the pause's re-seek — it is re-decoded from
    // the pause point, so the resume continues it.
    bool redecoded = false;
    if (m_worker->eosSeen()) {
        if (m_queued != nullptr) {
            doBoundary();
            if (m_worker == nullptr)
                return;
        }
        if (m_worker->eosSeen()) {
            const QUrl source = m_worker->source();
            const qint64 at = positionMs();
            redecoded = freshWorker(source, at) != nullptr;
            if (!redecoded)
                return;
        }
    }
    m_worker->setPaused(true);
    // The position is frozen while paused (the silence is never
    // counted).
    m_positionTimer.stop();
    // The decode runs ahead of the position (the ring is full of audio
    // the output has not pulled): rewind the worker to the position
    // and drop the ring, so the silence lands now and the resume starts
    // exactly at the pause point. The re-decoded worker is already at
    // the point (its initial seek) — no second drain.
    if (!redecoded)
        m_worker->seekToMs(positionMs());
    m_ringDrainReads = m_ring.drain();
    // The queued worker's buffered frames go with the drain; its decode
    // restarts from the top on the resume.
    killQueued();
}

void FFmpegPlayer::resume()
{
    if (m_worker == nullptr)
        return;
    // The pause drained the ring (the re-seek the worker applies on the
    // first unpause drains it again — onRingDrained re-anchors the
    // same way): the stretch starts at that drain's read count. The
    // position does not move — the silence the output pulled while
    // paused was never counted.
    m_segStartReads = m_ringDrainReads;
    m_worker->setPaused(false);
    m_positionTimer.start();
    // The queued worker's buffer went with the drain: a fresh decode
    // (the remembered item's source is the check — it is set by
    // startQueued and cleared by stopPlayback).
    if (!m_queuedItem.source.isEmpty())
        startQueued(m_queuedItem);
}

void FFmpegPlayer::stop()
{
    stopPlayback();
}

void FFmpegPlayer::stopPlayback()
{
    m_positionTimer.stop();
    retireWorker(m_worker);
    m_worker = nullptr;
    killQueued();
    if (m_output != nullptr) {
        // Discard the queued ready/failed of the thread we are about
        // to join, so a stale emission can never land on (and kill)
        // the next session's output.
        disconnect(m_output);
        m_output->stop();
        delete m_output;
        m_output = nullptr;
    }
    m_rate = 0;
    m_outputReady = false;
    m_currentDone = false;  // no current: the gate is closed
    m_ringDrainReads = m_ring.drain();  // also clears the stop flag (next play)
    m_queuedItem = MediaItem();
    m_boundaryReadsPending = false;
    m_offsetFrames = 0;
}

void FFmpegPlayer::retireWorker(DecodeWorker *worker)
{
    if (worker == nullptr)
        return;
    worker->stop();  // joins the decode thread
    delete worker;   // safe: the thread has been joined
}

qint64 FFmpegPlayer::playedFrames() const
{
    return m_output != nullptr ? m_output->playedFrames() : 0;
}

qint64 FFmpegPlayer::positionMs() const
{
    if (m_output == nullptr || m_rate == 0)
        return 0;
    const qint64 frames = playedFrames() - m_offsetFrames;
    if (frames <= 0)
        return 0;
    return static_cast<qint64>(static_cast<double>(frames) * 1000.0 / m_rate);
}

qint64 FFmpegPlayer::durationMs() const
{
    return m_worker != nullptr ? m_worker->durationMs() : 0;
}

void FFmpegPlayer::setPositionMs(qint64 positionMs)
{
    if (m_worker == nullptr || m_rate == 0)
        return;
    const qint64 maxMs = m_worker->durationMs();
    if (maxMs > 0)
        positionMs = qBound<qint64>(0, positionMs, maxMs - 1);

    // The play-out window (the current is a corpse — its tail is in
    // the ring, the display already shows the armed item): the seek
    // is for the promoted item — the boundary resolves at the seek
    // (the handoff — finished fires, the manager's early-armed
    // no-op), and the seek applies to the promoted worker. Without
    // this the seek would be a silent no-op (a corpse has no consumer
    // for it) and the boundary would never re-fire.
    if (m_worker->eosSeen() && m_queued != nullptr) {
        doBoundary();
        if (m_worker == nullptr)
            return;
    }

    if (m_output != nullptr)
        m_output->setPlayedFrames(static_cast<qint64>(
            static_cast<double>(positionMs) * m_rate / 1000.0));
    // The position runs from the seek itself (the output's count is
    // the seek): the item's offset resets — a seek in a later track
    // must not read the position from the session start.
    m_offsetFrames = 0;
    // The seek's drain takes the queued worker's buffered frames with
    // it; the manager re-queues the handoff (queueNext is idempotent).
    // The pending boundary dies with the stretch: it was computed for
    // the frames the seek just discarded (the worker's counter reset
    // with the drain, and the re-decoded tail would re-cross the old
    // count early) — the re-decode's eos notes the new one.
    killQueued();
    m_boundaryReadsPending = false;
    m_currentDone = false;

    if (m_worker->eosSeen()) {
        // The promoted worker is a corpse too (a short one, fully
        // staged): its seek would have no consumer — a fresh decode
        // from the seek point (its initial seek drains the ring and
        // re-anchors the segment).
        const QUrl source = m_worker->source();
        if (freshWorker(source, positionMs) == nullptr)
            return;
        return;
    }
    m_worker->seekToMs(positionMs);
}

bool FFmpegPlayer::queueNext(const MediaItem &item)
{
    // The source is all the driver needs (the manager arms with the
    // playlist's item, which is valid; the guard is on the source, not
    // the id, so a source-only item is queued too). The answer is the
    // contract (docs/components.md): true only when the item is
    // really queued — the manager's boundary decision rests on it.
    if (m_worker == nullptr || item.source.isEmpty())
        return false;  // nothing playing: no handoff to arm
    if (m_queued != nullptr && m_queued->source() == item.source)
        return true;  // idempotent (the manager's re-queue after a seek)
    const QString scheme = item.source.scheme();
    if (scheme != QLatin1String("file") && scheme != QLatin1String("http")
            && scheme != QLatin1String("https")) {
        // The manager armed the display for this item: the error marks
        // it (it is the playing item) and stops.
        emit errorOccurred(
            QStringLiteral("Unsupported source scheme “%1”").arg(scheme));
        return false;
    }
    return startQueued(item);
}

bool FFmpegPlayer::startQueued(const MediaItem &item)
{
    killQueued();
    m_queuedItem = item;
    // The queued decode is GATED (the ring's order invariant —
    // docs/plugins.md): its frames sit in a private staging (as big
    // as the ring — the boundary accounting's invariant) until the
    // gate opens on the current decode's eos, so the ring's FIFO order
    // is the current item's tail then the queued item's head, never
    // interleaved. Decode runs faster than real time: by the boundary
    // the ring holds the current item's tail and the queued item's
    // head, and the output never underruns (a drained ring in the
    // rare slow-decode case is a gap — v1 accepted).
    m_queued = new DecodeWorker;
    // The worker pointer is captured: a queued signal of a superseded
    // worker (killed by a seek / pause, deleted, the signal in flight)
    // must not land on its successor.
    connect(m_queued, &DecodeWorker::decodeError, this,
            [this, worker = m_queued](const QString &message) {
                if (worker != m_queued)
                    return;
                killQueued();
                // The queued item is unplayable: the current one keeps
                // playing until the manager's stop (onPlayerError).
                emit errorOccurred(message);
            });
    if (!m_queued->start(item.source, &m_ring, kOutputRate, kQueuedWriterId,
                         &m_currentDone)) {
        killQueued();
        return false;
    }
    // The current item's decode is already done (a short track) and
    // its boundary was not noted yet: it is known now — the queued
    // frames follow its last.
    if (m_worker != nullptr && m_worker->eosSeen() && !m_boundaryReadsPending)
        noteStretchEnd();
    return true;
}

bool FFmpegPlayer::killQueued()
{
    if (m_queued == nullptr)
        return false;
    m_queued->stop();  // joins the decode thread (a parked worker wakes
                       // on the stop — its drain loop breaks on it)
    delete m_queued;   // safe: the thread has been joined
    m_queued = nullptr;
    return true;
}

void FFmpegPlayer::noteStretchEnd()
{
    // All of the current item's stretch frames are written (its decode
    // hit its eos); the stretch's reads started at m_segStartReads.
    // The boundary (the ring read count at which its last frame leaves
    // the ring) is their sum — a read count, so the silence padding and
    // any DSP drops cannot skew it.
    if (m_boundaryReadsPending)
        return;
    m_boundaryReads = m_segStartReads + m_worker->framesWritten();
    m_boundaryReadsPending = true;
}

void FFmpegPlayer::doBoundary()
{
    m_boundaryReadsPending = false;
    if (m_queued != nullptr) {
        doHandoff();
        return;
    }
    // The play-out is over (no queued item): the plain end.
    stopPlayback();
    emit finished();
}

void FFmpegPlayer::doHandoff()
{
    // The boundary is here: from this read on the ring holds only the
    // queued item's audio. The position's offset is the output's count
    // minus the queued frames already read out (a no-DSP handoff: the
    // output's count at the boundary itself).
    m_offsetFrames = playedFrames()
        - static_cast<qint64>(m_ring.framesRead() - m_boundaryReads);
    // The promoted stretch's anchor: at the natural boundary the
    // boundary's own count (framesRead has just crossed it); in the
    // play-out window (a seek / pause resolving the boundary early —
    // framesRead is still short of it) the present read count, which
    // the operation's drain re-anchors onRingDrained's way (a boundary
    // count the ring never reaches would make the next boundary
    // unreachable).
    m_segStartReads = qMin(m_ring.framesRead(), m_boundaryReads);
    m_boundaryReadsPending = false;

    retireWorker(m_worker);
    m_worker = m_queued;
    m_queued = nullptr;
    m_queuedItem = MediaItem();  // the promoted item is current now
    // The promotion clears the gated flag: the promoted worker is the
    // current — a live one writes straight into the ring from here on
    // (a gate that closed on it would detour its mid-track frames to
    // the staging — silence mid-track, a burst at the end). The gate
    // itself reflects the new state: open iff the promoted decode is
    // already done (the corpse's drain-wait ends there), closed for
    // the next queued worker.
    if (m_worker != nullptr)
        m_worker->clearGated();
    m_currentDone = m_worker != nullptr && m_worker->eosSeen();
    // The promoted worker's frames are in the ring (or, after a
    // pause-restart, on their way — the position simply stays put at
    // zero until the output counts them: the silence was never
    // counted).
    m_offsetFrames = qMax<qint64>(m_offsetFrames, 0);
    if (m_worker != nullptr) {
        // The promoted item's own stretch's end is the next boundary:
        // a live worker's eos notes it (its eos was never wired —
        // startQueued carries only the error — so wire it now, with
        // the same pointer-capture guard as play()); a corpse's (its
        // decode already ran to its end) is known now.
        if (m_worker->eosSeen()) {
            noteStretchEnd();
        } else {
            wireWorker(m_worker);
        }
        // The promoted item's container takes over the display's
        // numbers; the tap and the position signals continue
        // unbroken (the output is the same).
        emit durationChanged(m_worker->durationMs());
        emit streamInfo(m_worker->streamInfo());
    }
    emit finished();
}

void FFmpegPlayer::reportPosition()
{
    if (m_boundaryReadsPending
            && m_ring.framesRead() >= m_boundaryReads)
        doBoundary();
    emit positionChanged(positionMs());
}

void FFmpegPlayer::onRingDrained(uint64_t readsAtDrain)
{
    // The current worker's seek drained the ring: the present stretch
    // of its frames starts now (its counter reset with the drain), and
    // so does the segment's anchor — at the drain's own count (by the
    // time this queued signal lands, a pump pass may have already
    // pulled re-decoded frames past it). Forward-only: a drain's read
    // count is always past the current anchor (the re-decode's
    // initial-seek drain of a slow open can land after a later GUI
    // anchor — the newest drain is the stretch's start, an old one
    // must not move it back).
    m_segStartReads = qMax(m_segStartReads, readsAtDrain);
}

bool FFmpegPlayer::supportsVolume() const
{
    // The output sets the stream's sink input volume on the server.
    return true;
}

void FFmpegPlayer::setVolume(float volume)
{
    m_volume = volume;
    if (m_output != nullptr)
        m_output->setSinkVolume(volume);
}

bool FFmpegPlayer::supportsMute() const
{
    // The output sets the stream's sink input mute on the server.
    return true;
}

void FFmpegPlayer::setMute(bool muted)
{
    m_muted = muted;
    if (m_output != nullptr)
        m_output->setSinkMute(muted);
}

bool FFmpegPlayer::supportsBoost() const
{
    // The output scales the frames it hands to the server.
    return true;
}

void FFmpegPlayer::setBoost(float boost)
{
    m_boost = boost;
    if (m_output != nullptr)
        m_output->setBoost(boost);
}
