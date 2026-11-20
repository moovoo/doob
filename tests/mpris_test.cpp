#include <QtTest>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <memory>

#include "fakeplayer.h"
#include "librarymanager.h"
#include "mediaitemstore.h"
#include "mpris.h"
#include "playbackmanager.h"
#include "playlistmanager.h"
#include "settingsmanager.h"
#include "testsupport.h"

namespace {

// The second bus connection's name (the MPRIS registration lives on
// the default one, and a connection never receives its own signals —
// libdbus broadcasts them to the other connections only).
constexpr const char* secondBusName = "org.doob.mpris.test.second";

class MprisTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void playbackStatusMapping();
    void loopStatusMapping();
    void trackIdIsStableForTheItem();
    void metadataCarriesTheItemFields();
    void metadataDropsTheAbsentParts();
    void objectExposesThePlayerAndRootInterfaces();
    void thePlayerTracksTheManagerState();
    void theControlsAreBounded();
    void theChangesReachTheBusAsPropertiesChanged();

    // The spec's PropertiesChanged as the clients receive it: the
    // string-based connect of the second bus connection delivers the
    // signal's arguments (the interface, the changed map, the
    // unchanged list) to this slot — its parameter types must match
    // the message's exactly (the delivery drops a non-matching one).
    void onPropertiesChanged(const QString &interface,
                             const QVariantMap &changed,
                             const QStringList &unchanged);

private:
    MediaItem makeItem(quint64 id, const QString &title);
    // The test-only service name (a running doob keeps the well-known
    // one). The pid element is prefixed with a letter: D-Bus name
    // elements must not start with a digit (Qt's isValidBusName
    // rejects them).
    QString testName() const;

    FakePlayer m_fake{this};
    LibraryManager* m_lib = nullptr;
    PlaylistManager* m_pl = nullptr;
    PlaybackManager* m_pm = nullptr;
    MediaItemStore* m_store = nullptr;
    std::unique_ptr<QTemporaryDir> m_dir;
    Mpris* m_mpris = nullptr;
    // The changed maps the onPropertiesChanged slot has recorded (the
    // second connection's view of the bus).
    QList<QVariantMap> m_changes;
};

void MprisTest::init()
{
    m_lib = LibraryManager::getInstance();
    m_pl = PlaylistManager::getInstance();
    m_pm = PlaybackManager::getInstance();
    m_store = MediaItemStore::getInstance();
    m_dir = std::make_unique<QTemporaryDir>();
    test::usePrivateDatabase(m_dir->path());
    m_lib->clear();
    m_pl->reset();
    m_store->clear();
    m_changes.clear();

    // The playback settings (the manager registers them in its ctor):
    // keep that store private too.
    SettingsManager* settings = SettingsManager::getInstance();
    settings->setSettingsPath(m_dir->path() + "/settings.json");
    settings->load();
    m_pm->loadSettings();

    test::installFakePlayer(m_pm, &m_fake);
}

void MprisTest::cleanup()
{
    delete m_mpris;
    m_mpris = nullptr;
    // The second bus connection (the PropertiesChanged listener): the
    // test's local handle is gone, close the bus side too (a no-op
    // when no test opened it).
    QDBusConnection::disconnectFromBus(secondBusName);
    m_pm->stop();
    m_lib->clear();
    m_pl->reset();
    m_store->clear();
    SettingsManager::reset();
}

MediaItem MprisTest::makeItem(quint64 id, const QString &title)
{
    MediaItem item;
    item.id = id;
    item.title = title;
    // A real (dummy) file: the playback is checked against the disk.
    const QString path = m_dir->filePath(QStringLiteral("%1.mp3").arg(id));
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write("\x00\x01\x02");
    item.source = QUrl::fromLocalFile(path);
    return item;
}

QString MprisTest::testName() const
{
    return QStringLiteral("org.doob.mpris.test.p")
        + QString::number(QCoreApplication::applicationPid());
}

