// pulsestub — the private audio server of the spotify plugin.
//
// The server (the Server class in pulsestub.h): a minimal fake
// PulseAudio server that speaks the native protocol over its own
// AF_UNIX stream socket, serves one null sink (`doob`) and nothing
// else, and hands every stream's PCM — remapped to the pinned capture
// format (s16le, 44100 Hz, stereo) — to its sink callback as it
// arrives. The spotify plugin runs it in-process (PrivatePulse; the
// sink is the capture's ring); pulsestub_main.cpp runs the same
// server as a separate process (the test fixture — a fifo or a
// discard as the output).
//
// The output is pinned to the capture's spec (s16le, 44100 Hz, stereo)
// exactly like the real server's pipe sink: the stub remaps each
// stream's native format to it before it reaches the sink, so the
// capture's chunk size and pace are built from a constant. The
// sink's blocking is the pacing: a full capture (a stalled reader)
// stops the server's write requests, the client's buffer fills, and
// the WebEngine slows to real time — a stall costs latency, never a
// gap.
//
// The wire format is verified against the pulseaudio v17.0 sources —
// the full notes are in pulsestub.h.

#include "pulsestub.h"

#include <algorithm>
#include <array>
#include <arpa/inet.h>
#include <cerrno>
#include <cstdarg>
#include <cmath>
#include <cstring>
#include <deque>
#include <iterator>
#include <map>
#include <memory>
#include <unordered_map>

#include <endian.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

extern "C" {
#include <pulse/channelmap.h>
#include <pulse/sample.h>
}

namespace {

// The protocol constants (not in the installed public headers — the
// values from src/pulsecore/native-common.h and tagstruct.h).
enum : uint32_t {
    kCmdError = 0,
    kCmdReply = 2,
    kCmdCreatePlayback = 3,
    kCmdDeletePlayback = 4,
    kCmdAuth = 8,
    kCmdSetClientName = 9,
    kCmdLookupSink = 10,
    kCmdDrainPlayback = 12,
    kCmdGetPlaybackLatency = 14,
    kCmdGetServerInfo = 20,
    kCmdGetSinkInfo = 21,
    kCmdGetSinkInfoList = 22,
    kCmdGetSourceInfoList = 24,
    kCmdGetSinkInputInfo = 29,
    kCmdGetSinkInputInfoList = 30,
    kCmdSubscribe = 35,
    kCmdSetSinkInputVolume = 37,
    kCmdSetSinkMute = 39,
    kCmdCorkPlayback = 41,
    kCmdFlushPlayback = 42,
    kCmdTriggerPlayback = 43,
    kCmdSetDefaultSink = 44,
    kCmdSetPlaybackStreamName = 46,
    kCmdRequest = 61,  // server->client
    kCmdSubscribeEvent = 66,  // server->client
    kCmdSetSinkInputMute = 69,  // supported since protocol v11
};

// The subscription event values (src/pulsecore/subscription.h): the
// facility in the low byte, the type in the high byte.
constexpr uint32_t kEventSinkInput = 2;
constexpr uint32_t kEventNew = 0;
constexpr uint32_t kEventChange = 0x100;
constexpr uint32_t kEventRemove = 0x200;
constexpr uint32_t kSubMaskSinkInput = 0x4;  // PA_SUBSCRIPTION_MASK_SINK_INPUT

enum : uint8_t {
    kTagString = 't',
    kTagU32 = 'L',
    kTagU8 = 'B',
    kTagU64 = 'R',
    kTagS64 = 'r',
    kTagSampleSpec = 'a',
    kTagArbitrary = 'x',
    kTagBoolTrue = '1',
    kTagTimeval = 'T',
    kTagUsec = 'U',
    kTagChannelMap = 'm',
    kTagCvolume = 'v',
    kTagProplist = 'P',
    kTagVolume = 'V',
    kTagFormatInfo = 'f',
};

constexpr uint32_t kVolumeNorm = 65536;  // PA_VOLUME_NORM
constexpr int kErrProtocol = 7;      // PA_ERR_PROTOCOL (def.h)
constexpr int kErrNotImplemented = 23;  // PA_ERR_NOTIMPLEMENTED (def.h)
constexpr uint32_t kControlChannel = 0xFFFFFFFFU;
constexpr uint32_t kProtocolVersion = 35;  // this libpulse's PA_PROTOCOL_VERSION
constexpr uint32_t kInvalidIndex = 0xFFFFFFFFU;
constexpr size_t kMaxPacket = static_cast<size_t>(16) * 1024 * 1024;

// The sink's base latency (the GET_PLAYBACK_LATENCY reply's sink_usec,
// the create reply's sink latency). A null sink has no hardware buffer,
// so its base latency is small — the stream's *measured* latency is
// the bytes still in the server buffer (write_index - read_index),
// which the real-time consumption model below keeps at ~one pump pass.
// A small positive constant keeps the client's latency reading
// comfortably above zero without lagging the tap's shift by seconds.
constexpr uint64_t kSinkLatencyUsec = 20000;  // 20 ms

// The fake device the stub reports (the name and the pinned spec of
// the private sink, so a client sees what it would in production).
constexpr const char *kSinkName = "doob";
constexpr const char *kMonitorSource = "doob.monitor";
constexpr uint8_t kSinkFormat = PA_SAMPLE_S16LE;
constexpr uint8_t kSinkChannels = 2;
constexpr uint32_t kSinkRate = 44100;

// The capture's pinned format (the sink's output always carries it —
// the server remaps the streams' native formats to it, as the real
// server's pipe sink did).
constexpr uint8_t kOutChannels = 2;
constexpr uint32_t kOutRate = kSinkRate;

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic,bugprone-signed-bitwise)
std::string hexdump(const uint8_t *p, size_t n)
{
    const auto digit = [](uint8_t v) {
        return static_cast<char>(v < 10 ? '0' + v : 'a' + v - 10);
    };
    std::string s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        s += digit((p[i] >> 4) & 0xF);
        s += digit(p[i] & 0xF);
    }
    return s;
}
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic,bugprone-signed-bitwise)

const char *cmdName(uint32_t cmd)
{
    switch (cmd) {
    case kCmdAuth: return "AUTH";
    case kCmdSetClientName: return "SET_CLIENT_NAME";
    case kCmdCreatePlayback: return "CREATE_PLAYBACK_STREAM";
    case kCmdDeletePlayback: return "DELETE_PLAYBACK_STREAM";
    case kCmdDrainPlayback: return "DRAIN_PLAYBACK_STREAM";
    case kCmdGetPlaybackLatency: return "GET_PLAYBACK_LATENCY";
    case kCmdGetServerInfo: return "GET_SERVER_INFO";
    case kCmdLookupSink: return "LOOKUP_SINK";
    case kCmdGetSinkInfo: return "GET_SINK_INFO";
    case kCmdGetSinkInfoList: return "GET_SINK_INFO_LIST";
    case kCmdGetSourceInfoList: return "GET_SOURCE_INFO_LIST";
    case kCmdGetSinkInputInfo: return "GET_SINK_INPUT_INFO";
    case kCmdGetSinkInputInfoList: return "GET_SINK_INPUT_INFO_LIST";
    case kCmdSubscribe: return "SUBSCRIBE";
    case kCmdSetSinkInputVolume: return "SET_SINK_INPUT_VOLUME";
    case kCmdSetSinkInputMute: return "SET_SINK_INPUT_MUTE";
    case kCmdSetSinkMute: return "SET_SINK_MUTE";
    case kCmdSetDefaultSink: return "SET_DEFAULT_SINK";
    case kCmdCorkPlayback: return "CORK_PLAYBACK_STREAM";
    case kCmdFlushPlayback: return "FLUSH_PLAYBACK_STREAM";
    case kCmdTriggerPlayback: return "TRIGGER_PLAYBACK_STREAM";
    case kCmdSetPlaybackStreamName: return "SET_PLAYBACK_STREAM_NAME";
    case kCmdRequest: return "REQUEST";
    case kCmdSubscribeEvent: return "SUBSCRIBE_EVENT";
    default: return nullptr;
    }
}

// The pa_sample_format enum names (sample.h) for the format.* props.
const char *sampleFormatName(uint8_t format)
{
    switch (format) {
    case PA_SAMPLE_U8: return "u8";
    case PA_SAMPLE_ALAW: return "alaw";
    case PA_SAMPLE_ULAW: return "ulaw";
    case PA_SAMPLE_S16LE: return "s16le";
    case PA_SAMPLE_S16BE: return "s16be";
    case PA_SAMPLE_FLOAT32LE: return "f32le";
    case PA_SAMPLE_FLOAT32BE: return "f32be";
    case PA_SAMPLE_S32LE: return "s32le";
    case PA_SAMPLE_S32BE: return "s32be";
    case PA_SAMPLE_S24LE: return "s24le";
    case PA_SAMPLE_S24BE: return "s24be";
    case PA_SAMPLE_S24_32LE: return "s24le";
    case PA_SAMPLE_S24_32BE: return "s24be";
    default: return "unknown";
    }
}

