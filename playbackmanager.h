#ifndef PLAYBACKMANAGER_H
#define PLAYBACKMANAGER_H

#include <QObject>
#include <QPointer>
#include <QVector>

#include "mediaitem.h"

class AudioPlayer;
class PlaylistManager;

class PlaybackManager : public QObject
{
    Q_OBJECT
public:
    enum State { Stopped, Playing, Paused };
    Q_ENUM(State)
    // How the end of a track (or of the playlist) is handled: play once
    // through and stop, wrap the playlist, or replay the current track.
    // Shuffled playback picks the next track at random (the repeat mode
    // then governs only the auto-advance when a track finishes).
    enum class RepeatMode { NoRepeat, RepeatPlaylist, RepeatTrack };
    Q_ENUM(RepeatMode)

    static PlaybackManager* getInstance(QObject *parent = nullptr);
    ~PlaybackManager() override = default;

    State state() const { return m_state; }
    MediaItem playingItem() const { return m_playingItem; }
    // The item whose provider resolution is in flight (invalid = none) —
    // the "downloading" state the media views mark on the row it points
    // at.
    MediaItem resolvingItem() const { return m_resolvingItem; }
    // The item whose last play attempt failed (invalid = none) — the
    // "cannot play" mark. The next play attempt clears it.
    MediaItem failedItem() const { return m_failedItem; }
    // True while the given playlist is the one playing: the current
    // playlist in the Playing state (pause and stop are not playing).
    bool isPlayingPlaylist(quint64 playlistId) const;

    qint64 positionMs() const;
    qint64 durationMs() const;
    void setPositionMs(qint64 positionMs);

    void playCurrent();
    void playFromPlaylist(quint64 playlistId, int index);
    void togglePlayPause();
    void pause();
    void stop();
    void next();
    void previous();
    // The row the next track (manual next and auto-advance share) lands
    // on, given the shuffle/repeat state: a random other row when
    // shuffled, the next row sequentially (wrapping to 0 when the
    // playlist repeats), and -1 when there is nothing to play (the
    // caller stops). A pure query — the MPRIS's CanGoNext reads it.
    int nextTrackIndex() const;

    // Shuffle and repeat (the "playback.shuffle" / "playback.repeat"
    // settings, loaded by loadSettings() and written through on change).
    // Shuffle: the next track (manual next and auto-advance alike) is a
    // random other row. Repeat: what happens when a track finishes —
    // NoRepeat stops at the end of the playlist, RepeatPlaylist wraps to
    // the first row, RepeatTrack replays the current row.
    bool shuffle() const { return m_shuffle; }
    RepeatMode repeat() const { return m_repeat; }
    void setShuffle(bool shuffle);
    void setRepeat(RepeatMode mode);
    // The one-press controls a toolbar button uses.
    void toggleShuffle();
    // NoRepeat -> RepeatPlaylist -> RepeatTrack -> NoRepeat.
    void cycleRepeat();

    void setPlayer(AudioPlayer *player);
    AudioPlayer* player() const { return m_player; }

    // Driver selection (the "playback.player" setting): the id of the
    // active PlayerRegistry driver, "" for the default resolution: a
    // preferred default a plugin registered in the PlayerRegistry
    // (the FFmpeg backend advertises itself), else the built-in (the
    // virtual createDefaultPlayer(), the NullPlayer in production).
    // Applied at the next ensurePlayer — the currently set player keeps
    // playing until it is dropped. setActivePlayer rejects unknown ids;
    // an unknown id left in the file by a removed driver falls back to
    // the default resolution at ensurePlayer. loadSettings() also (re)binds
    // the manager to the current SettingsManager, so the settings page's live
    // edits of the playback.* keys reach the state (the app calls it
    // once from main, a test before it exercises the live path).
    void loadSettings();
    bool setActivePlayer(const QString &id);
    QString activePlayerId() const { return m_activePlayerId; }

    // The output volume and the mute over it: registered settings
    // ("playback.volume", "playback.muted"), loaded by loadSettings()
    // and written through on change. The level (0-100) is pushed to
    // the active driver as a linear level (AudioPlayer::setVolume) —
    // the server side of its output (the PulseAudio driver sets its
    // sink input's volume, so the level is shared with the other
    // clients of the server), and an external change of it comes back
    // through the driver's sinkVolumeChanged/sinkMuteChanged signals:
    // the state (and the setting) moves to the reported value, and the
    // previous level is not pushed back — the server is the source of
    // truth. setVolume() while muted unmutes. The mute over the level:
    // pushed as the driver's mute flag when it has one (AudioPlayer::
    // setMute — the level is kept, not zeroed), else as a 0.0 volume.
    // The level is kept when muted.
    int volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    // The active driver can control the output volume (a widget that
    // offers the control disables itself when it cannot).
    bool volumeSupported() const;
    void setVolume(int volume);
    void setMuted(bool muted);

