#ifndef FFMPEG_FFMPEGPLAYER_H
#define FFMPEG_FFMPEGPLAYER_H

#include <QTimer>
#include <QString>
#include <QVariant>

#include <doob/audioplayer.h>

#include "decodeworker.h"
#include "pulseout.h"
#include <doob/ringbuffer.h>

// The FFmpeg playback driver: a decode worker (a file: / http(s): URL
// → interleaved float32 stereo at the fixed output rate) feeds a ring
// buffer that a PulseAudio output stream pulls from. The position is
// the count of the frames the output played; the duration comes from
// the container. The visualization tap sits in the output (the frames
// it hands to the server, re-emitted here as the audioLevels/
// audioWaveform/audioFrames signals),
// and the DSP chain runs there too (the core's DspRegistry — the
// processors shape the frames as they go out). The volume is the
// sink input's volume on the server (the output reads the server's
// state back — the volume and the mute it reports, re-emitted here as
// the sinkVolumeChanged/sinkMuteChanged signals), and the volume
// boost is the output's frame scale (the level above the server's
// normal volume).
//
// finished fires at the real end of the item — the moment its last
// frame leaves the ring (a ring read count, not the decode's eos: a
// short track decodes in milliseconds and the ring's tail must play
// out first). Gapless (AudioPlayer::supportsGapless, the contract in
// docs/components.md): the output is persistent — it comes up once
// per play session and survives the track boundary — and queueNext()
// starts a second, GATED decode worker while the current item is in
// its final seconds: its frames sit in a private staging until the
// gate (m_currentDone, open on the current decode's eos) opens, so
// the ring's frame order — the FIFO's write order — is the current
// item's tail then the next item's head, never interleaved. At the
// boundary the handoff (doHandoff) promotes the queued worker (its
// gated flag is cleared — it is the current now — and the accounting
// switches to it) and finished() fires there; with no queued worker
// the boundary is the play-out's end (stop + finished). In the
// play-out window (the current a corpse — its tail playing out) a
// seek/pause resolves the boundary first (the handoff) and applies to
// the promoted worker, re-decoding a corpse from the operation's
// point. Pause and seek kill the queued worker (its staging goes with
// the ring's drain); a resume re-decodes it from the top, and the
// manager re-queues after a seek.
class FFmpegPlayer : public AudioPlayer
{
    Q_OBJECT
public:
    explicit FFmpegPlayer(QObject *parent = nullptr);
    ~FFmpegPlayer() override;

    void play(const MediaItem &item) override;
    void pause() override;
    void resume() override;
    void stop() override;

    qint64 positionMs() const override;
    qint64 durationMs() const override;
    void setPositionMs(qint64 positionMs) override;
    bool supportsVolume() const override;
    void setVolume(float volume) override;
    bool supportsMute() const override;
    void setMute(bool muted) override;
    bool supportsBoost() const override;
    void setBoost(float boost) override;
    bool supportsGapless() const override { return true; }
    bool queueNext(const MediaItem &item) override;

private slots:
    void onOpened(int sampleRate, const QVariantMap &info);
    void onEos();
    void onDecodeError(const QString &message);
    void onOutputReady();
    void onOutputFailed(const QString &message);
    void reportPosition();
    // The current worker's seek drained the ring (the present stretch
    // of its frames starts now; the segment anchor moves to the
    // drain's own read count).
    void onRingDrained(uint64_t readsAtDrain);

protected:
    // The seam for test doubles: the output this player creates when
    // its source opens. The default is the real PulseAudio output; a
    // test subclass overrides it to inject a fake that consumes the
    // ring without a server.
    virtual PulseOut *createOutput();

private:
    // The fixed output rate: the workers resample to it, so the
    // persistent output and the shared ring survive a boundary whose
    // two streams have different rates (the handoff's contract).
    static constexpr int kOutputRate = 48000;
    // The ring's per-writer stop flags (the handoff's two decode
    // workers share the ring; stopping one must not silence the
    // other).
    static constexpr int kCurrentWriterId = 0;
    static constexpr int kQueuedWriterId = 1;