// The pa_channel_map position names (channelmap.h) for the format.*
// channel_map prop (the gold capture quotes "front-left,front-right").
const char *channelPositionName(uint8_t pos)
{
    switch (pos) {
    case PA_CHANNEL_POSITION_MONO: return "mono";
    case PA_CHANNEL_POSITION_FRONT_LEFT: return "front-left";
    case PA_CHANNEL_POSITION_FRONT_RIGHT: return "front-right";
    case PA_CHANNEL_POSITION_FRONT_CENTER: return "front-center";
    case PA_CHANNEL_POSITION_LFE: return "low-frequency";
    case PA_CHANNEL_POSITION_REAR_LEFT: return "rear-left";
    case PA_CHANNEL_POSITION_REAR_RIGHT: return "rear-right";
    case PA_CHANNEL_POSITION_REAR_CENTER: return "rear-center";
    case PA_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER: return "front-left-of-center";
    case PA_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER: return "front-right-of-center";
    case PA_CHANNEL_POSITION_SIDE_LEFT: return "side-left";
    case PA_CHANNEL_POSITION_SIDE_RIGHT: return "side-right";
    default: return "unknown";
    }
}

// "front-left,front-right" from a channel map id list.
std::string channelMapName(uint32_t n, const uint8_t *ids)
{
    std::string s;
    for (uint32_t i = 0; i < n; ++i) {
        if (i != 0)
            s += ',';
        s += channelPositionName(ids[i]);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    }
    return s;
}

// --- the capture's format remap (the pinned output) -------------------
//
// Byte-level sample decoders (the G.711 port, the fixed-width reads,
// the resampler): bit shifts, byte pointer walks and 16/24/32-bit
// narrowing are the substance of this code, so the bounds/signed/
// narrowing checks are suppressed for the section.
// NOLINTBEGIN(bugprone-signed-bitwise,cppcoreguidelines-pro-bounds-pointer-arithmetic,bugprone-narrowing-conversions,modernize-avoid-c-style-cast,readability-implicit-bool-conversion)

size_t bytesPerSample(uint8_t format)
{
    switch (format) {
    case PA_SAMPLE_U8:
    case PA_SAMPLE_ALAW:
    case PA_SAMPLE_ULAW:
        return 1;
    case PA_SAMPLE_S16LE:
    case PA_SAMPLE_S16BE:
        return 2;
    case PA_SAMPLE_S24LE:
    case PA_SAMPLE_S24BE:
        return 3;
    case PA_SAMPLE_S24_32LE:
    case PA_SAMPLE_S24_32BE:
    case PA_SAMPLE_S32LE:
    case PA_SAMPLE_S32BE:
    case PA_SAMPLE_FLOAT32LE:
    case PA_SAMPLE_FLOAT32BE:
        return 4;
    default:
        return 0;
    }
}

// The Sun G.711 decodes (pulseaudio's src/pulsecore/g711.c).
int16_t alaw2linear16(uint8_t a_val)
{
    constexpr int16_t kSignBit = 0x80;
    constexpr int16_t kQuantMask = 0xF;
    constexpr int16_t kSegMask = 0x70;
    constexpr int16_t kSegShift = 4;
    int16_t t = 0;
    int16_t seg = 0;
    a_val ^= 0x55;
    t = (a_val & kQuantMask) << 4;
    seg = ((int)a_val & kSegMask) >> kSegShift;
    switch (seg) {
    case 0:
        t += 8;
        break;
    case 1:
        t += 0x108;
        break;
    default:
        t += 0x108;
        t <<= seg - 1;
    }
    return ((a_val & kSignBit) ? t : -t);
}

int16_t ulaw2linear16(uint8_t u_val)
{
    constexpr int16_t kSignBit = 0x80;
    constexpr int16_t kQuantMask = 0xf;
    constexpr int16_t kSegMask = 0x70;
    constexpr int16_t kSegShift = 4;
    constexpr int16_t kBias = 0x84;
    int16_t t = 0;
    u_val = static_cast<uint8_t>(~u_val);
    t = ((u_val & kQuantMask) << 3) + kBias;
    t <<= ((int)u_val & kSegMask) >> kSegShift;
    return ((u_val & kSignBit) ? (kBias - t) : (t - kBias));
}

// A native sample to float (the conversion goes through float so the
// resampler and the clamp share one path).
float decodeSample(uint8_t format, const uint8_t *p)
{
    switch (format) {
    case PA_SAMPLE_U8:
        return static_cast<float>(p[0] - 128) / 128.0F;
    case PA_SAMPLE_ALAW:
        return static_cast<float>(alaw2linear16(p[0])) / 32768.0F;
    case PA_SAMPLE_ULAW:
        return static_cast<float>(ulaw2linear16(p[0])) / 32768.0F;
    case PA_SAMPLE_S16LE: {
        uint16_t v = 0;
        std::memcpy(&v, p, 2);
        return static_cast<float>(static_cast<int16_t>(v)) / 32768.0F;
    }
    case PA_SAMPLE_S16BE:
        return static_cast<float>(static_cast<int16_t>(
            (static_cast<uint16_t>(p[0]) << 8) | p[1])) / 32768.0F;
    case PA_SAMPLE_S24LE: {
        int32_t v = p[0] | (p[1] << 8) | (p[2] << 16);
        if (v & 0x800000)
            v |= ~0xFFFFFF;
        return static_cast<float>(v) / 8388608.0F;
    }
    case PA_SAMPLE_S24BE: {
        int32_t v = (p[0] << 16) | (p[1] << 8) | p[2];
        if (v & 0x800000)
            v |= ~0xFFFFFF;
        return static_cast<float>(v) / 8388608.0F;
    }
    case PA_SAMPLE_S24_32LE:
        return decodeSample(PA_SAMPLE_S24LE, p);
    case PA_SAMPLE_S24_32BE:
        return decodeSample(PA_SAMPLE_S24BE, p);
    case PA_SAMPLE_S32LE: {
        int32_t v = 0;
        std::memcpy(&v, p, 4);
        return static_cast<float>(v) / 2147483648.0F;
    }
    case PA_SAMPLE_S32BE: {
        int32_t v = (static_cast<int32_t>(p[0]) << 24) | (p[1] << 16)
            | (p[2] << 8) | p[3];
        return static_cast<float>(v) / 2147483648.0F;
    }
    case PA_SAMPLE_FLOAT32LE: {
        float f = 0.0F;
        std::memcpy(&f, p, 4);
        return f;
    }
    case PA_SAMPLE_FLOAT32BE: {
        uint32_t u = 0;
        std::memcpy(&u, p, 4);
        u = ((u & 0xFF000000U) >> 24) | ((u & 0x00FF0000U) >> 8)
            | ((u & 0x0000FF00U) << 8) | ((u & 0x000000FFU) << 24);
        float f = 0.0F;
        std::memcpy(&f, &u, 4);
        return f;
    }
    default:
        return 0.0F;
    }
}

// One stream's remap to the pinned capture format (s16le, kOutRate,
// stereo): the samples through float (the format's decode), the
// channels to stereo (mono duplicated, the rest dropped), and the rate
// by linear interpolation when it differs from the pinned one. The
// per-stream state is the resampler's position — the conversion is
// stateless otherwise, so a packet boundary cannot desync it.
class Converter
{
public:
    Converter() = default;
    Converter(uint8_t format, uint8_t channels, uint32_t rate)
        : m_format(format)
        , m_channels(channels)
        , m_rate(rate)
    {
        m_resample = (rate != 0 && rate != kOutRate);
        m_pos = 0.0;
    }

    // The pinned-format bytes of the native data, appended to out.
    void convert(const uint8_t *data, size_t nbytes, std::vector<uint8_t> &out)
    {
        const size_t frameBytes = m_channels * bytesPerSample(m_format);
        if (frameBytes == 0)
            return;
        m_tail.insert(m_tail.end(), data, data + nbytes);
        size_t from = 0;
        while (m_tail.size() - from >= frameBytes) {
            const uint8_t *frame = m_tail.data() + from;
            from += frameBytes;
            float l = 0.0F;
            float r = 0.0F;
            if (m_channels >= 1)
                l = decodeSample(m_format, frame);
            if (m_channels >= 2)
                r = decodeSample(m_format, frame + bytesPerSample(m_format));
            else
                r = l;  // mono: duplicate to both channels
            if (m_resample)
                resampleFrame(l, r, out);
            else
                directFrame(l, r, out);
        }
        if (from > 0)
            m_tail.erase(m_tail.begin(), m_tail.begin() + from);
    }

private:
    void directFrame(float l, float r, std::vector<uint8_t> &out)
    {
        int16_t sl = static_cast<int16_t>(
            std::clamp(static_cast<int>(l * 32768.0F), -32768, 32767));
        int16_t sr = static_cast<int16_t>(
            std::clamp(static_cast<int>(r * 32768.0F), -32768, 32767));
        out.insert(out.end(),
                   {static_cast<uint8_t>(sl & 0xFF),
                    static_cast<uint8_t>((sl >> 8) & 0xFF),
                    static_cast<uint8_t>(sr & 0xFF),
                    static_cast<uint8_t>((sr >> 8) & 0xFF),});
    }

