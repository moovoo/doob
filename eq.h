#ifndef EQ_H
#define EQ_H

#include <array>

// The 10-band equalizer's contract: the band table (center frequencies,
// low to high), the per-band gain range and the shared keys. The DSP
// itself sits in plugins/ffmpeg/ (the only driver with an open PCM
// path) and registers itself in the core's DspRegistry under eq::Id;
// its per-band gains are a plugin-owned setting the core's
// EqualizerWidget reads and writes through the SettingsManager (the
// on/off switch is the DSP's entry in the registry, not a setting).
// 0 dB is flat: a flat EQ is the identity, so the default (all-zero)
// gains change nothing.
namespace eq
{
inline constexpr int BandCount = 10;
// The band centers: the ends are shelves (31 Hz low, 16 kHz high), the
// middle eight peaking bands sit one per octave.
inline constexpr std::array<double, BandCount> Hz{
    31, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000,
};
// The per-band gain, in dB: the span of the widget's sliders and the
// clamp the driver applies to a stored value.
inline constexpr double MaxGainDb = 12.0;
// The slider's step (the slider's int range is ±MaxGainDb/GainStepDb).
inline constexpr double GainStepDb = 0.5;
// The processor's id in the DspRegistry (the FFmpeg plugin registers
// it, the core's EqualizerWidget finds it there).
inline constexpr const char *Id = "ffmpeg.eq";
// The plugin-owned setting (the owner is the plugin's id; the stored
// value survives an unregistration, so a re-added driver finds it
// intact): the 10-band gains, in dB.
inline constexpr const char *GainsKey = "ffmpeg.eq.gains";
}

#endif // EQ_H
