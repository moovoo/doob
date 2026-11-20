#include "mpris.h"

#include "mediaitemstore.h"
#include "playlistmanager.h"
#include "trayicon.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QFile>
#include <QTemporaryDir>
#include <QUrl>
#include <QWidget>

Mpris::Mpris(QObject *parent)
    : QObject(parent)
    , m_player(new MprisPlayer(this))
    , m_root(new MprisRoot(this))
{
    // The Player's properties mirror the manager: each re-emitted as
    // PropertiesChanged from the matching signal. Position is the
    // exception (the spec's invariant): it is polled, never emitted.
    PlaybackManager *pm = PlaybackManager::getInstance();
    connect(pm, &PlaybackManager::stateChanged, this, &Mpris::onStateChanged);
    connect(pm, &PlaybackManager::playingItemChanged, this, &Mpris::onPlayingItemChanged);
    connect(pm, &PlaybackManager::shuffleChanged, this, &Mpris::onShuffleChanged);
    connect(pm, &PlaybackManager::repeatChanged, this, &Mpris::onRepeatChanged);
    connect(pm, &PlaybackManager::volumeChanged, this, &Mpris::onVolumeChanged);
    connect(pm, &PlaybackManager::mutedChanged, this, &Mpris::onMutedChanged);
    // The capabilities (the Can* properties) follow the playlist's
    // shape and the current row.
    PlaylistManager *pl = PlaylistManager::getInstance();
    connect(pl, &PlaylistManager::playlistChanged, this, &Mpris::onPlaylistChanged);
    connect(pl, &PlaylistManager::currentPlaylistChanged, this, &Mpris::onCurrentPlaylistChanged);
    connect(pl, &PlaylistManager::currentIndexChanged, this, &Mpris::onCurrentIndexChanged);
}

Mpris::~Mpris()
{
    // The quit's teardown: release the name (the objects unregister
    // with their destruction).
    if (!m_serviceName.isEmpty())
        QDBusConnection::sessionBus().unregisterService(m_serviceName);
}

bool Mpris::start(const QString &serviceName)
{
    // The session bus is a process-level shared connection: the
    // registration lives on it, no member to keep it.
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qWarning("mpris: no session bus — the MPRIS is off");
        return false;
    }
    // The spec's single object: both interfaces on
    // /org/mpris/MediaPlayer2 (the adaptors are its children, so each
    // exports its Q_CLASSINFO interface on that path). The default
    // flags export adaptors only — the invokable controls need the
    // invokable flags on top.
    bus.registerObject(QStringLiteral("/org/mpris/MediaPlayer2"), this,
                       QDBusConnection::ExportAdaptors
                       | QDBusConnection::ExportAllInvokables);
    if (!bus.registerService(serviceName)) {
        qWarning("mpris: %s is not available — the MPRIS is off",
                 qPrintable(serviceName));
        return false;
    }
    m_serviceName = serviceName;
    return true;
}

QVariantMap Mpris::metadataFor(const MediaItem &item, const QString &artUrl)
{
    QVariantMap meta;
    if (!item.isValid())
        return meta; // a stop (the invalid item): no track
    meta.insert(QStringLiteral("mpris:trackid"),
                QDBusObjectPath(trackId(item.id)));
    meta.insert(QStringLiteral("x:doob:id"), static_cast<qulonglong>(item.id));
    // The title, artist and album in both namespaces: the spec defines
    // the dc: (Dublin Core) and xesam: keys, and the clients read one
    // or the other — KDE's tray reads xesam: (Spotify sends only that),
    // so without it the title and artist are missing there.
    meta.insert(QStringLiteral("dc:title"), item.title);
    meta.insert(QStringLiteral("xesam:title"), item.title);
    if (!item.artist.isEmpty())
    {
        // MPRIS's creator list: the artist is the single creator.
        meta.insert(QStringLiteral("dc:creator"), QStringList{ item.artist });
        meta.insert(QStringLiteral("xesam:artist"), QStringList{ item.artist });
    }
    if (!item.album.isEmpty())
    {
        meta.insert(QStringLiteral("dc:album"), item.album);
        meta.insert(QStringLiteral("xesam:album"), item.album);
    }
    if (item.durationMs > 0)
        meta.insert(QStringLiteral("mpris:length"),
                    static_cast<qint64>(item.durationMs * 1000));
    if (!artUrl.isEmpty())
        meta.insert(QStringLiteral("mpris:artUrl"), artUrl);
    if (!item.provider.isEmpty())
        meta.insert(QStringLiteral("x:doob:provider"), item.provider);
    return meta;
}

