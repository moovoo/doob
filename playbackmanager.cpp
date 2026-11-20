#include "playbackmanager.h"

#include <QFileInfo>
#include <QRandomGenerator>

#include "audioplayer.h"
#include "mediaitemstore.h"
#include "nullplayer.h"
#include "playerregistry.h"
#include "playlistmanager.h"
#include "settingsmanager.h"
#include "shortcutmanager.h"
#include "sourcemanager.h"

namespace {

// The gapless pre-roll (ms): the driver's final stretch in which the
// next item is queued (the display advances this early — the FFmpeg
// driver's ring headroom (2 s) keeps the handoff seamless well within
// it).
constexpr qint64 kGaplessLeadMs = 3000;
// The same lead for a NEXT item whose provider resolves it (a stream
// to connect and buffer, not a local file to read): the connection
// (the handshake, the first bytes) eats the short lead whole, so the
// ring would hold almost none of it at the boundary — the longer lead
// spends it on the wire, with the demuxer's buffer to spare.
constexpr qint64 kProviderGaplessLeadMs = 10000;

// One of the player signals the manager follows: connected on the way
// in, disconnected on the way out (one list, both directions share it).
template <typename Signal, typename Slot>
void wirePlayerSignal(AudioPlayer *player, PlaybackManager *manager,
                      Signal signal, Slot slot, bool on)
{
    if (on)
        QObject::connect(player, signal, manager, slot);
    else
        QObject::disconnect(player, signal, manager, slot);
}

} // namespace

PlaybackManager::PlaybackManager(QObject *parent)
    : QObject(parent)
{
    m_pl = PlaylistManager::getInstance();

    // The driver selection (like the built-in driver registration above,
    // idempotent: a second PlaybackManager in the same process is a no-op).
    SettingsManager::getInstance()->registerSetting(
        { QStringLiteral("playback.player"),
          QStringLiteral("Active playback driver"),
          QStringLiteral("The id of the active playback driver "
                         "(\"\" for the preferred driver a plugin "
                         "registered, else the built-in default). "
                         "Applied at the next track start."),
          QString(),
          QString(), });
    SettingsManager::getInstance()->registerSetting(
        { QStringLiteral("playback.volume"),
          QStringLiteral("Playback volume"),
          QStringLiteral("The output volume (0-100), on the server "
                         "side of the active playback driver's output "
                         "(the PulseAudio driver's sink input — shared "
                         "with the other clients of the server, which "
                         "can change it back)."),
          100,
          QString(), });
    SettingsManager::getInstance()->registerSetting(
        { QStringLiteral("playback.muted"),
          QStringLiteral("Muted"),
          QStringLiteral("Mute the output without losing the volume "
                         "(the slider keeps its value)."),
          false,
          QString(), });
    SettingsManager::getInstance()->registerSetting(
        { QStringLiteral("playback.volume_boost"),
          QStringLiteral("Volume boost"),
          QStringLiteral("A gain applied to the frames the driver "
                         "sends out — the level above the server's "
                         "normal volume (0-200, 100 = no change, "
                         "above 100 amplifies and can clip). A driver "
                         "without headroom ignores it."),
          100,
          QString(), });
    SettingsManager::getInstance()->registerSetting(
        { QStringLiteral("playback.shuffle"),
          QStringLiteral("Shuffle"),
          QStringLiteral("Play the next track at random instead of "
                         "sequentially."),
          false,
          QString(), });
    SettingsManager::getInstance()->registerSetting(
        { QStringLiteral("playback.repeat"),
          QStringLiteral("Repeat mode"),
          QStringLiteral("What happens when a track finishes: 0 = play "
                         "through once, 1 = repeat the playlist, "
                         "2 = repeat the current track."),
          0,
          QString(), });

    // The playback shortcuts (the ShortcutManager's registered actions):
    // the "Shortcuts" preferences topic lists them and the user's
    // sequence/scope choices persist in the SettingsManager. All global by
    // default — playback is driven from wherever the focus is. The three
    // transport actions additionally default to the hardware media keys
    // (X11 delivers them as key events, and a Wayland compositor that
    // forwards them as keys does the same — the compositors that route
    // them through MPRIS are covered by the MPRIS itself): the media key
    // is the primary sequence, the former default the fallback.
    ShortcutManager* sc = ShortcutManager::getInstance();
    sc->registerAction({ QStringLiteral("playback.play_pause"),
                         QStringLiteral("Play/pause"), QStringLiteral("Playback"),
                         QKeySequence(Qt::Key_MediaTogglePlayPause,
                                      static_cast<int>(Qt::CTRL | Qt::Key_P)), true,
                         [this] { togglePlayPause(); }, });
    sc->registerAction({ QStringLiteral("playback.stop"),
                         QStringLiteral("Stop"), QStringLiteral("Playback"),
                         QKeySequence(QStringLiteral("Ctrl+Shift+P")), true,
                         [this] { stop(); }, });
    sc->registerAction({ QStringLiteral("playback.next"),
                         QStringLiteral("Next track"), QStringLiteral("Playback"),
                         QKeySequence(Qt::Key_MediaNext,
                                      static_cast<int>(Qt::CTRL | Qt::Key_Right)),
                         true,
                         [this] { next(); }, });
    sc->registerAction({ QStringLiteral("playback.previous"),
                         QStringLiteral("Previous track"), QStringLiteral("Playback"),
                         QKeySequence(Qt::Key_MediaPrevious,
                                      static_cast<int>(Qt::CTRL | Qt::Key_Left)),
                         true,
                         [this] { previous(); }, });

    connect(m_pl, &PlaylistManager::currentIndexChanged, this, &PlaybackManager::onPlaylistCurrentIndexChanged);
    connect(m_pl, &PlaylistManager::playlistChanged, this, &PlaybackManager::onPlaylistChanged);
    connect(m_pl, &PlaylistManager::currentPlaylistChanged, this, &PlaybackManager::onCurrentPlaylistChanged);
}

