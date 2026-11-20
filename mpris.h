#ifndef MPRIS_H
#define MPRIS_H

#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>
#include <QWidget>

#include <memory>

#include "mediaitem.h"
#include "playbackmanager.h"

class QTemporaryDir;
class MprisPlayer;
class MprisRoot;

// The MPRIS 2.2 implementation (the desktop media interface): doob
// registers org.mpris.MediaPlayer2.doob on the session bus — the spec's
// well-known name must be unique and begin with org.mpris.MediaPlayer2
// (one per application, so a second MPRIS client, e.g. the native
// Spotify app, is not displaced) — and serves one object at the spec's
// path:
//   /org/mpris/MediaPlayer2 — org.mpris.MediaPlayer2 (the identity,
//                             Raise, Quit) and
//                             org.mpris.MediaPlayer2.Player (the controls
//                             and the state)
// (The spec's path is the one both interfaces must sit on — the
// clients query the Player interface there; the old .Player object
// path is not a valid D-Bus object path, the dot in its last element.)
// The Player's properties mirror the PlaybackManager: each one is
// re-emitted as PropertiesChanged from the matching manager signal.
// The spec's invariants are kept: Position is polled, not emitted (no
// PropertiesChanged), the Can* flags report what the methods can do
// (and are true when unknown, per the spec), and SetPosition/Seek are
// bounded (stale track ids, out-of-range positions — a Seek past the
// track's end acts like Next). The TrackList and Playlists interfaces
// are out of scope for v1 (the playlists are browsable in doob's own
// UI; the MPRIS clients that need them are rare for an audio player).
//
// A missing session bus (a headless run) is a quiet no-op: start()
// reports false and the app runs without the interface.
class Mpris : public QObject
{
    Q_OBJECT
public:
    explicit Mpris(QObject *parent = nullptr);
    ~Mpris() override;

    // Registers the service name and the object (both interfaces) on
    // the session bus. The default is the spec-mandated per-app name
    // (a test seam: a test registers under its own name, so it never
    // fights a running doob). False (and a qWarning) when no session
    // bus is available.
    bool start(const QString &serviceName =
                   QStringLiteral("org.mpris.MediaPlayer2.doob"));

    // The window Raise() brings forward (main sets it once the window
    // exists).
    void setWindow(QWidget *window) { m_window = window; }

    // The pure, bus-free mappings (the Player object feeds them; the
    // tests cover them directly):
    // The Metadata dict of an item (an empty dict for the invalid one,
    // i.e. on stop). artUrl is the file:// URL of the item's cached art
    // (absent — no mpris:artUrl key — when it is empty).
    static QVariantMap metadataFor(const MediaItem &item, const QString &artUrl);
    static QString playbackStatusFor(PlaybackManager::State state);
    static QString loopStatusFor(PlaybackManager::RepeatMode mode);
    // The inverse of loopStatusFor (the LoopStatus write): the mode the
    // spec's value names, NoRepeat for anything not a spec value.
    static PlaybackManager::RepeatMode repeatModeFor(const QString &loopStatus);
    // The stable track id of an item (the Metadata's mpris:trackid):
    // a D-Bus object path (the spec's type for the key), the same for
    // the same id, across runs.
    static QString trackId(quint64 id);

    // The Player's state, read live from the PlaybackManager (the
    // adaptor's property accessors): MPRIS units where the spec wants
    // them (Position in microseconds), the effective volume (0 while
    // muted).
    QString playbackStatus() const;
    double volume() const;
    bool shuffle() const;
    QString loopStatus() const;
    qint64 positionUsec() const;
    QVariantMap metadata() const { return m_metadata; }
    // The capability flags (the spec's Can* properties): what the
    // controls can do right now, read live. CanControl is intrinsic
    // (always true — the adaptor's constant, not here).
    bool canPlay() const;
    bool canPause() const;
    bool canSeek() const;
    bool canGoNext() const;
    bool canGoPrevious() const;