// The spec's Properties.Set: the value crosses the bus as a D-Bus
// variant (the in-process dispatch matches the exact 'ssv' signature).
// Qt encodes a plain argument as its own type, so the variant is the
// explicit QDBusVariant wrapper.
QDBusMessage setPropertyCall(const QString &service, const QString &interface,
                             const QString &property, const QVariant &value)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        service, QStringLiteral("/org/mpris/MediaPlayer2"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("Set"));
    // The value as a variant of the QDBusVariant type: the message's
    // arguments are QVariants encoded by their type, and a QDBusVariant
    // encodes as the 'v' the Properties.Set dispatch matches (a plain
    // value would cross as its own type, 'ssd').
    msg << interface << property
        << QVariant::fromValue(QDBusVariant(value));
    return msg;
}

// A changed-map value as a map: the bus signal is a proper nested
// a{sv}, but this Qt decodes a nested a{sv} inside the D-Bus variant
// as a QDBusArgument, not a QVariantMap (a simple value such as the
// PlaybackStatus string decodes to its own type). The clients
// (KDE's tray) decode the wire form as a dict; the test reads the
// argument's entries (string key, variant value) back into a map.
QVariantMap asMap(const QVariant &value)
{
    if (value.metaType().id() == QMetaType::QVariantMap)
        return value.toMap();
    if (value.metaType() != QMetaType::fromType<QDBusArgument>())
        return {};
    const QDBusArgument &arg = value.value<QDBusArgument>();
    QVariantMap map;
    arg.beginMap();
    while (!arg.atEnd()) {
        arg.beginMapEntry();
        QString key;
        QDBusVariant entry;  // the a{sv} value is a variant
        arg >> key >> entry;
        arg.endMapEntry();
        map.insert(key, entry.variant());
    }
    arg.endMap();
    return map;
}

void MprisTest::playbackStatusMapping()
{
    QCOMPARE(Mpris::playbackStatusFor(PlaybackManager::Stopped),
             QString("Stopped"));
    QCOMPARE(Mpris::playbackStatusFor(PlaybackManager::Playing),
             QString("Playing"));
    QCOMPARE(Mpris::playbackStatusFor(PlaybackManager::Paused),
             QString("Paused"));
}

void MprisTest::loopStatusMapping()
{
    // The spec's capitalized values (the clients compare them
    // case-sensitively).
    QCOMPARE(Mpris::loopStatusFor(PlaybackManager::RepeatMode::NoRepeat),
             QString("None"));
    QCOMPARE(Mpris::loopStatusFor(PlaybackManager::RepeatMode::RepeatTrack),
             QString("Track"));
    QCOMPARE(Mpris::loopStatusFor(PlaybackManager::RepeatMode::RepeatPlaylist),
             QString("Playlist"));
    // The write's inverse (the LoopStatus setter): each spec value
    // round-trips, anything else is the default.
    QCOMPARE(Mpris::repeatModeFor(QStringLiteral("None")),
             PlaybackManager::RepeatMode::NoRepeat);
    QCOMPARE(Mpris::repeatModeFor(QStringLiteral("Track")),
             PlaybackManager::RepeatMode::RepeatTrack);
    QCOMPARE(Mpris::repeatModeFor(QStringLiteral("Playlist")),
             PlaybackManager::RepeatMode::RepeatPlaylist);
    QCOMPARE(Mpris::repeatModeFor(QStringLiteral("bogus")),
             PlaybackManager::RepeatMode::NoRepeat);
}

void MprisTest::trackIdIsStableForTheItem()
{
    QCOMPARE(Mpris::trackId(41), Mpris::trackId(41));
    QVERIFY(Mpris::trackId(41) != Mpris::trackId(42));
    // A D-Bus object path (the spec's type for mpris:trackid) — the
    // same item, the same id, across runs. The spec forbids the
    // /org/mpris prefix for track ids, the /doob one is free.
    QCOMPARE(Mpris::trackId(41), QStringLiteral("/doob/track/41"));
}