PlaybackManager* PlaybackManager::getInstance(QObject *parent)
{
    if (m_instance == nullptr) {
        m_instance = new PlaybackManager(parent);
    }
    return m_instance;
}

void PlaybackManager::setPlayer(AudioPlayer *player)
{
    if (m_player == player)
        return;

    const bool wasSupported = volumeSupported();

    if (m_player != nullptr) {
        // The outgoing player is replaced, not shared: it must stop,
        // or it keeps playing in parallel with its successor (a
        // provider's device switched out of the way still holds the
        // queue — nothing else tells it to pause). The stop follows
        // the AudioPlayer's contract (silent and externally
        // un-activatable), the gate this whole mechanism rests on.
        m_player->stop();
        wirePlayerSignals(m_player, false);
        if (m_player->parent() == this)
            delete m_player;
    }

    m_player = player;
    m_gaplessArmed = false;  // the outgoing driver's queue is gone
    m_gaplessQueued = false;
    if (m_player != nullptr)
        wirePlayerSignals(m_player, true);

    // A new driver starts from the current level and boost (a fresh
    // player knows nothing of the user's settings).
    applyVolume();
    applyBoost();
    const bool supported = volumeSupported();
    if (supported != wasSupported)
        emit volumeSupportChanged(supported);
}

void PlaybackManager::wirePlayerSignals(AudioPlayer *player, bool on)
{
    wirePlayerSignal(player, this, &AudioPlayer::finished,
                     &PlaybackManager::onPlayerFinished, on);
    wirePlayerSignal(player, this, &AudioPlayer::errorOccurred,
                     &PlaybackManager::onPlayerError, on);
    wirePlayerSignal(player, this, &AudioPlayer::paused,
                     &PlaybackManager::onPlayerPaused, on);
    wirePlayerSignal(player, this, &AudioPlayer::resumed,
                     &PlaybackManager::onPlayerResumed, on);
    wirePlayerSignal(player, this, &AudioPlayer::stopped,
                     &PlaybackManager::onPlayerStopped, on);
    wirePlayerSignal(player, this, &AudioPlayer::positionChanged,
                     &PlaybackManager::onPlayerPositionChanged, on);
    wirePlayerSignal(player, this, &AudioPlayer::durationChanged,
                     &PlaybackManager::durationChanged, on);
    wirePlayerSignal(player, this, &AudioPlayer::audioLevels,
                     &PlaybackManager::audioLevels, on);
    wirePlayerSignal(player, this, &AudioPlayer::audioWaveform,
                     &PlaybackManager::audioWaveform, on);
    wirePlayerSignal(player, this, &AudioPlayer::audioFrames,
                     &PlaybackManager::audioFrames, on);
    wirePlayerSignal(player, this, &AudioPlayer::streamInfo,
                     &PlaybackManager::streamInfoChanged, on);
    wirePlayerSignal(player, this, &AudioPlayer::sinkVolumeChanged,
                     &PlaybackManager::onPlayerSinkVolumeChanged, on);
    wirePlayerSignal(player, this, &AudioPlayer::sinkMuteChanged,
                     &PlaybackManager::onPlayerSinkMuteChanged, on);
}