    // The LoopStatus/Volume/Shuffle writes (the spec's Read/Write
    // properties): applied to the manager, which re-emits the change
    // (the adaptor's NOTIFY follows the manager's signals).
    void setVolume(double volume);
    void setShuffle(bool shuffle);
    void setLoopStatus(const QString &loopStatus);

    // The file:// URL of the playing item's cached art ("" when it has
    // none): the art is written to a per-process temp file (the cache
    // is in the database, MPRIS wants a URI).
    QString artUrl() const { return m_artUrl; }

    // The service name start() registered ("" when it bailed): the
    // destructor releases it (a re-started Mpris — a test's next case —
    // takes the name back).
    QString serviceName() const { return m_serviceName; }

private:
    friend class MprisPlayer;
    friend class MprisRoot;

    // The art of the new playing item: refreshes m_artUrl and returns
    // the URL ("" for an invalid item or one without cached art).
    QString refreshArt(const MediaItem &item);
    // Raise() brings the main window forward.
    void raiseWindow();
    // The Can* re-emit (the capabilities moved: the playlist rows, the
    // current row, the shuffle/repeat or the playing item changed).
    void refreshCapabilities();
    // The spec's PropertiesChanged on the Player interface: the names'
    // current values as the changed map. This Qt build does not
    // re-emit the adaptor's property NOTIFY signals as it (upstream
    // Qt does, on the adaptor's registration — its dbus module has no
    // such wiring), and the MPRIS clients watch for this signal alone:
    // without it the tray's state stays stale (the methods and the
    // property reads work, nothing tells the clients a value moved).
    // On a Qt that auto-emits, the clients see the change twice —
    // the same values, an idempotent apply.
    void emitPropertiesChanged(const QStringList &names);
    // A Player property's current value (the emit's changed map).
    QVariant propertyValue(const QString &name) const;

    MprisPlayer *m_player;
    MprisRoot *m_root;
    QPointer<QWidget> m_window;
    QVariantMap m_metadata;
    QString m_artUrl;
    QString m_serviceName;
    std::unique_ptr<QTemporaryDir> m_artDir;

private slots:
    void onStateChanged(PlaybackManager::State state);
    void onPlayingItemChanged(const MediaItem &item);
    void onShuffleChanged(bool shuffle);
    void onRepeatChanged(PlaybackManager::RepeatMode mode);
    void onVolumeChanged(int volume);
    void onMutedChanged(bool muted);
    void onPlaylistChanged(quint64 playlistId);
    void onCurrentPlaylistChanged(quint64 playlistId);
    void onCurrentIndexChanged(int index);
};

// org.mpris.MediaPlayer2.Player — the player's state and controls. The
// property and method names are the MPRIS spec's (case included); the
// properties read the PlaybackManager live and are re-emitted as
// PropertiesChanged from its signals (Mpris wires the connections).
// The spec's Position invariant: no NOTIFY (clients poll it; emitting
// it ~4x/s would flood the bus), the Seeked signal marks the jumps.
class MprisPlayer : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus NOTIFY playbackStatusChanged)
    Q_PROPERTY(QVariantMap Metadata READ metadata NOTIFY metadataChanged)
    Q_PROPERTY(double Rate READ rate CONSTANT)
    Q_PROPERTY(double MinimumRate READ minimumRate CONSTANT)
    Q_PROPERTY(double MaximumRate READ maximumRate CONSTANT)
    Q_PROPERTY(double Volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus NOTIFY loopStatusChanged)
    Q_PROPERTY(qint64 Position READ position)
    Q_PROPERTY(bool CanGoNext READ canGoNext NOTIFY canGoNextChanged)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious NOTIFY canGoPreviousChanged)
    Q_PROPERTY(bool CanPlay READ canPlay NOTIFY canPlayChanged)
    Q_PROPERTY(bool CanPause READ canPause NOTIFY canPauseChanged)
    Q_PROPERTY(bool CanSeek READ canSeek NOTIFY canSeekChanged)
    Q_PROPERTY(bool CanControl READ canControl CONSTANT)