    // The volume boost (the "playback.volume_boost" setting): a
    // linear multiplier on the frames the driver sends out — the
    // level above the server's normal volume (0-200, 100 = a no-op,
    // above 100 amplifies and can clip). Loaded by loadSettings() and
    // written through on change; pushed to the active driver as a
    // linear gain (AudioPlayer::setBoost) when it has headroom.
    int volumeBoost() const { return m_volumeBoost; }
    void setVolumeBoost(int volumeBoost);

signals:
    void activePlayerChanged(const QString &id);
    void volumeChanged(int volume);
    void mutedChanged(bool muted);
    void volumeBoostChanged(int volumeBoost);
    // The active driver's volume support changed (a driver switch).
    void volumeSupportChanged(bool supported);
    void stateChanged(PlaybackManager::State state);
    void playingItemChanged(const MediaItem &item);
    void shuffleChanged(bool shuffle);
    void repeatChanged(PlaybackManager::RepeatMode mode);
    void positionChanged(qint64 positionMs);
    void durationChanged(qint64 durationMs);
    // A user-visible failure (the main window shows it in the status
    // bar): the item could not be played (a provider that resolved no
    // stream).
    void playbackError(const QString &message);
    // Emitted with the item whose provider resolution started, and with
    // an invalid item when it lands (or is superseded): the views mark
    // the row "downloading" in between.
    void resolvingItemChanged(const MediaItem &item);
    // The audio tap of the active driver (re-emitted from the player, so
    // a widget observes the manager, not the driver): the per-channel
    // levels (0..1), the min/max downsampled waveform and the raw
    // frames (interleaved stereo float — the frequency display's tap),
    // all with the stream's sample rate. The re-emit is one-for-one (no
    // cap — the taps' rhythm is the driver's, and a widget that wants a
    // steady cadence paces itself against the audio clock the taps
    // describe, see OscillogramWidget). Silent while the active driver
    // has no sample access (the built-in null driver).
    void audioLevels(float leftPeak, float rightPeak);
    void audioWaveform(const QVector<float> &waveform, quint32 sampleRate);
    void audioFrames(const QVector<float> &frames, quint32 sampleRate);
    // The stream info of the source the active driver opened (re-emitted
    // from the player, so a widget observes the manager, not the driver):
    // the codec, the container format and the decoded stream's actual
    // bitrate / sample rate / channels. An empty map is "no info" — the
    // driver has no access to the stream (the built-in null driver),
    // a new track is starting, or the playback is stopped.
    void streamInfoChanged(const QVariantMap &info);

private slots:
    // A volume setting edited outside the manager (the settings page's
    // live browser): the state moves from the file's value and the
    // views' signals follow. Idempotent — the set*() methods' own
    // writes land here as no-ops (the state already matches).
    void onSettingsChanged(const QString &key, const QVariant &value);
    // The driver's position (re-emitted), and the gapless arm: when a
    // gapless-capable driver is in the current item's final seconds,
    // the next item is queued for the gapless handoff (the display
    // advances early — standard for gapless players).
    void onPlayerPositionChanged(qint64 positionMs);
    void onPlayerFinished();
    // The player could not (no longer can) play the current item: it is
    // marked failed and the playback stops — a failure is not a track
    // end, and auto-advancing over it would hide the broken row.
    void onPlayerError(const QString &message);
    // The transport moved outside of doob (a backend with other clients
    // — the native Spotify app — paused, resumed or stopped the stream):
    // the manager's state follows the driver's report.
    void onPlayerPaused();
    void onPlayerResumed();
    void onPlayerStopped();
    // The driver's sink volume / mute changed outside of doob (another
    // client of the server — pavucontrol changed the app's stream
    // volume): the state (and the setting) moves to the reported value
    // and the previous level is NOT pushed back — the server is the
    // source of truth. An echo of doob's own push is a no-op (the
    // state already matches).
    void onPlayerSinkVolumeChanged(float linear);
    void onPlayerSinkMuteChanged(bool muted);
    void onPlaylistCurrentIndexChanged(int index);
    void onPlaylistChanged();
    void onCurrentPlaylistChanged(quint64 id);

protected:
    explicit PlaybackManager(QObject *parent = nullptr);

