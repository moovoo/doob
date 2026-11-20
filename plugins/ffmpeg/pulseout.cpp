#include "pulseout.h"

#include <sys/syscall.h>

#include "audiotap.h"

void PulseLoopThread::run()
{
    m_owner->pump();
}

PulseOut::PulseOut(QObject *parent)
    : QObject(parent)
{
    // The tap signals cross the mainloop → GUI boundary by queued
    // delivery, which serializes the waveform payload: the type must be
    // a registered metatype for the hop to work.
    qRegisterMetaType<QVector<float>>();
}

PulseOut::~PulseOut()
{
    stop();
}

bool PulseOut::start(RingBuffer *ring, int sampleRate)
{
    if (m_running.exchange(true))
        return false;
    m_ring = ring;
    m_rate = sampleRate;
    // The chain's snapshot re-tunes its processors to the stream's
    // rate (a fresh stream starts flat — the processors apply their
    // stored state themselves).
    m_chain.start(sampleRate, 2);
    m_playedFrames = 0;
    m_quit = false;
    // The tap's state (a fresh stream starts fresh — the thread starts
    // below, so nothing reads these on another thread yet).
    m_tapDelay.clear();
    m_latencyMs = 0;
    m_forcedDelayMs = -1;
    m_tapClock.start();
    // QThread::start (Qt6) reports no failure: the m_running guard
    // above keeps a double start from ever happening.
    m_thread.start(QThread::HighPriority);
    return true;
}

void PulseOut::stop()
{
    if (!m_running.exchange(false))
        return;
    m_quit = true;
    // Wake the loop as soon as it exists (the pump creates it right
    // away; if the pump has not entered the loop yet, the quit flag
    // above keeps it from ever entering). The quit goes under the
    // mutex so it cannot land on a mainloop the pump already took
    // back and freed. Then join — the PulseAudio objects are freed
    // on the loop's own thread.
    bool woken = false;
    int waited = 0;
    while (!woken && waited < 2000) {
        {
            QMutexLocker lock{&m_loopMutex};
            if (m_mainloop != nullptr) {
                pa_mainloop_quit(m_mainloop, 0);
                woken = true;
            } else if (m_thread.isFinished()) {
                woken = true;
            }
        }
        if (!woken) {
            QThread::msleep(1);
            waited += 1;
        }
    }
    m_thread.wait();
    // The thread is joined: no pump runs any more, and the
    // processors' stop() (a per-track reset for the dropping ones)
    // lands without racing one.
    m_chain.stop();
    m_ring = nullptr;
}

void PulseOut::pump()
{
    // The loop's own thread id (tests measure its CPU: the loop must
    // park on its timeout between pumps, not spin).
    // QThread::currentThreadId() is a pointer on this Qt, not the
    // /proc tid — use gettid() directly.
    m_threadId = static_cast<quint64>(syscall(SYS_gettid));
    if (m_quit.load())
        return;
    {
        QMutexLocker lock{&m_loopMutex};
        m_mainloop = pa_mainloop_new();
    }
    m_context = pa_context_new(pa_mainloop_get_api(m_mainloop), "doob");
    pa_context_set_state_callback(m_context,
                                  &PulseOut::onContextStateTrampoline, this);
    pa_context_set_subscribe_callback(m_context,
                                      &PulseOut::onSubscribeTrampoline, this);
    // The default server (PULSE_SERVER, else the user's default) —
    // the connect argument is a server address, not the client name.
    pa_context_connect(m_context, nullptr, PA_CONTEXT_NOFLAGS, nullptr);

    // Self-paced: prepare parks the subsequent poll at most 20 ms
    // (the timeout is in microseconds; -1 would block until an event
    // lands) and every wake-up — a real event or the expiry — pumps.
    // The wake-up must never depend on the GUI thread: this loop is
    // the only thing feeding the server buffer, and the GUI can be
    // busy for hundreds of milliseconds (a style sheet re-polish)
    // without the audio being allowed to stop. The wake-up on stop is
    // pa_mainloop_quit() (prepare answers it with a negative return).
    while (!m_quit.load()) {
        if (pa_mainloop_prepare(m_mainloop, 20 * 1000) < 0)
            break;
        if (pa_mainloop_poll(m_mainloop) < 0)
            break;
        if (pa_mainloop_dispatch(m_mainloop) < 0)
            break;
        pumpAudio();
    }
    // The tap's queue dies with the loop (the next stream starts fresh,
    // and stop() on the GUI thread must not touch it).
    m_tapDelay.clear();
    // Take the mainloop back before freeing it, so stop()'s quit
    // can no longer reach it.
    pa_mainloop *mainloop = nullptr;
    {
        QMutexLocker lock{&m_loopMutex};
        std::swap(mainloop, m_mainloop);
    }
    // Clear the state and subscription callbacks before the unref:
    // dropping the last reference transitions the objects to
    // TERMINATED, which would fire them and report a clean stop as a
    // failure.
    m_sinkIndex = PA_INVALID_INDEX;
    if (m_stream != nullptr) {
        pa_stream_set_state_callback(m_stream, nullptr, nullptr);
        pa_stream_disconnect(m_stream);
        pa_stream_unref(m_stream);
        m_stream = nullptr;
    }
    if (m_context != nullptr) {
        pa_context_set_state_callback(m_context, nullptr, nullptr);
        pa_context_set_subscribe_callback(m_context, nullptr, nullptr);
        pa_context_disconnect(m_context);
        pa_context_unref(m_context);
        m_context = nullptr;
    }
    pa_mainloop_free(mainloop);
}

