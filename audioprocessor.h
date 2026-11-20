#ifndef AUDIOPROCESSOR_H
#define AUDIOPROCESSOR_H

#include <QObject>
#include <QString>

#include <cstddef>

// The interface a DSP (digital signal processor) implements: a
// transform that runs over the audio frames as they leave a driver's
// open PCM path, one enabled processor at a time in the user's chain
// order (the DspRegistry, the DspChain runs them). A processor can
// shape the samples (an equalizer, a gain), change their count (a
// silence skip drops chunks and returns fewer frames) or fork the
// stream to its own destination (a fork passes the chunk through
// unchanged and feeds a second sink on the side — no extra surface is
// needed for that: the processor owns its destination).
//
// The registration is non-owning: the processor is parented to
// whoever registered it (a plugin) and dies with it; a running chain
// holds QPointers and skips a processor that an unload has deleted.
//
// Threading: start(), process() and stop() run on the driver's audio
// thread, serialized against each other by the chain's runner — a
// processor does not race itself through the chain, and the hooks and
// process() must not block (the audio is paced by them). State a
// processor mutates from the GUI thread (a setting's changed()) it
// guards itself (a mutex, the way the equalizer's gains are guarded).
class AudioProcessor : public QObject
{
    Q_OBJECT
public:
    explicit AudioProcessor(QObject *parent = nullptr)
        : QObject(parent)
    {
    }
    ~AudioProcessor() override = default;

    // The chain's identity: the id is what the enabled/order state is
    // keyed on (unique among the registered processors), the title is
    // what the DSP preferences page lists.
    virtual QString id() const = 0;
    virtual QString title() const = 0;

    // The stream lifecycle: the driver calls start() on its audio
    // thread when a new stream comes up (a new track) and stop() when
    // it tears down. A processor that changes the time axis (drops
    // frames) resets its per-track state here.
    virtual void start(int sampleRate, int channels)
    {
        Q_UNUSED(sampleRate);
        Q_UNUSED(channels);
    }
    virtual void stop()
    {
    }

    // One chunk: interleaved frames, `channels` floats per frame.
    // Modify the samples in place; shorten the chunk (drop frames) by
    // returning a smaller count — never return more than received. The
    // return value is the next processor's input (and, at the end of
    // the chain, what the driver writes out and counts as played).
    virtual size_t process(float *frames, size_t frameCount,
                           int channels) = 0;
};

#endif // AUDIOPROCESSOR_H