    // The player used when none is explicitly set via setPlayer(). Tests
    // override this to avoid requiring a real media backend.
    virtual AudioPlayer* createDefaultPlayer();

private:
    void ensurePlayer();
    // The configured driver (the "playback.player" selection, resolved
    // like ensurePlayer's fallbacks) replaces the active player: the
    // switch-back from a provider-owned driver that the next item
    // does not belong to.
    void restoreActivePlayer();
    void setState(State state);
    // The player signals the manager follows (the connect on the way
    // in, the disconnect on the way out — one list, both directions).
    void wirePlayerSignals(AudioPlayer *player, bool on);
    // Pushes the level and the mute to the active driver (the level as
    // a linear level — 0.0 when muted on a driver without a mute flag
    // — and the mute as the flag when it has one).
    void applyVolume();
    // Pushes the boost gain to the active driver (a no-op when it has
    // no headroom).
    void applyBoost();
    // Sets the item whose resolution is in flight (an invalid item
    // clears it), emitting resolvingItemChanged() on a change.
    void setResolving(const MediaItem &item);
    // Arms the gapless handoff for the current item (once per item):
    // advances the playlist index, switches the display item and
    // queues the next item on the driver. A no-op when the driver has
    // no capability, the item is not in its final seconds, or there is
    // no next row.
    void maybeArmGapless(qint64 positionMs);
    // Queues item for the gapless handoff, provider-aware (like
    // playItem): a provider item's stored source is the provider's
    // (a watch page), so it is resolved to a stream URL first and the
    // resolved copy is queued on the landing; a non-provider item is
    // queued as stored.
    void queueGaplessItem(const MediaItem &item);
    void playItem(const MediaItem &item);
    // The resolution of a played item landed (token guards against a
    // superseded in-flight resolution): starts the playback, or stops
    // when the provider could not produce a stream and the active player
    // is not the provider's own (which may play the stored source
    // itself).
    void startPlayback(int token, const MediaItem &item, const QUrl &stream,
                       bool providerOwnedPlayer);
    void advanceTo(int index);
    // A stored repeat value to its mode (a hand-edited file may carry an
    // out-of-range int: an unknown value is NoRepeat).
    static RepeatMode repeatModeFromInt(int value);

    PlaylistManager* m_pl = nullptr;
    AudioPlayer* m_player = nullptr;
    // The provider-owned driver the last provider item switched in (an
    // empty pointer when the active player is the configured one): the
    // next non-provider item (or one of another provider) switches back.
    QPointer<AudioPlayer> m_providerPlayer;
    State m_state = Stopped;
    MediaItem m_playingItem;
    // The in-flight provider resolution and the last failed play (both
    // invalid = none).
    MediaItem m_resolvingItem;
    MediaItem m_failedItem;
    bool m_followingPlaylist = false;
    // The gapless arm (true = the display advanced early and the next
    // item is queued on the driver): latched from the arm until the
    // boundary's finished (the driver's no-op for it), a stop, a new
    // play, or a driver switch. A plain flag, not the armed item's id —
    // the arm switches the display item, so an id guard would compare
    // against the *new* item on the next position update and arm a
    // second time within the same pre-roll window (skipping a track).
    bool m_gaplessArmed = false;
    // The queueNext answer (direct, or the provider resolution's
    // landing): true only when the armed item actually reached the
    // driver's queue. The boundary's finished is a no-op only then:
    // if the queue never made it (a slow resolution, a refusal, a
    // pause), the driver ended the old item plainly and the manager
    // plays the armed item itself (a gap, never a stuck state).
    bool m_gaplessQueued = false;
    // Bumped by every playItem and stop: a resolution callback whose
    // token is stale was superseded (a newer play, or a stop) and is a
    // no-op.
    int m_resolveToken = 0;
    // The selected driver ("" = built-in default), held in the
    // "playback.player" setting.
    QString m_activePlayerId;
    // The output volume (0-100, the server's normal range) and the
    // mute over it, held in the "playback.volume" / "playback.muted"
    // settings.
    int m_volume = 100;
    bool m_muted = false;
    // The volume boost (0-200, 100 = no-op, above 100 = gain), held in
    // the "playback.volume_boost" setting.
    int m_volumeBoost = 100;
    // Shuffle and repeat, held in the "playback.shuffle" / "playback.repeat"
    // settings.
    bool m_shuffle = false;
    RepeatMode m_repeat = RepeatMode::NoRepeat;

    inline static PlaybackManager* m_instance;
};

#endif // PLAYBACKMANAGER_H
