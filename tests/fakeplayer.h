#ifndef DOOB_TESTS_FAKEPLAYER_H
#define DOOB_TESTS_FAKEPLAYER_H

#include <QObject>
#include <QVector>

#include "audioplayer.h"
#include "mediaitem.h"

// The shared test player: the superset of the FakePlayer copies the
// widget and manager tests used to carry. It records what the playback
// manager asked of a player (play/pause/resume/stop, the position and
// duration, the seeks, the volume/mute/boost) and simulates the
// player's signals (a failure, the end of the track, position/
// duration changes, the sink's volume/mute reports). A test must never
// create a real playback driver. No Q_OBJECT: the double adds no
// signals, slots or properties of its own, so the base metaobject
// suffices (and the class needs no moc, which a shared header cannot
// provide to the per-test translation units).
class FakePlayer : public AudioPlayer
{
public:
    explicit FakePlayer(QObject *parent = nullptr) : AudioPlayer(parent) {}

    void play(const MediaItem &item) override
    {
        m_item = item;
        m_playing = true;
        m_paused = false;
        m_position = 0;
        ++m_playCount;
    }

    void pause() override
    {
        m_paused = true;
    }

    void resume() override
    {
        m_paused = false;
        ++m_resumeCount;
    }

    void stop() override
    {
        m_playing = false;
        m_paused = false;
        m_position = 0;
        m_item = MediaItem();
        ++m_stopCount;
    }

    qint64 positionMs() const override { return m_position; }
    qint64 durationMs() const override { return m_duration; }

    // The volume capability: on by default (the fake emulates a modern
    // driver); a test flips m_supportsVolume to exercise the manager's
    // no-volume path (the level is then not applied).
    bool supportsVolume() const override { return m_supportsVolume; }
    void setVolume(float volume) override
    {
        m_volume = volume;
        ++m_setVolumeCount;
    }

    // The mute capability: on by default (the fake emulates a
    // server-side mute — the level is kept, not zeroed); a test flips
    // m_supportsMute to exercise the manager's no-mute path (the mute
    // is then expressed as a 0.0 volume).
    bool supportsMute() const override { return m_supportsMute; }
    void setMute(bool muted) override
    {
        m_muted = muted;
        ++m_setMuteCount;
    }

    // The boost capability: on by default; a test flips m_supportsBoost
    // to exercise the manager's no-boost path (the gain is then not
    // applied).
    bool supportsBoost() const override { return m_supportsBoost; }
    void setBoost(float boost) override
    {
        m_boost = boost;
        ++m_setBoostCount;
    }

    // The gapless capability: off by default (the plain finished-at-end
    // behaviour, what the built-in driver and the pre-Phase-6 tests
    // exercise); a test flips m_supportsGapless to exercise the
    // manager's early arm and the boundary's no-op finished.
    bool supportsGapless() const override { return m_supportsGapless; }
    // The gapless queue's answer (the contract: true only when the
    // item is really queued). A test flips m_queueNextRefused to
    // exercise the manager's refused-queue boundary (the armed item
    // plays at the end through the plain path — a gap, never a stuck
    // state).
    bool queueNext(const MediaItem &item) override
    {
        ++m_queueNextCount;
        if (m_queueNextRefused)
            return false;
        m_queuedItem = item;
        return true;
    }

    void setPositionMs(qint64 positionMs) override
    {
        m_position = positionMs;
        ++m_seekCount;
        emit positionChanged(m_position);
    }

    // Simulates a playback failure (a missing file, a corrupt stream, a
    // dead connection).
    void fail(const QString &message) { emit errorOccurred(message); }

    void triggerFinished()
    {
        emit finished();
    }

    void triggerPosition(qint64 positionMs)
    {
        m_position = positionMs;
        emit positionChanged(m_position);
    }

    void triggerDuration(qint64 durationMs)
    {
        m_duration = durationMs;
        emit durationChanged(m_duration);
    }

    // Simulates the transport moving outside of doob (a backend with
    // other clients — the native Spotify app — paused, resumed or
    // stopped the stream).
    void triggerPaused() { emit paused(); }
    void triggerResumed() { emit resumed(); }
    void triggerStopped() { emit stopped(); }

    // Simulates the driver's sink reporting (the server-side volume
    // and mute a driver reads back — an external client changed the
    // app's stream). Emits the inherited AudioPlayer signals.
    void triggerSinkVolume(float linear)
    {
        emit sinkVolumeChanged(linear);
    }
    void triggerSinkMute(bool muted)
    {
        emit sinkMuteChanged(muted);
    }

    // Simulates the audio tap (the levels and waveform a driver with
    // sample access reports). The waveform carries the stream's sample
    // rate (the display's scroll runs on the audio clock). Emits the
    // inherited AudioPlayer signals.
    void triggerLevels(float leftPeak, float rightPeak)
    {
        emit audioLevels(leftPeak, rightPeak);
    }

    void triggerWaveform(const QVector<float> &waveform,
                         quint32 sampleRate = 48000)
    {
        emit audioWaveform(waveform, sampleRate);
    }

    // Simulates the audio tap's raw frames (the interleaved stereo
    // float samples a driver with sample access reports, for the
    // frequency display). Emits the inherited AudioPlayer signal.
    void triggerFrames(const QVector<float> &frames,
                       quint32 sampleRate = 48000)
    {
        emit audioFrames(frames, sampleRate);
    }

    // Simulates the driver's stream info (the codec, container and the
    // decoded stream's actual parameters, reported when the source
    // opens). Emits the inherited AudioPlayer signal.
    void triggerStreamInfo(const QVariantMap &info)
    {
        emit streamInfo(info);
    }

    void resetCounters()
    {
        m_item = MediaItem();
        m_playing = false;
        m_paused = false;
        m_position = 0;
        m_duration = 0;
        m_playCount = 0;
        m_resumeCount = 0;
        m_stopCount = 0;
        m_seekCount = 0;
        m_supportsVolume = true;
        m_volume = 1.0F;
        m_setVolumeCount = 0;
        m_supportsMute = true;
        m_muted = false;
        m_setMuteCount = 0;
        m_supportsBoost = true;
        m_boost = 1.0F;
        m_setBoostCount = 0;
        m_supportsGapless = false;
        m_queueNextCount = 0;
        m_queueNextRefused = false;
        m_queuedItem = MediaItem();
    }

    MediaItem m_item;
    bool m_playing = false;
    bool m_paused = false;
    qint64 m_position = 0;
    qint64 m_duration = 0;
    int m_playCount = 0;
    int m_resumeCount = 0;
    int m_stopCount = 0;
    int m_seekCount = 0;
    bool m_supportsVolume = true;
    float m_volume = 1.0F;
    int m_setVolumeCount = 0;
    bool m_supportsMute = true;
    bool m_muted = false;
    int m_setMuteCount = 0;
    bool m_supportsBoost = true;
    float m_boost = 1.0F;
    int m_setBoostCount = 0;
    bool m_supportsGapless = false;
    int m_queueNextCount = 0;
    bool m_queueNextRefused = false;
    MediaItem m_queuedItem;
};

#endif // DOOB_TESTS_FAKEPLAYER_H
