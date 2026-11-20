#ifndef SYSTEMRELAY_H
#define SYSTEMRELAY_H

#include <cstddef>

struct pa_simple;

// The last hop of the Spotify playback path: a pa_simple playback
// stream on the user's default PulseAudio server (the system sink).
// The writes are blocking: a full server buffer parks the caller (the
// capture thread) until space opens — real-time pacing without a timer
// or an event loop, the way paplay paces itself.
//
// The stream is opened in the capture's format (the monitor's actual
// rate and channels): the server remaps it to the sink's format, so no
// resampler is needed on this side.
class SystemSinkRelay
{
public:
    SystemSinkRelay() = default;
    ~SystemSinkRelay();
    SystemSinkRelay(const SystemSinkRelay &) = delete;
    SystemSinkRelay &operator=(const SystemSinkRelay &) = delete;

    // Opens the stream on the server's default sink. False when the
    // server refuses it (no server at all, a rejected format).
    bool open(int sampleRate, int channels);
    void close();
    bool isOpen() const { return m_simple != nullptr; }

    // Writes every byte (the blocking API loops internally).
    bool write(const void *data, size_t bytes);

private:
    struct pa_simple *m_simple = nullptr;
};

#endif // SYSTEMRELAY_H