qint64 PlaybackManager::positionMs() const
{
    return m_player != nullptr ? m_player->positionMs() : 0;
}

qint64 PlaybackManager::durationMs() const
{
    return m_player != nullptr ? m_player->durationMs() : 0;
}

void PlaybackManager::setPositionMs(qint64 positionMs)
{
    if (m_player == nullptr || m_state == Stopped)
        return;
    m_player->setPositionMs(positionMs);
    // A seek flushes the driver's ring and kills its queued worker —
    // the handoff it dropped is re-armed (the driver re-decodes the
    // queued item from the top; queueNext is idempotent).
    if (m_gaplessArmed && m_playingItem.isValid())
        queueGaplessItem(m_playingItem);
}

void PlaybackManager::ensurePlayer()
{
    // Route through setPlayer() so the finished signal is connected — the
    // default player must auto-advance just like an explicitly set one.
    if (m_player != nullptr)
        return;
    restoreActivePlayer();
}

void PlaybackManager::restoreActivePlayer()
{
    m_providerPlayer.clear();
    PlayerRegistry* registry = PlayerRegistry::getInstance();
    AudioPlayer* player = nullptr;
    if (!m_activePlayerId.isEmpty())
        player = registry->create(m_activePlayerId, this);
    if (player == nullptr)
        // No selection, or the selected driver is gone (its plugin was
        // removed): a preferred default (a plugin that advertises its
        // driver, e.g. the FFmpeg backend) wins over the built-in
        // default. create() answers nullptr for an empty or stale id.
        player = registry->create(registry->preferredDefault(), this);
    if (player == nullptr)
        player = createDefaultPlayer();
    setPlayer(player);
}

AudioPlayer* PlaybackManager::createDefaultPlayer()
{
    // The built-in fallback: a doob without a plugin driver (the FFmpeg
    // player) refuses a play visibly (the driver's errorOccurred)
    // instead of playing into the void.
    return new NullPlayer(this);
}

void PlaybackManager::loadSettings()
{
    SettingsManager* settings = SettingsManager::getInstance();
    // (Re)bind to the current SettingsManager (the tests reset it between
    // runs):
    // the settings page's live edits of the volume keys arrive through
    // its changed() signal. The guard on the volume keys keeps a SettingsManager
    // without a registration and no stored value (a fresh test
    // SettingsManager) from clobbering the state with invalids.
    disconnect(settings, &SettingsManager::changed, this, nullptr);
    connect(settings, &SettingsManager::changed, this,
            &PlaybackManager::onSettingsChanged);

    m_activePlayerId = settings->value("playback.player").toString();
    // A hand-edited file may carry out-of-range values.
    const QVariant volume = settings->value("playback.volume");
    if (volume.isValid())
        m_volume = qBound(0, volume.toInt(), 100);
    const QVariant muted = settings->value("playback.muted");
    if (muted.isValid())
        m_muted = muted.toBool();
    // The old slider ceiling ("playback.max_volume", 1-200) was the
    // user's boost headroom: carry it into the boost setting, then
    // drop the key (an explicit "playback.volume_boost" wins below).
    if (settings->isModified("playback.max_volume")) {
        m_volumeBoost = qBound(0,
                               settings->value("playback.max_volume").toInt(),
                               200);
        settings->resetToDefault("playback.max_volume");
    }
    const QVariant boost = settings->value("playback.volume_boost");
    if (boost.isValid())
        m_volumeBoost = qBound(0, boost.toInt(), 200);
    const QVariant shuffle = settings->value("playback.shuffle");
    if (shuffle.isValid())
        m_shuffle = shuffle.toBool();
    const QVariant repeat = settings->value("playback.repeat");
    if (repeat.isValid())
        m_repeat = repeatModeFromInt(repeat.toInt());
}