void PulseOut::onContextStateTrampoline(struct pa_context *context, void *data)
{
    static_cast<PulseOut *>(data)->onContextState(context);
}

void PulseOut::onStreamStateTrampoline(struct pa_stream *stream, void *data)
{
    static_cast<PulseOut *>(data)->onStreamState(stream);
}

void PulseOut::onContextState(struct pa_context *context)
{
    // The teardown is in progress (stop() set the flag before the
    // unref below fired this): a stop is not a failure.
    if (m_quit.load())
        return;
    switch (pa_context_get_state(context)) {
    case PA_CONTEXT_READY: {
        // Watch the server's sink-input events: a NEW/CHANGE of this
        // stream's own sink input (another client of the server —
        // pavucontrol — touched its volume) is read back and reported
        // as sinkVolumeChanged/sinkMuteChanged (the server is the
        // source of truth for the level).
        pa_context_subscribe(context, PA_SUBSCRIPTION_MASK_SINK_INPUT,
                             nullptr, nullptr);

        pa_sample_spec spec{};
        spec.format = PA_SAMPLE_FLOAT32LE;
        spec.rate = static_cast<uint32_t>(m_rate);
        spec.channels = 2;

        // The server uses the standard stereo map for 2 channels.
        m_stream = pa_stream_new(context, "doob-playback", &spec, nullptr);
        if (m_stream == nullptr) {
            emit failed(QStringLiteral("PulseAudio: cannot create the stream"));
            return;
        }
        pa_stream_set_state_callback(m_stream,
                                     &PulseOut::onStreamStateTrampoline, this);
        // No explicit buffer parameters: this libpulse honors the
        // server-chosen ones, but with a client-supplied attribute
        // the client's own copy never lands and begin_write() stays
        // stuck. The pump paces itself against the server's buffer.
        // The timing flags feed the tap's latency shift: without them
        // the server pushes no timing info and pa_stream_get_latency
        // stays NODATA forever — AUTO updates it every 100 ms,
        // INTERPOLATE keeps the estimate current between updates.
        // EARLY_REQUESTS (with the server's ~20 ms minreq) makes the
        // server push a write request every ~20 ms of audio: the
        // steady stream of requests keeps the server's main loop —
        // and its periodic timing updates for this stream — regular
        // even when doob is the only client (an idle server batches
        // them, the measured latency jumps in steps, and the tap's
        // release gaps and bursts with it). The pump ignores the
        // requests (it self-paces against writable); only the
        // server-side cadence matters.
        const pa_stream_flags_t flags = static_cast<pa_stream_flags_t>(
            PA_STREAM_AUTO_TIMING_UPDATE | PA_STREAM_INTERPOLATE_TIMING
            | PA_STREAM_EARLY_REQUESTS);
        if (pa_stream_connect_playback(m_stream, nullptr, nullptr, flags,
                                       nullptr, nullptr) < 0) {
            emit failed(QStringLiteral("PulseAudio: cannot connect the stream"));
        }
        break;
    }
    case PA_CONTEXT_FAILED:
    case PA_CONTEXT_TERMINATED:
        emit failed(QStringLiteral("PulseAudio: connection failed"));
        break;
    default:
        break;
    }
}