    void stopPlayback();
    // Wires a current worker's signals (opened / eos / ringDrained,
    // with the pointer-capture guard that a superseded worker's queued
    // signal must not land on its successor); the worker's decodeError
    // is wired by its creator (play / freshWorker / startQueued).
    void wireWorker(DecodeWorker *worker);
    // A fresh current worker decoding source from offsetMs (the
    // re-decode of a corpse — the play-out window's seek / pause):
    // retires the current one, starts it, anchors the position; the
    // position's offset resets (the re-decoded stretch is the item's
    // present one). The worker is nullptr when it cannot start.
    DecodeWorker *freshWorker(const QUrl &source, qint64 offsetMs);
    // Starts the queued decode of item into the shared ring (the
    // gapless pre-roll); false — with decodeError emitted — when the
    // worker cannot start at all.
    bool startQueued(const MediaItem &item);
    // Kills the queued worker (a seek's / pause's drain took its
    // buffered frames); true when there was one.
    bool killQueued();
    // The current item's stretch is fully written (its decode hit its
    // eos): the boundary (the ring read count at which its last frame
    // leaves the ring) is known; the promotion (or the play-out's end)
    // waits for it in the position timer (the ring's tail still plays
    // out).
    void noteStretchEnd();
    // The boundary is reached: with a queued worker the handoff
    // promotes it (the accounting switches, finished() fires — the
    // manager's early-armed no-op); without one the play-out is over
    // (stop + finished).
    void doBoundary();
    // The handoff itself: the accounting switches to the promoted
    // worker (offset, duration, stream info, its own next boundary).
    void doHandoff();
    // Retires a finished worker (joins its thread, deletes it) without
    // touching the output (a handoff keeps it).
    void retireWorker(DecodeWorker *worker);
    // The output's frame count (0 when the output is down).
    qint64 playedFrames() const;

    RingBuffer m_ring{96000};  // 2 s of stereo float32 at kOutputRate
    DecodeWorker *m_worker = nullptr;
    DecodeWorker *m_queued = nullptr;
    // The gapless gate (docs/plugins.md): the current decode's done
    // flag — closed on play/seek/stop, open on its eos (onEos), and
    // set to the promoted worker's state on the handoff. A GATED
    // worker (the queued one, until its promotion clears the flag)
    // stages its frames while it is closed; the closed state is what a
    // parked corpse's drain-wait ends on.
    std::atomic<bool> m_currentDone{false};
    PulseOut *m_output = nullptr;
    int m_rate = 0;
    // The user's volume (a linear level, the server's sink input
    // volume), the mute over it and the boost gain: the output is per
    // session, so the levels are kept here and pushed to a fresh
    // output when it comes up.
    float m_volume = 1.0F;
    bool m_muted = false;
    float m_boost = 1.0F;
    bool m_outputReady = false;
    QTimer m_positionTimer;
    // The item the handoff will play (a pause / a seek kills the
    // queued worker; a resume re-decodes it, the manager re-queues).
    // The handoff clears it — the promoted item is the current, not
    // the queued one — so a resume after it re-decodes nothing.
    MediaItem m_queuedItem;
    // The position's accounting. The boundary is a RING READ count
    // (the output's underrun silence is never counted, and a dropping
    // DSP processor cannot skew a read count): the current item's
    // present stretch starts at m_segStartReads (the ring's read count
    // at a promotion / a seek's drain / a resume — the worker's frame
    // counter counts from there), and the boundary is
    // m_boundaryReads = m_segStartReads + framesWritten() (the count
    // at which the stretch's last frame leaves the ring);
    // m_boundaryReadsPending marks a noted boundary the position timer
    // is waiting on (it fires doBoundary there). m_offsetFrames is the
    // output's (audible) frame count at the current item's first frame
    // — the position is the output's count from there.
    uint64_t m_segStartReads = 0;
    uint64_t m_boundaryReads = 0;
    bool m_boundaryReadsPending = false;
    // The ring's read count at the last GUI-thread drain (a stop, a
    // pause): the next play / resume anchors the segment there (the
    // pump cannot advance the count between the drain and the anchor —
    // the ring is empty and the writer down).
    uint64_t m_ringDrainReads = 0;
    qint64 m_offsetFrames = 0;
};

#endif // FFMPEG_FFMPEGPLAYER_H