void MprisTest::metadataCarriesTheItemFields()
{
    MediaItem item = makeItem(7, "Title");
    item.artist = "The Artist";
    item.album = "The Album";
    item.durationMs = 200000;
    item.provider = "fake";

    const QVariantMap meta =
        Mpris::metadataFor(item, QStringLiteral("file:///art/7.png"));
    QCOMPARE(meta.value("mpris:trackid").value<QDBusObjectPath>(),
             QDBusObjectPath(Mpris::trackId(7)));
    QCOMPARE(meta.value("x:doob:id").toULongLong(), 7ULL);
    QCOMPARE(meta.value("dc:title").toString(), QString("Title"));
    QCOMPARE(meta.value("dc:creator").toStringList(), QStringList{ "The Artist" });
    QCOMPARE(meta.value("dc:album").toString(), QString("The Album"));
    // The same fields in the xesam: namespace (KDE's tray reads this
    // one — the dc: keys alone leave the title and artist missing).
    QCOMPARE(meta.value("xesam:title").toString(), QString("Title"));
    QCOMPARE(meta.value("xesam:artist").toStringList(), QStringList{ "The Artist" });
    QCOMPARE(meta.value("xesam:album").toString(), QString("The Album"));
    // MPRIS counts microseconds.
    QCOMPARE(meta.value("mpris:length").toLongLong(), 200000000LL);
    QCOMPARE(meta.value("mpris:artUrl").toString(),
             QString("file:///art/7.png"));
    QCOMPARE(meta.value("x:doob:provider").toString(), QString("fake"));
}

void MprisTest::metadataDropsTheAbsentParts()
{
    const MediaItem item = makeItem(9, "Bare");
    const QVariantMap meta = Mpris::metadataFor(item, QString());
    // The identity and the title ride along; the rest is absent.
    QCOMPARE(meta.value("mpris:trackid").value<QDBusObjectPath>(),
             QDBusObjectPath(Mpris::trackId(9)));
    QCOMPARE(meta.value("x:doob:id").toULongLong(), 9ULL);
    QCOMPARE(meta.value("dc:title").toString(), QString("Bare"));
    QCOMPARE(meta.value("xesam:title").toString(), QString("Bare"));
    QVERIFY(!meta.contains("dc:creator"));
    QVERIFY(!meta.contains("dc:album"));
    QVERIFY(!meta.contains("xesam:artist"));
    QVERIFY(!meta.contains("xesam:album"));
    QVERIFY(!meta.contains("mpris:length"));
    QVERIFY(!meta.contains("mpris:artUrl"));
    QVERIFY(!meta.contains("x:doob:provider"));

    // A stop (the invalid item): no track at all.
    QVERIFY(Mpris::metadataFor(MediaItem(), QString()).isEmpty());
}

