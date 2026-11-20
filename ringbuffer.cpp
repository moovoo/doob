#include <doob/ringbuffer.h>

#include <algorithm>
#include <cstring>

RingBuffer::RingBuffer(size_t capacityFrames)
    : m_data(capacityFrames * 2),
      m_capacity(capacityFrames)
{
}

void RingBuffer::write(const float *frames, size_t frameCount, int writerId)
{
    // Clamp defensively: a writer id out of range would index the stop
    // flag out of bounds.
    if (writerId < 0 || writerId >= kMaxWriters)
        return;
    std::unique_lock lock(m_mutex);
    while (m_count + frameCount > m_capacity
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
            && !m_stop[writerId])
        m_cv.wait(lock);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    if (m_stop[writerId])
        return;
    // When the write does not fit linearly, compact: pull the pending
    // frames to the front (a write chunk is far smaller than the ring,
    // so this amortizes to nothing).
    if (m_head + frameCount > m_capacity) {
        std::memmove(m_data.data(), &m_data[m_tail * 2],
                     m_count * 2 * sizeof(float));
        m_head = m_count;
        m_tail = 0;
    }
    std::memcpy(&m_data[m_head * 2], frames,
                frameCount * 2 * sizeof(float));
    m_head += frameCount;
    m_count += frameCount;
    m_cv.notify_all();
}

size_t RingBuffer::read(float *out, size_t frameCount)
{
    std::unique_lock lock(m_mutex);
    const size_t n = std::min(frameCount, m_count);
    if (n < frameCount)
        std::memset(out, 0, frameCount * 2 * sizeof(float));
    if (n > 0) {
        std::memcpy(out, &m_data[m_tail * 2], n * 2 * sizeof(float));
        m_tail += n;
        m_count -= n;
        m_readTotal += n;
        if (m_tail == m_head) {
            m_tail = 0;
            m_head = 0;
        }
    }
    m_cv.notify_all();
    return n;
}

void RingBuffer::requestStop(int writerId)
{
    if (writerId < 0 || writerId >= kMaxWriters)
        return;
    {
        std::scoped_lock lock(m_mutex);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        m_stop[writerId] = true;
    }
    m_cv.notify_all();
}

uint64_t RingBuffer::drain()
{
    uint64_t reads = 0;
    {
        std::scoped_lock lock(m_mutex);
        m_head = m_tail = m_count = 0;
        std::fill(std::begin(m_stop), std::end(m_stop), false);
        reads = m_readTotal;
    }
    m_cv.notify_all();
    return reads;
}

size_t RingBuffer::capacityFrames() const
{
    return m_capacity;
}

size_t RingBuffer::availableFrames() const
{
    std::scoped_lock lock(m_mutex);
    return m_count;
}

uint64_t RingBuffer::framesRead() const
{
    std::scoped_lock lock(m_mutex);
    return m_readTotal;
}
