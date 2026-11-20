#include <QtTest>

#include <array>
#include <cmath>
#include <vector>

#include "equalizer.h"

namespace
{

class EqualizerTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void flatIsTheIdentity();
    void bypassLeavesTheSamples();
    void lowShelfBoostsLowFrequencies();
    void highShelfLeavesLowFrequencies();
    void highShelfBoostsItsCorner();
    void peakingBoostsItsCenter();
    void peakingLeavesOtherFrequencies();
    void setSampleRateReTunes();
    void gainsAreClamped();

private:
    // The steady-state amplitude of a sine through the equalizer: the
    // first quarter of the buffer is thrown away as the filter settles.
    float steadyAmplitude(double hz, double rate,
                          const std::array<float, eq::BandCount> &gainsDb,
                          double inputAmplitude = 0.5);
};

void EqualizerTest::init()
{
}

void EqualizerTest::cleanup()
{
}

float EqualizerTest::steadyAmplitude(
    double hz, double rate,
    const std::array<float, eq::BandCount> &gainsDb, double inputAmplitude)
{
    Equalizer eq(static_cast<int>(rate));
    eq.setGains(gainsDb);
    const std::size_t frames = static_cast<std::size_t>(rate); // one second
    std::vector<float> in(2 * frames);
    for (std::size_t f = 0; f < frames; ++f) {
        // Per frame: the sample of frame f sits at time f/rate (a
        // per-float-index phase would double the tone's frequency,
        // since the frame's two channels share the index space).
        const float s = static_cast<float>(
            inputAmplitude
            * std::sin((2.0 * M_PI) * hz * static_cast<double>(f) / rate));
        in[2 * f] = s;
        in[(2 * f) + 1] = s;
    }
    eq.process(in.data(), frames);
    float peak = 0.0F;
    for (std::size_t f = frames / 4; f < frames; ++f)
        peak = qMax(peak, qAbs(in[2 * f]));
    return peak;
}

// Every band at 0 dB reduces exactly to the identity (in exact
// arithmetic the numerator equals the denominator), so a flat EQ
// touches the audio by at most the float recurrence's rounding —
// the 31 Hz shelf's slow pole accumulates it to a few 1e-4, still
// ~80 dB below full scale.
void EqualizerTest::flatIsTheIdentity()
{
    Equalizer eq(48000);
    const int n = 4800;
    std::vector<float> in(n);
    for (int i = 0; i < n; ++i)
        in[i] = 0.7F * static_cast<float>(
                           std::sin(0.1 * i) + (0.3 * std::sin(0.37 * i)));
    const std::vector<float> ref = in;
    eq.process(in.data(), in.size() / 2);
    float maxError = 0.0F;
    for (int i = 0; i < n; ++i)
        maxError = qMax(maxError, qAbs(in[i] - ref[i]));
    QVERIFY(maxError < 1e-3F);
}

// A bypassed EQ (any gains) is a no-op: process() returns before it
// touches a sample.
void EqualizerTest::bypassLeavesTheSamples()
{
    Equalizer eq(48000);
    std::array<float, eq::BandCount> gainsDb{};
    gainsDb[0] = 12.0F;
    gainsDb[9] = -12.0F;
    eq.setGains(gainsDb);
    eq.setBypass(true);
    QVERIFY(eq.isBypassed());
    const int n = 960;
    std::vector<float> in(n);
    for (int i = 0; i < n; ++i)
        in[i] = 0.4F * static_cast<float>(std::sin(0.13 * i));
    const std::vector<float> ref = in;
    eq.process(in.data(), in.size() / 2);
    bool identical = true;
    for (int i = 0; i < n; ++i)
        if (in[i] != ref[i])
            identical = false;
    QVERIFY(identical);
}

// DC through a +12 dB low shelf converges to +12 dB (the shelf's DC
// gain); a flat shelf would leave the 0.5 untouched.
void EqualizerTest::lowShelfBoostsLowFrequencies()
{
    Equalizer eq(48000);
    std::array<float, eq::BandCount> gainsDb{};
    gainsDb[0] = 12.0F;
    eq.setGains(gainsDb);
    std::vector<float> in(48000, 0.5F);
    eq.process(in.data(), in.size() / 2);
    const double expected = 0.5 * std::pow(10.0, 12.0 / 20.0);
    // Float coefficient quantization shifts the DC gain by <1%.
    QVERIFY(qAbs(static_cast<double>(in.back()) - expected) < 0.03);
}