void PlaybackManager::onSettingsChanged(const QString &key,
                                             const QVariant &value)
{
    if (key == "playback.volume") {
        const int level = qBound(0, value.toInt(), 100);
        if (level != m_volume) {
            m_volume = level;
            emit volumeChanged(m_volume);
            applyVolume();
        }
    } else if (key == "playback.muted") {
        const bool muted = value.toBool();
        if (muted != m_muted) {
            m_muted = muted;
            emit mutedChanged(m_muted);
            applyVolume();
        }
    } else if (key == "playback.volume_boost") {
        const int boost = qBound(0, value.toInt(), 200);
        if (boost != m_volumeBoost) {
            m_volumeBoost = boost;
            emit volumeBoostChanged(m_volumeBoost);
            applyBoost();
        }
    } else if (key == "playback.shuffle") {
        const bool shuffle = value.toBool();
        if (shuffle != m_shuffle) {
            m_shuffle = shuffle;
            emit shuffleChanged(m_shuffle);
        }
    } else if (key == "playback.repeat") {
        const RepeatMode mode = repeatModeFromInt(value.toInt());
        if (mode != m_repeat) {
            m_repeat = mode;
            emit repeatChanged(m_repeat);
        }
    }
}

bool PlaybackManager::volumeSupported() const
{
    return m_player != nullptr && m_player->supportsVolume();
}

void PlaybackManager::setVolume(int volume)
{
    const int clamped = qBound(0, volume, 100);
    if (clamped == m_volume && !m_muted)
        return;
    if (clamped != m_volume) {
        m_volume = clamped;
        SettingsManager::getInstance()->setValue("playback.volume", clamped);
        emit volumeChanged(m_volume);
    }
    // Adjusting the volume while muted unmutes: the user just expressed
    // a desired loudness.
    if (m_muted)
        setMuted(false);
    else
        applyVolume();
}

void PlaybackManager::setMuted(bool muted)
{
    if (muted == m_muted)
        return;
    m_muted = muted;
    SettingsManager::getInstance()->setValue("playback.muted", muted);
    emit mutedChanged(m_muted);
    applyVolume();
}

void PlaybackManager::setVolumeBoost(int volumeBoost)
{
    const int clamped = qBound(0, volumeBoost, 200);
    if (clamped == m_volumeBoost)
        return;
    m_volumeBoost = clamped;
    SettingsManager::getInstance()->setValue("playback.volume_boost", clamped);
    emit volumeBoostChanged(m_volumeBoost);
    applyBoost();
}

void PlaybackManager::applyVolume()
{
    if (m_player == nullptr)
        return;
    // The level is pushed as-is: a driver with a server-side mute flag
    // silences through the flag (the level is kept, not zeroed); a
    // driver without one gets the old way (a 0.0 volume).
    const float level = m_muted && !m_player->supportsMute()
        ? 0.0F
        : static_cast<float>(m_volume) / 100.0F;
    m_player->setVolume(level);
    if (m_player->supportsMute())
        m_player->setMute(m_muted);
}

void PlaybackManager::applyBoost()
{
    if (m_player == nullptr || !m_player->supportsBoost())
        return;
    m_player->setBoost(static_cast<float>(m_volumeBoost) / 100.0F);
}

void PlaybackManager::onPlayerSinkVolumeChanged(float linear)
{
    // The server's level moved (an external client changed the app's
    // stream volume): the state follows it. An echo of doob's own
    // push answers the same value — the comparison makes it a no-op,
    // and the level is never pushed back (the server is the source of
    // truth).
    const int level = qBound(0, qRound(linear * 100.0F), 100);
    if (level == m_volume)
        return;
    m_volume = level;
    SettingsManager::getInstance()->setValue("playback.volume", level);
    emit volumeChanged(m_volume);
}

void PlaybackManager::onPlayerSinkMuteChanged(bool muted)
{
    // The server's mute flag moved (an external client muted/unmuted
    // the app's stream): the state follows it, the level is kept.
    if (muted == m_muted)
        return;
    m_muted = muted;
    SettingsManager::getInstance()->setValue("playback.muted", muted);
    emit mutedChanged(m_muted);
}