    // Linear interpolation in the input domain: the read position
    // advances by in/out per output frame, interpolating between the
    // two input frames it sits between (one output frame of latency —
    // the read needs both endpoints).
    void resampleFrame(float l, float r, std::vector<uint8_t> &out)
    {
        m_buf.emplace_back(l, r);
        const double step = static_cast<double>(m_rate) / kOutRate;
        while (static_cast<double>(m_buf.size()) - m_pos >= 2.0) {
            const size_t base = static_cast<size_t>(m_pos);
            const double f = m_pos - static_cast<double>(base);
            const auto &a = m_buf[base];
            const auto &b = m_buf[base + 1];
            directFrame(
                static_cast<float>(
                    ((1.0 - f) * a.first) + (f * b.first)),
                static_cast<float>(
                    ((1.0 - f) * a.second) + (f * b.second)),
                out);
            m_pos += step;
        }
        // Drop the frames fully behind the read position.
        const size_t drop = static_cast<size_t>(m_pos);
        if (drop > 0) {
            m_buf.erase(m_buf.begin(), m_buf.begin() + drop);
            m_pos -= static_cast<double>(drop);
        }
    }

    uint8_t m_format = 0;
    uint8_t m_channels = 0;
    uint32_t m_rate = 0;
    bool m_resample = false;
    std::vector<uint8_t> m_tail;  // the not-yet-complete frame bytes
    std::deque<std::pair<float, float>> m_buf;  // the input frames
    double m_pos = 0.0;  // the read position in input frames
};
// NOLINTEND(bugprone-signed-bitwise,cppcoreguidelines-pro-bounds-pointer-arithmetic,bugprone-narrowing-conversions,modernize-avoid-c-style-cast,readability-implicit-bool-conversion)

// --- the wire format ----------------------------------------------------

// A big-endian byte builder. The tag-aware methods write the type
// byte + the data (the tagstruct value layout); the raw* methods are
// the data only (the composites' interiors).
// The builder and the cursor below walk raw byte buffers: pointer
// arithmetic over them and the byte-wise casts are the wire codec
// itself, so the bounds checks are suppressed for the section.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-bounds-array-to-pointer-decay,cppcoreguidelines-pro-type-reinterpret-cast)
struct Buf
{
    std::vector<uint8_t> b;
    void rawU8(uint8_t v) { b.push_back(v); }
    void rawU32(uint32_t v)
    {
        v = htonl(v);
        const auto *p = reinterpret_cast<const uint8_t *>(&v);
        b.insert(b.end(), p, p + 4);
    }
    void rawU64(uint64_t v)
    {
        v = htobe64(v);
        const auto *p = reinterpret_cast<const uint8_t *>(&v);
        b.insert(b.end(), p, p + 8);
    }
    void u32(uint32_t v)
    {
        rawU8(kTagU32);  // 'L'
        rawU32(v);
    }
    void u64(uint64_t v)
    {
        rawU8(kTagU64);  // 'R'
        rawU64(v);
    }
    void s64(int64_t v)
    {
        rawU8(kTagS64);  // 'r'
        rawU64(static_cast<uint64_t>(v));
    }
    void str(const char *s)
    {
        if (s == nullptr) {
            rawU8('N');  // PA_TAG_STRING_NULL
            return;
        }
        rawU8(kTagString);  // 't' — the data is NUL-terminated, no length
        b.insert(b.end(), s, s + std::strlen(s) + 1);
    }
    void str(const std::string &s) { str(s.c_str()); }
    void boolean(bool v) { rawU8(v ? '1' : '0'); }
    void u8(uint8_t v) { rawU8(v); }
    // A tagged u8 (the 'B' type byte + the value) — pa_tagstruct_putu8.
    void u8Tagged(uint8_t v)
    {
        rawU8(kTagU8);  // 'B'
        rawU8(v);
    }
    void sampleSpec(uint8_t format, uint8_t channels, uint32_t rate)
    {
        rawU8(kTagSampleSpec);  // 'a'
        rawU8(format);
        rawU8(channels);
        rawU32(rate);
    }
    void channelMap(uint32_t n, const uint8_t *ids)
    {
        rawU8(kTagChannelMap);  // 'm'
        rawU8(static_cast<uint8_t>(n));  // the count is a raw u8
        for (uint32_t i = 0; i < n; ++i)
            rawU8(ids[i]);
    }
    void proplist()
    {
        rawU8(kTagProplist);  // 'P'
        str(nullptr);  // the terminator only
    }
    // A proplist with the standard device.* props (the gold null-sink
    // carries exactly these three), so introspection shows a device.
    void proplistDevice(const char *description)
    {
        rawU8(kTagProplist);  // 'P'
        prop("device.description", description);
        prop("device.class", "abstract");
        prop("device.icon_name", "audio-card");
        str(nullptr);  // the terminator
    }
    void cvolume(uint32_t n, const uint32_t *values)
    {
        rawU8(kTagCvolume);  // 'v'
        rawU8(static_cast<uint8_t>(n));  // the count is a raw u8
        for (uint32_t i = 0; i < n; ++i)
            rawU32(values[i]);
    }
    void cvolume(const std::vector<uint32_t> &values)
    {
        cvolume(static_cast<uint32_t>(values.size()), values.data());
    }
    void volume(uint32_t v)
    {
        rawU8(kTagVolume);  // 'V'
        rawU32(v);
    }
    void usec(uint64_t v)
    {
        rawU8(kTagUsec);  // 'U'
        rawU64(v);
    }
    void timeval(uint32_t sec, uint32_t usec)
    {
        rawU8(kTagTimeval);  // 'T'
        rawU32(sec);
        rawU32(usec);
    }
    // A proplist entry: (t key, L len, x len, data) — the data is the
    // NUL-terminated value (the server's format.* props are quoted
    // strings for sample_format/channel_map, plain for rate/channels).
    void prop(const char *key, const char *val)
    {
        str(key);
        const uint32_t len = static_cast<uint32_t>(std::strlen(val)) + 1;
        rawU8(kTagU32);
        rawU32(len);
        rawU8(0x78);  // PA_TAG_DATA 'x'
        rawU32(len);
        b.insert(b.end(), val, val + len);
    }
    // The v21+ format info. The server writes encoding = PA_ENCODING_PCM
    // (1) with the format details in the proplist (the gold capture).
    // `emptyProps` matches the sink/source info, which carries no props.
    void formatInfoPcm(uint8_t format, uint8_t channels, uint32_t rate,
                       uint32_t nmap, const uint8_t *map, bool emptyProps)
    {
        rawU8(kTagFormatInfo);  // 'f'
        rawU8(kTagU8);  // 'B'
        rawU8(1);  // PA_ENCODING_PCM
        rawU8(kTagProplist);  // 'P'
        if (!emptyProps) {
            const std::string sf =
                std::string("\"") + sampleFormatName(format) + "\"";
            prop("format.sample_format", sf.c_str());
            prop("format.rate", std::to_string(rate).c_str());
            prop("format.channels", std::to_string(channels).c_str());
            const std::string cm =
                std::string("\"") + channelMapName(nmap, map) + "\"";
            prop("format.channel_map", cm.c_str());
        }
        str(nullptr);  // the proplist terminator
    }
};

// A tagstruct cursor. Every tagstruct value is a type byte + the
// data; the getters consume the type byte and fail on a mismatch.
// The composites' interiors are raw (no type bytes).
struct Rdr
{
    const uint8_t *p = nullptr;
    size_t n = 0;
    size_t i = 0;
    bool ok = true;

