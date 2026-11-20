#ifndef PULSESTUB_H
#define PULSESTUB_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// The private audio server of the spotify plugin.
//
// A minimal fake PulseAudio server: it speaks the PulseAudio native
// protocol over its own AF_UNIX stream socket (the helper's
// PULSE_SERVER points at it) and serves one null sink (`doob`) and
// nothing else. The WebEngine's playback stream lands on it, and the
// server hands the stream's PCM — remapped to the pinned capture
// format (s16le, 44100 Hz, stereo) — to the sink callback as it
// arrives. Exclusive capture: no other app's audio can leak in, and
// the system server is only used for the final output.
//
// The server runs the multi-client poll loop on its own thread (the
// owning process drives it; the standalone pulsestub_main.cpp is the
// same server as a separate process — the test fixture). Chromium
// opens two connections per session (the "Chromium input" service and
// the output service), so the loop is non-blocking and serves them
// all. The client is paced to real time by each stream's virtual
// reader (the write requests are issued as the reader frees space),
// and the sink callback's blocking is the capture's backpressure — a
// full ring stops the server's write requests, the client's buffer
// fills, and the WebEngine slows to real time: a stall costs
// latency, never a gap. The requests are issued kLeadMs AHEAD of the
// reader (the client's writable headroom stays at the lead): the tick
// runs on the server's thread, which a deschedule can delay by a few
// ms — with a zero headroom that delay starves the client's render
// thread for the same duration (a ~1 ms hole in the audio, a pop),
// so the headroom must outlive the tick's jitter. The server's thread
// runs at a raised priority (best effort — SCHED_FIFO, a high nice;
// an unprivileged session can do neither, and the lead carries it).
//
// The server also keeps the sink inputs' server-side state (the name,
// the volume, the mute) and answers the matching queries with
// subscription events — the client-side volume sync (PulseOut's, and
// the tests that exercise it) treats the server as the source of
// truth.
//
// The wire format is verified against the pulseaudio v17.0 sources
// (this distro's libpulse is 17.x, PA_PROTOCOL_VERSION 35):
//
// - src/pulsecore/native-common.h — the PA_COMMAND_* numbering and the
//   20-byte big-endian packet descriptor (length, channel, offset_hi,
//   offset_lo, flags; channel 0xFFFFFFFF = control);
// - src/pulsecore/tagstruct.h/.c — the tagstruct value types (t/L/B/x/
//   1/a/m/P/U/T/R/r/v/V/f);
// - src/pulse/context.c — the AUTH/SET_CLIENT_NAME handshake (the
//   reply's version is 16-bit, the upper bytes are the SHM/memfd
//   flags — absent here, so the client keeps the audio inline);
// - src/pulse/introspect.c — the GET_SERVER_INFO / GET_SINK_INFO /
//   GET_SINK_INPUT_INFO reply field orders (every reply must end at
//   tagstruct EOF — the client asserts it);
// - src/pulse/stream.c + src/pulsecore/protocol-native.c — the
//   CREATE_PLAYBACK_STREAM request/reply (the reply's sample spec and
//   channel map must echo the request's, the v21 reply carries a
//   format info) and the GET_PLAYBACK_LATENCY reply (v13:
//   underrun_for + playing_for);
// - the flow control: after the create reply (and after every consumed
//   data packet) the server sends PA_COMMAND_REQUEST (tag 0, channel,
//   bytes); the client only writes what it has been asked for.
//
// The environment (read on start): PULSESTUB_TRACE hexdumps every
// control packet, both directions; PULSESTUB_LOG=<path> duplicates
// the log (stderr) into the file, flushed per line.

namespace pulsestub {

class Conn;
class Stream;

class Server
{
public:
    // The client's persistent lead (the write requests are issued this
    // far AHEAD of the stream's virtual reader — the client's writable
    // headroom stays at the lead, so the tick's jitter (a descheduled
    // server's thread) cannot starve the client's render thread into
    // an underrun — a ~1 ms hole in the audio, a pop).
    static constexpr uint64_t kLeadMs = 300;

    // The captured PCM (the pinned format: s16le, 44100 Hz, stereo),
    // delivered on the server's thread as it arrives.
    using Sink = std::function<void(const uint8_t *pcm, size_t bytes)>;

    explicit Server(Sink sink);
    ~Server();  // stop()
    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;

    // Binds the socket (it is up when this returns) and starts the
    // thread. False when the socket would not bind (a running server
    // is refused by start() itself).
    bool start(const std::string &socketPath);
    // Stops the thread (joins it), closes the clients and unlinks the
    // socket. Safe to call twice and from the destructor.
    void stop();
    bool running() const;

private:
    // The thread body: the multi-client poll loop.
    void run();
    // The server-side log (stderr, and the PULSESTUB_LOG file when
    // set).
    void log(const std::string &name, const char *fmt, ...) const;
    // The converted bytes to the sink (the capture).
    void deliver(const std::vector<uint8_t> &pcm);
    // The sink input by its index (nullptr when none).
    Stream *findStream(uint32_t index);

    friend class Conn;

    Sink m_sink;
    int m_listenFd = -1;
    std::string m_socketPath;
    std::atomic<bool> m_stop{false};
    std::thread m_thread;
    // The server's objects (the server's thread): the client
    // connections and the sink inputs (global — any client can query
    // them, as in the real server).
    std::vector<std::unique_ptr<Conn>> m_conns;
    std::vector<std::unique_ptr<Stream>> m_streams;
    uint32_t m_nextIndex = 0;  // the sink inputs' indexes
    uint32_t m_nextClient = 0;  // the clients' indexes
    uint64_t m_totalBytes = 0;  // the captured bytes (the log)
    bool m_trace = false;  // PULSESTUB_TRACE: hexdump the control packets
    int m_traceSeq = 0;
    FILE *m_logFile = nullptr;  // PULSESTUB_LOG: a copy of the log
};

}  // namespace pulsestub

#endif // PULSESTUB_H