QString Mpris::playbackStatusFor(PlaybackManager::State state)
{
    switch (state) {
    case PlaybackManager::Playing:
        return QStringLiteral("Playing");
    case PlaybackManager::Paused:
        return QStringLiteral("Paused");
    case PlaybackManager::Stopped:
        break;
    }
    return QStringLiteral("Stopped");
}

QString Mpris::loopStatusFor(PlaybackManager::RepeatMode mode)
{
    switch (mode) {
    case PlaybackManager::RepeatMode::RepeatTrack:
        return QStringLiteral("Track");
    case PlaybackManager::RepeatMode::RepeatPlaylist:
        return QStringLiteral("Playlist");
    case PlaybackManager::RepeatMode::NoRepeat:
        break;
    }
    return QStringLiteral("None");
}

PlaybackManager::RepeatMode Mpris::repeatModeFor(const QString &loopStatus)
{
    if (loopStatus == QStringLiteral("Track"))
        return PlaybackManager::RepeatMode::RepeatTrack;
    if (loopStatus == QStringLiteral("Playlist"))
        return PlaybackManager::RepeatMode::RepeatPlaylist;
    return PlaybackManager::RepeatMode::NoRepeat;
}

QString Mpris::trackId(quint64 id)
{
    // A D-Bus object path (the spec's type for mpris:trackid): the
    // clients may not expect an object behind it, it is an id.
    return QStringLiteral("/doob/track/%1").arg(id);
}

QString Mpris::playbackStatus() const
{
    return playbackStatusFor(PlaybackManager::getInstance()->state());
}

double Mpris::volume() const
{
    // The effective level: a mute is a 0, the level above it the scale.
    const PlaybackManager *pm = PlaybackManager::getInstance();
    if (pm->muted())
        return 0.0;
    return pm->volume() / 100.0;
}

bool Mpris::shuffle() const
{
    return PlaybackManager::getInstance()->shuffle();
}

QString Mpris::loopStatus() const
{
    return loopStatusFor(PlaybackManager::getInstance()->repeat());
}

qint64 Mpris::positionUsec() const
{
    // MPRIS counts microseconds; the manager milliseconds.
    return PlaybackManager::getInstance()->positionMs() * 1000;
}

bool Mpris::canPlay() const
{
    // There is a track to start (the current playlist is non-empty):
    // the spec's CanPlay is about the track's existence, not the state.
    return PlaylistManager::getInstance()->count() > 0;
}

bool Mpris::canPause() const
{
    // A track is loaded (pausable media — an intrinsic property of the
    // current track, not of its paused/playing state).
    return PlaybackManager::getInstance()->playingItem().isValid();
}

bool Mpris::canSeek() const
{
    // A track is loaded (the current drivers all seek; a track without
    // a position is the only unseekable case doob has).
    return PlaybackManager::getInstance()->playingItem().isValid();
}

bool Mpris::canGoNext() const
{
    // The manager's advance knows the shuffle and the repeat: -1 is
    // "no next track" (the spec's stop case).
    return PlaybackManager::getInstance()->nextTrackIndex() >= 0;
}

bool Mpris::canGoPrevious() const
{
    // A row exists to land on (previous wraps, so any non-empty list
    // has a previous).
    return PlaylistManager::getInstance()->count() > 0;
}

void Mpris::setVolume(double volume)
{
    // The spec's write: negative is 0.0, the scale is 0.0..1.0 (the
    // manager clamps to its max on top).
    const int percent = qBound(0, qRound(volume * 100.0), 100);
    PlaybackManager::getInstance()->setVolume(percent);
}

void Mpris::setShuffle(bool shuffle)
{
    PlaybackManager::getInstance()->setShuffle(shuffle);
}

void Mpris::setLoopStatus(const QString &loopStatus)
{
    // An out-of-spec value is a no-op (the property keeps its state).
    PlaybackManager::getInstance()->setRepeat(repeatModeFor(loopStatus));
}

QString Mpris::refreshArt(const MediaItem &item)
{
    m_artUrl.clear();
    if (!item.isValid())
        return {};
    QString mime;
    QByteArray data;
    if (!MediaItemStore::getInstance()->artwork(item.id, &mime, &data)
            || data.isEmpty())
        return {};
    if (!m_artDir || !m_artDir->isValid())
        m_artDir = std::make_unique<QTemporaryDir>();
    const QString path = m_artDir->filePath(
        QStringLiteral("art-%1").arg(item.id));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
        qWarning("mpris: the art of item %ull could not be written",
                 static_cast<unsigned long long>(item.id));
        return {};
    }
    m_artUrl = QUrl::fromLocalFile(path).toString();
    return m_artUrl;
}