PlaybackManager::RepeatMode PlaybackManager::repeatModeFromInt(int value)
{
    switch (value) {
    case static_cast<int>(RepeatMode::RepeatPlaylist):
        return RepeatMode::RepeatPlaylist;
    case static_cast<int>(RepeatMode::RepeatTrack):
        return RepeatMode::RepeatTrack;
    default:
        return RepeatMode::NoRepeat;
    }
}

void PlaybackManager::setShuffle(bool shuffle)
{
    if (shuffle == m_shuffle)
        return;
    m_shuffle = shuffle;
    SettingsManager::getInstance()->setValue("playback.shuffle", shuffle);
    emit shuffleChanged(m_shuffle);
}

void PlaybackManager::setRepeat(RepeatMode mode)
{
    if (mode == m_repeat)
        return;
    m_repeat = mode;
    SettingsManager::getInstance()->setValue("playback.repeat",
                                             static_cast<int>(mode));
    emit repeatChanged(m_repeat);
}

void PlaybackManager::toggleShuffle()
{
    setShuffle(!m_shuffle);
}

void PlaybackManager::cycleRepeat()
{
    switch (m_repeat) {
    case RepeatMode::NoRepeat:
        setRepeat(RepeatMode::RepeatPlaylist);
        break;
    case RepeatMode::RepeatPlaylist:
        setRepeat(RepeatMode::RepeatTrack);
        break;
    case RepeatMode::RepeatTrack:
        setRepeat(RepeatMode::NoRepeat);
        break;
    }
}

int PlaybackManager::nextTrackIndex() const
{
    const int count = m_pl->count();
    if (count == 0)
        return -1;

    const int current = m_pl->currentIndex();

    if (m_shuffle) {
        // A random other row (a single-row playlist has no "other", so it
        // stays: the repeat mode governs the auto-advance from there).
        if (count < 2)
            return current < 0 ? 0 : current;
        int target = current;
        while (target == current)
            target = QRandomGenerator::global()->bounded(count);
        return target;
    }

    // Sequential.
    const int next = (current < 0) ? 0 : current + 1;
    if (next < count)
        return next;
    // At the end: wrap when the playlist repeats, otherwise nothing left.
    if (m_repeat == RepeatMode::RepeatPlaylist)
        return 0;
    return -1;
}

bool PlaybackManager::setActivePlayer(const QString &id)
{
    if (!id.isEmpty() && !PlayerRegistry::getInstance()->contains(id)) {
        qWarning("PlaybackManager::setActivePlayer: unknown player id %s",
                 qPrintable(id));
        return false;
    }
    if (id == m_activePlayerId)
        return true;
    m_activePlayerId = id;
    SettingsManager::getInstance()->setValue("playback.player", id);
    emit activePlayerChanged(id);
    return true;
}

void PlaybackManager::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(m_state);
}

void PlaybackManager::setResolving(const MediaItem &item)
{
    if (m_resolvingItem == item)
        return;
    m_resolvingItem = item;
    emit resolvingItemChanged(item);
}

void PlaybackManager::playItem(const MediaItem &item)
{
    ensurePlayer();

    // A new play attempt: a previous failure no longer marks the row.
    m_failedItem = MediaItem();

    // A local file that is gone is unplayable before the player is even
    // asked (a corrupt file or a mid-track failure is caught by the
    // player's error signal instead).
    if (item.kind == MediaItem::Local
            && !QFileInfo::exists(item.source.toLocalFile())) {
        m_failedItem = item;
        emit playbackError(QStringLiteral("Cannot play “%1”: the file is missing")
                               .arg(item.title.isEmpty()
                                         ? item.source.toLocalFile()
                                         : item.title));
        stop();
        return;
    }

    // Provider-aware playback: the item's provider (looked up by the id
    // recorded at import) may switch in its own driver and resolve the
    // stored source to a playable stream URL — asynchronously, because
    // the resolution can take a network round-trip (a download). The
    // display item (m_playingItem) stays the original — the player never
    // reports items back. A switched-in driver stays active for
    // subsequent items until the next switch. The token supersedes an
    // in-flight resolution: a newer playItem (or stop) makes the stale
    // callback a no-op.
    const int token = ++m_resolveToken;

    if (SourceProvider *provider =
            SourceManager::getInstance()->providerById(item.provider)) {
        AudioPlayer* own = provider->player();
        if (own != nullptr) {
            // A provider-owned driver plays only its provider's items:
            // it switches in for them, and a stale one (of another
            // provider) is replaced outright.
            if (m_player != own)
                setPlayer(own);
            m_providerPlayer = own;
        } else if (m_providerPlayer && m_player == m_providerPlayer) {
            // The provider has no driver of its own, and the active one
            // belongs to a previous provider item: restore the
            // configured driver.
            restoreActivePlayer();
        }
        // The resolution is in flight: the views mark the row
        // "downloading" until it lands.
        setResolving(item);
        provider->resolvePlayback(item,
                                  [this, token, item, own](const QUrl &stream) {
                                      startPlayback(token, item, stream,
                                                    own != nullptr);
                                  });
        return;
    }

    // A non-provider item (a local file, a plain stream): a
    // provider-owned driver left over from a previous item cannot play
    // it — restore the configured driver.
    if (m_providerPlayer && m_player == m_providerPlayer)
        restoreActivePlayer();
    startPlayback(token, item, QUrl(), false);
}

