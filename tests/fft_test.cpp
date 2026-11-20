#include <QtTest>

#include <QVector>

#include <cmath>
#include <complex>
#include <random>

#include "fft.h"

namespace {

constexpr float kPi = 3.14159265358979323846F;

class FftTest : public QObject
{
    Q_OBJECT

private slots:
    void powerOfTwo();
    void magnitudesSize();
    void magnitudesOfConstant();
    void magnitudesOfSineAtBinCenter();
    void sineBetweenBinsKeepsTheEnergy();
    void inverseRoundTrip();
    void parseval();
    void magnitudesGuardNullAndBadSize();
};

// A sine of n samples at bin k (frequency k * rate / n, any rate —
// only the bin matters): the classic test signal.
QVector<float> sine(int n, int k, float amplitude = 1.0F)
{
    QVector<float> out(static_cast<qsizetype>(n));
    for (int i = 0; i < n; ++i)
        out[i] = amplitude * std::sin(2.0F * kPi * static_cast<float>(k)
                                      * static_cast<float>(i)
                                      / static_cast<float>(n));
    return out;
}

void FftTest::powerOfTwo()
{
    QVERIFY(!fft::isPowerOfTwo(0));
    QVERIFY(!fft::isPowerOfTwo(1));
    QVERIFY(fft::isPowerOfTwo(2));
    QVERIFY(!fft::isPowerOfTwo(3));
    QVERIFY(fft::isPowerOfTwo(4));
    QVERIFY(!fft::isPowerOfTwo(6));
    QVERIFY(fft::isPowerOfTwo(1024));
}

// The one-sided spectrum of a real signal of size n covers the bins
// 0..n/2 (size n/2 + 1).
void FftTest::magnitudesSize()
{
    const QVector<float> samples = sine(64, 4);
    QCOMPARE(fft::magnitudes(samples).size(), 33);
}

// A constant c: all the energy in the DC bin (|X[0]| = n*c), the rest
// flat (up to float noise).
void FftTest::magnitudesOfConstant()
{
    const int n = 64;
    QVector<float> samples(n, 0.75F);
    const QVector<float> mag = fft::magnitudes(samples);
    QVERIFY(qAbs(mag[0] - (n * 0.75F)) < 0.01F);
    for (int k = 1; k <= n / 2; ++k)
        QVERIFY(mag[k] < 0.01F);
}

// A full-scale sine at a bin center: the peak bin reads n/2 (the
// unwindowed sine's amplitude times n/2), the other bins flat.
void FftTest::magnitudesOfSineAtBinCenter()
{
    const int n = 64;
    const int k = 5;
    const QVector<float> samples = sine(n, k);
    const QVector<float> mag = fft::magnitudes(samples);
    QVERIFY(qAbs(mag[k] - (n / 2.0F)) < 0.1F);
    for (int i = 0; i <= n / 2; ++i)
        if (i != k)
            QVERIFY(mag[i] < 0.1F);
}

// A sine halfway between two bins leaks its energy over many bins: the
// two adjacent bins are the largest, each below the bin-centered peak
// (n/2 — the energy spread out), and the pair keeps most of the
// spectrum's energy (Parseval).
void FftTest::sineBetweenBinsKeepsTheEnergy()
{
    const int n = 64;
    const int k = 5;
    // A sine at 11 cycles over 128 samples, truncated to 64: the
    // frequency is 5.5 bins of the 64-point DFT (a half-bin offset).
    const QVector<float> samples = sine(2 * n, (2 * k) + 1).mid(0, n);
    const QVector<float> mag = fft::magnitudes(samples);
    // The two adjacent bins are the largest (the rest is leakage).
    const float top = qMax(mag[k], mag[k + 1]);
    for (int i = 0; i <= n / 2; ++i)
        if (i != k && i != k + 1)
            QVERIFY(mag[i] < top);
    // Each is below the bin-centered peak: the energy spread out.
    QVERIFY(mag[k] < (n / 2.0F));
    QVERIFY(mag[k + 1] < (n / 2.0F));
    // ...and the pair holds most of the energy (the one-sided sum, the
    // interior bins doubled).
    double energy =
        (static_cast<double>(mag[0]) * mag[0])
        + (static_cast<double>(mag[n / 2]) * mag[n / 2]);
    for (int i = 1; i < n / 2; ++i)
        energy += (2.0 * static_cast<double>(mag[i]) * mag[i]);
    const double pair =
        2.0 * ((static_cast<double>(mag[k]) * mag[k])
               + (static_cast<double>(mag[k + 1]) * mag[k + 1]));
    qInfo() << "pair share of the energy:" << pair / energy;
    QVERIFY(pair / energy > 0.75);
}

// inverse(forward(x)) * (1/n) is x: the transform is a true round trip.
void FftTest::inverseRoundTrip()
{
    const int n = 128;
    QVector<std::complex<float>> data(static_cast<qsizetype>(n));
    // A fixed seed: the test data is meant to be reproducible.
    std::mt19937 rng(42);  // NOLINT(bugprone-random-generator-seed)
    std::uniform_real_distribution<float> dist(-1.0F, 1.0F);
    for (auto &value : data)
        value = std::complex<float>(dist(rng), dist(rng));
    const QVector<std::complex<float>> original = data;

    fft::transform(data, false);
    fft::transform(data, true);
    for (int i = 0; i < n; ++i) {
        data[i] /= n;
        QVERIFY2(qAbs(data[i].real() - original[i].real()) < 1e-3F
                   && qAbs(data[i].imag() - original[i].imag()) < 1e-3F,
                 "round trip mismatch");
    }
}

// Parseval: the (one-sided) spectrum's energy is n times the signal's
// (the DC and Nyquist bins are unpaired, the rest doubled).
void FftTest::parseval()
{
    const int n = 128;
    // A fixed seed: the test data is meant to be reproducible.
    std::mt19937 rng(7);  // NOLINT(bugprone-random-generator-seed)
    std::uniform_real_distribution<float> dist(-1.0F, 1.0F);
    QVector<float> samples(static_cast<qsizetype>(n));
    double signalEnergy = 0.0;
    for (auto &sample : samples) {
        sample = dist(rng);
        signalEnergy += (static_cast<double>(sample) * sample);
    }
    const QVector<float> mag = fft::magnitudes(samples);
    double spectrumEnergy = 0.0;
    for (int k = 1; k < n / 2; ++k)
        spectrumEnergy += (2.0 * static_cast<double>(mag[k]) * mag[k]);
    spectrumEnergy += (static_cast<double>(mag[0]) * mag[0])
        + (static_cast<double>(mag[n / 2]) * mag[n / 2]);
    qWarning() << "Parseval mismatch:" << spectrumEnergy
               << "vs" << (n * signalEnergy);
    QVERIFY(qAbs(spectrumEnergy - (n * signalEnergy))
            < (1e-2 * n * signalEnergy));
}

// An empty signal or a non-power-of-two length yields all zeros (no
// crash, no garbage).
void FftTest::magnitudesGuardNullAndBadSize()
{
    const QVector<float> magEmpty = fft::magnitudes({});
    QCOMPARE(magEmpty.size(), 1);
    QCOMPARE(magEmpty[0], 0.0F);

    const QVector<float> samples = sine(64, 4);
    const QVector<float> magBad = fft::magnitudes(samples.mid(0, 63));
    QCOMPARE(magBad.size(), 32);
    for (const float v : magBad)
        QCOMPARE(v, 0.0F);
}

}  // namespace

QTEST_MAIN(FftTest)
#include "fft_test.moc"
