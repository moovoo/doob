#ifndef AUDIOPLAYER_H
#define AUDIOPLAYER_H

#include <QObject>
#include <QVariantMap>
#include <QVector>

#include "mediaitem.h"

// The playback driver contract. The PlaybackManager wires exactly one
// driver at a time: on a switch (setPlayer) it stops the outgoing
// driver and disconnects its signals, so only the active driver can
// reach the manager. The contract a driver must keep for that to mean
// anything — a non-active driver must never be able to write audio
// frames or change the playback state:
//
// - play(item) starts the item, superseding whatever was playing (the
//   previous source is stopped, not mixed with the new one).
// - stop() leaves the driver silent and externally un-activatable:
//   after it returns, no outside client of the driver's backend may be
//   able to start audio through it (a driver with a shared backend —
//   the Spotify Connect device — releases the backend on a stop, so
//   the other clients of that backend have nothing to select), and
//   any state the driver observed afterwards is discarded. A stop is
//   a release, not a pause: a pause keeps the backend owned (the user
//   comes back), a stop gives it up.
//
// A driver with a single-client backend (its own decoder, its own
// sink) keeps the contract for free: the thread stops, the sink is
// gone, there is no other client. A driver with a shared backend has
// to do the work itself (see the Spotify plugin's device teardown).
//
// The built-in null driver (NullPlayer) is the resolution chain's end:
// it keeps the contract trivially (it owns nothing) and refuses a
// play with errorOccurred — a doob without a real backend (the FFmpeg
// player plugin) fails the track visibly instead of playing silence.
class AudioPlayer : public QObject
{
    Q_OBJECT
public:
    explicit AudioPlayer(QObject *parent = nullptr) : QObject(parent) {}
    ~AudioPlayer() override = default;

    virtual void play(const MediaItem &item) = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    // Stops the playback and releases the driver's backend (the
    // class's contract: silent and externally un-activatable).
    virtual void stop() = 0;

    virtual qint64 positionMs() const = 0;

    // Optional capabilities: players that cannot report a duration or seek
    // keep the defaults and simply never emit the corresponding signals.
    virtual qint64 durationMs() const { return 0; }
    virtual void setPositionMs(qint64 positionMs) { Q_UNUSED(positionMs); }
    // The output volume (a linear level, 0.0 = silent, 1.0 = full):
    // the level on the server side of the driver's output — the
    // PulseAudio driver sets its sink input's volume, so other clients
    // (pavucontrol) see it and can change it back (the driver reports
    // the change through the sinkVolumeChanged signal below). The
    // level is kept when muted — the mute is a separate flag (setMute
    // when supported), never a 0.0 pushed as the level. An optional
    // capability like duration and seek: a driver without it keeps the
    // defaults (reports no support, ignores the level).
    virtual bool supportsVolume() const { return false; }
    virtual void setVolume(float volume) { Q_UNUSED(volume); }
    // The mute over the output level (an optional capability, like
    // volume): a driver with server-side mute (the PulseAudio sink
    // input's mute flag) silences without losing the level and
    // reports external un/mute through sinkMuteChanged. A driver
    // without it keeps the defaults — the PlaybackManager then
    // expresses the mute as a 0.0 volume (the old way).
    virtual bool supportsMute() const { return false; }
    virtual void setMute(bool muted) { Q_UNUSED(muted); }
    // The boost gain (an optional capability, like volume): a linear
    // multiplier the driver applies to the frames it sends out — the
    // level above the server's normal volume (the PulseAudio driver
    // scales the frames in its output). 1.0 is a no-op, 0.0 is
    // silent, above 1.0 amplifies (and can clip — the nature of the
    // feature). A driver without headroom keeps the defaults (reports
    // no support, ignores the gain).
    virtual bool supportsBoost() const { return false; }
    virtual void setBoost(float boost) { Q_UNUSED(boost); }
    // The gapless handoff (an optional capability, like volume): a
    // driver that supports it takes the next item early — queueNext,
    // called by the PlaybackManager while the current item is in its
    // final seconds — and plays into it without a gap when the current
    // item ends. finished then fires at the real boundary (when the
    // queued item's frames start being played), not at the current
    // item's container end. A driver without the capability keeps the
    // plain finished-at-end behaviour (the manager's normal advance).
    // queueNext answers true only when the item is really queued (the
    // manager's boundary decision rests on it — a false keeps the
    // plain finished path, a gap never a stuck state); the default
    // refuses.
    virtual bool supportsGapless() const { return false; }
    virtual bool queueNext(const MediaItem &item)
    {
        Q_UNUSED(item);
        return false;
    }

signals:
    void finished();
    // The player failed to play (or kept playing) the current item — a
    // missing file, a corrupt stream, a dead network connection. A
    // driver that cannot detect failure simply never emits it. (A
    // signal, not a virtual: adding it keeps the driver ABI stable.)
    void errorOccurred(const QString &message);
    // The transport state changed outside of doob (another client of
    // the same backend — e.g. the native Spotify app — paused, resumed
    // or stopped the stream): the driver mirrors what it observes, so
    // the manager's state (and the UI) follows a remote control. A
    // driver with a single-client backend never emits them. (Signals,
    // like errorOccurred, so adding them keeps the driver ABI stable.)
    void paused();
    void resumed();
    void stopped();
    // The driver's observed sink volume (a linear level, 0.0..1.0)
    // and sink mute: what the driver reads back from the server side
    // of its output — the PulseAudio driver reports its sink input's
    // volume and mute, which other clients (pavucontrol) can change.
    // The PlaybackManager treats the server as the source of truth:
    // it moves its state (and the settings) to the reported values and
    // does not push the previous level back. (Signals, like
    // errorOccurred, so adding them keeps the driver ABI stable.)
    void sinkVolumeChanged(float linear);
    void sinkMuteChanged(bool muted);
    void positionChanged(qint64 positionMs);
    void durationChanged(qint64 durationMs);
    // The audio tap (optional): the levels of the audio going out, per
    // channel (0..1 peak) and a min/max downsampled waveform (2 * buckets
    // floats: min0, max0, ...) of the frames going out, with the stream's
    // sample rate (a display can place the columns on the audio clock
    // from it). A driver with no access to the samples (the built-in
     // null driver) simply never emits them. (Signals, like errorOccurred, so
    // adding them keeps the driver ABI stable.)
    void audioLevels(float leftPeak, float rightPeak);
    void audioWaveform(const QVector<float> &waveform, quint32 sampleRate);
    // The raw frames of the audio going out (interleaved stereo float,
    // 2 * frames floats), with the stream's sample rate: the frequency
    // display's tap (an FFT over the frames, see the spectrum's
    // widget). A driver with no access to the samples (the built-in
     // null driver) simply never emits it. (A signal, like audioLevels, so
    // adding it keeps the driver ABI stable.)
    void audioFrames(const QVector<float> &frames, quint32 sampleRate);
    // The stream info of the source the driver opened (optional, like
    // the audio tap): what is actually being decoded — "codec" (the
    // codec id, e.g. "aac"), "format" (the container, e.g. "mp4"),
    // "bitrate" (bits/s, when known), "sampleRate" (Hz), "channels"
    // (count) and "channelLayout" (e.g. "stereo"). A driver with no
    // access to the stream (the built-in null driver) simply never emits it.
    // (A signal, like errorOccurred, so adding it keeps the driver ABI
    // stable.)
    void streamInfo(const QVariantMap &info);
};

#endif // AUDIOPLAYER_H
