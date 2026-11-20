#include <QtTest>

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <memory>

#include "artfallback.h"
#include "artsource.h"
#include "mediaitemstore.h"
#include "playbackmanager.h"
#include "playlistmanager.h"
#include "settingsmanager.h"
#include "testsupport.h"

namespace {

// The source the resolver looks through: it records every fetch and
// answers with canned bytes (or hangs, holding the callback, to test
// the one-lookup-at-a-time discipline).
class ResolverSource : public ArtSource
{
public:
    explicit ResolverSource(QObject *parent = nullptr)
        : ArtSource(parent)
    {
    }

    QString id() const override { return QStringLiteral("fake"); }
    QString title() const override { return QStringLiteral("Fake"); }
    void fetch(const MediaItem &item, ArtKind kind,
               const Callback &cb) override
    {
        Q_UNUSED(kind);
        m_items.append(item);
        if (m_hang) {
            m_pending = cb;
            return;
        }
        cb(m_mime, m_data);
    }
    // Answers the held callback (the in-flight lookup completes).
    void completePending()
    {
        if (m_pending == nullptr)
            return;
        const Callback cb = std::move(m_pending);
        m_pending = nullptr;
        cb(m_mime, m_data);
    }

    QList<MediaItem> m_items;
    QString m_mime = QStringLiteral("image/jpeg");
    QByteArray m_data = QByteArray("art");
    bool m_hang = false;
    Callback m_pending;
};

class ArtFallbackTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void theSettingIsRegisteredWithTheItunesDefault();
    void anArtlessPlayStartStartsOneLookup();
    void aHitStoresTheArtAndEmits();
    void aMissNegativeCachesUntilTheTtl();
    void aDisabledFallbackStartsNoLookup();
    void anItemWithArtStartsNoLookup();
    void aStopStartsNoLookup();
    void aLookupInFlightSkipsTheNextPlayStart();
    void aRemovedSourceIsIgnored();

private:
    MediaItem makeItem(quint64 id, const QString &title);

    FakePlayer m_fake{this};
    PlaybackManager* m_pm = nullptr;
    PlaylistManager* m_pl = nullptr;
    MediaItemStore* m_store = nullptr;
    SettingsManager* m_settings = nullptr;
    ArtRegistry* m_registry = nullptr;
    ArtFallback* m_fallback = nullptr;
    std::unique_ptr<ResolverSource> m_source;
    std::unique_ptr<QTemporaryDir> m_dir;
};

void ArtFallbackTest::init()
{
    // The SettingsManager first: the fallback registers its setting in
    // its ctor, and the registry's built-in reads through it.
    SettingsManager::reset();
    m_settings = SettingsManager::getInstance();
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_settings->setSettingsPath(m_dir->path() + "/settings.json");
    m_settings->load();

    // The database components private to this test (write-through).
    m_pl = PlaylistManager::getInstance();
    m_store = MediaItemStore::getInstance();
    test::usePrivateDatabase(m_dir->path());
    m_pl->reset();
    m_store->clear();

    // The fake source under the id the setting names, then the
    // fallback (its ctor re-runs against the fresh singletons).
    ArtRegistry::reset();
    m_registry = ArtRegistry::getInstance();
    m_source = std::make_unique<ResolverSource>();
    QVERIFY(m_registry->registerSource(m_source.get(), QString()));

    ArtFallback::reset();
    m_fallback = ArtFallback::getInstance();

    m_pm = PlaybackManager::getInstance();
    test::installFakePlayer(m_pm, &m_fake);
}

void ArtFallbackTest::cleanup()
{
    m_pm->stop();
    ArtFallback::reset();
    ArtRegistry::reset();
    m_source.reset();
    m_pl->reset();
    m_store->clear();
}

// Registers the item in the store and appends it to the playlist (the
// playback resolves its rows against both). No QVERIFY here: the
// macros expand to a bare return, which a value-returning helper
// cannot do.
MediaItem ArtFallbackTest::makeItem(quint64 id, const QString &title)
{
    MediaItem item;
    item.id = id;
    item.title = title;
    item.artist = QStringLiteral("Artist");
    // A real (dummy) file: the playback checks the local file against
    // the disk.
    const QString path = m_dir->path() + "/" + title + ".mp3";
    {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.write("\x00\x01\x02");
    }
    item.source = QUrl::fromLocalFile(path);
    m_store->insert(item);
    m_pl->addItems({ item.id });
    return item;
}

void ArtFallbackTest::theSettingIsRegisteredWithTheItunesDefault()
{
    const Setting setting = m_settings->setting(ArtFallback::FallbackKey);
    QVERIFY(setting.isValid());
    QCOMPARE(setting.defaultValue.toString(), QString("itunes"));
}