    uint8_t u8()
    {
        if (!ok || i + 1 > n) { ok = false; return 0; }
        return p[i++];
    }
    uint32_t rawU32()
    {
        if (!ok || i + 4 > n) { ok = false; return 0; }
        uint32_t v = 0;
        std::memcpy(&v, p + i, 4);
        i += 4;
        return ntohl(v);
    }
    uint8_t u8Tagged()
    {
        if (u8() != kTagU8) { ok = false; return 0; }
        return u8();
    }
    uint32_t u32()
    {
        if (u8() != kTagU32) { ok = false; return 0; }
        return rawU32();
    }
    uint64_t u64()
    {
        if (u8() != kTagU64) { ok = false; return 0; }
        if (!ok || i + 8 > n) { ok = false; return 0; }
        uint64_t v = 0;
        std::memcpy(&v, p + i, 8);
        i += 8;
        return be64toh(v);
    }
    std::string str()
    {
        const uint8_t t = u8();
        if (t == 'N')  // PA_TAG_STRING_NULL
            return {};
        if (t != kTagString) { ok = false; return {}; }
        // The data is NUL-terminated (no length prefix).
        size_t len = 0;
        while (i + len < n && p[i + len] != 0)
            ++len;
        if (i + len >= n) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char *>(p + i), len);
        i += len + 1;
        return s;
    }
    bool boolean()
    {
        const uint8_t t = u8();
        if (t != kTagBoolTrue && t != '0') { ok = false; return false; }
        return t == kTagBoolTrue;
    }
    void sampleSpec(uint8_t &format, uint8_t &channels, uint32_t &rate)
    {
        if (u8() != kTagSampleSpec) { ok = false; return; }
        format = u8();
        channels = u8();
        rate = rawU32();
    }
    bool channelMap(std::vector<uint8_t> &ids)
    {
        if (u8() != kTagChannelMap) { ok = false; return false; }
        const uint32_t count = u8();  // a raw u8
        if (!ok || i + count > n) { ok = false; return false; }
        ids.assign(p + i, p + i + count);
        i += count;
        return true;
    }
    void skipProplist()
    {
        if (u8() != kTagProplist) { ok = false; return; }
        for (;;) {
            const std::string key = str();
            if (!ok)
                return;
            if (key.empty())
                return;  // the terminator
            // L len + x len + data
            if (u8() != kTagU32) { ok = false; return; }
            const uint32_t len = rawU32();
            if (u8() != kTagArbitrary) { ok = false; return; }
            const uint32_t xlen = rawU32();
            if (!ok || xlen != len || i + len > n) { ok = false; return; }
            i += len;
        }
    }
    // The proplist's entries (key -> value, the value's NUL trimmed).
    void proplist(std::map<std::string, std::string> &out)
    {
        if (u8() != kTagProplist) { ok = false; return; }
        for (;;) {
            const std::string key = str();
            if (!ok)
                return;
            if (key.empty())
                return;  // the terminator
            if (u8() != kTagU32) { ok = false; return; }
            const uint32_t len = rawU32();
            if (u8() != kTagArbitrary) { ok = false; return; }
            const uint32_t xlen = rawU32();
            if (!ok || xlen != len || i + len > n) { ok = false; return; }
            out[key] =
                std::string(reinterpret_cast<const char *>(p + i),
                            len > 0 ? len - 1 : 0);
            i += len;
        }
    }
    void skipCvolume()
    {
        if (u8() != kTagCvolume) { ok = false; return; }
        const uint32_t count = u8();  // a raw u8
        for (uint32_t c = 0; c < count; ++c)
            rawU32();
    }
    // The cvolume's values (the sink input's volume).
    void cvolume(std::vector<uint32_t> &out)
    {
        if (u8() != kTagCvolume) { ok = false; return; }
        const uint32_t count = u8();  // a raw u8
        out.clear();
        for (uint32_t c = 0; c < count; ++c)
            out.push_back(rawU32());
    }
    void skipArbitrary()
    {
        if (u8() != kTagArbitrary) { ok = false; return; }
        const uint32_t len = rawU32();
        if (!ok || i + len > n) { ok = false; return; }
        i += len;
    }
    void skipTimeval()
    {
        if (u8() != kTagTimeval) { ok = false; return; }
        rawU32();
        rawU32();
    }
    void skipTimevalValue(uint32_t &sec, uint32_t &usec)
    {
        if (u8() != kTagTimeval) { ok = false; return; }
        sec = rawU32();
        usec = rawU32();
    }
    void skipFormatInfo()
    {
        if (u8() != kTagFormatInfo) { ok = false; return; }
        if (u8() != kTagU8) { ok = false; return; }  // the encoding is 'B'
        u8();
        skipProplist();
    }
};
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-bounds-array-to-pointer-decay,cppcoreguidelines-pro-type-reinterpret-cast)

struct Packet
{
    uint32_t length = 0;
    uint32_t channel = 0;
    std::vector<uint8_t> payload;
};

bool sendAll(int fd, const void *data, size_t length)
{
    const auto *p = static_cast<const uint8_t *>(data);
    size_t sent = 0;
    while (sent < length) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        const ssize_t r = ::send(fd, p + sent, length - sent, MSG_NOSIGNAL);
        if (r <= 0)
            return false;
        sent += static_cast<size_t>(r);
    }
    return true;
}

}  // namespace

namespace pulsestub {

// The server-side object: a sink input (one per playback stream —
// global to the server, as in the real PA: any client can query it).
struct Stream
{
    uint32_t index = 0;  // the sink input's index
    uint32_t client = 0;  // the creating client's index
    uint8_t format = 0;
    uint8_t channels = 0;
    uint32_t rate = 0;
    std::vector<uint8_t> map;
    std::string name;  // the media.name prop (the client's stream name)
    uint32_t maxlength = 0;
    uint32_t tlength = 0;
    uint32_t prebuf = 0;
    uint32_t minreq = 0;
    bool corked = false;
    bool playing = false;
    uint64_t writeBytes = 0;  // the total inline bytes received
    // The null sink's real-time consumption model: a virtual reader
    // advances at the stream's byte rate from the first byte, and the
    // server requests more only as the reader frees space. That paces
    // the client to real time (a request flood would let it drain the
    // buffer at CPU speed) and keeps the buffer — the client's
    // measured latency — at ~one pump pass.
    uint64_t byteRate = 0;  // the stream's bytes per second
    bool started = false;  // the first byte has arrived
    uint64_t consumedUsec = 0;  // the reader's clock (frozen while corked)
    struct timeval lastTick{};  // the last tick (for the clock's delta)
    uint64_t lastRequested = 0;  // the total bytes requested so far
    std::vector<uint32_t> volume;  // per channel (PA_VOLUME_NORM)
    bool muted = false;
    Converter converter;

    // The virtual reader's position (the bytes "played" so far): the
    // consumed clock at the stream's byte rate, clamped to what has
    // been written (the reader never runs ahead of the data).
    uint64_t readIndex() const
    {
        if (!started || byteRate == 0)
            return 0;
        const uint64_t played = byteRate * consumedUsec / 1000000ULL;
        return played < writeBytes ? played : writeBytes;
    }

