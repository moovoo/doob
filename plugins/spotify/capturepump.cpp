#include "capturepump.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QVector>

#include <doob/ringbuffer.h>

#include <unistd.h>

void PumpThread::run()
{
    m_owner->pump();
}

CapturePump::CapturePump(RingBuffer *ring, int rate, int channels,
                         QObject *parent)
    : QObject(parent)
    , m_ring(ring)
    , m_rate(rate)
    , m_channels(channels)
{
}

CapturePump::~CapturePump()
{
    stop();
}

bool CapturePump::start()
{
    if (m_running.exchange(true))
        return false;
    m_quit = false;
    // QThread::start (Qt6) reports no failure: the m_running guard
    // above keeps a double start from ever happening.
    m_thread.start(QThread::HighPriority);
    return true;
}

void CapturePump::stop()
{
    if (!m_running.exchange(false))
        return;
    // The pump checks the flag on every period (<= 20 ms): the join is
    // quick.
    m_quit = true;
    m_thread.wait();
}

bool CapturePump::running() const
{
    return m_running.load();
}

void CapturePump::pump()
{
    // One chunk per period: 20 ms of the sink's format.
    const int chunkFrames = m_rate / 50;
    const int chunkBytes = chunkFrames * m_channels * 2;
    QVector<float> staging(static_cast<qsizetype>(chunkFrames)
                           * m_channels);
    QElapsedTimer clock;
    clock.start();
    qint64 next = 0;
    while (!m_quit.load()) {
        next += kPeriodNs;
        // One period's share from the ring (the rest of the ring — a
        // burst — stays in it and back-pressures the server); the ring
        // pads a shortfall with silence, so the output never drops.
        const size_t got =
            m_ring->read(staging.data(), static_cast<size_t>(chunkFrames));
        // float -> s16le (the chunk's format; the shortfall's silence
        // is the ring's zero-fill).
        QByteArray buf(chunkBytes, '\0');
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        auto *out = reinterpret_cast<int16_t *>(buf.data());
        const qsizetype n = static_cast<qsizetype>(got) * m_channels;
        for (qsizetype i = 0; i < n; ++i) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            out[i] = static_cast<int16_t>(qBound(
                -32768, static_cast<int>(staging[i] * 32768.0F), 32767));
        }
        emit chunk(buf, chunkFrames);
        // Pace to the fixed period: a slow consumer (the relay's
        // blocking write) re-anchors the clock — the excess stays in
        // the ring, it is never dumped as a burst.
        const qint64 nowNs = clock.nsecsElapsed();
        next = qMax(next, nowNs);
        if (next > nowNs)
            ::usleep(static_cast<useconds_t>((next - nowNs) / 1000));
    }
}
