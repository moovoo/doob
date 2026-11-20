#include "dspchain.h"

#include "dspregistry.h"

DspChain::DspChain(QObject *parent)
    : QObject(parent)
{
    connect(DspRegistry::getInstance(), &DspRegistry::chainChanged, this,
            &DspChain::refresh);
    refresh();
}

void DspChain::start(int sampleRate, int channels)
{
    QMutexLocker lock{&m_mutex};
    m_sampleRate = sampleRate;
    m_channels = channels;
    m_running = true;
    for (const QPointer<AudioProcessor> &processor : m_chain)
        if (processor)
            processor->start(sampleRate, channels);
}

void DspChain::stop()
{
    QMutexLocker lock{&m_mutex};
    m_running = false;
    m_sampleRate = 0;
    m_channels = 0;
    for (const QPointer<AudioProcessor> &processor : m_chain)
        if (processor)
            processor->stop();
}

size_t DspChain::process(float *frames, size_t frameCount, int channels)
{
    if (frameCount == 0)
        return 0;
    QMutexLocker lock{&m_mutex};
    size_t count = frameCount;
    for (const QPointer<AudioProcessor> &processor : m_chain) {
        if (count == 0)
            break;
        if (processor)
            count = processor->process(frames, count, channels);
    }
    return count;
}

void DspChain::refresh()
{
    QMutexLocker lock{&m_mutex};
    const QList<QPointer<AudioProcessor>> current = m_chain;
    m_chain.clear();
    for (AudioProcessor *processor : DspRegistry::getInstance()->chain())
        m_chain.append(processor);
    if (!m_running)
        return;  // no stream: the next start() reaches every processor
    // A mid-stream edit: the processors that left the chain are
    // stopped (their per-stream state must not survive the edit) and
    // the ones that joined it are started on the stream's parameters —
    // a newly enabled DSP takes effect from the next chunk, not the
    // next track.
    for (const QPointer<AudioProcessor> &processor : current)
        if (processor && !m_chain.contains(processor))
            processor->stop();
    for (const QPointer<AudioProcessor> &processor : m_chain)
        if (processor && !current.contains(processor))
            processor->start(m_sampleRate, m_channels);
}