    // One frame's bytes (channels * sample size). Request amounts are
    // always frame-aligned: the client writes in whole frames, so an
    // unaligned request leaves a sub-frame residual in its
    // requested_bytes that never drains — pa_stream_writable_size()
    // stays > 0, the pump issues zero-byte writes, and the mainloop's
    // poll never parks (a full core).
    uint32_t frameSize() const
    {
        return static_cast<uint32_t>(channels)
            * static_cast<uint32_t>(bytesPerSample(format));
    }
};

// The reply bodies and the auto-attr resolution: the server's
// private helpers (Conn's dispatch is the only caller).
namespace {

// The server's auto-attr resolution (protocol-native.c's
// update_buffer_attr): a client may pass 0xFFFFFFFF for any of the four
// buffer attrs and the server then picks its defaults — the reply and
// the flow control must carry the *resolved* values, not the
// 0xFFFFFFFF (paplay sends all four auto).
void resolveAttrs(Stream &s)
{
    const uint32_t frame =
        s.channels * bytesPerSample(s.format);
    const auto bytesForMsec = [&s](uint32_t msec) {
        const uint64_t bytes = static_cast<uint64_t>(s.rate) * msec
            * s.channels * bytesPerSample(s.format) / 1000U;
        return static_cast<uint32_t>(bytes);
    };
    constexpr uint32_t kMaxMemblockqLength = 4U * 1024U * 1024U;
    // The 0xFFFFFFFF "auto" value exceeds the cap, so it is covered.
    s.maxlength = std::min(s.maxlength, kMaxMemblockqLength);
    if (s.maxlength <= 0)
        s.maxlength = frame;
    if (s.tlength == 0xFFFFFFFFU)
        s.tlength = bytesForMsec(2000);  // DEFAULT_TLENGTH_MSEC
    if (s.tlength <= 0)
        s.tlength = frame;
    s.tlength = std::min(s.tlength, s.maxlength);
    if (s.minreq == 0xFFFFFFFFU) {
        const uint32_t process = bytesForMsec(20);  // DEFAULT_PROCESS_MSEC
        uint32_t m = s.tlength / 4;
        if (frame != 0)
            m -= m % frame;
        s.minreq = std::min(process, m);
    }
    if (s.minreq <= 0)
        s.minreq = frame;
    s.tlength = std::max(s.tlength, s.minreq + frame);
    const uint32_t maxPrebuf = s.tlength + frame - s.minreq;
    if (s.prebuf == 0xFFFFFFFFU || s.prebuf > maxPrebuf)
        s.prebuf = maxPrebuf;
}

// The sink input's info (the verified field order of
// protocol-native.c's sink_input_fill_tagstruct, the v11/v13/v19/v20/
// v21 tail included — the client asserts the EOF after it).
Buf sinkInputInfo(const Stream &s)
{
    Buf body;
    body.u32(s.index);
    body.str(s.name);
    body.u32(0);  // owner_module
    body.u32(s.client);
    body.u32(0);  // the sink index (the single sink is index 0)
    body.sampleSpec(s.format, s.channels, s.rate);
    body.channelMap(static_cast<uint32_t>(s.map.size()), s.map.data());
    body.cvolume(s.volume);
    body.usec(0);  // the stream's latency
    body.usec(0);  // the sink's latency
    body.str("sinc");  // the resample method
    body.str("protocol-native");  // the driver
    body.boolean(s.muted);  // mute (v11)
    body.proplist();  // (v13)
    body.boolean(s.corked);  // (v19)
    body.boolean(true);  // has_volume (v20)
    body.boolean(true);  // volume_writable (v20)
    body.formatInfoPcm(s.format, s.channels, s.rate,
                       static_cast<uint32_t>(s.map.size()), s.map.data(),
                       /*emptyProps=*/true);  // (v21)
    return body;
}

// The fake sink's info (the verified GET_SINK_INFO reply order, the
// v16/v21 tail included — the client asserts n_formats >= 1).
Buf sinkInfo()
{
    Buf body;
    body.u32(0);  // the sink index (the single sink is index 0)
    body.str(kSinkName);
    body.str("doob (the stub sink)");
    body.sampleSpec(kSinkFormat, kSinkChannels, kSinkRate);
    const std::array<uint8_t, 2> map = {
        PA_CHANNEL_POSITION_FRONT_LEFT,
        PA_CHANNEL_POSITION_FRONT_RIGHT,
    };
    body.channelMap(2, map.data());
    body.u32(0);  // owner_module
    const std::array<uint32_t, 2> vol = {kVolumeNorm, kVolumeNorm};
    body.cvolume(2, vol.data());
    body.boolean(false);  // mute
    body.u32(0);  // monitor_source index
    body.str(kMonitorSource);
    body.usec(0);  // latency
    body.str("pulsestub");  // driver
    body.u32(0);  // flags
    body.proplistDevice("doob (the stub sink)");
    body.usec(0);  // requested_latency (v13)
    body.volume(kVolumeNorm);  // base_volume (v15)
    body.u32(1);  // state PA_SINK_RUNNING (v15; 0=UNLINKED)
    body.u32(kVolumeNorm + 1);  // n_volume_steps (PA_VOLUME_NORM+1, v15)
    body.u32(kInvalidIndex);  // card (v15)
    body.u32(0);  // n_ports (v16)
    body.str(nullptr);  // active_port (v16, none)
    body.u8Tagged(1);  // n_formats (v21)
    body.formatInfoPcm(kSinkFormat, kSinkChannels, kSinkRate, 2,
                       map.data(),
                       /*emptyProps=*/true);
    return body;
}

}  // namespace

// One client connection. Event-driven: the main loop polls all conns,
// onReadable() drains what is available (MSG_DONTWAIT) and dispatches
// every complete packet.
class Conn
{
public:
    Conn(Server &server, int fd, std::string name, uint32_t clientIndex)
        : m_server(server)
        , m_fd(fd)
        , m_name(std::move(name))
        , m_clientIndex(clientIndex)
    {
    }

    int fd() const { return m_fd; }
    bool alive() const { return m_fd >= 0 && !m_dead; }

    void onReadable()
    {
        // Drain the available bytes (bounded; a stream socket returns
        // short reads, MSG_DONTWAIT stops at the empty queue, 0 is the
        // peer's EOF).
        constexpr size_t kRecvChunk = static_cast<size_t>(64) * 1024;
        std::array<uint8_t, kRecvChunk> tmp;
        bool eof = false;
        for (;;) {
            const ssize_t n = ::recv(m_fd, tmp.data(), tmp.size(),
                                     MSG_DONTWAIT);
            if (n > 0) {
                m_readBuf.insert(m_readBuf.end(), tmp.data(),
                                 tmp.data() + n);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                continue;
            }
            if (n < 0 && errno == EINTR)
                continue;
            eof = (n == 0);
            break;
        }
        if (eof) {
            closeNow();
            return;
        }
        // Dispatch every complete packet from the front of the buffer.
        while (m_readBuf.size() >= 20) {
            std::array<uint32_t, 5> desc{};
            std::memcpy(desc.data(), m_readBuf.data(),
                        sizeof(desc[0]) * desc.size());
            const uint32_t length = ntohl(desc[0]);
            const uint32_t channel = ntohl(desc[1]);
            if (length == 0 || length > kMaxPacket) {
                m_server.log(m_name, "bad packet length %u — dropping",
                             length);
                closeNow();
                return;
            }
            if (m_readBuf.size() < 20 + length)
                break;  // wait for the rest of the packet
            Packet packet;
            packet.length = length;
            packet.channel = channel;
            // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            packet.payload.assign(m_readBuf.data() + 20,
                                  m_readBuf.data() + 20 + length);
            // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            m_readBuf.erase(m_readBuf.begin(),
                            m_readBuf.begin() + 20 + static_cast<long>(length));
            if (m_server.m_trace && packet.channel == kControlChannel)
                m_server.log(m_name, "IN  #%03d ctrl len=%u %s",
                             ++m_server.m_traceSeq, packet.length,
                             hexdump(packet.payload.data(),
                                     packet.payload.size()).c_str());
            else if (m_server.m_trace)
                m_server.log(m_name, "IN  #%03d data ch=%u len=%u",
                             ++m_server.m_traceSeq, packet.length,
                             packet.length);
            if (packet.channel == kControlChannel)
                handleControl(packet);
            else
                handleData(packet);
            if (m_dead) {
                closeNow();
                return;
            }
        }
    }

    void closeNow()
    {
        ::close(m_fd);
        m_fd = -1;
        m_server.log(m_name, "disconnected");
    }

    // The subscription's event to this connection (when it subscribed
    // to the sink inputs).
    void sendSubscribeEvent(uint32_t event, uint32_t index)
    {
        if ((m_subscription & kSubMaskSinkInput) == 0)
            return;
        Buf b;
        b.u32(kCmdSubscribeEvent);
        b.u32(0xFFFFFFFFU);  // tag — a server push
        b.u32(event);
        b.u32(index);
        sendPacket(kControlChannel, b.b);
    }

    // Advance each stream's virtual reader (in real time, frozen while
    // corked) and request the space it has freed. Called on the loop's
    // ~20 ms wake-up, this is what paces the client to real time.
    void tick(const struct timeval &now)
    {
        for (auto &entry : m_streams) {
            Stream &s = *entry.second;
            if (!s.started)
                continue;
            // The reader's clock: add the elapsed time since the last
            // tick, except while corked (a corked stream plays nothing).
            if (now.tv_sec > s.lastTick.tv_sec
                || (now.tv_sec == s.lastTick.tv_sec
                    && now.tv_usec >= s.lastTick.tv_usec)) {
                if (!s.corked)
                    s.consumedUsec +=
                        (static_cast<uint64_t>(now.tv_sec
                                                - s.lastTick.tv_sec)
                         * 1000000ULL)
                        + (static_cast<uint64_t>(now.tv_usec
                                                 - s.lastTick.tv_usec));
                s.lastTick = now;
            }
            // The persistent lead (Server::kLeadMs): the client is
            // authorized ahead of the reader, so its writable headroom
            // stays at the lead — a late tick (a descheduled server's
            // thread) starves the client only after the headroom is
            // gone (a ~1 ms starvation is a ~1 ms hole in the audio,
            // a pop).
            const uint64_t read = s.readIndex();
            const uint64_t target =
                read + ((s.byteRate * Server::kLeadMs) / 1000ULL);
            const uint64_t free = target > s.lastRequested
                ? target - s.lastRequested
                : 0;
            // Batch into minreq chunks (the real server's memblockq only
            // requests at minreq granularity): a per-tick trickle would
            // wake the client's pump for a tiny pass each time (a full
            // core), while 20 ms chunks keep the pump at its 20 ms pace.
            // Frame-aligned, so the client's requested_bytes drains to
            // exactly zero — a sub-frame residual would keep
            // pa_stream_writable_size() positive forever, the pump would
            // zero-write at CPU speed, and the mainloop would spin.
            const uint32_t frame = s.frameSize();
            const uint64_t aligned = frame != 0 ? free / frame * frame : free;
            if (aligned >= s.minreq) {
                request(entry.first, static_cast<uint32_t>(aligned));
                s.lastRequested += aligned;
            }
        }
    }

