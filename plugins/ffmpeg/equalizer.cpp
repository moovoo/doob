#include "equalizer.h"

#include <algorithm>
#include <cmath>

namespace
{

// The RBJ audio-EQ cookbook, normalized to a0 = 1. A = 10^(dB/40), so
// a band's extreme gain is A^2 = 10^(dB/20) — and A = 1 (0 dB)
// reduces every band exactly to the identity (the numerator equals
// the denominator), which is what makes a flat EQ a no-op.

void peaking(double A, double w0, float &b0, float &b1, float &b2,
             float &a1, float &a2)
{
    const double cosw = std::cos(w0);
    // Q = sqrt(2): the bands are one octave apart, so each band's
    // -3 dB bandwidth spans the gap to its neighbours.
    const double alpha = std::sin(w0) / (2.0 * std::sqrt(2.0));
    const double a0 = 1.0 + (alpha / A);
    b0 = static_cast<float>((1.0 + (alpha * A)) / a0);
    b1 = static_cast<float>(-2.0 * cosw / a0);
    b2 = static_cast<float>((1.0 - (alpha * A)) / a0);
    a1 = static_cast<float>(-2.0 * cosw / a0);
    a2 = static_cast<float>((1.0 - (alpha / A)) / a0);
}

// S = 1: the steepest shelf slope that stays monotonic in frequency.
// (For S = 1 the cookbook's alpha collapses to sin(w0)/sqrt(2).) The
// DC gain is A^2 (the shelf) and the Nyquist gain 1 (flat up top).
void lowShelf(double A, double w0, float &b0, float &b1, float &b2,
              float &a1, float &a2)
{
    const double cosw = std::cos(w0);
    const double sqrtA = std::sqrt(A);
    const double alpha = std::sin(w0) / std::sqrt(2.0);
    const double a0 =
        (A + 1.0) + ((A - 1.0) * cosw) + (2.0 * sqrtA * alpha);
    b0 = static_cast<float>(A * ((A + 1.0) - ((A - 1.0) * cosw)
                                 + (2.0 * sqrtA * alpha))
                            / a0);
    b1 = static_cast<float>(
        2.0 * A * ((A - 1.0) - ((A + 1.0) * cosw)) / a0);
    b2 = static_cast<float>(A * ((A + 1.0) - ((A - 1.0) * cosw)
                                 - (2.0 * sqrtA * alpha))
                            / a0);
    a1 = static_cast<float>(
        -2.0 * ((A - 1.0) + ((A + 1.0) * cosw)) / a0);
    a2 = static_cast<float>(((A + 1.0) + ((A - 1.0) * cosw)
                             - (2.0 * sqrtA * alpha))
                            / a0);
}

// The mirror of lowShelf: the DC gain is 1 (flat at the bottom) and
// the Nyquist gain A^2 (the shelf).
void highShelf(double A, double w0, float &b0, float &b1, float &b2,
               float &a1, float &a2)
{
    const double cosw = std::cos(w0);
    const double sqrtA = std::sqrt(A);
    const double alpha = std::sin(w0) / std::sqrt(2.0);
    const double a0 =
        (A + 1.0) - ((A - 1.0) * cosw) + (2.0 * sqrtA * alpha);
    b0 = static_cast<float>(A * ((A + 1.0) + ((A - 1.0) * cosw)
                                 + (2.0 * sqrtA * alpha))
                            / a0);
    b1 = static_cast<float>(
        -2.0 * A * ((A - 1.0) + ((A + 1.0) * cosw)) / a0);
    b2 = static_cast<float>(A * ((A + 1.0) + ((A - 1.0) * cosw)
                                 - (2.0 * sqrtA * alpha))
                            / a0);
    a1 = static_cast<float>(
        2.0 * ((A - 1.0) - ((A + 1.0) * cosw)) / a0);
    a2 = static_cast<float>(((A + 1.0) - ((A - 1.0) * cosw)
                             - (2.0 * sqrtA * alpha))
                            / a0);
}

// One biquad step on one channel's delay lines (updated in place).
float step(float b0, float b1, float b2, float a1, float a2,
           float &x1, float &x2, float &y1, float &y2, float x)
{
    const float y = (b0 * x) + (b1 * x1) + (b2 * x2) - (a1 * y1) - (a2 * y2);
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    return y;
}

} // namespace

Equalizer::Equalizer(int sampleRate)
    : m_rate(sampleRate)
{
    computeAll();
}

void Equalizer::setSampleRate(int sampleRate)
{
    if (m_rate == sampleRate) {
        // The same rate keeps its coefficients, but the delay lines
        // reset anyway — a fresh stream starts fresh (a same-rate track
        // must not inherit its predecessor's tail).
        resetState();
        return;
    }
    m_rate = sampleRate;
    computeAll();
    resetState();
}

void Equalizer::setGains(const std::array<float, eq::BandCount> &gainsDb)
{
    // The bands are addressed by their position in the eq::Hz table.
    for (int i = 0; i < eq::BandCount; ++i)
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        m_gains[i] = std::clamp(gainsDb[i],
                                static_cast<float>(-eq::MaxGainDb),
                                static_cast<float>(eq::MaxGainDb));
    computeAll();
}

void Equalizer::setBypass(bool bypass)
{
    if (m_bypass == bypass)
        return;
    m_bypass = bypass;
    if (bypass)
        resetState();
}

void Equalizer::computeAll()
{
    // Without a rate the coefficients stay the identity (and process()
    // bails anyway): a rate-less EQ is a wire.
    if (m_rate <= 0)
        return;
    for (int i = 0; i < eq::BandCount; ++i) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        Band &band = m_bands[i];
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        const double A = std::pow(10.0, m_gains[i] / 40.0);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        const double w0 = (2.0 * M_PI) * eq::Hz[i] / m_rate;
        if (i == 0)
            lowShelf(A, w0, band.b0, band.b1, band.b2, band.a1, band.a2);
        else if (i == eq::BandCount - 1)
            highShelf(A, w0, band.b0, band.b1, band.b2, band.a1, band.a2);
        else
            peaking(A, w0, band.b0, band.b1, band.b2, band.a1, band.a2);
    }
}

void Equalizer::resetState()
{
    for (Band &band : m_bands) {
        band.lx1 = band.lx2 = band.ly1 = band.ly2 = 0.0F;
        band.rx1 = band.rx2 = band.ry1 = band.ry2 = 0.0F;
    }
}

void Equalizer::process(float *frames, std::size_t frameCount)
{
    if (m_bypass || m_rate <= 0 || frameCount == 0)
        return;
    // The raw interleaved buffer (L, R, L, R, ...) has no accessor but
    // the subscript.
    for (std::size_t f = 0; f < frameCount; ++f) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        float left = frames[2 * f];
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        float right = frames[(2 * f) + 1];
        for (Band &band : m_bands) {
            left = step(band.b0, band.b1, band.b2, band.a1, band.a2,
                        band.lx1, band.lx2, band.ly1, band.ly2, left);
            right = step(band.b0, band.b1, band.b2, band.a1, band.a2,
                         band.rx1, band.rx2, band.ry1, band.ry2, right);
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        frames[2 * f] = left;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        frames[(2 * f) + 1] = right;
    }
}