// Registers on the real session bus (a test-only name, so a running doob
// is not displaced): the object, its two interfaces, the root's
// identity and the player's initial properties. QSKIPs when no session
// bus is available (CI headless).
void MprisTest::objectExposesThePlayerAndRootInterfaces()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        QSKIP("no session bus");

    const QString service = testName();
    m_mpris = new Mpris(this);
    QVERIFY(m_mpris->start(service));
    QCOMPARE(m_mpris->serviceName(), service);

    // The spec's single object: both interfaces on
    // /org/mpris/MediaPlayer2.
    QDBusInterface player(service, QStringLiteral("/org/mpris/MediaPlayer2"),
                          QStringLiteral("org.mpris.MediaPlayer2.Player"), bus);
    QVERIFY(player.isValid());
    QDBusInterface root(service, QStringLiteral("/org/mpris/MediaPlayer2"),
                        QStringLiteral("org.mpris.MediaPlayer2"), bus);
    QVERIFY(root.isValid());

    // The root's identity (the spec's properties): doob is doob, no
    // track list, no uri schemes or mime types advertised (OpenUri is
    // not implemented), no fullscreen (an audio player).
    QCOMPARE(root.property("Identity").toString(), QString("doob"));
    QCOMPARE(root.property("DesktopEntry").toString(), QString("doob"));
    QCOMPARE(root.property("CanQuit").toBool(), true);
    QCOMPARE(root.property("CanRaise").toBool(), true);
    QCOMPARE(root.property("HasTrackList").toBool(), false);
    QVERIFY(root.property("SupportedUriSchemes").toStringList().isEmpty());
    QVERIFY(root.property("SupportedMimeTypes").toStringList().isEmpty());
    QCOMPARE(root.property("Fullscreen").toBool(), false);
    QCOMPARE(root.property("CanSetFullscreen").toBool(), false);

    // The methods are advertised (Introspect — Quit itself is not
    // called: it would end the test's application).
    QDBusInterface introspectable(service, QStringLiteral("/org/mpris/MediaPlayer2"),
                                  QStringLiteral("org.freedesktop.DBus.Introspectable"),
                                  bus);
    const QString xml =
        introspectable.call("Introspect").arguments().value(0).toString();
    QVERIFY(xml.contains(QLatin1String("interface name=\"org.mpris.MediaPlayer2\"")));
    QVERIFY(xml.contains(QLatin1String("interface name=\"org.mpris.MediaPlayer2.Player\"")));
    // The methods are advertised (a zero-argument one is rendered
    // <method name="X"> with an empty body, so match by prefix).
    for (const QString &method : { QStringLiteral("Raise"), QStringLiteral("Quit"),
                                   QStringLiteral("Play"),
                                   QStringLiteral("Pause"),
                                   QStringLiteral("PlayPause"),
                                   QStringLiteral("Stop"),
                                   QStringLiteral("Next"),
                                   QStringLiteral("Previous"),
                                   QStringLiteral("Seek"),
                                   QStringLiteral("SetPosition"),
    })
        QVERIFY(xml.contains(QStringLiteral("<method name=\"%1\"").arg(method)));

    // The initial state (nothing playing, no rows): the properties
    // read live.
    QCOMPARE(player.property("PlaybackStatus").toString(), QString("Stopped"));
    QCOMPARE(player.property("LoopStatus").toString(), QString("None"));
    QCOMPARE(player.property("Shuffle").toBool(), false);
    QCOMPARE(player.property("Metadata").toMap().isEmpty(), true);
    // The default volume (100, unmuted) is 1.0.
    QCOMPARE(player.property("Volume").toDouble(), 1.0);
    // The rate: doob plays at one speed (the spec requires the
    // property even then, with the bounds set to 1.0).
    QCOMPARE(player.property("Rate").toDouble(), 1.0);
    QCOMPARE(player.property("MinimumRate").toDouble(), 1.0);
    QCOMPARE(player.property("MaximumRate").toDouble(), 1.0);
    // The capabilities: controllable, but with no rows nothing can be
    // played, paused, seeked or skipped to.
    QCOMPARE(player.property("CanControl").toBool(), true);
    QCOMPARE(player.property("CanPlay").toBool(), false);
    QCOMPARE(player.property("CanPause").toBool(), false);
    QCOMPARE(player.property("CanSeek").toBool(), false);
    QCOMPARE(player.property("CanGoNext").toBool(), false);
    QCOMPARE(player.property("CanGoPrevious").toBool(), false);
}