private:
    void sendPacket(uint32_t channel, const std::vector<uint8_t> &payload)
    {
        if (m_server.m_trace)
            m_server.log(m_name, "OUT #%03d %s ch=%u len=%zu %s",
                         ++m_server.m_traceSeq,
                         channel == kControlChannel ? "ctrl" : "data", channel,
                         payload.size(),
                         hexdump(payload.data(), payload.size()).c_str());
        std::array<uint32_t, 5> desc{};
        desc[0] = htonl(static_cast<uint32_t>(payload.size()));
        desc[1] = htonl(channel);
        // desc[2..3] = the offset (unused), desc[4] = flags 0
        if (!sendAll(m_fd, desc.data(), sizeof(desc[0]) * desc.size())
            || (!payload.empty()
                && !sendAll(m_fd, payload.data(), payload.size()))) {
            m_dead = true;
        }
    }

    void reply(uint32_t tag, const Buf &body)
    {
        Buf full;
        full.u32(kCmdReply);
        full.u32(tag);
        full.b.insert(full.b.end(), body.b.begin(), body.b.end());
        sendPacket(kControlChannel, full.b);
    }

    void replyError(uint32_t tag, int err)
    {
        Buf b;
        b.u32(kCmdError);
        b.u32(tag);
        b.u32(static_cast<uint32_t>(err));
        sendPacket(kControlChannel, b.b);
    }

    void request(uint32_t channel, uint32_t bytes)
    {
        Buf b;
        b.u32(kCmdRequest);
        b.u32(0xFFFFFFFFU);  // tag — a server push (the gold uses 0xFFFFFFFF)
        b.u32(channel);
        b.u32(bytes);
        sendPacket(kControlChannel, b.b);
    }

    // The flat protocol command dispatch (one case per native-protocol
    // command): the complexity is the protocol surface, not nesting.
    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    void handleControl(const Packet &packet)
    {
        Rdr r{packet.payload.data(), packet.payload.size()};
        const uint32_t cmd = r.u32();
        const uint32_t tag = r.u32();
        const char *name = cmdName(cmd);
        if (name != nullptr)
            m_server.log(m_name, "command %s (tag %u)", name, tag);
        else
            m_server.log(m_name, "UNHANDLED command %u (tag %u)", cmd, tag);
        if (!r.ok) {
            m_server.log(m_name, "  a malformed request");
            replyError(tag, kErrProtocol);
            return;
        }

        switch (cmd) {
        case kCmdAuth: {
            const uint32_t version = r.u32();
            r.skipArbitrary();  // the cookie
            m_version = version & 0xFFFFU;
            m_authorized = true;
            m_server.log(m_name,
                         "  auth: client version %u (flags 0x%06x) — "
                         "replying %u, no SHM/memfd",
                         version & 0xFFFFU, version & 0xFFFF0000U,
                         kProtocolVersion);
            Buf body;
            body.u32(kProtocolVersion);
            reply(tag, body);
            break;
        }
        case kCmdSetClientName: {
            if (m_version >= 13)
                r.skipProplist();
            m_server.log(m_name,
                         "  client name set (the context goes READY)");
            Buf body;
            body.u32(m_clientIndex);
            reply(tag, body);
            break;
        }
        case kCmdGetServerInfo: {
            Buf body;
            body.str("pulsestub");
            body.str("17.0.0 (pulsestub)");
            body.str("stub");
            body.str("localhost");
            body.sampleSpec(kSinkFormat, kSinkChannels, kSinkRate);
            body.str(kSinkName);
            body.str(kMonitorSource);
            body.u32(0);  // the cookie
            const std::array<uint8_t, 2> map = {
                PA_CHANNEL_POSITION_FRONT_LEFT,
                PA_CHANNEL_POSITION_FRONT_RIGHT,
            };
            body.channelMap(2, map.data());
            reply(tag, body);
            break;
        }
        case kCmdCreatePlayback: {
            uint8_t format = 0;
            uint8_t channels = 0;
            uint32_t rate = 0;
            r.sampleSpec(format, channels, rate);
            std::vector<uint8_t> map;
            r.channelMap(map);
            const uint32_t sinkIndex = r.u32();
            const std::string sink = r.str();
            const uint32_t maxlength = r.u32();
            const bool corked = r.boolean();
            const uint32_t tlength = r.u32();
            const uint32_t prebuf = r.u32();
            const uint32_t minreq = r.u32();
            const uint32_t syncid = r.u32();
            r.skipCvolume();
            for (int i = 0; i < 7; ++i)
                r.boolean();  // the v12 flags
            const bool muted = r.boolean();
            const bool adjustLatency = r.boolean();
            std::map<std::string, std::string> props;
            r.proplist(props);
            r.boolean();  // volume_set (v14)
            r.boolean();  // early_requests (v14)
            r.boolean();  // muted_set (v15)
            r.boolean();  // dont_inhibit_auto_suspend (v15)
            r.boolean();  // fail_on_suspend (v15)
            if (m_version >= 17)
                r.boolean();  // relative_volume
            if (m_version >= 18)
                r.boolean();  // passthrough
            if (m_version >= 21) {
                const uint8_t n = r.u8Tagged();  // a raw u8 in the request
                for (uint8_t i = 0; i < n; ++i)
                    r.skipFormatInfo();
            }
            if (!r.ok) {
                m_server.log(m_name, "  a malformed create request");
                replyError(tag, kErrProtocol);
                return;
            }

            // The sink input (the server-side object, global).
            auto stream = std::make_unique<Stream>();
            stream->index = m_server.m_nextIndex++;
            stream->client = m_clientIndex;
            stream->format = format;
            stream->channels = channels;
            stream->rate = rate;
            stream->map = std::move(map);
            stream->name = props.count("media.name") != 0
                               ? props["media.name"]
                               : std::string("playback");
            stream->maxlength = maxlength;
            stream->tlength = tlength;
            stream->prebuf = prebuf;
            stream->minreq = minreq;
            stream->corked = corked;
            stream->playing = !corked;
            stream->volume.assign(channels, kVolumeNorm);
            stream->muted = muted;
            stream->converter = Converter(format, channels, rate);
            stream->byteRate =
                static_cast<uint64_t>(rate) * channels
                * bytesPerSample(format);
            resolveAttrs(*stream);
            uint32_t channel = m_nextChannel++;  // our own channel ids
            Stream *sp = stream.get();
            m_server.m_streams.push_back(std::move(stream));
            m_streams[channel] = sp;
            if (m_server.m_streams.size() > 1)
                m_server.log(m_name,
                             "  note: a second playback stream (the "
                             "capture muxes them all)");
            m_server.log(m_name,
                         "  sink input %u (channel %u): %s, %u ch %u Hz "
                         "format=%u sink=%s corked=%d maxlength=%u "
                         "tlength=%u prebuf=%u minreq=%u "
                         "adjust_latency=%d muted=%d syncid=%u",
                         sp->index, channel, sp->name.c_str(), sp->channels,
                         sp->rate, sp->format,
                         sink.empty() ? "(default)" : sink.c_str(),
                         static_cast<int>(corked), sp->maxlength, sp->tlength,
                         sp->prebuf, sp->minreq,
                         static_cast<int>(adjustLatency),
                         static_cast<int>(muted), syncid);

            // The reply (the verified field order; the spec/map echo the
            // request — the client asserts it without the FIX flags).
            Buf body;
            body.u32(channel);
            body.u32(sp->index);
            body.u32(sp->tlength);  // the initial "missing" amount
            body.u32(sp->maxlength);
            body.u32(sp->tlength);
            body.u32(sp->prebuf);
            body.u32(sp->minreq);
            body.sampleSpec(format, channels, rate);
            body.channelMap(static_cast<uint32_t>(sp->map.size()),
                            sp->map.data());
            body.u32(0);  // the sink index (the single sink is index 0)
            body.str(kSinkName);
            body.boolean(false);  // suspended
            body.usec(kSinkLatencyUsec);  // the sink's (small) base latency
            body.formatInfoPcm(format, channels, rate,
                               static_cast<uint32_t>(sp->map.size()),
                               sp->map.data(),
                               /*emptyProps=*/false);
            reply(tag, body);

            if (!corked) {
                // The initial request (the buffer's target): the client
                // may fill up to this much; the tick paces the rest.
                // Frame-aligned, like the tick's requests, so the
                // client's requested_bytes can drain to exactly zero.
                const uint32_t frame = sp->frameSize();
                const uint32_t initial
                    = frame != 0 ? sp->tlength / frame * frame : sp->tlength;
                sp->lastRequested = initial;
                request(channel, initial);
            }
            broadcastEvent(kEventSinkInput | kEventNew, sp->index);
            (void)sinkIndex;
            break;
        }
        case kCmdDeletePlayback: {
            const uint32_t channel = r.u32();
            auto it = m_streams.find(channel);
            if (it != m_streams.end()) {
                Stream *s = it->second;
                m_streams.erase(it);
                for (size_t i = 0; i < m_server.m_streams.size(); ++i)
                    if (m_server.m_streams[i]->index == s->index) {
                        m_server.m_streams.erase(
                            std::next(m_server.m_streams.begin(),
                                      static_cast<std::ptrdiff_t>(i)));
                        break;
                    }
                m_server.log(m_name, "  sink input %u (channel %u) "
                                     "deleted",
                             s->index, channel);
                broadcastEvent(kEventSinkInput | kEventRemove, s->index);
            }
            reply(tag, {});
            break;
        }
        case kCmdDrainPlayback: {
            const uint32_t channel = r.u32();
            m_server.log(m_name,
                         "  stream %u drained (the server consumes "
                         "immediately)",
                         channel);
            reply(tag, {});
            break;
        }
        case kCmdGetPlaybackLatency: {
            const uint32_t channel = r.u32();
            uint32_t xsec = 0;
            uint32_t xusec = 0;
            r.skipTimevalValue(xsec, xusec);
            auto it = m_streams.find(channel);
            if (it == m_streams.end()) {
                m_server.log(m_name, "  no such stream %u", channel);
                replyError(tag, kErrNotImplemented);
                break;
            }
            const Stream &s = *it->second;
            // The real-time model: the sink's base latency is small and
            // constant, and the buffer is what the virtual reader has
            // not yet played (write_index - read_index). The client's
            // measured latency is that buffer's play-out time, which the
            // paced requests keep at ~one pump pass (small, positive).
            struct timeval now;
            ::gettimeofday(&now, nullptr);
            Buf body;
            body.usec(kSinkLatencyUsec);
            body.usec(0);  // source_usec
            body.boolean(s.playing);
            body.timeval(xsec, xusec);  // the request's xtime
            body.timeval(static_cast<uint32_t>(now.tv_sec),
                         static_cast<uint32_t>(now.tv_usec));
            body.s64(static_cast<int64_t>(s.writeBytes));  // write_index
            body.s64(static_cast<int64_t>(s.readIndex()));  // read_index
            body.u64(0);  // underrun_for (v13)
            body.u64(1000000ULL);  // playing_for (v13)
            reply(tag, body);
            break;
        }
        case kCmdCorkPlayback: {
            const uint32_t channel = r.u32();
            const bool cork = r.boolean();
            auto it = m_streams.find(channel);
            if (it != m_streams.end()) {
                it->second->corked = cork;
                it->second->playing = !cork;
            }
            m_server.log(m_name, "  stream %u corked=%d", channel,
                         static_cast<int>(cork));
            reply(tag, {});
            break;
        }
        case kCmdFlushPlayback: {
            const uint32_t channel = r.u32();
            m_server.log(m_name, "  stream %u flushed", channel);
            reply(tag, {});
            break;
        }
        case kCmdTriggerPlayback: {
            const uint32_t channel = r.u32();
            auto it = m_streams.find(channel);
            if (it != m_streams.end()) {
                it->second->corked = false;
                it->second->playing = true;
                request(channel, it->second->tlength);
            }
            m_server.log(m_name, "  stream %u triggered", channel);
            reply(tag, {});
            break;
        }
        case kCmdSetSinkInputVolume: {
            const uint32_t index = r.u32();
            std::vector<uint32_t> volume;
            r.cvolume(volume);
            Stream *s = m_server.findStream(index);
            if (s != nullptr) {
                s->volume = std::move(volume);
                m_server.log(m_name, "  sink input %u volume set", index);
                broadcastEvent(kEventSinkInput | kEventChange, index);
            }
            reply(tag, {});
            break;
        }
        case kCmdSetSinkInputMute: {
            const uint32_t index = r.u32();
            const bool mute = r.boolean();
            Stream *s = m_server.findStream(index);
            if (s != nullptr) {
                s->muted = mute;
                m_server.log(m_name, "  sink input %u mute=%d", index,
                             static_cast<int>(mute));
                broadcastEvent(kEventSinkInput | kEventChange, index);
            }
            reply(tag, {});
            break;
        }
        case kCmdSetSinkMute: {
            r.u32();  // the sink's index (invalid for a name lookup)
            r.str();  // the name (null for an index lookup)
            r.boolean();  // the mute
            m_server.log(m_name, "  sink mute set (the server is never "
                                 "muted)");
            reply(tag, {});
            break;
        }
        case kCmdSetDefaultSink: {
            const std::string name = r.str();
            m_server.log(m_name, "  default sink set to %s", name.c_str());
            reply(tag, {});
            break;
        }
        case kCmdSetPlaybackStreamName: {
            const uint32_t channel = r.u32();
            const std::string name = r.str();
            auto it = m_streams.find(channel);
            if (it != m_streams.end()) {
                it->second->name = name;
                m_server.log(m_name, "  stream %u named %s", channel,
                             name.c_str());
                broadcastEvent(kEventSinkInput | kEventChange,
                               it->second->index);
            }
            reply(tag, {});
            break;
        }
        case kCmdGetSinkInputInfo: {
            const uint32_t index = r.u32();
            Stream *s = m_server.findStream(index);
            if (s == nullptr) {
                replyError(tag, kErrNotImplemented);
                break;
            }
            reply(tag, sinkInputInfo(*s));
            break;
        }
        case kCmdGetSinkInputInfoList: {
            // The client loops over concatenated infos until the
            // tagstream runs out (no explicit terminator — the gold
            // capture). Bind each info to a named local: the iterators
            // must not point into a temporary (a dangling read of freed
            // memory aborts the server as soon as a stream exists).
            Buf body;
            for (const auto &stream : m_server.m_streams) {
                const Buf info = sinkInputInfo(*stream);
                body.b.insert(body.b.end(), info.b.begin(), info.b.end());
            }
            reply(tag, body);
            break;
        }
        case kCmdLookupSink:
        case kCmdGetSinkInfo: {
            r.u32();  // the index (kInvalidIndex for a name lookup)
            if (cmd == kCmdLookupSink)
                r.str();  // the name
            reply(tag, sinkInfo());
            break;
        }
        case kCmdGetSinkInfoList: {
            // No count: the client loops `while (!pa_tagstruct_eof(t))`
            // over concatenated sink infos (the gold 3.yaml capture).
            reply(tag, sinkInfo());
            break;
        }
        case kCmdGetSourceInfoList: {
            Buf body;
            body.u32(0);  // the source index (gold: index 0)
            body.str(kMonitorSource);
            body.str("doob monitor");
            body.sampleSpec(kSinkFormat, kSinkChannels, kSinkRate);
            const std::array<uint8_t, 2> map = {
                PA_CHANNEL_POSITION_FRONT_LEFT,
                PA_CHANNEL_POSITION_FRONT_RIGHT,
            };
            body.channelMap(2, map.data());
            body.u32(0);  // owner_module
            const std::array<uint32_t, 2> vol = {kVolumeNorm, kVolumeNorm};
            body.cvolume(2, vol.data());
            body.boolean(false);  // mute
            body.u32(kInvalidIndex);  // monitor_of (none)
            body.str("");
            body.usec(0);  // latency
            body.str("pulsestub");  // driver
            body.u32(0);  // flags
            body.proplist();
            body.usec(0);  // requested_latency (v13)
            body.volume(kVolumeNorm);  // base_volume (v15)
            body.u32(1);  // state PA_SOURCE_RUNNING (v15; 0=UNLINKED)
            body.u32(kVolumeNorm + 1);  // n_volume_steps (PA_VOLUME_NORM+1, v15)
            body.u32(kInvalidIndex);  // card (v15)
            body.u32(0);  // n_ports (v16)
            body.str(nullptr);  // active_port (v16, none)
            body.u8Tagged(1);  // n_formats (v22)
            body.formatInfoPcm(kSinkFormat, kSinkChannels, kSinkRate, 2,
                               map.data(),
                               /*emptyProps=*/true);
            reply(tag, body);
            break;
        }
        case kCmdSubscribe: {
            const uint32_t what = r.u32();
            m_server.log(m_name, "  subscribe (what=%u)", what);
            // The subscription replaces the previous one (the server's
            // behavior), and the existing objects are reported as NEW
            // right away (the ack after — the client's stream index is
            // known by then, as in the real server's ordering).
            m_subscription = what;
            if ((what & kSubMaskSinkInput) != 0) {
                for (const auto &stream : m_server.m_streams)
                    sendSubscribeEvent(kEventSinkInput | kEventNew,
                                       stream->index);
            }
            reply(tag, {});
            break;
        }
        default:
            m_server.log(m_name, "  → PA_ERR_NOTIMPLEMENTED");
            replyError(tag, kErrNotImplemented);
            break;
        }
    }

    void handleData(const Packet &packet)
    {
        auto it = m_streams.find(packet.channel);
        if (it == m_streams.end()) {
            m_server.log(m_name, "data for an unknown channel %u "
                                 "(%u bytes) — dropped",
                         packet.channel, packet.length);
            return;
        }
        Stream &s = *it->second;
        s.writeBytes += packet.length;
        if (!s.started) {
            // The first byte: the virtual reader starts here (a corked
            // stream's clock is frozen by the tick until it un-corks).
            s.started = true;
            ::gettimeofday(&s.lastTick, nullptr);
        }
        std::vector<uint8_t> pcm;
        pcm.reserve(packet.length);
        s.converter.convert(packet.payload.data(), packet.payload.size(),
                            pcm);
        // The sink's blocking is the pacing: a full capture (a stalled
        // reader) parks the server here, the write requests stop, the
        // client's buffer fills, and the WebEngine slows to real time.
        // No per-packet request: the tick (Conn::tick) requests the
        // space the virtual reader frees, in real time. A per-packet
        // echo would flood requests and let the client drain the buffer
        // at CPU speed (the tap's latency shift then outlives the feed).
        m_server.deliver(pcm);
    }

    void broadcastEvent(uint32_t event, uint32_t index)
    {
        // The event goes to every connection subscribed to the sink
        // inputs (the server posts it to all subscribers, including the
        // changing client's own).
        for (auto &conn : m_server.m_conns)
            conn->sendSubscribeEvent(event, index);
    }

    Server &m_server;
    int m_fd = -1;
    std::string m_name;
    uint32_t m_clientIndex = 0;
    uint32_t m_version = 0;
    bool m_authorized = false;
    bool m_dead = false;
    uint32_t m_nextChannel = 0;
    uint32_t m_subscription = 0;
    std::unordered_map<uint32_t, Stream *> m_streams;  // channel -> input
    std::vector<uint8_t> m_readBuf;  // the not-yet-complete packet bytes
};

