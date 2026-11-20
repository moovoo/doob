#ifndef FFMPEG_PULSEOUT_H
#define FFMPEG_PULSEOUT_H

#include <QObject>
#include <QElapsedTimer>
#include <QMutex>
#include <QThread>
#include <QVector>

#include <atomic>

extern "C" {
#include <pulse/pulseaudio.h>
}

#include <doob/dspchain.h>

#include "audiotap.h"
#include <doob/ringbuffer.h>

class PulseOut;

// The mainloop's thread. The loop self-paces:
// pa_mainloop_prepare(mainloop, 20 ms) parks the subsequent poll at
// most that long (the timeout is in microseconds; -1 would block
// until an event lands) and every wake-up (a real event or the
// expiry) pumps. The wake-up on stop is pa_mainloop_quit()
// (thread-safe), and the PulseAudio objects are freed here, on the
// thread that ran them.
class PulseLoopThread : public QThread
{
public:
    explicit PulseLoopThread(PulseOut *owner)
        : m_owner(owner)
    {
    }

protected:
    void run() override;

private:
    PulseOut *m_owner;
};

// The output side of the FFmpeg player: a PulseAudio playback stream
// on a plain pa_mainloop running on its own thread (the same shape as
// paplay). This server pushes no write requests past the first one,
// so the client paces the writes itself: the loop wakes on its own
// 20 ms timeout (pa_mainloop_prepare's timeout argument, parked on
// the event loop's poll — the GUI thread plays no part, so a busy
// GUI cannot stall the audio) and every wake-up pulls interleaved
// stereo float frames from the ring into the space
// pa_stream_begin_write() offers (silence on an underrun). start()
// only fails if a stream is already running; the stream itself comes
// up asynchronously (ready(), or failed() when the server rejects it
// — a no-show is the player's watchdog to catch, not this object's).
class PulseOut : public QObject
{
    Q_OBJECT
    friend class PulseLoopThread;

public:
    explicit PulseOut(QObject *parent = nullptr);
    ~PulseOut() override;

    // Starts the stream, pulling interleaved stereo float frames at
    // sampleRate (PulseAudio resamples to the sink's rate). false
    // when a stream is already running.
    virtual bool start(RingBuffer *ring, int sampleRate);
    // Stops the stream and joins the thread (safe to call twice).
    virtual void stop();
    bool isRunning() const { return m_running.load(); }

    // The frames written to the server since start() (reset by
    // start/stop).
    qint64 playedFrames() const { return m_playedFrames.load(); }
    void setPlayedFrames(qint64 frames) { m_playedFrames = frames; }

    // The tap's delay (ms): the measured stream latency in charge, or
    // the forced value (the test seam, negative = measure).
    qint64 tapDelayMs() const;
    void setTapDelayMs(qint64 delayMs) { m_forcedDelayMs = delayMs; }

    // The boost gain (the user's volume boost, a linear 1.0 = no-op):
    // the frames are scaled after the DSP chain and before the tap —
    // the level above the server's normal volume, a property of the
    // output, not a DSP, and the meter shows what is heard.
    void setBoost(float boost) { m_boost = boost; }
    float boost() const { return m_boost.load(); }

    // The sink input's volume (a linear level, 0.0..1.0) and mute: the
    // server-side level of this stream (what pavucontrol shows), set
    // on the server and read back from it — the initial sync (a fresh
    // sink input starts at the server's default) and the external
    // changes (another client of the server) come back through the
    // sinkVolumeChanged/sinkMuteChanged signals. The stored values are
    // re-applied when the stream comes up (a new sink input starts
    // fresh on the server).
    virtual void setSinkVolume(float volume);
    virtual void setSinkMute(bool muted);