// The bus round-trip: the controls drive the manager, the properties
// read it back, and the state changes ride the bus as PropertiesChanged.
void MprisTest::thePlayerTracksTheManagerState()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        QSKIP("no session bus");

    const QString service = testName();
    m_mpris = new Mpris(this);
    QVERIFY(m_mpris->start(service));

    QDBusInterface player(service, QStringLiteral("/org/mpris/MediaPlayer2"),
                          QStringLiteral("org.mpris.MediaPlayer2.Player"), bus);
    QVERIFY(player.isValid());
    QDBusInterface props(service, QStringLiteral("/org/mpris/MediaPlayer2"),
                         QStringLiteral("org.freedesktop.DBus.Properties"), bus);
    QVERIFY(props.isValid());

    // The PropertiesChanged trail: the adaptor's NOTIFY signals are
    // what the bus export re-emits (spied locally — the state changes
    // are delivered synchronously, no waiting).
    MprisPlayer* adaptor = m_mpris->findChild<MprisPlayer*>();
    QVERIFY(adaptor != nullptr);
    QSignalSpy statusSpy(adaptor, &MprisPlayer::playbackStatusChanged);
    QSignalSpy metadataSpy(adaptor, &MprisPlayer::metadataChanged);
    QSignalSpy volumeSpy(adaptor, &MprisPlayer::volumeChanged);
    QSignalSpy shuffleSpy(adaptor, &MprisPlayer::shuffleChanged);
    QSignalSpy loopSpy(adaptor, &MprisPlayer::loopStatusChanged);
    QSignalSpy canPlaySpy(adaptor, &MprisPlayer::canPlayChanged);
    QSignalSpy canPauseSpy(adaptor, &MprisPlayer::canPauseChanged);
    QSignalSpy canSeekSpy(adaptor, &MprisPlayer::canSeekChanged);
    QSignalSpy canNextSpy(adaptor, &MprisPlayer::canGoNextChanged);
    QSignalSpy canPrevSpy(adaptor, &MprisPlayer::canGoPreviousChanged);
    QSignalSpy seekedSpy(adaptor, &MprisPlayer::seeked);

    MediaItem a = makeItem(1, "One");
    MediaItem b = makeItem(2, "Two");
    a.durationMs = 200000;
    m_store->insert(a);
    m_store->insert(b);
    m_pl->addItems({ a.id, b.id });

    // The capabilities move with the rows (CanPlay/CanGoPrevious from
    // the count, CanGoNext from the manager's advance).
    QCOMPARE(player.property("CanPlay").toBool(), true);
    QCOMPARE(player.property("CanGoPrevious").toBool(), true);

    // Play: the state, the metadata and the capabilities land.
    player.call("Play");
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(player.property("PlaybackStatus").toString(), QString("Playing"));
    const QVariantMap meta = player.property("Metadata").toMap();
    QCOMPARE(meta.value("mpris:trackid").value<QDBusObjectPath>(),
             QDBusObjectPath(Mpris::trackId(1)));
    QCOMPARE(meta.value("dc:title").toString(), QString("One"));
    QCOMPARE(meta.value("mpris:length").toLongLong(), 200000000LL);
    QCOMPARE(player.property("CanPause").toBool(), true);
    QCOMPARE(player.property("CanSeek").toBool(), true);
    QCOMPARE(player.property("CanGoNext").toBool(), true);

    // Pause / resume through the bus (the spec's PlayPause: playing
    // pauses, paused resumes).
    player.call("Pause");
    QCOMPARE(m_pm->state(), PlaybackManager::Paused);
    QCOMPARE(player.property("PlaybackStatus").toString(), QString("Paused"));
    player.call("PlayPause");
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    player.call("PlayPause");
    QCOMPARE(m_pm->state(), PlaybackManager::Paused);

    // The Position: read live (MPRIS microseconds), and — the spec's
    // invariant — never emitted: a position change moves none of the
    // adaptor's NOTIFY signals.
    player.call("SetPosition", QDBusObjectPath(Mpris::trackId(1)),
                static_cast<qint64>(5000000));
    QCOMPARE(m_pm->positionMs(), 5000);
    QCOMPARE(player.property("Position").toLongLong(), 5000000LL);
    const qsizetype emittedBefore = statusSpy.count() + metadataSpy.count()
        + volumeSpy.count() + shuffleSpy.count() + loopSpy.count()
        + canPlaySpy.count() + canPauseSpy.count() + canSeekSpy.count()
        + canNextSpy.count() + canPrevSpy.count() + seekedSpy.count();
    m_pm->setPositionMs(7777); // the manager's positionChanged, no more
    QCOMPARE(statusSpy.count() + metadataSpy.count()
             + volumeSpy.count() + shuffleSpy.count() + loopSpy.count()
             + canPlaySpy.count() + canPauseSpy.count() + canSeekSpy.count()
             + canNextSpy.count() + canPrevSpy.count() + seekedSpy.count(),
             emittedBefore);

    // Next: the second track (the metadata follows).
    player.call("Next");
    QCOMPARE(m_pm->playingItem().id, 2ULL);
    QCOMPARE(player.property("Metadata").toMap().value("dc:title").toString(),
             QString("Two"));

    // SetPosition: only for the track it names (a stale id is a no-op).
    player.call("SetPosition", QDBusObjectPath(Mpris::trackId(1)),
                static_cast<qint64>(1000000));
    QCOMPARE(m_pm->positionMs(), 0); // the stale id is a no-op
    // Seek: relative, the Seeked signal carries the new position.
    player.call("Seek", static_cast<qint64>(6000000));
    QCOMPARE(m_pm->positionMs(), 6000);
    QCOMPARE(seekedSpy.count() > 0, true);
    QCOMPARE(seekedSpy.last().at(0).toLongLong(), 6000000LL);

    // Volume, the shuffle and the loop: the spec's Read/Write
    // properties, written through the Properties interface.
    QDBusReply<void> volumeReply = bus.call(
        setPropertyCall(service, QStringLiteral("org.mpris.MediaPlayer2.Player"),
                        QStringLiteral("Volume"), 0.5));
    QCOMPARE(volumeReply.isValid(), true);
    QCOMPARE(m_pm->volume(), 50);
    QCOMPARE(player.property("Volume").toDouble(), 0.5);
    m_pm->setMuted(true);
    QCOMPARE(player.property("Volume").toDouble(), 0.0);
    m_pm->setMuted(false);
    QCOMPARE(player.property("Volume").toDouble(), 0.5);
    QDBusReply<void> shuffleReply = bus.call(
        setPropertyCall(service, QStringLiteral("org.mpris.MediaPlayer2.Player"),
                        QStringLiteral("Shuffle"), true));
    QCOMPARE(shuffleReply.isValid(), true);
    QCOMPARE(m_pm->shuffle(), true);
    QCOMPARE(player.property("Shuffle").toBool(), true);
    QDBusReply<void> loopReply = bus.call(
        setPropertyCall(service, QStringLiteral("org.mpris.MediaPlayer2.Player"),
                        QStringLiteral("LoopStatus"),
                        QStringLiteral("Track")));
    QCOMPARE(loopReply.isValid(), true);
    QCOMPARE(m_pm->repeat(), PlaybackManager::RepeatMode::RepeatTrack);
    QCOMPARE(player.property("LoopStatus").toString(), QString("Track"));

    // Stop: the state, an empty metadata and the capabilities drop.
    player.call("Stop");
    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);
    QCOMPARE(player.property("PlaybackStatus").toString(), QString("Stopped"));
    QCOMPARE(player.property("Metadata").toMap().isEmpty(), true);
    QCOMPARE(player.property("CanPause").toBool(), false);
    QCOMPARE(player.property("CanSeek").toBool(), false);
    QCOMPARE(player.property("CanPlay").toBool(), true); // the rows remain

    // The changes re-emitted the adaptor's NOTIFY signals (the bus's
    // PropertiesChanged itself is theChangesReachTheBusAsPropertiesChanged's).
    QVERIFY(statusSpy.count() > 0);
    QVERIFY(metadataSpy.count() > 0);
    QVERIFY(volumeSpy.count() > 0);
    QVERIFY(shuffleSpy.count() > 0);
    QVERIFY(loopSpy.count() > 0);
    QVERIFY(canPlaySpy.count() > 0);
    QVERIFY(canPauseSpy.count() > 0);
    QVERIFY(canSeekSpy.count() > 0);
    QVERIFY(canNextSpy.count() > 0);
    QVERIFY(canPrevSpy.count() > 0);
}

