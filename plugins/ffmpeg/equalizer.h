#ifndef FFMPEG_EQUALIZER_H
#define FFMPEG_EQUALIZER_H

#include <cstddef>
#include <array>

#include <doob/eq.h>

// The 10-band equalizer: a low shelf (band 0, 31 Hz), eight peaking
// bands (one per octave, Q = sqrt(2)) and a high shelf (band 9,
// 16 kHz), each a second-order IIR (biquad, the RBJ audio-EQ
// cookbook). The gains are in dB per band (the order of eq::Hz); 0 dB
// reduces a band exactly to the identity (its numerator equals its
// denominator), so a flat EQ leaves the audio untouched.
//
// Not thread-safe on its own: PulseOut touches it under its own mutex
// (the pump thread in process(), the GUI thread in setGains/setBypass).
class Equalizer
{
public:
    explicit Equalizer(int sampleRate = 0);

    // Re-derive every band's coefficients for the rate (the gains are
    // kept; the delay lines reset — the audio starts fresh).
    void setSampleRate(int sampleRate);
    // New per-band gains (dB): clamped to ±eq::MaxGainDb, the
    // coefficients re-derive at the current rate. The delay lines keep
    // running (a 0.5 dB step is inaudible through them; a bypass
    // toggle resets them instead).
    void setGains(const std::array<float, eq::BandCount> &gainsDb);
    // Bypass: process() becomes a no-op. Engaging it clears the delay
    // lines so a re-engage cannot leak the pre-bypass audio.
    void setBypass(bool bypass);
    bool isBypassed() const { return m_bypass; }
    // The clamped gains as they were set — the test seam.
    std::array<float, eq::BandCount> gainsDb() const { return m_gains; }

    // Filters the interleaved stereo frames in place (a frame is two
    // floats, left then right; each channel runs its own delay lines
    // through every band).
    void process(float *frames, std::size_t frameCount);

private:
    // One biquad (normalized, a0 = 1) with the two channels' delay
    // lines (named left/right rather than indexed: a channel is the
    // interleaved frame's side, not a number).
    struct Band
    {
        float b0 = 1.0F, b1 = 0.0F, b2 = 0.0F, a1 = 0.0F, a2 = 0.0F;
        float lx1 = 0.0F, lx2 = 0.0F, ly1 = 0.0F, ly2 = 0.0F;
        float rx1 = 0.0F, rx2 = 0.0F, ry1 = 0.0F, ry2 = 0.0F;
    };

    void computeAll();
    void resetState();

    int m_rate = 0;
    bool m_bypass = false;
    std::array<float, eq::BandCount> m_gains{};
    std::array<Band, eq::BandCount> m_bands;
};

#endif // FFMPEG_EQUALIZER_H