void ArtFallbackTest::anArtlessPlayStartStartsOneLookup()
{
    makeItem(1, QStringLiteral("One"));
    m_settings->setValue(ArtFallback::FallbackKey, QStringLiteral("fake"));

    m_pm->playCurrent();
    QCOMPARE(m_source->m_items.size(), 1);
    QCOMPARE(m_source->m_items.first().id, quint64(1));
}

void ArtFallbackTest::aHitStoresTheArtAndEmits()
{
    const MediaItem item = makeItem(1, QStringLiteral("One"));
    m_settings->setValue(ArtFallback::FallbackKey, QStringLiteral("fake"));

    QSignalSpy spy(m_store, &MediaItemStore::itemArtworkChanged);
    m_pm->playCurrent();

    QVERIFY(m_store->hasArtwork(item.id));
    QString mime;
    QByteArray data;
    QVERIFY(m_store->artwork(item.id, &mime, &data));
    QCOMPARE(mime, QString("image/jpeg"));
    QCOMPARE(data, QByteArray("art"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<MediaItem>().id, item.id);
}

void ArtFallbackTest::aMissNegativeCachesUntilTheTtl()
{
    const MediaItem item = makeItem(1, QStringLiteral("One"));
    m_settings->setValue(ArtFallback::FallbackKey, QStringLiteral("fake"));
    m_source->m_data.clear(); // the miss

    m_pm->playCurrent();
    QCOMPARE(m_source->m_items.size(), 1);
    QVERIFY(!m_store->hasArtwork(item.id));

    // A second play of the same item (within the TTL) starts no
    // lookup: the miss is remembered.
    m_pm->stop();
    m_pm->playCurrent();
    QCOMPARE(m_source->m_items.size(), 1);

    // A shorter TTL than the time since the miss: the next play
    // re-tries (the negative cache is a TTL, not a lifetime memory).
    m_fallback->setMissTtlMs(0);
    m_pm->stop();
    m_pm->playCurrent();
    QCOMPARE(m_source->m_items.size(), 2);
}

void ArtFallbackTest::aDisabledFallbackStartsNoLookup()
{
    makeItem(1, QStringLiteral("One"));
    m_settings->setValue(ArtFallback::FallbackKey, QStringLiteral("off"));

    m_pm->playCurrent();
    QCOMPARE(m_source->m_items.size(), 0);
}

void ArtFallbackTest::anItemWithArtStartsNoLookup()
{
    const MediaItem item = makeItem(1, QStringLiteral("One"));
    QVERIFY(m_store->setArtwork(item.id, QStringLiteral("image/png"),
                                QByteArray("already")));
    m_settings->setValue(ArtFallback::FallbackKey, QStringLiteral("fake"));

    m_pm->playCurrent();
    QCOMPARE(m_source->m_items.size(), 0);
    // The stored art is untouched by the play.
    QString mime;
    QByteArray data;
    QVERIFY(m_store->artwork(item.id, &mime, &data));
    QCOMPARE(data, QByteArray("already"));
}

void ArtFallbackTest::aStopStartsNoLookup()
{
    m_settings->setValue(ArtFallback::FallbackKey, QStringLiteral("fake"));

    // The stop (an invalid playing item) is not a play start.
    m_pm->playCurrent(); // nothing to play (an empty playlist)
    m_pm->stop();
    QCOMPARE(m_source->m_items.size(), 0);
}

void ArtFallbackTest::aLookupInFlightSkipsTheNextPlayStart()
{
    makeItem(1, QStringLiteral("One"));
    makeItem(2, QStringLiteral("Two"));
    m_settings->setValue(ArtFallback::FallbackKey, QStringLiteral("fake"));
    m_source->m_hang = true;

    m_pm->playCurrent(); // the first lookup, in flight
    QCOMPARE(m_source->m_items.size(), 1);
    m_pm->next();        // skipped: one lookup at a time
    QCOMPARE(m_source->m_items.size(), 1);

    // The first lookup completes; the skipped item's next play start
    // re-tries (back to the arted item 1, then forward to 2 again).
    m_source->completePending();
    m_pm->previous();
    QCOMPARE(m_source->m_items.size(), 1); // item 1 has its art now
    m_pm->next();
    QCOMPARE(m_source->m_items.size(), 2);
    QCOMPARE(m_source->m_items.last().id, quint64(2));
}

void ArtFallbackTest::aRemovedSourceIsIgnored()
{
    makeItem(1, QStringLiteral("One"));
    m_registry->unregisterSource(QStringLiteral("fake"));
    m_settings->setValue(ArtFallback::FallbackKey, QStringLiteral("fake"));

    // The id names no source (the plugin was removed): no lookup.
    m_pm->playCurrent();
    QCOMPARE(m_source->m_items.size(), 0);
}

}

QTEST_MAIN(ArtFallbackTest)
#include "artfallback_test.moc"