void PlaybackManager::startPlayback(int token, const MediaItem &item, const QUrl &stream,
                                    bool providerOwnedPlayer)
{
    if (token != m_resolveToken)
        return;  // superseded

    if (stream.isEmpty() && !item.provider.isEmpty() && !providerOwnedPlayer) {
        // The provider could not resolve the item to a stream, and the
        // active player is not the provider's own: the item is
        // unplayable. Do not hand the stored source (the watch page) to
        // the player — it would fail there and auto-advance past the
        // track.
        qWarning("PlaybackManager::playItem: no stream for provider item %llu "
                 "(provider %s)",
                 static_cast<qulonglong>(item.id), qPrintable(item.provider));
        // The failed item keeps the "cannot play" mark on its row until
        // the next play attempt. Record it before clearing the
        // "downloading" mark, so the views' recompute in between sees
        // the outcome.
        m_failedItem = item;
        setResolving(MediaItem());
        emit playbackError(QStringLiteral("Cannot play “%1”: the provider "
                                          "could not resolve a stream")
                               .arg(item.title.isEmpty()
                                        ? item.source.toString()
                                        : item.title));
        stop();
        return;
    }

    // The resolution landed (or there was none): the "downloading"
    // marker is over.
    setResolving(MediaItem());

    MediaItem playable = item;
    if (!stream.isEmpty())
        playable.source = stream;
    qDebug("PlaybackManager: playing item %llu from %s",
           static_cast<qulonglong>(item.id), qPrintable(playable.source.toString()));
    // The previous track's stream info is stale from the moment a new
    // one starts: clear it, the driver re-reports when the source opens.
    emit streamInfoChanged(QVariantMap());
    // The item is set before the play request, so a driver that refuses
    // synchronously (an errorOccurred inside play()) is seen by
    // onPlayerError with a valid item: it marks the failure, reports it
    // and stops (clearing the item again). Set after the play, the
    // synchronous error would land with no item and be dropped.
    m_gaplessArmed = false;  // the play tears down the driver's queue
    m_gaplessQueued = false;
    m_playingItem = item;
    m_player->play(playable);
    if (!m_playingItem.isValid())
        return;  // refused above: reported and stopped already
    setState(Playing);
    emit playingItemChanged(item);
    // The play counter (write-through, itemChanged) — every track start
    // counts, including repeats of the same item.
    MediaItemStore::getInstance()->registerPlay(item.id);
}

void PlaybackManager::advanceTo(int index)
{
    if (index == m_pl->currentIndex())
        return;
    m_followingPlaylist = true;
    m_pl->setCurrentIndex(index);
    m_followingPlaylist = false;
}

void PlaybackManager::playCurrent()
{
    const MediaItem item = m_pl->currentItem();
    if (!item.isValid()) {
        stop();
        return;
    }
    playItem(item);
}

void PlaybackManager::playFromPlaylist(quint64 playlistId, int index)
{
    if (m_pl->playlistIndexOf(playlistId) < 0)
        return;

    m_followingPlaylist = true;
    m_pl->setCurrentPlaylist(playlistId);
    m_pl->setCurrentIndex(index);
    m_followingPlaylist = false;

    playCurrent();
}

bool PlaybackManager::isPlayingPlaylist(quint64 playlistId) const
{
    return state() == State::Playing
        && playlistId == m_pl->currentPlaylistId();
}