public:
    explicit MprisPlayer(Mpris *parent);

    // The controls. Seek is a relative offset in microseconds (the
    // MPRIS spec's; a seek back past 0 lands at 0, a seek past the
    // track's end acts like Next); SetPosition is absolute, bounded
    // (below 0 or above the track length is a no-op) and applies only
    // when its track id is the current track's (a stale client's seek
    // is a no-op, per the spec). The track id is a D-Bus object path
    // (the spec's mpris:trackid type).
    Q_INVOKABLE void Play();
    Q_INVOKABLE void Pause();
    Q_INVOKABLE void PlayPause();
    Q_INVOKABLE void Stop();
    Q_INVOKABLE void Next();
    Q_INVOKABLE void Previous();
    Q_INVOKABLE void Seek(qint64 offsetUsec);
    Q_INVOKABLE void SetPosition(const QDBusObjectPath &trackId,
                                 qint64 positionUsec);

    // The property accessors (the parent Mpris holds the state). Rate
    // is fixed at 1.0 (doob plays at one speed; the spec requires the
    // property even then, with the min and max set to 1.0).
    QString playbackStatus() const;
    QVariantMap metadata() const;
    double rate() const { return 1.0; }
    double minimumRate() const { return 1.0; }
    double maximumRate() const { return 1.0; }
    double volume() const;
    void setVolume(double volume);
    bool shuffle() const;
    void setShuffle(bool shuffle);
    QString loopStatus() const;
    void setLoopStatus(const QString &loopStatus);
    qint64 position() const;
    bool canGoNext() const;
    bool canGoPrevious() const;
    bool canPlay() const;
    bool canPause() const;
    bool canSeek() const;
    bool canControl() const { return true; }

private:
    // The parent (the adaptor's state lives there).
    Mpris* mpris() const;
    // The current track's length in microseconds (0 when unknown).
    static qint64 trackLengthUsec();

signals:
    void playbackStatusChanged();
    void metadataChanged();
    void volumeChanged();
    void shuffleChanged();
    void loopStatusChanged();
    void canGoNextChanged();
    void canGoPreviousChanged();
    void canPlayChanged();
    void canPauseChanged();
    void canSeekChanged();
    // A position jump (the spec's Seeked): the new position in
    // microseconds, emitted after a Seek/SetPosition that applied.
    void seeked(qint64 newPositionUsec);
};

// org.mpris.MediaPlayer2 — the application's identity and controls:
// Raise (bring the window forward) and Quit (the whole app —
// CanQuit/CanRaise advertise them). The identity is constant: doob is
// doob, and the spec's DesktopEntry is the .desktop basename (the file
// ships later, the name is already fixed).
class MprisRoot : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanRaise READ canRaise CONSTANT)
    Q_PROPERTY(bool CanQuit READ canQuit CONSTANT)
    Q_PROPERTY(bool HasTrackList READ hasTrackList CONSTANT)
    Q_PROPERTY(QString Identity READ identity CONSTANT)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry CONSTANT)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes CONSTANT)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes CONSTANT)
    Q_PROPERTY(bool Fullscreen READ fullscreen CONSTANT)
    Q_PROPERTY(bool CanSetFullscreen READ canSetFullscreen CONSTANT)
public:
    explicit MprisRoot(Mpris *parent);

    Q_INVOKABLE void Raise();
    Q_INVOKABLE void Quit();

    bool canRaise() const { return true; }
    bool canQuit() const { return true; }
    bool hasTrackList() const { return false; }
    QString identity() const { return QStringLiteral("doob"); }
    QString desktopEntry() const { return QStringLiteral("doob"); }
    QStringList supportedUriSchemes() const { return {}; }
    QStringList supportedMimeTypes() const { return {}; }
    bool fullscreen() const { return false; }
    bool canSetFullscreen() const { return false; }
};

#endif // MPRIS_H
