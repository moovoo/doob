#ifndef PRIVATEPULSE_H
#define PRIVATEPULSE_H

#include <QObject>

#include <vector>

#include <doob/ringbuffer.h>

#include "pulsestub.h"

class QFile;

// A private audio server owned by the plugin, with one null sink
// (`doob`) and nothing else: the WebEngine's audio lands on it, and
// the plugin reads the sink's output back from the capture's ring —
// exclusive capture, no other app's audio can leak in, and the
// system server is only used for the final output.
//
// The server is the plugin's own `pulsestub` (plugins/spotify/
// pulsestub.cpp) run in-process: a minimal fake PulseAudio that
// speaks the native protocol on its own AF_UNIX socket on its own
// thread and hands each stream's PCM — remapped to the capture's
// pinned format (s16le, kSampleRate, kChannels) — to the ring. The
// ring's backpressure paces the whole chain (a full ring stops the
// server's write requests, the client's buffer fills, the WebEngine
// slows to real time), so a stall costs latency, never a gap. The
// server's write requests carry a persistent lead (the client's
// writable headroom stays at it — a descheduled server's thread
// must not starve the engine into a ~1 ms hole in the audio, a
// pop; pulsestub.h). The capture's pump (CapturePump) reads the
// ring at the audio's natural rate, the same way the FFmpeg
// driver's output pulls its ring.
//
// The directory layout under the base dir (the plugin's data
// directory): one subdirectory per launch, `pulse-<pid>-<n>` (unique
// per launch — a stale one from a crashed doob is swept at the next
// start, judged by the pid file). The server's files land there: the
// protocol socket (`native`) and `pid` (this process' pid). stop()
// does not remove the directory: the sweep on the next start does.
class PrivatePulse : public QObject
{
    Q_OBJECT
public:
    explicit PrivatePulse(QObject *parent = nullptr);
    ~PrivatePulse() override; // stops the server

    // The parent directory for the per-launch socket directories (the
    // plugin's data directory). Must be set before the first start().
    void setBaseDir(const QString &dir);

    // Starts the server (it is up when this returns — the socket is
    // bound before the thread starts). False (with a qWarning) when
    // the base dir is unset, the directory cannot be made, or the
    // socket would not bind.
    bool start();
    void stop();
    bool running() const;

    // The libpulse server address of the private server
    // ("unix:<dir>/native").
    QString serverString() const;
    QString socketDir() const { return m_socketDir; }

    // The sink's name (the server reports one sink, this one).
    static const char *sinkName();
    // The capture's ring: the server's sink writes into it (the
    // pinned s16le, kSampleRate, kChannels as interleaved stereo
    // float), the capture's pump reads it.
    RingBuffer &ring() { return m_ring; }
    // The capture's pinned spec (the server remaps every stream's
    // native format to it, so the capture's chunk size and pace are
    // built from a constant).
    static constexpr int kSampleRate = 44100;
    static constexpr int kChannels = 2;

private:
    // Sweeps the stale per-launch directories and makes the next one.
    QString makeSocketDir();
    // This process' pid into the pid file (the stale sweep reads it).
    void writePidFile();

    // The capture's decoupling (the server's sink writes, the pump
    // reads): two seconds of the pinned format — a stalled consumer
    // (the relay's blocking write) is absorbed as latency, not a gap.
    static constexpr int kRingSeconds = 2;
    RingBuffer m_ring{static_cast<size_t>(kSampleRate) * kRingSeconds};
    // The sink's s16le -> float staging (the server's thread alone).
    std::vector<float> m_sinkBuf;
    pulsestub::Server m_server;
    QString m_baseDir;
    QString m_socketDir;
    int m_sequence = 0;
};

#endif // PRIVATEPULSE_H