// --- the server -----------------------------------------------------------

Server::Server(Sink sink)
    : m_sink(std::move(sink))
{
}

Server::~Server()
{
    stop();
}

bool Server::running() const
{
    return m_thread.joinable();
}

// The socket setup and the poll-flag tests are the platform idiom, so
// the bounds/signed checks are suppressed for the function.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-bounds-array-to-pointer-decay,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-signed-bitwise,readability-function-cognitive-complexity)
bool Server::start(const std::string &socketPath)
{
    if (running())
        return false;
    m_trace = ::getenv("PULSESTUB_TRACE") != nullptr;
    if (const char *logPath = ::getenv("PULSESTUB_LOG"))
        m_logFile = std::fopen(logPath, "a");

    ::unlink(socketPath.c_str());
    m_listenFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_listenFd < 0) {
        log("pulsestub", "socket: %s", strerror(errno));
        return false;
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socketPath.c_str(),
                 sizeof(addr.sun_path) - 1);
    if (::bind(m_listenFd, reinterpret_cast<sockaddr *>(&addr),
               sizeof(addr)) < 0
        || ::listen(m_listenFd, 8) < 0) {
        log("pulsestub", "bind/listen on %s: %s", socketPath.c_str(),
            strerror(errno));
        ::close(m_listenFd);
        m_listenFd = -1;
        return false;
    }
    // Non-blocking so the accept loop can drain the pending queue and
    // return to poll() (a blocking accept would wedge the loop on the
    // first idle gap).
    const int lflags = ::fcntl(m_listenFd, F_GETFL, 0);
    ::fcntl(m_listenFd, F_SETFL, lflags | O_NONBLOCK);
    log("pulsestub", "listening on %s", socketPath.c_str());
    m_socketPath = socketPath;
    m_stop = false;
    m_thread = std::thread(&Server::run, this);
    return true;
}
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-bounds-array-to-pointer-decay,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-signed-bitwise,readability-function-cognitive-complexity)

