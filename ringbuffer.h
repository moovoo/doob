#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include <array>
#include <cstddef>
#include <condition_variable>
#include <mutex>
#include <vector>

// The fixed-capacity ring of interleaved stereo float samples
// separating an audio producer (writer) from its consumer (reader):
// the FFmpeg driver's decode workers into the output, the Spotify
// driver's private server into its capture pump. A full ring blocks
// the writer (back-pressure); an empty one yields silence to the
// reader (an underrun is a gap, never a stall). Two writers can share
// a ring (the gapless handoff: the current and the queued-next
// decode), so the stop flag is PER WRITER (a writer id 0..
// kMaxWriters-1): stopping one must not silence the other (a killed
// queued worker's stop would no-op the current worker's writes, and
// its frame counter would keep counting a phantom eos). The frame
// ORDER is the writers' job, not the ring's: the ring is a FIFO, so
// its order is the write order, and the gapless driver keeps the
// queued writer's frames out of the ring (its private staging) until
// the current writer is done (docs/plugins.md). requestStop(id)
// wakes a blocked writer; drain() resets the ring (a seek) and
// clears all the stop flags.
class RingBuffer
{
public:
    // The writers that can share a ring (the gapless handoff's two
    // decode workers).
    static constexpr int kMaxWriters = 2;

    explicit RingBuffer(size_t capacityFrames);

    RingBuffer(const RingBuffer &) = delete;
    RingBuffer &operator=(const RingBuffer &) = delete;

    // Writes frameCount frames as writerId, blocking while the ring is
    // full (a no-op when that writer's stop flag is set).
    void write(const float *frames, size_t frameCount, int writerId);
    // Reads up to frameCount frames into out (the rest is filled with
    // silence); returns the frames actually read.
    size_t read(float *out, size_t frameCount);
    // Resets the ring (a seek) and clears all the stop flags; returns
    // the total real frames read at the moment of the drain (the
    // player anchors its segment there — the anchor must be the
    // drain's own count, read later a pump pass may already have
    // advanced it).
    uint64_t drain();
    // Sets writerId's stop flag (a blocked writer wakes up and no-ops;
    // the other writers are untouched).
    void requestStop(int writerId);

    size_t capacityFrames() const;
    size_t availableFrames() const;
    // The total real frames read since construction (drain() does not
    // reset it): the player's gapless accounting re-anchors its
    // segment on it (the handoff's boundary is a read count, so a
    // dropping DSP processor cannot skew it).
    uint64_t framesRead() const;

private:
    std::vector<float> m_data;
    const size_t m_capacity;
    size_t m_head = 0;   // next write position
    size_t m_tail = 0;   // next read position
    size_t m_count = 0;
    uint64_t m_readTotal = 0;
    std::array<bool, kMaxWriters> m_stop{};
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
};

#endif // RINGBUFFER_H