// The spec's bounds on the seek controls: a SetPosition below 0 or
// above the track's length is a no-op, a Seek back past 0 lands at 0
// and a Seek past the track's end acts like Next.
void MprisTest::theControlsAreBounded()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        QSKIP("no session bus");

    const QString service = testName();
    m_mpris = new Mpris(this);
    QVERIFY(m_mpris->start(service));

    QDBusInterface player(service, QStringLiteral("/org/mpris/MediaPlayer2"),
                          QStringLiteral("org.mpris.MediaPlayer2.Player"), bus);
    QVERIFY(player.isValid());

    MediaItem a = makeItem(1, "One");
    MediaItem b = makeItem(2, "Two");
    a.durationMs = 200000; // 200 s = 200_000_000 us
    m_store->insert(a);
    m_store->insert(b);
    m_pl->addItems({ a.id, b.id });

    player.call("Play");
    QCOMPARE(m_pm->playingItem().id, 1ULL);

    // SetPosition: above the track's length and below 0 are no-ops.
    player.call("SetPosition", QDBusObjectPath(Mpris::trackId(1)),
                static_cast<qint64>(300000000));
    QCOMPARE(m_pm->positionMs(), 0);
    player.call("SetPosition", QDBusObjectPath(Mpris::trackId(1)),
                static_cast<qint64>(5000000));
    QCOMPARE(m_pm->positionMs(), 5000);
    player.call("SetPosition", QDBusObjectPath(Mpris::trackId(1)),
                static_cast<qint64>(-1));
    QCOMPARE(m_pm->positionMs(), 5000);

    // Seek: back past 0 lands at 0.
    player.call("Seek", static_cast<qint64>(-10000000));
    QCOMPARE(m_pm->positionMs(), 0);
    // Seek: exactly to the end is the end (not past it).
    player.call("Seek", static_cast<qint64>(200000000));
    QCOMPARE(m_pm->positionMs(), 200000);
    QCOMPARE(m_pm->playingItem().id, 1ULL);
    // Seek: past the end acts like Next (the spec's).
    player.call("Seek", static_cast<qint64>(1000000));
    QCOMPARE(m_pm->playingItem().id, 2ULL);
    QCOMPARE(m_pm->positionMs(), 0);
}