void Server::stop()
{
    if (!running())
        return;
    m_stop = true;
    // The loop's poll times out at 20 ms: the join is quick.
    m_thread.join();
    for (auto &conn : m_conns)
        conn->closeNow();
    m_conns.clear();
    m_streams.clear();
    if (m_listenFd >= 0) {
        ::close(m_listenFd);
        m_listenFd = -1;
    }
    if (!m_socketPath.empty()) {
        ::unlink(m_socketPath.c_str());
        m_socketPath.clear();
    }
    if (m_logFile != nullptr) {
        std::fclose(m_logFile);
        m_logFile = nullptr;
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void Server::run()
{
    // The tick (the client's write requests) is the engine's real-time
    // clock: a descheduled server starves the client's render thread
    // (the client's writable headroom is the flow control's lead, and
    // only that). Best effort: SCHED_FIFO (the kernel's RT throttling
    // keeps a runaway bounded) — a typical unprivileged session can do
    // neither (EPERM): the lead carries it.
    struct sched_param param{};
    param.sched_priority = 2;
    if (::sched_setscheduler(0, SCHED_FIFO, &param) != 0)
        ::setpriority(PRIO_PROCESS, 0, -10);
    ::pthread_setname_np(::pthread_self(), "pulsestub");
    // The poll-flag tests are the platform idiom (a signed revents).
    // NOLINTBEGIN(bugprone-signed-bitwise)
    // The multi-client loop: poll the listen fd and every open conn.
    for (;;) {
        std::vector<pollfd> pfds;
        pfds.reserve(m_conns.size() + 1);
        pfds.push_back({m_listenFd, POLLIN, 0});
        for (const auto &c : m_conns)
            pfds.push_back({c->fd(), POLLIN, 0});
        // A bounded poll: the ~20 ms wake-up drives the streams'
        // virtual readers (Conn::tick) even when no data arrives, so
        // the paced write requests keep up in real time.
        int ready = ::poll(pfds.data(), pfds.size(), 20);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if ((pfds[0].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            for (;;) {
                const int fd = ::accept(m_listenFd, nullptr, nullptr);
                if (fd < 0)
                    break;
                // The kernel's socket buffer is the engine's last
                // headroom (behind the flow control's lead, when the
                // server's write to the ring is blocked): grow it
                // (best effort — clamped to rmem_max without
                // privilege).
                const int rcvbuf = 2 * 1024 * 1024;
                ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf,
                             sizeof(rcvbuf));
                log("pulsestub", "a client connected (fd %d)", fd);
                m_conns.push_back(
                    std::make_unique<Conn>(*this, fd, "client",
                                           ++m_nextClient));
            }
        }
        // Iterate pfds, not m_conns: the accept loop above may have
        // grown m_conns, and a fresh conn has no entry in this
        // iteration's pfds (it is polled from the next one).
        for (size_t i = 1; i < pfds.size(); ++i) {
            if ((pfds[i].revents & (POLLIN | POLLHUP | POLLERR)) != 0)
                m_conns[i - 1]->onReadable();
        }
        struct timeval now;
        ::gettimeofday(&now, nullptr);
        for (auto &conn : m_conns)
            conn->tick(now);
        m_conns.erase(
            std::remove_if(m_conns.begin(), m_conns.end(),
                           [](const std::unique_ptr<Conn> &c) {
                               return !c->alive();
                           }),
            m_conns.end());
        if (m_conns.empty())
            log("pulsestub", "all clients gone, total captured: %llu bytes",
                static_cast<unsigned long long>(m_totalBytes));
        if (m_stop.load())
            break;
    }
    // NOLINTEND(bugprone-signed-bitwise)
}

// NOLINTNEXTLINE(modernize-avoid-variadic-functions)
void Server::log(const std::string &name, const char *fmt, ...) const
{
    // A C-style varargs logger (the va_* mechanism decays the format
    // argument — the checks are inherent to the idiom).
    // NOLINTBEGIN(modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    const std::string prefix = std::string("[pulsestub ") + name + "] ";
    fputs(prefix.c_str(), stderr);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    if (m_logFile != nullptr) {
        fputs(prefix.c_str(), m_logFile);
        va_list ap2;
        va_start(ap2, fmt);
        vfprintf(m_logFile, fmt, ap2);
        va_end(ap2);
        fputc('\n', m_logFile);
        fflush(m_logFile);
    }
    // NOLINTEND(modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-array-to-pointer-decay)
}

void Server::deliver(const std::vector<uint8_t> &pcm)
{
    m_totalBytes += pcm.size();
    if (!pcm.empty())
        m_sink(pcm.data(), pcm.size());
}

Stream *Server::findStream(uint32_t index)
{
    for (auto &stream : m_streams)
        if (stream->index == index)
            return stream.get();
    return nullptr;
}

}  // namespace pulsestub