void PulseOut::onStreamState(struct pa_stream *stream)
{
    if (m_quit.load())
        return;  // see onContextState
    switch (pa_stream_get_state(stream)) {
    case PA_STREAM_READY:
        // The stream's sink input exists on the server: remember its
        // index (the subscription's events are matched against it) and
        // apply the stored sink volume and mute — the initial sync (a
        // fresh sink input starts at the server's default, not the
        // user's level). The read-back is an explicit query, not the
        // subscription's creation events: those can land before this
        // callback (the index is unknown then and they are dropped),
        // and a no-op set (the stored value is the server's default)
        // raises no event at all — the query, queued after the sets,
        // reports the values that landed on the server.
        m_sinkIndex = pa_stream_get_index(stream);
        applySinkVolume();
        applySinkMute();
        pa_context_get_sink_input_info(m_context, m_sinkIndex,
                                       &PulseOut::onSinkInputInfoTrampoline,
                                       this);
        emit ready();
        break;
    case PA_STREAM_FAILED:
    case PA_STREAM_TERMINATED:
        m_sinkIndex = PA_INVALID_INDEX;
        emit failed(QStringLiteral("PulseAudio: stream failed"));
        break;
    default:
        break;
    }
}

void PulseOut::onSubscribeTrampoline(struct pa_context *context,
                                     pa_subscription_event_type_t type,
                                     uint32_t index, void *data)
{
    Q_UNUSED(context)
    static_cast<PulseOut *>(data)->onSubscribe(type, index);
}

void PulseOut::onSinkInputInfoTrampoline(struct pa_context *context,
                                         const struct pa_sink_input_info *info,
                                         int eol, void *data)
{
    Q_UNUSED(context)
    // The single-object query's reply carries its payload in the eol
    // call (one callback with info and eol set): pass the payload
    // through — a bare eol (info vanished mid-query) has none.
    static_cast<PulseOut *>(data)->onSinkInputInfo(info);
}

void PulseOut::onSubscribe(pa_subscription_event_type_t type, uint32_t index)
{
    // Only this stream's own sink input matters (the server reports
    // every client's inputs): a NEW/CHANGE queries its volume and mute
    // (read back from the server — the external change, and the
    // confirmation of doob's own push, both land here), a REMOVE drops
    // the index (the stream's state reports the failure).
    if (m_stream == nullptr)
        return;
    if (index != pa_stream_get_index(m_stream))
        return;
    const auto facility = type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    if (facility != PA_SUBSCRIPTION_EVENT_SINK_INPUT)
        return;
    const auto event = type & PA_SUBSCRIPTION_EVENT_TYPE_MASK;
    if (event == PA_SUBSCRIPTION_EVENT_REMOVE) {
        m_sinkIndex = PA_INVALID_INDEX;
        return;
    }
    if (event != PA_SUBSCRIPTION_EVENT_NEW && event != PA_SUBSCRIPTION_EVENT_CHANGE)
        return;
    if (m_context == nullptr)
        return;
    pa_context_get_sink_input_info(m_context, index,
                                   &PulseOut::onSinkInputInfoTrampoline, this);
}

void PulseOut::onSinkInputInfo(const struct pa_sink_input_info *info)
{
    // eol (or a vanished input) ends the query without a payload.
    if (info == nullptr)
        return;
    if (info->has_volume == 0)
        return;
    // The channels' average is the single-slider level (the output is
    // uniform stereo, so the average is the level).
    emit sinkVolumeChanged(
        static_cast<float>(pa_sw_volume_to_linear(pa_cvolume_avg(&info->volume))));
    emit sinkMuteChanged(info->mute != 0);
}