// The high shelf is flat at the bottom: DC comes through unchanged
// (+12 dB on the 16 kHz band affects nothing below it).
void EqualizerTest::highShelfLeavesLowFrequencies()
{
    Equalizer eq(48000);
    std::array<float, eq::BandCount> gainsDb{};
    gainsDb[9] = 12.0F;
    eq.setGains(gainsDb);
    std::vector<float> in(48000, 0.5F);
    eq.process(in.data(), in.size() / 2);
    QVERIFY(qAbs(static_cast<double>(in.back()) - 0.5) < 0.01);
}

// At its corner frequency the high shelf sits between flat and the
// full +12 dB (the S = 1 slope is monotonic, so it cannot overshoot;
// the exact corner gain is A = 10^(dB/40) in amplitude, +6 dB for a
// +12 dB band). 16 kHz at 48 kHz is three samples per cycle, so the
// sampled peak undershoots the true amplitude by at most a factor of
// sin(60°) = 0.866 — the window's max lands in [0.864, 0.998].
void EqualizerTest::highShelfBoostsItsCorner()
{
    const float peak = steadyAmplitude(16000.0, 48000.0, [] {
        std::array<float, eq::BandCount> gainsDb{};
        gainsDb[9] = 12.0F;
        return gainsDb;
    }());
    QVERIFY(peak > 0.8F); // well above flat (0.5)
    QVERIFY(peak < 1.25F); // well below full +12 dB (1.99)
}

// A +12 dB peaking band boosts its own center by exactly +12 dB (the
// other nine flat bands are the identity).
void EqualizerTest::peakingBoostsItsCenter()
{
    const float peak = steadyAmplitude(1000.0, 48000.0, [] {
        std::array<float, eq::BandCount> gainsDb{};
        gainsDb[5] = 12.0F;
        return gainsDb;
    }());
    const double expected = 0.5 * std::pow(10.0, 12.0 / 20.0);
    QVERIFY(qAbs(static_cast<double>(peak) - expected) < 0.02);
}

// Far from the boosted band's center the peaking filter is flat: a
// 100 Hz sine with a +12 dB 1 kHz band comes through unchanged (the
// 125 Hz band beside it is flat, hence the identity).
void EqualizerTest::peakingLeavesOtherFrequencies()
{
    const float peak = steadyAmplitude(100.0, 48000.0, [] {
        std::array<float, eq::BandCount> gainsDb{};
        gainsDb[5] = 12.0F;
        return gainsDb;
    }());
    QVERIFY(qAbs(static_cast<double>(peak) - 0.5) < 0.01);
}

// The coefficients depend on the sample rate: the same 1 kHz tone at
// 44.1 kHz must see the same +12 dB at the boosted band's center.
void EqualizerTest::setSampleRateReTunes()
{
    const float peak = steadyAmplitude(1000.0, 44100.0, [] {
        std::array<float, eq::BandCount> gainsDb{};
        gainsDb[5] = 12.0F;
        return gainsDb;
    }());
    const double expected = 0.5 * std::pow(10.0, 12.0 / 20.0);
    QVERIFY(qAbs(static_cast<double>(peak) - expected) < 0.02);
}

void EqualizerTest::gainsAreClamped()
{
    Equalizer eq(48000);
    std::array<float, eq::BandCount> gainsDb{};
    gainsDb[0] = 99.0F;
    gainsDb[9] = -99.0F;
    eq.setGains(gainsDb);
    QVERIFY(qAbs(eq.gainsDb()[0] - static_cast<float>(eq::MaxGainDb)) < 1e-6F);
    QVERIFY(qAbs(eq.gainsDb()[9] + static_cast<float>(eq::MaxGainDb)) < 1e-6F);
}

} // namespace

QTEST_MAIN(EqualizerTest)
#include "equalizer_test.moc"
