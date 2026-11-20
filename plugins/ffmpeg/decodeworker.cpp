#include "decodeworker.h"

#include <QDebug>
#include <QFileInfo>

#include <array>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

namespace {
// The max decoded frames per swr_convert call (the per-frame output
// buffer).
constexpr size_t kMaxFrames = 16384;

QString avError(int code)
{
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buf{};
    av_strerror(code, buf.data(), buf.size());
    return QString::fromUtf8(buf.data());
}

// The container's name is a comma-separated alias list
// ("mov,mp4,m4a,3gp,3g2,mj2") — for display, prefer the alias that
// matches the source's file extension, the first alias when there is
// no match (an http stream has none).
QString containerName(const AVFormatContext *fmt, const QUrl &source)
{
    const QStringList aliases =
        QString::fromUtf8(fmt->iformat->name).split(QLatin1Char(','),
                                                     Qt::SkipEmptyParts);
    if (aliases.isEmpty())
        return {};
    QString suffix = QFileInfo(source.fileName()).suffix().toLower();
    if (!suffix.isEmpty() && aliases.contains(suffix))
        return suffix;
    return aliases.first();
}

// The channel layout's display name. This FFmpeg describes a native
// layout by count alone ("2 channels"); the unambiguous counts get
// their standard names, the rest keep the descriptor (a named layout
// like "5.1" already comes back as its name).
QString channelLayoutName(const AVCodecContext *codec)
{
    std::array<char, 32> buf{};
    QString desc;
    if (av_channel_layout_describe(&codec->ch_layout, buf.data(),
                                   buf.size()) >= 0)
        desc = QString::fromUtf8(buf.data());
    if (desc == QStringLiteral("1 channels"))
        return QStringLiteral("mono");
    if (desc == QStringLiteral("2 channels"))
        return QStringLiteral("stereo");
    // A count-only descriptor adds nothing beyond the channel count.
    if (desc == QStringLiteral("%1 channels").arg(codec->ch_layout.nb_channels))
        return {};
    return desc;
}
}

DecodeWorker::DecodeWorker(QObject *parent)
    : QObject(parent)
{
}

DecodeWorker::~DecodeWorker()
{
    stop();
}

bool DecodeWorker::start(const QUrl &source, RingBuffer *ring, int targetRate,
                         int writerId,
                         const std::atomic<bool> *currentDone,
                         size_t stagingFrames, qint64 initialOffsetMs)
{
    m_source = source;
    m_ring = ring;
    m_targetRate = targetRate;
    m_writerId = writerId;
    m_currentDone = currentDone;
    m_gated = currentDone != nullptr;
    m_initialOffsetMs = initialOffsetMs;
    // The staging (the gated worker's private ring): as big as the
    // player asks — the player keeps it at the shared ring's capacity
    // (the boundary accounting's invariant, docs/plugins.md).
    m_staging = nullptr;
    if (currentDone != nullptr)
        m_staging = std::make_unique<RingBuffer>(
            stagingFrames == 0 ? ring->capacityFrames() : stagingFrames);
    m_stageBuf.resize(kMaxFrames * 2);
    m_stop = false;
    m_paused = false;
    m_seekMs = -1;
    m_durationMs = 0;
    m_written = 0;
    m_eosSeen = false;
    if (!moveToThread(&m_thread)) {
        // A parented worker stays on the calling thread — its loop
        // would then block that thread; refuse to start at all.
        emit decodeError(QStringLiteral("Cannot move the decode worker to its thread"));
        return false;
    }
    connect(&m_thread, &QThread::started, this, &DecodeWorker::runLoop);
    m_thread.start();
    return true;
}

void DecodeWorker::stop()
{
    if (m_thread.isRunning()) {
        m_stop = true;
        if (m_ring != nullptr)
            // A writer blocked on a full ring must wake up (the output
            // may have stopped pulling) — by this worker's own id, so
            // the other writer of a shared ring (the gapless handoff)
            // keeps writing.
            m_ring->requestStop(m_writerId);
        // Defensive: if the loop never ran on this thread (a failed
        // move), it sits in the thread's default event loop — leave it
        // so the join cannot hang.
        m_thread.quit();
        m_thread.wait();
    }
}

