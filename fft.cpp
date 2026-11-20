#include "fft.h"

#include <algorithm>
#include <cmath>

namespace fft {

namespace {
constexpr float kPi = 3.14159265358979323846F;
}

bool isPowerOfTwo(int n)
{
    if (n < 2)
        return false;
    const unsigned u = static_cast<unsigned>(n);
    return (u & (u - 1)) == 0;
}

void transform(QVector<std::complex<float>> &data, bool inverse)
{
    const unsigned n = static_cast<unsigned>(data.size());
    if (!isPowerOfTwo(static_cast<int>(n)))
        return;

    // The bit-reversal permutation (the DIT's precondition).
    unsigned j = 0;
    for (unsigned i = 1; i < n; ++i) {
        unsigned bit = n / 2;
        for (; (j & bit) != 0; bit /= 2)
            j ^= bit;
        j += bit;
        if (j > i)
            std::swap(data[i], data[j]);
    }

    // The butterflies: stages of size 2, 4, ..., n; the sign of the
    // twiddle angle separates the forward (negative) and the inverse
    // (positive) transform.
    for (unsigned len = 2; len <= n; len *= 2) {
        const float angle = (inverse ? 2.0F : -2.0F) * kPi
            / static_cast<float>(len);
        const std::complex<float> wlen(std::cos(angle), std::sin(angle));
        for (unsigned i = 0; i < n; i += len) {
            std::complex<float> w(1.0F, 0.0F);
            for (unsigned j = 0; j < (len / 2); ++j) {
                const std::complex<float> u = data[i + j];
                const std::complex<float> v =
                    data[(i + j) + (len / 2)] * w;
                data[i + j] = u + v;
                data[(i + j) + (len / 2)] = u - v;
                w *= wlen;
            }
        }
    }
}

QVector<float> magnitudes(const QVector<float> &samples)
{
    const int n = static_cast<int>(samples.size());
    QVector<float> out(static_cast<qsizetype>((n / 2) + 1), 0.0F);
    if (n == 0 || !isPowerOfTwo(n))
        return out;
    QVector<std::complex<float>> data(static_cast<qsizetype>(n));
    for (int i = 0; i < n; ++i)
        data[i] = std::complex<float>(samples[i], 0.0F);
    transform(data, false);
    for (int k = 0; k <= (n / 2); ++k)
        out[k] = std::abs(data[k]);
    return out;
}

}  // namespace fft
