#ifndef PAINTRATE_H
#define PAINTRATE_H

#include <QString>

class QTimer;

// The paint rate of the audio visualizations (the oscillogram's, the
// peak meter's, the spectrum's, the spectrogram's): how often per
// second they may paint while playing. A user setting
// (visualization.fps) that the widgets read through these helpers, so
// the key, the range and the registration have one home. The widgets
// paint from a timer at the rate: the oscillogram's timer samples a
// trace that scrolls on the audio clock (see OscillogramWidget) — the
// cadence is steady and the user's choice without the driver's chunky
// tap rhythm tearing it — the peak meter's, the spectrum's and the
// spectrogram's timers are their decays' release clocks.
namespace PaintRate
{
// The setting's key ("visualization.fps").
QString key();
// The default rate and the range a stored value is clamped to (a
// hand-edited settings.json may hold anything else).
int defaultFps();
int minFps();
int maxFps();
// Register the setting (a silent no-op when it is already registered —
// the visualization widgets' constructors call this, the later ones
// are the no-ops).
void ensureRegistered();
// The current fps, clamped to the range (the default when the value
// is missing or invalid).
int fps();
// The paint timer's interval, in ms, for a fps (clamped like fps()).
int intervalMs(int fps);
// Sets `timer`'s interval to the current rate (the widgets' paint
// clocks: their constructors call this after ensureRegistered, and
// again on a rate change).
void applyTo(QTimer *timer);
// The per-tick decay factor for a release that decays by `base` per
// `baseMs`, rescaled to the timer's `intervalMs`: the release is per
// unit of time, not per tick, so it runs out in the same time at any
// paint rate (the peak meter's and the spectrum's 0.85 per 16 ms, the
// spectrogram's halving per its persistence).
double decayFactor(int intervalMs, double base, double baseMs);
}

#endif // PAINTRATE_H
