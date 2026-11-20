// pulsestub (standalone) — the fake PulseAudio server as a separate
// process: the test fixture (the ffmpeg plugin's pulseout test runs it
// in discard mode, a gold-capture harness runs it against a fifo).
// The server itself is the in-process one the spotify plugin runs
// (pulsestub.cpp) — here the captured PCM goes to a fifo (the old
// capture contract: a blocking writer whose full pipe paces the
// pipeline) or is discarded.
//
// usage: pulsestub <socket-path> [out.fifo]
//   (out omitted, "-" or "discard": the PCM is dropped, not written)

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <unistd.h>

#include "pulsestub.h"

namespace {

// The capture's output: the fifo (a blocking writer — a full pipe
// paces the pipeline) or a drop.
struct Out
{
    bool discard = false;
    int fd = -1;

    bool open(const char *path)
    {
        if (path == nullptr) {
            discard = true;
            return true;
        }
        // O_RDWR (not O_WRONLY): a plain O_WRONLY open on a fifo parks
        // until the reader appears (before the stub can even listen),
        // and O_RDWR keeps the write end open so a dead reader
        // surfaces as a full pipe (the write blocks — the signal's
        // stop ends it) rather than SIGPIPE.
        fd = ::open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0)
            return false;
        // The pipe's default capacity is small (4-64 kB by distro),
        // while the capture's quanta are ~100 ms: grow the pipe so a
        // stalled reader costs latency, never a gap (a failed resize
        // keeps the default).
        ::fcntl(fd, F_SETPIPE_SZ, 262144);
        return true;
    }

    void write(const uint8_t *p, size_t n) const
    {
        if (discard)
            return;
        size_t sent = 0;
        while (sent < n) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            const ssize_t r = ::write(fd, p + sent, n - sent);
            if (r < 0) {
                if (errno == EINTR)
                    continue;
                return;  // a dead reader (the write end stays ours)
            }
            sent += static_cast<size_t>(r);
        }
    }
};

Out g_out;  // the captured PCM (the loop is single-threaded)

// The main loop's stop flag (the signal handlers set it — a plain
// store, the safe subset for a signal handler).
std::atomic<bool> g_term{false};

void onTerm(int /*signum*/)
{
    g_term = true;
}

}  // namespace

int main(int argc, char **argv)
{
    // The argv reads are the command-line idiom.
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: %s <socket-path> [out.fifo]\n", argv[0]);
        return 2;
    }
    const std::string socketPath = argv[1];
    const std::string outPath =
        (argc == 3 && std::string(argv[2]) != "-"
         && std::string(argv[2]) != "discard")
            ? argv[2]
            : "";
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    // A dead reader must not kill the stub with SIGPIPE (the reader's
    // open/close is the normal lifecycle).
    ::signal(SIGPIPE, SIG_IGN);
    ::signal(SIGTERM, onTerm);
    ::signal(SIGINT, onTerm);
    if (!outPath.empty() && !g_out.open(outPath.c_str())) {
        fprintf(stderr, "pulsestub: cannot open %s\n", outPath.c_str());
        return 1;
    }

    pulsestub::Server server{[](const uint8_t *p, size_t n) {
        g_out.write(p, n);
    }};
    if (!server.start(socketPath)) {
        fprintf(stderr, "pulsestub: cannot listen on %s\n",
                socketPath.c_str());
        return 1;
    }
    while (!g_term.load())
        ::usleep(20000);
    server.stop();
    return 0;
}