// The clients' view: a state change rides the bus as the spec's
// PropertiesChanged (this Qt does not auto-emit it from the adaptor's
// NOTIFY — the emission is the Mpris's own, and the clients watch for
// this signal alone: without it the tray's button and track stay
// stale while the methods and the property reads work).
void MprisTest::theChangesReachTheBusAsPropertiesChanged()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        QSKIP("no session bus");

    const QString service = testName();
    m_mpris = new Mpris(this);
    QVERIFY(m_mpris->start(service));

    // A second connection: the registration lives on the default one,
    // and a connection never receives its own signals (libdbus
    // broadcasts them to the other connections only).
    QDBusConnection second = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, secondBusName);
    QVERIFY(second.isConnected());
    // The service in the connect resolves the owner synchronously
    // (a GetNameOwner on the bus), so the match is live before the
    // first state change; it also keeps the other players'
    // PropertiesChanged (the spec's same path) out of the trail.
    // The slot argument is this Qt build's full normalized signature
    // (name plus types), not a bare name: its dbus module looks the
    // slot up as slot + 1 (hence the leading space — the public
    // connect does not prepend it, so without it the first letter is
    // eaten), and its indexOfMethod on a bare name only matches a
    // zero-argument method (this slot takes the signal's three
    // arguments, so the types must be spelled out to be found).
    QVERIFY(second.connect(service, QStringLiteral("/org/mpris/MediaPlayer2"),
                           QStringLiteral("org.freedesktop.DBus.Properties"),
                           QStringLiteral("PropertiesChanged"),
                           this,
                           " onPropertiesChanged(QString,QVariantMap,QStringList)"));

    MediaItem a = makeItem(1, "One");
    a.durationMs = 200000;
    m_store->insert(a);
    m_pl->addItems({ a.id });

    QDBusInterface player(service, QStringLiteral("/org/mpris/MediaPlayer2"),
                          QStringLiteral("org.mpris.MediaPlayer2.Player"), bus);
    QVERIFY(player.isValid());

    // Play: the state and the metadata, at their current values.
    player.call("Play");
    QTRY_VERIFY_WITH_TIMEOUT(
        std::any_of(m_changes.cbegin(), m_changes.cend(),
                    [](const QVariantMap &change) {
                        return change.value("PlaybackStatus").toString()
                            == QLatin1String("Playing");
                    }),
        5000);
    QTRY_VERIFY_WITH_TIMEOUT(
        std::any_of(m_changes.cbegin(), m_changes.cend(),
                    [](const QVariantMap &change) {
                        return asMap(change.value("Metadata")).value("dc:title")
                                   .toString()
                            == QLatin1String("One");
                    }),
        5000);

    // Pause: the state move (the tray's button follows it).
    player.call("Pause");
    QTRY_VERIFY_WITH_TIMEOUT(
        std::any_of(m_changes.cbegin(), m_changes.cend(),
                    [](const QVariantMap &change) {
                        return change.value("PlaybackStatus").toString()
                            == QLatin1String("Paused");
                    }),
        5000);

    // Stop: the state and the cleared metadata (the empty map).
    player.call("Stop");
    QTRY_VERIFY_WITH_TIMEOUT(
        std::any_of(m_changes.cbegin(), m_changes.cend(),
                    [](const QVariantMap &change) {
                        return change.value("PlaybackStatus").toString()
                                   == QLatin1String("Stopped")
                            && asMap(change.value("Metadata")).isEmpty();
                    }),
        5000);
}

// The second connection's delivery of the spec's PropertiesChanged:
// the interface, the changed map, the unchanged list. The slot
// records the changed map; the assertions read it.
void MprisTest::onPropertiesChanged(const QString &interface,
                                    const QVariantMap &changed,
                                    const QStringList &unchanged)
{
    Q_UNUSED(interface)
    Q_UNUSED(unchanged)
    m_changes.append(changed);
}
}

QTEST_MAIN(MprisTest)
#include "mpris_test.moc"