    // The /proc tid (gettid) of the mainloop's thread; 0 until the
    // thread has started its loop. Tests use it to measure the
    // thread's CPU: the loop must park in its timeout between pumps,
    // not spin.
    quint64 threadId() const { return m_threadId.load(); }

signals:
    // The stream is playing (emitted once, from the mainloop thread).
    void ready();
    // A runtime failure (no server, the server rejected the stream).
    void failed(const QString &message);
    // The audio tap: the per-channel peaks (0..1), a min/max downsampled
    // waveform (2 * buckets floats) and the raw frames (interleaved
    // stereo float) of a slice of the frames handed to the server (the
    // pass is sliced — kTapSlices).
    // The frames only reach the speaker after the stream's latency, so
    // each tap is held (TapDelay) until it has aged past it and then
    // emitted — the display follows the audible audio instead of
    // leading it by the server buffer. The slices are stamped at their
    // own audible moments, so the taps come due ~kTapSlices times
    // faster than the pump's passes (~50/s): a display that paints
    // faster than the passes still sees fine-grained scroll. Emitted
    // from the mainloop thread as taps come due (several per pump at a
    // large latency), so it reaches the receiver (the player) by
    // queued delivery.
    void audioLevels(float leftPeak, float rightPeak);
    void audioWaveform(const QVector<float> &waveform, quint32 sampleRate);
    void audioFrames(const QVector<float> &frames, quint32 sampleRate);
    // The sink input's volume (a linear level) and mute, read back from
    // the server: the initial sync (the stream's ready, a query) and
    // every external change (a subscription event for this stream's
    // sink input, a query). Emitted from the mainloop thread, so it
    // reaches the receiver (the player) by queued delivery.
    void sinkVolumeChanged(float linear);
    void sinkMuteChanged(bool muted);

private:
    // The thread body (the mainloop's thread): create the context,
    // connect, run the self-paced loop, tear down.
    void pump();
    // The C event trampolines (the libpulse callbacks carry no
    // receiver).
    static void onContextStateTrampoline(struct pa_context *context, void *data);
    static void onStreamStateTrampoline(struct pa_stream *stream, void *data);
    static void onSubscribeTrampoline(struct pa_context *context,
                                      pa_subscription_event_type_t type,
                                      uint32_t index, void *data);
    static void onSinkInputInfoTrampoline(struct pa_context *context,
                                          const struct pa_sink_input_info *info,
                                          int eol, void *data);
    // The context/stream state (the mainloop thread).
    void onContextState(struct pa_context *context);
    void onStreamState(struct pa_stream *stream);
    // A sink-input subscription event (the mainloop thread): a NEW /
    // CHANGE of this stream's own sink input queries its volume and
    // mute (read back from the server), a REMOVE drops the index (the
    // stream's state reports the failure).
    void onSubscribe(pa_subscription_event_type_t type, uint32_t index);
    // The sink input's info landed (the mainloop thread): the volume
    // (a single-slider level, the channels' average) and the mute go
    // out as signals.
    void onSinkInputInfo(const struct pa_sink_input_info *info);
    // The sink input's volume/mute on the server (the API is
    // thread-safe — the calls may come from the GUI thread; the
    // stored values cross into the mainloop thread for the stream's
    // initial sync).
    void applySinkVolume();
    void applySinkMute();
    // One pump wake-up (the mainloop thread): a write pass when the
    // ring has audio, and the tap's release on every wake-up (the
    // release is not tied to a write — see the body).
    void pumpAudio();
    // The write pass: fill whatever the server buffer can take right
    // now (called only with audio in the ring).
    void pumpPass();

    PulseLoopThread m_thread{this};
    // m_mainloop is published by the pump and read by stop() (which
    // calls pa_mainloop_quit on it); the mutex keeps a quit from
    // landing on a mainloop the pump already freed.
    mutable QMutex m_loopMutex;
    struct pa_mainloop *m_mainloop = nullptr;
    struct pa_context *m_context = nullptr;    // mainloop thread only
    struct pa_stream *m_stream = nullptr;      // mainloop thread only
    // This stream's sink input on the server (PA_INVALID_INDEX until
    // the stream is ready): the subscription's events are matched
    // against it, and the volume/mute API targets it. The mainloop
    // thread writes it, the GUI thread's setSinkVolume/setSinkMute
    // read it.
    std::atomic<uint32_t> m_sinkIndex{PA_INVALID_INDEX};
    RingBuffer *m_ring = nullptr;
    int m_rate = 0;
    // The DSP chain (the core's DspRegistry, the user's order and
    // switches): the frames are shaped — and possibly shortened by a
    // dropping processor — as they go out, before the tap, so the
    // meter shows what is actually heard. The GUI thread's
    // start/stop/refresh meet the pump on the chain's own mutex.
    DspChain m_chain{this};
    std::atomic<qint64> m_playedFrames{0};

protected:
    // The boost gain (see setBoost); the pump reads it on the mainloop
    // thread, the GUI thread writes it. A test double's pump reads it
    // too, so it is protected.
    std::atomic<float> m_boost{1.0F};

private:
    // The sink input's volume and mute (see setSinkVolume/
    // setSinkMute); the GUI thread writes them, the mainloop thread
    // applies them to the server (on a set and on the stream's ready).
    std::atomic<float> m_sinkVolume{1.0F};
    std::atomic<bool> m_sinkMute{false};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_quit{false};
    std::atomic<quint64> m_threadId{0};
    // The tap's latency shift: the server buffer plays out (pa_stream_
    // get_latency, the last good reading) before the frames reach the
    // speaker, so the taps are held until they have aged past it. The
    // queue and the clock are touched on the mainloop thread only (the
    // delays are atomics — the test seam and the getter cross threads).
    // 5 s bounds both: past it a misreported latency would queue
    // unbounded audio and the meter would lag by minutes.
    static constexpr qint64 kMaxTapDelayMs = 5000;
    // The pass's audio is tapped in this many slices, each stamped at
    // the moment its own audio reaches the speaker (see pumpAudio), so
    // the taps come due ~kTapSlices apart in time (~2.5 ms at the
    // pump's ~50 passes/s) and the tap stream outruns the pass rate —
    // the visualization's scroll is then paced by its paint setting,
    // not by the pump's pass.
    static constexpr int kTapSlices = 8;
    TapDelay m_tapDelay;
    QElapsedTimer m_tapClock;
    std::atomic<qint64> m_latencyMs{0};
    std::atomic<qint64> m_forcedDelayMs{-1};
};

#endif // FFMPEG_PULSEOUT_H
