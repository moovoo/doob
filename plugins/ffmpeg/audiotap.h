#ifndef FFMPEG_AUDIOTAP_H
#define FFMPEG_AUDIOTAP_H

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

// The tap is produced at the moment the frames are handed to the audio
// server, but they only reach the speaker after the stream's latency —
// unshifted, a meter would lead the sound by that delay. TapDelay holds
// each tap until it has aged past the latency (against the caller's
// monotonic clock) and then emits it, oldest first, so a display fed
// from the due taps follows the audible audio.
class TapDelay
{
public:
    // Records a tap at the caller's monotonic time (ms). The entries
    // stay sorted by time (due/prune pop from the front), so a tap
    // stamped for the future (a pass slice's audible moment) is
    // inserted in place, not appended.
    void push(qint64 nowMs, const AudioTap &tap);
    // Every entry at least delayMs old, oldest first (pops them).
    QVector<AudioTap> due(qint64 nowMs, qint64 delayMs);
    // Drops entries older than maxMs: a tap whose moment has passed is
    // stale, not late — the sound it describes is over.
    void prune(qint64 nowMs, qint64 maxMs);
    void clear();
    int size() const { return static_cast<int>(m_entries.size()); }

private:
    struct Entry
    {
        qint64 timeMs = 0;
        AudioTap tap;
    };
    QVector<Entry> m_entries;
};

// Samples frameCount interleaved stereo float frames (L, R, L, R, ...)
// into per-channel peaks and a buckets-long min/max waveform. The
// waveform is the combined envelope: each column spans from the smallest
// sample to the largest over its frames, so a silent gap reads as a flat
// zero line. A bucket that covers no frame (more buckets than frames)
// stays at zero. Pure and allocation-light: unit-testable with no
// decoder and no audio server.
AudioTap tapFrames(const float *frames, size_t frameCount, int buckets);

#endif // FFMPEG_AUDIOTAP_H
