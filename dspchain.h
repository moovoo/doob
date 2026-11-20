#ifndef DSPCHAIN_H
#define DSPCHAIN_H

#include <QList>
#include <QMutex>
#include <QObject>
#include <QPointer>

#include <cstddef>

#include "audioprocessor.h"

// The audio-thread side of the DSP chain: a driver's output object
// owns one per stream and runs it over every chunk of frames it hands
// to the server, before the tap — so the visualization and the
// position count what is actually heard. It takes its snapshot of the
// DspRegistry (the enabled processors, the user's order) on stream
// start and on every chainChanged(), so a toggle or a move in the DSP
// preferences page takes effect from the next chunk: while a stream is
// running, a refresh also stops the processors that left the chain and
// starts the ones that joined it (on the stream's parameters) — a
// mid-stream enable must not wait for the next track.
//
// The snapshot holds QPointers: a plugin that unloads (deleting the
// processors it parented) leaves dead entries, which the runner skips
// — the chain never touches a deleted processor. The mutex serializes
// the audio thread's process() against the GUI thread's
// start()/stop()/refresh() (the processors' own GUI-thread state is
// the processors' problem, see AudioProcessor).
class DspChain : public QObject
{
    Q_OBJECT
public:
    explicit DspChain(QObject *parent = nullptr);

    // The stream lifecycle (the driver calls from the GUI thread when
    // the stream starts / stops): forwards to every processor in the
    // snapshot.
    void start(int sampleRate, int channels);
    void stop();

    // Runs the chain over the chunk in place; returns the frames left
    // (a dropping processor may return fewer, 0 = the whole chunk was
    // dropped).
    size_t process(float *frames, size_t frameCount, int channels);

private slots:
    // DspRegistry::chainChanged (the GUI thread): re-snapshot.
    void refresh();

private:
    mutable QMutex m_mutex;
    QList<QPointer<AudioProcessor>> m_chain;
    // The stream the chain is running (start/stop): a mid-stream
    // refresh starts the joined processors on these parameters and
    // stops the ones that left (no stream — the next start() reaches
    // every processor, so a refresh before the first start touches no
    // lifecycle).
    int m_sampleRate = 0;
    int m_channels = 0;
    bool m_running = false;
};

#endif // DSPCHAIN_H