void Mpris::raiseWindow()
{
    if (m_window == nullptr)
        return;
    m_window->show();
    m_window->raise();
    m_window->activateWindow();
}

void Mpris::refreshCapabilities()
{
    // The capabilities are one family (the playlist's shape, the
    // current row, the shuffle/repeat, the playing item): re-emit them
    // together when any of their sources moved.
    m_player->canPlayChanged();
    m_player->canPauseChanged();
    m_player->canSeekChanged();
    m_player->canGoNextChanged();
    m_player->canGoPreviousChanged();
    emitPropertiesChanged({ QStringLiteral("CanPlay"), QStringLiteral("CanPause"),
                            QStringLiteral("CanSeek"), QStringLiteral("CanGoNext"),
                            QStringLiteral("CanGoPrevious"),
                          });
}

void Mpris::emitPropertiesChanged(const QStringList &names)
{
    if (m_serviceName.isEmpty())
        return;  // no session bus (start() bailed)
    // The spec's signal: (the interface, the changed properties, the
    // unchanged ones — none). The values are read back live, so a
    // change that was superseded in the meantime sends the current
    // one (the clients apply by value, the order is their problem).
    QDBusMessage message = QDBusMessage::createSignal(
        QStringLiteral("/org/mpris/MediaPlayer2"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"));
    QVariantMap changed;
    for (const QString &name : names)
        changed.insert(name, propertyValue(name));
    message << QStringLiteral("org.mpris.MediaPlayer2.Player")
            << changed
            << QStringList{};
    QDBusConnection::sessionBus().send(message);
}

QVariant Mpris::propertyValue(const QString &name) const
{
    if (name == QLatin1String("PlaybackStatus"))
        return playbackStatus();
    if (name == QLatin1String("Metadata"))
        return metadata();
    if (name == QLatin1String("Volume"))
        return volume();
    if (name == QLatin1String("Shuffle"))
        return shuffle();
    if (name == QLatin1String("LoopStatus"))
        return loopStatus();
    if (name == QLatin1String("CanPlay"))
        return canPlay();
    if (name == QLatin1String("CanPause"))
        return canPause();
    if (name == QLatin1String("CanSeek"))
        return canSeek();
    if (name == QLatin1String("CanGoNext"))
        return canGoNext();
    if (name == QLatin1String("CanGoPrevious"))
        return canGoPrevious();
    return {};
}

void Mpris::onStateChanged(PlaybackManager::State state)
{
    Q_UNUSED(state)  // the property reads the state back from the manager
    m_player->playbackStatusChanged();
    emitPropertiesChanged({ QStringLiteral("PlaybackStatus") });
}

void Mpris::onPlayingItemChanged(const MediaItem &item)
{
    m_metadata = metadataFor(item, refreshArt(item));
    m_player->metadataChanged();
    emitPropertiesChanged({ QStringLiteral("Metadata") });
    refreshCapabilities();
}

void Mpris::onShuffleChanged(bool shuffle)
{
    Q_UNUSED(shuffle)  // the property reads the state back from the manager
    m_player->shuffleChanged();
    emitPropertiesChanged({ QStringLiteral("Shuffle") });
    refreshCapabilities();
}

void Mpris::onRepeatChanged(PlaybackManager::RepeatMode mode)
{
    Q_UNUSED(mode)  // the property reads the state back from the manager
    m_player->loopStatusChanged();
    emitPropertiesChanged({ QStringLiteral("LoopStatus") });
    refreshCapabilities();
}

void Mpris::onVolumeChanged(int volume)
{
    Q_UNUSED(volume)  // the property reads the level back from the manager
    m_player->volumeChanged();
    emitPropertiesChanged({ QStringLiteral("Volume") });
}

void Mpris::onMutedChanged(bool muted)
{
    Q_UNUSED(muted)  // the property reads the level back from the manager
    m_player->volumeChanged();
    emitPropertiesChanged({ QStringLiteral("Volume") });
}

void Mpris::onPlaylistChanged(quint64 playlistId)
{
    if (playlistId != PlaylistManager::getInstance()->currentPlaylistId())
        return; // a non-current playlist's rows do not move the capabilities
    refreshCapabilities();
}

void Mpris::onCurrentPlaylistChanged(quint64 playlistId)
{
    Q_UNUSED(playlistId)  // the capabilities read the current playlist
    refreshCapabilities();
}

void Mpris::onCurrentIndexChanged(int index)
{
    Q_UNUSED(index)  // the capabilities read the current row
    refreshCapabilities();
}

// --- MprisPlayer -------------------------------------------------------

MprisPlayer::MprisPlayer(Mpris *parent)
    : QDBusAbstractAdaptor(parent)
{
}

Mpris *MprisPlayer::mpris() const
{
    return static_cast<Mpris *>(parent());
}

qint64 MprisPlayer::trackLengthUsec()
{
    const MediaItem item = PlaybackManager::getInstance()->playingItem();
    return item.durationMs > 0 ? static_cast<qint64>(item.durationMs * 1000) : 0;
}

void MprisPlayer::Play()
{
    PlaybackManager *pm = PlaybackManager::getInstance();
    if (pm->state() == PlaybackManager::Playing)
        return;
    if (pm->state() == PlaybackManager::Paused)
        pm->togglePlayPause(); // a resume
    else
        pm->playCurrent();
}

void MprisPlayer::Pause()
{
    PlaybackManager::getInstance()->pause();
}

void MprisPlayer::PlayPause()
{
    PlaybackManager *pm = PlaybackManager::getInstance();
    if (pm->state() == PlaybackManager::Playing)
        pm->pause();
    else if (pm->state() == PlaybackManager::Paused)
        pm->togglePlayPause(); // a resume
    else
        pm->playCurrent();
}

void MprisPlayer::Stop()
{
    PlaybackManager::getInstance()->stop();
}

void MprisPlayer::Next()
{
    PlaybackManager::getInstance()->next();
}

void MprisPlayer::Previous()
{
    PlaybackManager::getInstance()->previous();
}

void MprisPlayer::Seek(qint64 offsetUsec)
{
    // A relative offset (the MPRIS spec's): from the current position.
    // Back past 0 lands at 0, past the track's end acts like Next.
    PlaybackManager *pm = PlaybackManager::getInstance();
    const qint64 target = (pm->positionMs() * 1000) + offsetUsec;
    if (target < 0) {
        pm->setPositionMs(0);
        emit seeked(0);
        return;
    }
    const qint64 length = trackLengthUsec();
    if (length > 0 && target > length) {
        pm->next();
        return; // a track change: the position is 0, no Seeked (the spec's expected start)
    }
    pm->setPositionMs(target / 1000);
    emit seeked(target);
}

void MprisPlayer::SetPosition(const QDBusObjectPath &trackId,
                              qint64 positionUsec)
{
    // Absolute, and only for the track it names (a stale client's seek
    // is a no-op, per the spec) and within the track (below 0 or above
    // its length is a no-op, per the spec).
    PlaybackManager *pm = PlaybackManager::getInstance();
    if (trackId.path() != Mpris::trackId(pm->playingItem().id))
        return;
    if (positionUsec < 0)
        return;
    const qint64 length = trackLengthUsec();
    if (length > 0 && positionUsec > length)
        return;
    pm->setPositionMs(positionUsec / 1000);
    emit seeked(positionUsec);
}

QString MprisPlayer::playbackStatus() const
{
    return mpris()->playbackStatus();
}

QVariantMap MprisPlayer::metadata() const
{
    return mpris()->metadata();
}

double MprisPlayer::volume() const
{
    return mpris()->volume();
}

void MprisPlayer::setVolume(double volume)
{
    mpris()->setVolume(volume);
}

bool MprisPlayer::shuffle() const
{
    return mpris()->shuffle();
}

void MprisPlayer::setShuffle(bool shuffle)
{
    mpris()->setShuffle(shuffle);
}

QString MprisPlayer::loopStatus() const
{
    return mpris()->loopStatus();
}

void MprisPlayer::setLoopStatus(const QString &loopStatus)
{
    mpris()->setLoopStatus(loopStatus);
}

qint64 MprisPlayer::position() const
{
    return mpris()->positionUsec();
}

bool MprisPlayer::canGoNext() const
{
    return mpris()->canGoNext();
}

bool MprisPlayer::canGoPrevious() const
{
    return mpris()->canGoPrevious();
}

bool MprisPlayer::canPlay() const
{
    return mpris()->canPlay();
}

bool MprisPlayer::canPause() const
{
    return mpris()->canPause();
}

bool MprisPlayer::canSeek() const
{
    return mpris()->canSeek();
}

// --- MprisRoot ----------------------------------------------------------

MprisRoot::MprisRoot(Mpris *parent)
    : QDBusAbstractAdaptor(parent)
{
}

void MprisRoot::Raise()
{
    static_cast<Mpris *>(parent())->raiseWindow();
}

void MprisRoot::Quit()
{
    // A real quit (like the tray's Quit): the main window's
    // close-to-tray must not turn the teardown's close into a hide.
    TrayIcon::getInstance()->prepareQuit();
    QCoreApplication::quit();
}