void PulseOut::setSinkVolume(float volume)
{
    m_sinkVolume = qBound(0.0F, volume, 1.0F);
    applySinkVolume();
}

void PulseOut::setSinkMute(bool muted)
{
    m_sinkMute = muted;
    applySinkMute();
}

void PulseOut::applySinkVolume()
{
    // The API is thread-safe (the call is dispatched to the mainloop
    // thread), so it may run from the GUI thread; a not-ready stream
    // (no sink input yet) is a no-op — the stream's ready re-applies
    // the stored value.
    if (m_context == nullptr || m_sinkIndex == PA_INVALID_INDEX)
        return;
    pa_cvolume volume;
    pa_cvolume_set(&volume, 2, pa_sw_volume_from_linear(m_sinkVolume.load()));
    pa_context_set_sink_input_volume(m_context, m_sinkIndex.load(), &volume,
                                     nullptr, nullptr);
}

void PulseOut::applySinkMute()
{
    if (m_context == nullptr || m_sinkIndex == PA_INVALID_INDEX)
        return;
    pa_context_set_sink_input_mute(m_context, m_sinkIndex.load(),
                                   m_sinkMute.load() ? 1 : 0, nullptr,
                                   nullptr);
}

void PulseOut::pumpAudio()
{
    if (m_stream == nullptr || m_ring == nullptr
        || pa_stream_get_state(m_stream) != PA_STREAM_READY)
        return;
    // No audio in the ring: no pass (the next wake-up retries). An
    // empty pass must not reach the server — a zero-byte commit
    // starves the server's buffer, and with EARLY_REQUESTS a starved
    // buffer makes the server re-request writes in a flood: the
    // mainloop's poll then never parks (a full core) and the stream's
    // control replies (the sink-input's info queries) are lost in it.
    // An idle buffer is an underrun — which is what no audio is.
    if (m_ring->availableFrames() != 0)
        pumpPass();
    // The release runs on every wake-up, not only on write passes: the
    // server authorizes its lead's worth of headroom up front, so a
    // burst of passes can push out the whole queued audio in a few ms,
    // and the taps that come due afterwards (aged past the measured
    // latency) would sit in the queue forever — a pass with an empty
    // ring takes no write path, and the release lived in it.
    const qint64 nowMs = m_tapClock.elapsed();
    m_tapDelay.prune(nowMs, kMaxTapDelayMs);
    for (const AudioTap &old : m_tapDelay.due(nowMs, tapDelayMs())) {
        emit audioLevels(old.leftPeak, old.rightPeak);
        emit audioWaveform(old.waveform, static_cast<quint32>(m_rate));
        emit audioFrames(old.frames, static_cast<quint32>(m_rate));
    }
}

