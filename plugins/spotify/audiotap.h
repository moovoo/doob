#ifndef SPOTIFY_AUDIOTAP_H
#define SPOTIFY_AUDIOTAP_H

#include <QVector>

#include <QtGlobal>

#include <cstddef>

// One snapshot of the audio going out: the per-channel peak (0..1), a
// min/max downsampled waveform (2 * buckets floats: min0, max0, ...) and
// the slice's raw frames (interleaved stereo float, 2 * frames floats —
// the frequency display's tap).
struct AudioTap
{
    float leftPeak = 0.0F;
    float rightPeak = 0.0F;
    QVector<float> waveform;
    QVector<float> frames;
};

// Samples frameCount interleaved stereo float frames (L, R, L, R, ...)
// into per-channel peaks and a buckets-long min/max waveform. The
// waveform is the combined envelope: each column spans from the smallest
// sample to the largest over its frames, so a silent gap reads as a
// flat zero line. A bucket that covers no frame (more buckets than
// frames) stays at zero. Pure and allocation-light: unit-testable with
// no audio server.
AudioTap tapFrames(const float *frames, size_t frameCount, int buckets);

#endif // SPOTIFY_AUDIOTAP_H
