#ifndef FFMPEG_DECODEWORKER_H
#define FFMPEG_DECODEWORKER_H

#include <QObject>
#include <QThread>
#include <QUrl>
#include <QVariantMap>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <doob/ringbuffer.h>

struct AVFormatContext;
struct AVCodecContext;
struct SwrContext;
struct AVFrame;
struct AVPacket;

// The decode side of the FFmpeg player: a QThread that opens the
// source (a file: / http: / https: URL), decodes the audio to
// interleaved float32 stereo at the fixed target rate and pushes it
// into the shared ring (the output pulls from it). The fixed rate is
// the gapless handoff's contract: two workers (the current and the
// queued next item) write into one ring that one persistent output
// reads — the frames only line up at a boundary when both are at the
// same rate. Control (pause, seek, stop) is by atomic flags the loop
// checks each iteration. opened(), eos() and decodeError() reach the
// receiver (the GUI thread) by queued delivery. runLoop quits the
// thread's event loop on exit (a plain QThread's default run() spins
// one forever) and the owner deletes the worker after stop() (which
// joins the thread).
class DecodeWorker : public QObject
{
    Q_OBJECT
public:
    explicit DecodeWorker(QObject *parent = nullptr);
    ~DecodeWorker() override;

    // Opens + decodes source into ring on a fresh QThread, resampled
    // to targetRate, writing as the ring's writerId (the ring's stop
    // flag is per writer: the player's two workers — the current and
    // the queued-next — share the ring, and stopping one must not
    // silence the other). The worker must be parentless (moveToThread
    // refuses objects with a parent); false — with decodeError emitted
    // — when that fails.
    //
    // The gapless gate (the ring's order invariant — docs/plugins.md):
    // a non-null currentDone marks the worker GATED — while it is
    // gated and *currentDone is false (the current decode not done),
    // its frames go into a private staging of stagingFrames (0 = the
    // ring's capacity) and enter the ring only once *currentDone is
    // true (the worker drains its staging first). A gated worker that
    // reaches its eos with the gate still closed parks there (a poll
    // that a stop breaks) and drains its staging before emitting the
    // eos. The player clears the flag (setGated) when the worker is
    // promoted to the current — it then writes straight into the ring
    // from the handoff on. A non-null currentDone with initialOffsetMs
    // > 0 is a caller bug (the gated worker's initial seek would drain
    // the shared ring mid pre-roll) — gated workers start at the top.
    bool start(const QUrl &source, RingBuffer *ring, int targetRate,
               int writerId,
               const std::atomic<bool> *currentDone = nullptr,
               size_t stagingFrames = 0, qint64 initialOffsetMs = 0);
    // Clears the worker's gated flag (the player's promotion: the
    // worker is the current now and writes straight into the ring).
    void clearGated() { m_gated = false; }
    // Sets the stop flag and joins the thread (safe to call twice; the
    // join can never hang, even if the loop never ran on the thread).
    void stop();
    // Pauses / resumes the push (the ring keeps draining).
    void setPaused(bool paused);
    // Seeks to positionMs (applied at the next loop check).
    void seekToMs(qint64 positionMs);
    // The content's duration (ms; 0 when unknown — a live stream),
    // valid after opened().
    qint64 durationMs() const { return m_durationMs.load(); }
    // The stream info (the codec, container, the actual bitrate /
    // sample rate / channels) — valid after opened().
    QVariantMap streamInfo() const { return m_streamInfo; }
    // The source this worker decodes (the player's queueNext
    // idempotency check).
    const QUrl &source() const { return m_source; }
    // The frames pushed into the ring since start() (or the last seek
    // drain / resetWritten) — the handoff's boundary accounting (the
    // player sums them with the output's frame count).
    qint64 framesWritten() const { return m_written.load(); }
    void resetWritten() { m_written.store(0); }
    // True once the decode ran to its end (eos() emitted): the
    // player's handoff uses it to tell a corpse (already decoded, its
    // frames sitting in the ring) from a live worker.
    bool eosSeen() const { return m_eosSeen.load(); }

signals:
    // The source is open (the rate, duration and stream info are now
    // valid; the info carries the codec, the container format and the
    // actual bitrate / sample rate / channels of the decoded stream).
    void opened(int sampleRate, const QVariantMap &info);
    void eos();
    void decodeError(const QString &message);
    // The seek drained the ring (the present stretch of this item's
    // frames starts now; the player re-anchors the segment at
    // readsAtDrain — the ring's read count at the drain itself, since
    // by the time this queued signal lands a pump pass may have
    // advanced the count past it).
    void ringDrained(uint64_t readsAtDrain);

private:
    void runLoop();
    void decodeToEos();
    bool openSource();
    // Sends the packet and pushes the decoded frames; true on a decode
    // failure (exitCode is set).
    bool sendPacket(AVPacket *pkt, AVFrame *frame, int &exitCode);
    void pushFrame(AVFrame *frame);
    // The gate's detour: the frames go into the private staging (a
    // full-staging park is a 5 ms poll that a stop breaks — never a
    // blocking write, which a stop could not wake). True when the
    // chunk is staged; false when the caller takes the direct path
    // (the gate opened mid-park — the staging drains ahead of the
    // direct write — or a stop is in flight).
    bool pushStaged(const float *frames, size_t frameCount);
    // The gate's return: the staging's frames into the ring (a poll
    // that a stop breaks — stop() requests the shared ring only).
    void drainStaged();
    // The corpse's exit: park on the closed gate (a poll a stop
    // breaks), then drain the staging before the eos.
    void parkOnGateAndDrain();
    void doSeek(qint64 positionMs);
    void closeSource();
    // The decoded-frame buffer at swr's byte level (the one raw cast).
    uint8_t *outBytes();

    QThread m_thread;
    RingBuffer *m_ring = nullptr;
    // The ring's stop flag this worker owns (set by start).
    int m_writerId = 0;
    // The gapless gate (set by start; nullptr = the current worker,
    // ungated for life) and this worker's gated flag (set by start
    // with the gate, cleared by the player's promotion).
    const std::atomic<bool> *m_currentDone = nullptr;
    std::atomic<bool> m_gated{false};
    // The gated worker's private staging (the ring's back, kept out of
    // the ring until the gate opens). The drain's bounce buffer.
    std::unique_ptr<RingBuffer> m_staging;
    std::vector<float> m_stageBuf;
    qint64 m_initialOffsetMs = 0;
    QUrl m_source;
    int m_targetRate = 48000;
    AVFormatContext *m_fmt = nullptr;
    AVCodecContext *m_codec = nullptr;
    int m_audioStream = -1;
    int m_timeNum = 1;  // the stream's time_base (num/den)
    int m_timeDen = 1;
    SwrContext *m_swr = nullptr;
    std::vector<float> m_out;
    std::atomic<qint64> m_durationMs{0};
    // The decoded stream's info (the codec, container, the actual
    // bitrate / sample rate / channels), gathered at open.
    QVariantMap m_streamInfo;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_paused{false};
    std::atomic<qint64> m_seekMs{-1};
    // The frames pushed into the ring (the handoff's boundary
    // accounting). Touched on the worker thread (pushFrame, doSeek)
    // and read/reset across threads (atomic).
    std::atomic<qint64> m_written{0};
    // Set on the worker thread before eos() is emitted.
    std::atomic<bool> m_eosSeen{false};
};

#endif // FFMPEG_DECODEWORKER_H
