#include "audiotap.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>

AudioTap tapFrames(const float *frames, size_t frameCount, int buckets)
{
    AudioTap tap;
    if (frames == nullptr || frameCount == 0 || buckets <= 0)
        return tap;

    // The slice's raw frames (the frequency display's tap) ride the
    // snapshot: a copy, the caller's buffer is reused on the next pass.
    tap.frames.resize(2 * static_cast<qsizetype>(frameCount));
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    std::copy(frames, frames + (2 * frameCount), tap.frames.data());

    const qsizetype cols = buckets;
    tap.waveform.resize(2 * cols);
    // Each bucket starts "empty" (min +inf, max -inf) so the first sample
    // always lands; a bucket that receives no frame is flattened to zero
    // afterwards.
    const float inf = std::numeric_limits<float>::infinity();
    for (qsizetype b = 0; b < cols; ++b) {
        const qsizetype idx = 2 * b;
        tap.waveform[idx] = inf;
        tap.waveform[idx + 1] = -inf;
    }

    // Each bucket covers span consecutive frames; the last bucket takes
    // the remainder. With more buckets than frames, span is 0 and every
    // frame folds into bucket 0.
    const size_t span = frameCount / static_cast<size_t>(buckets);
    const size_t last = static_cast<size_t>(buckets) - 1;
    float leftPeak = 0.0F;
    float rightPeak = 0.0F;
    for (size_t i = 0; i < frameCount; ++i) {
        // The raw interleaved sample buffer (L, R, L, R, ...) has no
        // accessor but the subscript.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        const float l = frames[2 * i];
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        const float r = frames[(2 * i) + 1];
        leftPeak = qMax(leftPeak, std::abs(l));
        rightPeak = qMax(rightPeak, std::abs(r));

        const size_t bucket = span == 0 ? 0 : qMin(i / span, last);
        const qsizetype idx = 2 * static_cast<qsizetype>(bucket);
        tap.waveform[idx] = qMin(tap.waveform[idx], qMin(l, r));
        tap.waveform[idx + 1] = qMax(tap.waveform[idx + 1], qMax(l, r));
    }

    // Flatten the buckets no frame reached (a silent gap reads flat).
    for (qsizetype b = 0; b < cols; ++b) {
        const qsizetype idx = 2 * b;
        if (tap.waveform[idx] == inf) {
            tap.waveform[idx] = 0.0F;
            tap.waveform[idx + 1] = 0.0F;
        }
    }

    tap.leftPeak = qMin(1.0F, leftPeak);
    tap.rightPeak = qMin(1.0F, rightPeak);
    return tap;
}