void PlaybackManager::togglePlayPause()
{
    switch (m_state) {
    case Playing:
        pause();
        break;
    case Paused:
        ensurePlayer();
        m_player->resume();
        setState(Playing);
        break;
    case Stopped:
        playCurrent();
        break;
    }
}

void PlaybackManager::pause()
{
    if (m_state != Playing || m_player == nullptr)
        return;
    m_player->pause();
    setState(Paused);
}

void PlaybackManager::stop()
{
    m_resolveToken++;  // supersede any in-flight resolution
    setResolving(MediaItem());  // before the early return: a resolution
                                // in flight while stopped must clear too
    if (m_state == Stopped && !m_playingItem.isValid())
        return;

    if (m_player != nullptr)
        m_player->stop();
    m_playingItem = MediaItem();
    m_gaplessArmed = false;
    m_gaplessQueued = false;
    setState(Stopped);
    emit streamInfoChanged(QVariantMap());
    emit playingItemChanged(MediaItem());
}

void PlaybackManager::next()
{
    if (m_pl->count() == 0) {
        stop();
        return;
    }

    // The shared next-row selection (sequential, shuffled, and the
    // playlist-repeat wrap); -1 means nothing left to play.
    const int target = nextTrackIndex();
    if (target < 0) {
        stop();
        return;
    }

    advanceTo(target);
    playCurrent();
}

void PlaybackManager::previous()
{
    if (m_pl->count() == 0) {
        stop();
        return;
    }

    const int index = m_pl->currentIndex();
    if (index < 0) {
        advanceTo(0);
        playCurrent();
        return;
    }

    const bool earlyInTrack = m_player == nullptr || m_player->positionMs() <= 3000;
    if (earlyInTrack && index > 0) {
        advanceTo(index - 1);
    }
    playCurrent();
}

void PlaybackManager::onPlayerPositionChanged(qint64 positionMs)
{
    emit positionChanged(positionMs);
    maybeArmGapless(positionMs);
}

// The gapless pre-roll (the driver's opt-in capability): a few
// seconds before the end of the current item, the next row is
// advanced and queued on the driver (queueNext) — the display and
// playingItem switch early (standard for gapless players: the track
// label leads the audio by the pre-roll), and the driver plays into
// the queued item without a gap. The boundary's finished (the driver
// fires it when the queued item's frames start playing) is a no-op —
// the advance already happened here. Once per item; RepeatTrack never
// arms (its replay is the current item itself); a driver without the
// capability keeps the plain finished path.
void PlaybackManager::maybeArmGapless(qint64 positionMs)
{
    if (m_state != Playing)
        return;
    if (m_repeat == RepeatMode::RepeatTrack)
        return;
    if (m_player == nullptr || !m_player->supportsGapless())
        return;
    if (!m_playingItem.isValid() || m_gaplessArmed)
        return;
    const int target = nextTrackIndex();
    if (target < 0)
        return;  // nothing left to play: the plain finished path stops
    const MediaItem next = m_pl->itemAt(target);
    if (!next.isValid())
        return;
    // A next item whose provider owns a player needs a driver switch at
    // the boundary (playItem's path) — a handoff through this driver is
    // impossible: keep the plain finished path (the switch happens
    // there, with the usual gap).
    SourceProvider *nextProvider =
        SourceManager::getInstance()->providerById(next.provider);
    if (nextProvider != nullptr && nextProvider->player() != nullptr)
        return;
    // The lead the display advances (and the queued decode starts): a
    // local item's decode is faster than real time (the short lead
    // suffices); a provider-resolved item's stream has to connect and
    // buffer first (the long lead — see the constant).
    const qint64 leadMs =
        nextProvider != nullptr ? kProviderGaplessLeadMs : kGaplessLeadMs;
    const qint64 duration = m_player->durationMs();
    if (duration <= 0 || positionMs < duration - leadMs)
        return;

    m_gaplessArmed = true;
    m_gaplessQueued = false;
    advanceTo(target);
    m_playingItem = next;
    emit playingItemChanged(next);
    queueGaplessItem(next);
}