void PulseOut::pumpPass()
{
    // The paplay dance: ask the server how much it can take, and
    // hand that size to begin_write (which caps it and reserves the
    // buffer). A zero size means the buffer is full; the next pump
    // retries.
    const size_t writable = pa_stream_writable_size(m_stream);
    // A sub-frame amount cannot take even one frame (the pump writes in
    // whole frames): skip the pass rather than zero-byte commit — a
    // zero-byte write re-enables the server's flush defer event, and
    // with the buffer's requested_bytes stuck at a sub-frame residual
    // the mainloop would spin at CPU speed.
    if (writable < 2 * sizeof(float))
        return;
    void *data = nullptr;
    size_t nbytes = writable;
    if (pa_stream_begin_write(m_stream, &data, &nbytes) < 0 || nbytes == 0)
        return;

    // Fill the space begin_write() reserved, silence-padded (the
    // ring's read does it on an underrun): frames are interleaved
    // stereo, so each frame is two floats. At most one pump's worth
    // of audio per pass, so the server buffer (whose ceiling this
    // server sets in the multi-second range) cannot run ahead of
    // playback.
    const size_t frames =
        qMin(nbytes / (2 * sizeof(float)), static_cast<size_t>(m_rate) / 50);
    // The ring pads an underrun with silence: the first `real` frames
    // are the audio, the tail is padding (not audio — the position must
    // not count it, so a paused player does not roll on it).
    const size_t real = m_ring->read(static_cast<float *>(data), frames);
    // The DSP chain runs over the real audio only, before it goes out
    // and before the tap below (which reports what is actually heard):
    // a processor can shape the samples and shorten the chunk (a
    // dropping one skips leading silence, so the audible audio — and
    // the position — start fast). `kept` is what goes out.
    const size_t kept =
        real > 0 ? m_chain.process(static_cast<float *>(data), real, 2) : 0;
    // The user's volume boost: a linear scale after the chain (the
    // level above the server's normal volume is the output's concern,
    // not a DSP's) and before the tap (the meter shows what is heard).
    const float gain = m_boost.load();
    if (kept > 0 && gain != 1.0F) {
        float *frames = static_cast<float *>(data);
        for (size_t i = 0; i < kept * 2; ++i)
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            frames[i] *= gain;
    }
    // Commit the write even when nothing survives: begin_write()
    // reserved the buffer and an uncommitted write wedges the next
    // begin_write, so the pump must always close out the pass it
    // opened. A zero-length commit is not an option (the starvation
    // above), so a pass that kept nothing commits the silence padding
    // instead: a dropping processor only drops what is inaudible
    // (silence), so the padding is what the dropped audio sounded
    // like. The position counts kept only, as before.
    const size_t writeFrames = kept > 0 ? kept : frames;
    if (pa_stream_write(m_stream, data, writeFrames * 2 * sizeof(float),
                        nullptr, 0, PA_SEEK_RELATIVE) < 0)
        return;
    m_playedFrames += static_cast<qint64>(kept);
    // The audio tap, latency-shifted: the frames that just went out
    // (underrun silence included) only reach the speaker after the
    // server buffer plays out, so the tap is held until it has aged
    // past the stream's latency — the display follows the audible
    // audio instead of leading it by the buffer. The pass is sliced
    // (kTapSlices) and each slice is stamped at the moment its own
    // audio reaches the speaker, so the taps come due ~kTapSlices
    // apart in time instead of one burst per pass: the tap stream
    // then outruns the pump's pass rate by kTapSlices, and a display
    // that paints faster than the passes still sees fine-grained
    // scroll. The decimation (8 frames per bucket) is per slice, so
    // the total bucket count — and the scroll's speed — is the one a
    // single tap per pass would give. (No NODATA guard: a failed read
    // keeps the last good latency.)
    pa_usec_t latencyUsec = 0;
    int negative = 0;
    if (pa_stream_get_latency(m_stream, &latencyUsec, &negative) == 0)
        m_latencyMs = static_cast<qint64>(latencyUsec) / 1000;
    const qint64 nowMs = m_tapClock.elapsed();
    const size_t sliceFrames = kept / static_cast<size_t>(kTapSlices);
    for (int s = 0; s < kTapSlices; ++s) {
        const size_t off = static_cast<size_t>(s) * sliceFrames;
        const size_t n =
            (s + 1 == kTapSlices) ? kept - off : sliceFrames;
        if (n == 0)
            continue;  // a short pass's remainder rides the last slice
        const int buckets = qBound(1, static_cast<int>(n) / 8, 256);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        const float *slice = static_cast<const float *>(data) + (2 * off);
        const AudioTap tap = tapFrames(slice, n, buckets);
        // The slice's audio starts sounding off/rate after the write,
        // so the due rule (age >= latency) fires exactly at its
        // audible moment.
        const qint64 stampMs =
            nowMs + ((static_cast<qint64>(off) * 1000) / m_rate);
        m_tapDelay.push(stampMs, tap);
    }
}

qint64 PulseOut::tapDelayMs() const
{
    // The forced value (the test seam) wins; otherwise the measured
    // latency, clamped (a misreported latency must not lag the meter
    // by minutes or queue unbounded audio).
    const qint64 forced = m_forcedDelayMs.load();
    return qBound<qint64>(0, forced >= 0 ? forced : m_latencyMs.load(),
                          kMaxTapDelayMs);
}

