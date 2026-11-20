#ifndef FFT_H
#define FFT_H

#include <QVector>

#include <complex>

// A self-contained radix-2 FFT for the audio visualizations' frequency
// analysis (the spectrum's). No external dependency: an iterative
// Cooley-Tukey (decimation in time) over powers of two, sized to the
// display's window (a power of two of samples). Pure and
// allocation-light: unit-testable with no audio.
namespace fft {

// True when n is a power of two (and at least 2).
bool isPowerOfTwo(int n);

// In-place transform of a power-of-two-size vector (a smaller size is
// left untouched). inverse=true computes the unnormalized inverse
// (divide the result by the size for the usual convention).
void transform(QVector<std::complex<float>> &data, bool inverse = false);

// The magnitude spectrum of a real signal of a power-of-two length:
// |X[k]| for k = 0..n/2 (size n/2 + 1 — a real signal's spectrum is
// symmetric, the upper half carries no new information). An empty or
// a non-power-of-two signal yields all zeros.
QVector<float> magnitudes(const QVector<float> &samples);

}  // namespace fft

#endif  // FFT_H