void PlaybackManager::queueGaplessItem(const MediaItem &item)
{
    if (m_player == nullptr || !item.isValid())
        return;
    // Provider items need a stream URL (the stored source is the
    // provider's — a watch page the driver cannot decode): resolve
    // through the same seam playItem goes through, and queue the
    // resolved copy on the landing. A resolution that lands after a
    // stop / new play / seek is a no-op (the arm's latch is gone);
    // one that lands while paused is a no-op too (the driver's queue
    // is down with the pause; the boundary then plays the armed item
    // plainly). A failed resolution (an empty stream) is a no-op: the
    // boundary plays the armed item and the provider-aware play path
    // reports the failure there.
    if (SourceProvider *provider =
            SourceManager::getInstance()->providerById(item.provider)) {
        provider->resolvePlayback(item, [this, item](const QUrl &stream) {
            if (!m_gaplessArmed || m_state != Playing
                    || stream.isEmpty() || m_player == nullptr)
                return;
            MediaItem playable = item;
            playable.source = stream;
            m_gaplessQueued = m_player->queueNext(playable);
        });
        return;
    }
    m_gaplessQueued = m_player->queueNext(item);
}

void PlaybackManager::onPlayerFinished()
{
    // A finished that lands after a stop is a no-op (a stale signal —
    // the driver's boundary resolving across the stop, a queued
    // emission): the playback is dead, and the plain advance below
    // would start a track from nothing.
    if (m_state == Stopped)
        return;

    // The gapless boundary: the driver fired finished when the queued
    // item's frames started playing — the advance already happened
    // when the handoff was armed, so there is nothing to do (a second
    // advance would skip a track).
    if (m_gaplessArmed) {
        const bool handoff = m_gaplessQueued;
        m_gaplessArmed = false;
        m_gaplessQueued = false;
        if (handoff)
            return;
        // The queue never made it to the driver (a slow provider
        // resolution, a pause that dropped it): the driver ended the
        // old item plainly — play the armed item (the display is
        // already on it; a gap, never a stuck state).
        playCurrent();
        return;
    }

    // Repeat one: the track replays itself (a manual next still moves on).
    if (m_repeat == RepeatMode::RepeatTrack) {
        if (m_pl->currentIndex() < 0) {
            stop();
            return;
        }
        playCurrent();
        return;
    }

    if (m_pl->count() == 0) {
        stop();
        return;
    }

    // The shared next-row selection (shuffled, and the playlist-repeat
    // wrap); -1 means the playlist has played through.
    const int target = nextTrackIndex();
    if (target < 0) {
        stop();
        return;
    }

    advanceTo(target);
    playCurrent();
}

void PlaybackManager::onPlayerError(const QString &message)
{
    // An error after a stop (an asynchronous report of a playback that
    // already ended) has no item to mark.
    if (!m_playingItem.isValid())
        return;
    m_failedItem = m_playingItem;
    emit playbackError(QStringLiteral("Cannot play “%1”: %2")
                           .arg(m_playingItem.title.isEmpty()
                                     ? m_playingItem.source.toString()
                                     : m_playingItem.title,
                                message));
    stop();
}

void PlaybackManager::onPlayerPaused()
{
    setState(Paused);  // a no-op when doob paused itself
}

void PlaybackManager::onPlayerResumed()
{
    setState(Playing);  // a no-op when doob resumed itself
}

void PlaybackManager::onPlayerStopped()
{
    // A stop outside of doob (another client of the backend): the same
    // teardown as doob's own stop().
    stop();
}

void PlaybackManager::onPlaylistCurrentIndexChanged(int index)
{
    if (m_followingPlaylist)
        return;

    if (index < 0) {
        stop();
        return;
    }

    const MediaItem item = m_pl->itemAt(index);
    if (!item.isValid()) {
        stop();
        return;
    }
    if (item == m_playingItem)
        return;

    playItem(item);
}

void PlaybackManager::onPlaylistChanged()
{
    if (m_pl->count() == 0) {
        stop();
        return;
    }
    // The current row no longer holds the playing item: that row was
    // removed and a different row slid into its place. Stop — the playing
    // track is gone, and auto-advance must not skip over the row that
    // replaced it. Other changes (add/insert/reorder) keep currentIndex on
    // the playing item, so they do not stop.
    if (m_state != Stopped && m_pl->currentItem() != m_playingItem)
        stop();
}

void PlaybackManager::onCurrentPlaylistChanged(quint64 id)
{
    if (m_followingPlaylist)
        return;
    stop();
}
