#ifndef CAPTUREPUMP_H
#define CAPTUREPUMP_H

#include <QObject>
#include <QThread>

#include <atomic>

class RingBuffer;

class CapturePump;

// The pump's thread: a self-paced 20 ms loop on the wall clock (the
// ring is an untrusted source — the pump, not the delivery, owns the
// pace).
class PumpThread : public QThread
{
public:
    explicit PumpThread(CapturePump *owner)
        : m_owner(owner)
    {
    }

protected:
    void run() override;

private:
    CapturePump *m_owner;
};

// Reads the PCM the private server's sink hands to its ring (the
// server's pinned format: s16le, kRate, kChannels) and feeds it to
// the relay at the audio's natural rate — the same shape the FFmpeg
// driver's output pulls from its ring (the ring's back-pressure paces
// the server, the ring's silence-on-underrun paces the pump).
//
// The pump owns the clock: one chunk per 20 ms period, a fixed wall-
// clock period (re-anchored when the consumer is slow). Over-delivery
// (a burst in the ring) is never chased — it stays in the ring
// (bounded — the capture's decoupling, so the capture's ~100 ms
// delivery quanta smooth out) and back-pressures the server; a gap is
// padded with silence, so the output never drops.
//
// The chunk() signal runs on the pump thread: a consumer that writes
// to a sink (the relay) runs there too (direct connection — the
// blocking write paces the pump).
class CapturePump : public QObject
{
    Q_OBJECT
    friend class PumpThread;

public:
    // ring: the ring the private server's sink writes into (non-
    // owning — the server's owner keeps it alive); rate and channels:
    // the sink's pinned spec (the ring always carries it, so the chunk
    // size and the pace are known, no server query).
    CapturePump(RingBuffer *ring, int rate, int channels,
                QObject *parent = nullptr);
    ~CapturePump() override; // stops

    // Starts the pump. False when a pump is already running.
    bool start();
    void stop();
    bool running() const;
    int sampleRate() const { return m_rate; }
    int channels() const { return m_channels; }

signals:
    // Pump thread: interleaved s16le frames (a copy — a queued hop
    // must not dangle on the pump's buffer).
    void chunk(const QByteArray &frames, int frameCount);

private:
    // The pump's period: 20 ms (50 chunks/s), in nanoseconds.
    static constexpr qint64 kPeriodNs = 20'000'000;

    // The thread body: the self-paced pump.
    void pump();

    RingBuffer *m_ring = nullptr;
    int m_rate = 0;
    int m_channels = 0;
    PumpThread m_thread{this};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_quit{false};
};

#endif // CAPTUREPUMP_H