void DecodeWorker::setPaused(bool paused)
{
    m_paused = paused;
}

void DecodeWorker::seekToMs(qint64 positionMs)
{
    m_seekMs = positionMs;
    // Decode runs ahead of real time, so the worker usually sits blocked
    // in the ring's write (back-pressure). Wake it (by its own writer
    // id), so the seek lands on the next loop iteration — doSeek drains
    // the ring, which clears the stop flags.
    if (m_ring != nullptr)
        m_ring->requestStop(m_writerId);
}

void DecodeWorker::runLoop()
{
    if (openSource()) {
        // The rate reported is the target's: the resampler's output,
        // the ring's and the output's common rate.
        emit opened(m_targetRate, m_streamInfo);
        // The re-decode's initial seek (a corpse's replacement — the
        // player's play-out window): it drains the ring and re-anchors
        // the segment like a user's seek. Ungated workers only (a
        // gated worker's drain would take the shared ring mid
        // pre-roll — start() documents the invariant).
        if (m_initialOffsetMs > 0)
            doSeek(m_initialOffsetMs);
        decodeToEos();
    }
    // The thread's default run() spins an event loop after this slot
    // returns — leave it, so the thread exits and stop() can join it.
    QThread::currentThread()->quit();
}

void DecodeWorker::decodeToEos()
{
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    int exitCode = 0;  // < 0: a decode failure (no eos is emitted)
    while (!m_stop) {
        if (m_paused) {
            // Drop what the output has not pulled: a chunk can land in
            // the ring after the player's pause drain, and a paused
            // position must not keep rolling.
            m_ring->drain();
            QThread::msleep(5);
            continue;
        }
        if (m_seekMs >= 0)
            doSeek(m_seekMs.exchange(-1));
        const int r = av_read_frame(m_fmt, pkt);
        if (r < 0) {
            if (r != AVERROR_EOF && r != AVERROR(EAGAIN))
                exitCode = r;
            break;
        }
        if (pkt->stream_index == m_audioStream
                && sendPacket(pkt, frame, exitCode))
            break;
        av_packet_unref(pkt);
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    closeSource();

    // A stop is neither an eos nor an error.
    if (!m_stop) {
        if (exitCode < 0) {
            emit decodeError(
                QStringLiteral("Decode error: %1").arg(avError(exitCode)));
        } else {
            if (m_gated && m_currentDone != nullptr)
                parkOnGateAndDrain();
            m_eosSeen = true;
            emit eos();
        }
    }
}

bool DecodeWorker::sendPacket(AVPacket *pkt, AVFrame *frame, int &exitCode)
{
    // AVERROR(EAGAIN) is the negative EAGAIN (the positive errno would
    // never match a negative ret): the decoder still holds frames —
    // the receive loop below drains them and the next send succeeds.
    const int ret = avcodec_send_packet(m_codec, pkt);
    if (ret < 0 && ret != AVERROR(EAGAIN)) {
        qWarning("DecodeWorker: send_packet: %s", qPrintable(avError(ret)));
        exitCode = ret;
        return true;
    }
    while (avcodec_receive_frame(m_codec, frame) == 0)
        pushFrame(frame);
    return false;
}

bool DecodeWorker::openSource()
{
    // The file: protocol looks the path up literally (it does not
    // percent-decode), so a file: URL is handed over decoded — a
    // toString() would turn "… [FLAC]" into "… %5BFLAC%5D" and the
    // open would fail; http(s): URLs keep their encoding.
    const QString title =
        m_source.isLocalFile() ? m_source.toLocalFile()
                               : m_source.toString();
    const QByteArray url = title.toUtf8();
    if (avformat_open_input(&m_fmt, url.constData(), nullptr, nullptr) < 0) {
        emit decodeError(QStringLiteral("Cannot open “%1”").arg(title));
        return false;
    }
    if (avformat_find_stream_info(m_fmt, nullptr) < 0) {
        emit decodeError(
            QStringLiteral("Cannot read “%1”: not a playable stream").arg(title));
        closeSource();
        return false;
    }
    const AVCodec *codec = nullptr;
    m_audioStream = av_find_best_stream(m_fmt, AVMEDIA_TYPE_AUDIO, -1, -1,
                                        &codec, 0);
    if (m_audioStream < 0) {
        emit decodeError(QStringLiteral("No audio stream in “%1”").arg(title));
        closeSource();
        return false;
    }
    // C-API boundary: AVStream** is indexed (there is no accessor).
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    AVStream *stream = m_fmt->streams[m_audioStream];
    m_timeNum = stream->time_base.num;
    m_timeDen = stream->time_base.den;
    m_codec = avcodec_alloc_context3(codec);
    if (m_codec == nullptr
            || avcodec_parameters_to_context(m_codec, stream->codecpar) < 0
            || avcodec_open2(m_codec, codec, nullptr) < 0) {
        emit decodeError(QStringLiteral("Unsupported codec in “%1”").arg(title));
        closeSource();
        return false;
    }
    if (m_fmt->duration > 0)
        m_durationMs = m_fmt->duration / 1000;  // AV_TIME_BASE is µs → ms

    // The resampler: the source's layout and sample format at its own
    // rate → interleaved float32 stereo at the fixed target rate (the
    // ring's and the persistent output's common rate — the gapless
    // handoff; PulseAudio then resamples to the device rate).
    static const AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    if (swr_alloc_set_opts2(&m_swr, &stereo, AV_SAMPLE_FMT_FLT,
                            m_targetRate, &m_codec->ch_layout,
                            m_codec->sample_fmt, m_codec->sample_rate,
                            0, nullptr) < 0
            || swr_init(m_swr) < 0) {
        emit decodeError(
            QStringLiteral("Unsupported audio format in “%1”").arg(title));
        swr_free(&m_swr);
        closeSource();
        return false;
    }
    m_out.resize(kMaxFrames * 2);

    // The stream info: the codec, the container and the decoded
    // stream's actual parameters (the source layout and rate, not the
    // resampler's output).
    {
        QVariantMap info;
        info.insert(QStringLiteral("codec"),
                    QString::fromUtf8(codec->name));
        info.insert(QStringLiteral("format"),
                    containerName(m_fmt, m_source));
        if (m_codec->bit_rate > 0)
            info.insert(QStringLiteral("bitrate"),
                        static_cast<qint64>(m_codec->bit_rate));
        info.insert(QStringLiteral("sampleRate"), m_codec->sample_rate);
        info.insert(QStringLiteral("channels"),
                    m_codec->ch_layout.nb_channels);
        info.insert(QStringLiteral("channelLayout"),
                    channelLayoutName(m_codec));
        m_streamInfo = info;
    }
    return true;
}

uint8_t *DecodeWorker::outBytes()
{
    // The one raw cast: swr's byte-level API over the float32 buffer.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<uint8_t *>(m_out.data());
}

void DecodeWorker::pushFrame(AVFrame *frame)
{
    uint8_t *out = outBytes();
    const int converted = swr_convert(m_swr, &out, kMaxFrames,
                                      frame->extended_data, frame->nb_samples);
    if (converted < 0) {
        qWarning("DecodeWorker: swr_convert: %s",
                 qPrintable(avError(converted)));
        return;
    }
    m_written.fetch_add(static_cast<qint64>(converted));
    // The gate: a gated worker whose current is not done stages (the
    // ring's order invariant — the current's frames all precede
    // this worker's first); once the gate opens, the staging drains
    // ahead of each direct write (the invariant: no direct frame
    // before the staged ones). A gate that opens mid-park is taken up
    // right here (pushStaged answers false): the staging drains ahead
    // of this direct write — never a blocking staging write (the
    // staging is full in exactly that moment, and stop() requests the
    // shared ring only, so a blocking private write would hang the
    // worker's join on the GUI thread).
    if (m_gated && m_currentDone != nullptr
            && !m_currentDone->load(std::memory_order_acquire)
            && pushStaged(m_out.data(), static_cast<size_t>(converted))) {
        return;
    }
    drainStaged();
    m_ring->write(m_out.data(), static_cast<size_t>(converted), m_writerId);
}

bool DecodeWorker::pushStaged(const float *frames, size_t frameCount)
{
    // A full staging parks — a 5 ms poll that a stop breaks, never a
    // blocking write. The answer: true when the chunk is in the
    // staging, false when the caller takes the direct path (the gate
    // opened mid-park — the staging is drained ahead of it — or a stop
    // is in flight — the ring write below no-ops it).
    while (!m_stop
           && !m_currentDone->load(std::memory_order_acquire)
           && m_staging->availableFrames() + frameCount
                  > m_staging->capacityFrames())
        QThread::msleep(5);
    if (m_stop)
        return false;
    if (m_currentDone->load(std::memory_order_acquire))
        return false;  // the gate opened: the direct path drains
    // The park's exit guarantees the chunk fits (a single writer):
    // the write cannot block.
    m_staging->write(frames, frameCount, 0);
    return true;
}

void DecodeWorker::parkOnGateAndDrain()
{
    // The corpse's exit: a gated worker whose gate is still closed
    // parks here (a poll a stop breaks) until the current is done,
    // then drains its staging before the eos (the boundary's
    // accounting counts those frames — they must be in the ring by
    // its boundary).
    while (!m_stop
           && !m_currentDone->load(std::memory_order_acquire))
        QThread::msleep(5);
    if (!m_stop)
        drainStaged();
}

void DecodeWorker::drainStaged()
{
    if (m_staging == nullptr)
        return;  // an ungated worker (the current one) never staged
    while (!m_stop) {
        const size_t avail = m_staging->availableFrames();
        if (avail == 0)
            return;
        const size_t n = std::min(avail, m_stageBuf.size() / 2);
        m_staging->read(m_stageBuf.data(), n);
        // The shared ring's back-pressure paces the drain (on this
        // worker thread — never the GUI's); a stop wakes a blocked
        // write and the loop breaks.
        m_ring->write(m_stageBuf.data(), n, m_writerId);
    }
}

void DecodeWorker::doSeek(qint64 positionMs)
{
    if (m_fmt == nullptr || m_audioStream < 0)
        return;
    const int64_t ts =
        av_rescale_q(positionMs, AVRational{1, 1000},
                     AVRational{m_timeNum, m_timeDen});
    if (av_seek_frame(m_fmt, m_audioStream, ts, AVSEEK_FLAG_BACKWARD) < 0) {
        qWarning("DecodeWorker: seek to %lld ms failed (not seekable?)",
                 static_cast<qulonglong>(positionMs));
        return;
    }
    avcodec_flush_buffers(m_codec);
    // Flush the resampler's tail (swr_convert with a null input).
    uint8_t *out = outBytes();
    swr_convert(m_swr, &out, kMaxFrames, nullptr, 0);
    const uint64_t readsAtDrain = m_ring->drain();
    // The stretch of this item's frames starts over: the accounting
    // (and the player's segment anchor) do too (the worker re-decodes
    // to a new eos — the corpse's mark does not survive the seek).
    m_written = 0;
    m_eosSeen = false;
    emit ringDrained(readsAtDrain);
}

void DecodeWorker::closeSource()
{
    m_out.clear();
    if (m_swr != nullptr) {
        swr_free(&m_swr);
        m_swr = nullptr;
    }
    if (m_codec != nullptr) {
        avcodec_free_context(&m_codec);
        m_codec = nullptr;
    }
    if (m_fmt != nullptr) {
        avformat_close_input(&m_fmt);
        m_fmt = nullptr;
    }
    m_audioStream = -1;
}
