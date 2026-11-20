#include "systemrelay.h"

#include <QDebug>

extern "C" {
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
}

SystemSinkRelay::~SystemSinkRelay()
{
    close();
}

bool SystemSinkRelay::open(int sampleRate, int channels)
{
    close();
    pa_sample_spec spec{};
    spec.format = PA_SAMPLE_S16LE;
    spec.rate = static_cast<uint32_t>(sampleRate);
    spec.channels = static_cast<unsigned>(channels);
    pa_channel_map map{};
    pa_channel_map_init_stereo(&map);

    // The default server (the user's) and the default sink (nullptr
    // addresses): the relay's only job is to hand the captured audio
    // to whatever the user listens on.
    // The default stream attributes inherit the sink's (a ~2 s buffer
    // with a matching prebuf): audio starts behind the previous
    // track's buffered tail, and a write that catches up dumps a big
    // chunk into a full buffer — the audio plays in chunks with gaps.
    // Keep the relay's buffer small instead: audio starts ~100 ms in,
    // the steady fill is ~200 ms, and a catch-up burst cannot grow
    // past 500 ms (all values in bytes at the stream's rate —
    // 176400 B/s for s16le stereo 44100).
    const uint32_t byteRate =
        static_cast<uint32_t>(spec.rate * spec.channels * sizeof(int16_t));
    pa_buffer_attr attr{};
    attr.maxlength = byteRate / 2;   // 500 ms
    attr.tlength = byteRate / 5;     // 200 ms
    attr.prebuf = byteRate / 10;     // 100 ms
    attr.minreq = byteRate / 10;     // 100 ms
    attr.fragsize = 0;
    m_simple = pa_simple_new(nullptr, "doob-relay", PA_STREAM_PLAYBACK,
                             nullptr, "doob.spotify", &spec, &map, &attr,
                             nullptr);
    if (m_simple == nullptr) {
        // A sink that will not take the small buffer (some Bluetooth
        // sinks): the defaults stay, the relay still works.
        qWarning("spotify: the system sink relay would not take the "
                 "small buffer (the sink's defaults stay)");
        m_simple = pa_simple_new(nullptr, "doob-relay", PA_STREAM_PLAYBACK,
                                 nullptr, "doob.spotify", &spec, &map,
                                 nullptr, nullptr);
    }
    return m_simple != nullptr;
}

void SystemSinkRelay::close()
{
    if (m_simple != nullptr) {
        pa_simple_free(m_simple);
        m_simple = nullptr;
    }
}

bool SystemSinkRelay::write(const void *data, size_t bytes)
{
    if (m_simple == nullptr || bytes == 0)
        return true;
    return pa_simple_write(m_simple, data, bytes, nullptr) == 0;
}
