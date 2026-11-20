#include <QtTest>
#include <QFile>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUrlQuery>

#include <memory>

#include "librarymanager.h"
#include "sourcemanager.h"
#include "testsupport.h"

namespace {

// A provider that claims exactly the URLs it is told to; its metadata
// fetch answers with an empty TrackMeta (the item is built from the URL).
class UrlClaimingProvider : public SourceProvider
{
    Q_OBJECT
public:
    explicit UrlClaimingProvider(QObject *parent = nullptr) : SourceProvider(parent) {}

    QString id() const override { return QStringLiteral("fake"); }
    QString title() const override { return QStringLiteral("Fake"); }
    bool accepts(const QUrl &url) const override
    {
        return m_urls.contains(url.toString());
    }
    void fetchMetadata(const QUrl &url, MetadataCallback cb) override
    {
        Q_UNUSED(url);
        cb(TrackMeta());
    }
    QUrl canonicalUrl(const QUrl &url) const override
    {
        return m_canonical ? m_canonical(url) : url;
    }

    QStringList m_urls;
    std::function<QUrl(const QUrl &)> m_canonical;
};

class MediaItemStoreTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void allocateIdsAreMonotonic();
    void insertAllocatesIdWhenZero();
    void insertKeepsGivenId();
    void insertRejectsDuplicateIdAndSource();
    void idsAreAscending();
    void findBySourceHitAndMiss();
    void saveLoadRoundtripKeepsAllFields();
    void savePrunesUnreferencedLocalItems();
    void saveKeepsLibraryItems();
    void saveKeepsPlaylistReferencedItems();
    void saveKeepsUnreferencedNonLocalItems();
    void saveFailureRollsBackAndKeepsPreviousSnapshot();
    void mutationsSurviveRestartWithoutSave();
    void writeFailureRollsBackMemory();
    void resolveFindsLibraryItem();
    void resolveFindsStoreItem();
    void resolveMissIsInvalid();
    void setInLibraryFlipsFlag();
    void updateItemReplacesInPlaceAndEmits();
    void libraryRemovalKeepsItemInStore();
    void libraryRemovalPrunesItemImmediately();
    void playlistRowRemovalPrunesItemImmediately();
    void ensureItemCreatesStoreItemForNewFile();
    void ensureItemReusesLibraryItemIgnoringMeta();
    void ensureItemReusesStoreItemIgnoringMeta();
    void ensureItemRejectsMissingAndUnclaimedSources();
    void ensureItemCreatesRemoteItemForHttpUrl();
    void ensureItemCreatesPluginItemForProviderSource();
    void ensureItemStoresCanonicalProviderSource();
    void insertStampsImportedAt();
    void registerPlayIncrementsAndTimestamps();
    void registerPlayIsNoopForUnknownId();
    void registerPlaySurvivesRestart();
    void setArtworkStoresRetrievesAndReplaces();
    void setArtworkEmptyDataRemoves();
    void setArtworkUnknownIdIsNoop();
    void artworkSurvivesRestart();
    void removeDeletesArtworkRow();
    void savePrunesArtworkOfPrunedItems();
    void ensureItemStoresArtForNewItem();
    void ensureItemKeepsStoredArtOnSourceHit();
    void v1DatabaseMigratesToV2();

private:
    MediaItem makeItem(const QString &title, const QString &file);
    QUrl makeTempFile(const QString &name);
    MediaItem addLibraryItem(const QString &name);

    LibraryManager* m_lib;
    PlaylistManager* m_pl;
    MediaItemStore* m_store;
    std::unique_ptr<QTemporaryDir> m_dir;
    UrlClaimingProvider m_provider{this};
};

void MediaItemStoreTest::init()
{
    m_lib = LibraryManager::getInstance();
    m_pl = PlaylistManager::getInstance();
    m_store = MediaItemStore::getInstance();
    m_dir = std::make_unique<QTemporaryDir>();
    test::usePrivateDatabase(m_dir->path());
    m_lib->clear();
    m_pl->reset();
    m_store->clear();
    SourceManager::getInstance()->clear();
    m_provider.m_urls.clear();
}

void MediaItemStoreTest::cleanup()
{
    m_lib->clear();
    m_pl->reset();
    m_store->clear();
    SourceManager::getInstance()->clear();
}

MediaItem MediaItemStoreTest::makeItem(const QString &title, const QString &file)
{
    MediaItem item;
    item.source = QUrl::fromLocalFile(m_dir->path() + "/" + file);
    item.title = title;
    item.artist = QStringLiteral("Artist ") + title;
    item.album = QStringLiteral("Album ") + title;
    item.albumArtist = QStringLiteral("AlbumArtist ") + title;
    item.composer = QStringLiteral("Composer ") + title;
    item.genre = QStringLiteral("Genre ") + title;
    item.comment = QStringLiteral("Comment ") + title;
    item.trackNumber = 3;
    item.trackTotal = 12;
    item.discNumber = 1;
    item.discTotal = 2;
    item.year = 1999;
    item.durationMs = 123456;
    item.bitrate = 320;
    item.sampleRate = 44100;
    item.channels = 2;
    item.extra.insert(QStringLiteral("BPM"), QStringLiteral("128"));
    item.extra.insert(QStringLiteral("COPYRIGHT"), QStringLiteral("doob"));
    return item;
}

QUrl MediaItemStoreTest::makeTempFile(const QString &name)
{
    const QString path = m_dir->path() + "/" + name;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("makeTempFile: cannot open %s", qPrintable(path));
        return {};
    }
    f.write("\x00\x01\x02");
    f.close();
    return QUrl::fromLocalFile(path);
}

// addFile is asynchronous (tag reading runs on a worker thread): the
// shared add + itemAdded + idle wait.
MediaItem MediaItemStoreTest::addLibraryItem(const QString &name)
{
    return test::addLibraryFileAndWait(m_lib, makeTempFile(name));
}

void MediaItemStoreTest::allocateIdsAreMonotonic()
{
    const quint64 a = m_store->allocateId();
    const quint64 b = m_store->allocateId();
    QVERIFY(a > 0);
    QVERIFY(b > a);
}

void MediaItemStoreTest::insertAllocatesIdWhenZero()
{
    MediaItem item = makeItem("T", "a.mp3");
    QVERIFY(item.id == 0);
    QVERIFY(m_store->insert(item));

    QCOMPARE(m_store->count(), 1);
    const MediaItem stored = m_store->item(item.id);
    QVERIFY(stored.id > 0);
    QCOMPARE(stored.toVariantMap(), item.toVariantMap());
    QVERIFY(m_store->contains(stored.id));
}

void MediaItemStoreTest::insertKeepsGivenId()
{
    MediaItem item = makeItem("T", "a.mp3");
    item.id = 42;
    QVERIFY(m_store->insert(item));
    QVERIFY(m_store->contains(42));
    QCOMPARE(m_store->item(42).title, QString("T"));

    // The counter moved past the explicit id.
    QVERIFY(m_store->allocateId() > 42);
}

void MediaItemStoreTest::insertRejectsDuplicateIdAndSource()
{
    MediaItem first = makeItem("T", "a.mp3");
    QVERIFY(m_store->insert(first));

    // Same id (different source): rejected.
    MediaItem sameId = makeItem("U", "b.mp3");
    sameId.id = first.id;
    QVERIFY(!m_store->insert(sameId));

    // Same source (fresh id): rejected.
    MediaItem sameSource = makeItem("V", "a.mp3");
    QVERIFY(!m_store->insert(sameSource));

    QCOMPARE(m_store->count(), 1);
}

void MediaItemStoreTest::idsAreAscending()
{
    MediaItem low = makeItem("Low", "low.mp3");
    low.id = 3;
    MediaItem mid = makeItem("Mid", "mid.mp3");
    mid.id = 5;
    MediaItem high = makeItem("High", "high.mp3");
    high.id = 8;
    QVERIFY(m_store->insert(high));
    QVERIFY(m_store->insert(low));
    QVERIFY(m_store->insert(mid));

    QCOMPARE(m_store->ids(), QList<quint64>({ 3, 5, 8 }));
    QCOMPARE(m_store->items().at(0).title, QString("Low"));
    QCOMPARE(m_store->items().at(2).title, QString("High"));
}

void MediaItemStoreTest::findBySourceHitAndMiss()
{
    MediaItem item = makeItem("T", "a.mp3");
    QVERIFY(m_store->insert(item));

    const MediaItem found = m_store->findBySource(item.source);
    QVERIFY(found.isValid());
    QCOMPARE(found.id, item.id);

    MediaItem other = makeItem("U", "b.mp3");
    QVERIFY(m_store->findBySource(other.source).id == 0);
}

void MediaItemStoreTest::saveLoadRoundtripKeepsAllFields()
{
    MediaItem a = makeItem("A", "a.mp3");
    MediaItem b = makeItem("B", "b.mp3");
    a.inLibrary = true;
    b.inLibrary = true;
    QVERIFY(m_store->insert(a));
    QVERIFY(m_store->insert(b));

    m_store->save();
    m_store->clear();
    QCOMPARE(m_store->count(), 0);

    m_store->load();
    QCOMPARE(m_store->count(), 2);
    QCOMPARE(m_store->item(a.id).toVariantMap(), a.toVariantMap());
    QCOMPARE(m_store->item(b.id).toVariantMap(), b.toVariantMap());
    QCOMPARE(m_store->findBySource(a.source).id, a.id);

    // The counter continues past the loaded ids.
    MediaItem c = makeItem("C", "c.mp3");
    QVERIFY(m_store->insert(c));
    QVERIFY(c.id > qMax(a.id, b.id));
}

void MediaItemStoreTest::savePrunesUnreferencedLocalItems()
{
    MediaItem item = makeItem("T", "a.mp3");  // Local, not in the library
    QVERIFY(m_store->insert(item));

    m_store->save();
    m_store->clear();
    m_store->load();

    // The snapshot omitted the dead item: nothing to resolve.
    QCOMPARE(m_store->count(), 0);
    QVERIFY(!MediaItemStore::resolve(item.id).isValid());
}

void MediaItemStoreTest::saveKeepsLibraryItems()
{
    MediaItem item = makeItem("T", "a.mp3");
    item.inLibrary = true;
    QVERIFY(m_store->insert(item));

    m_store->save();
    m_store->clear();
    m_store->load();

    QCOMPARE(m_store->count(), 1);
    QVERIFY(m_store->item(item.id).inLibrary);
}

void MediaItemStoreTest::saveKeepsPlaylistReferencedItems()
{
    MediaItem item = makeItem("T", "a.mp3");
    QVERIFY(m_store->insert(item));
    m_pl->addItems({ item.id });

    m_store->save();
    m_store->clear();
    m_store->load();

    QCOMPARE(m_store->count(), 1);
    QCOMPARE(MediaItemStore::resolve(item.id).title, QString("T"));
}

void MediaItemStoreTest::saveKeepsUnreferencedNonLocalItems()
{
    // A non-local item's row may be the only copy of its metadata: it is
    // never pruned.
    MediaItem item = makeItem("T", "a.mp3");
    item.kind = MediaItem::Remote;
    QVERIFY(m_store->insert(item));

    m_store->save();
    m_store->clear();
    m_store->load();

    QCOMPARE(m_store->count(), 1);
    QCOMPARE(MediaItemStore::resolve(item.id).title, QString("T"));
}

void MediaItemStoreTest::saveFailureRollsBackAndKeepsPreviousSnapshot()
{
    // A save that fails mid-snapshot must roll back completely: the previous
    // snapshot stays intact (no truncated/committed partial state).
    MediaItem a = makeItem("A", "a.mp3");
    MediaItem b = makeItem("B", "b.mp3");
    a.inLibrary = true;
    b.inLibrary = true;
    // Both inserts persist through write-through, and the snapshot below
    // commits them as the "previous snapshot" the failure must preserve.
    QVERIFY(m_store->insert(a));
    QVERIFY(m_store->insert(b));
    QVERIFY(m_store->save());

    // A second connection to the same file: a trigger that aborts every
    // insert into media_items forces the snapshot to fail after the DELETEs.
    {
        QSqlDatabase probe = QSqlDatabase::addDatabase("QSQLITE", "store_save_probe");
        probe.setDatabaseName(m_dir->path() + "/doob.db");
        QVERIFY(probe.open());
        {
            QSqlQuery t(probe);
            QVERIFY(t.exec(QStringLiteral(
                "CREATE TRIGGER fail_insert BEFORE INSERT ON media_items "
                "BEGIN SELECT RAISE(ABORT, 'forced failure'); END")));
        }

        QVERIFY(!m_store->save());

        // The failed save left the previous snapshot in place: both items
        // are there and nothing was wiped.
        {
            QSqlQuery q(probe);
            QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM media_items")));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 2);
            QVERIFY(q.exec(QStringLiteral(
                "SELECT title FROM media_items ORDER BY title")));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QStringLiteral("A"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QStringLiteral("B"));
            QVERIFY(q.exec(QStringLiteral("DROP TRIGGER fail_insert")));
        }
        probe.close();
    }
    QSqlDatabase::removeDatabase("store_save_probe");

    // In RAM the state is intact; once the failure goes away the next save
    // writes the full snapshot.
    QCOMPARE(m_store->count(), 2);
    QVERIFY(m_store->save());
    m_store->clear();
    m_store->load();
    QCOMPARE(m_store->count(), 2);
    QCOMPARE(m_store->item(a.id).toVariantMap(), a.toVariantMap());
    QCOMPARE(m_store->item(b.id).toVariantMap(), b.toVariantMap());
}

void MediaItemStoreTest::mutationsSurviveRestartWithoutSave()
{
    // Write-through: every mutation is durable the moment it completes —
    // there is no save() call anywhere in this test.
    MediaItem a = makeItem("A", "a.mp3");
    MediaItem b = makeItem("B", "b.mp3");
    a.inLibrary = true;
    QVERIFY(m_store->insert(a));
    QVERIFY(m_store->insert(b));
    QVERIFY(m_store->setInLibrary(b.id, true));

    MediaItem updated = b;
    updated.title = QStringLiteral("B retagged");
    updated.extra.insert(QStringLiteral("BPM"), QStringLiteral("130"));
    QVERIFY(m_store->updateItem(updated));
    QVERIFY(m_store->remove(a.id));

    // Simulate a crash: the connection drops with no save, the next start
    // reloads from the file.
    m_store->setStoragePath(m_dir->path() + "/doob.db");  // closes
    m_store->load();

    QCOMPARE(m_store->count(), 1);
    QCOMPARE(m_store->item(b.id).toVariantMap(), updated.toVariantMap());
    QVERIFY(!MediaItemStore::resolve(a.id).isValid());
}

void MediaItemStoreTest::writeFailureRollsBackMemory()
{
    // A mutation whose database write fails must roll the in-memory change
    // back: RAM and the file must never drift.
    MediaItem a = makeItem("A", "a.mp3");
    QVERIFY(m_store->insert(a));

    // Triggers that abort every insert and every update of media_items.
    {
        QSqlDatabase probe = QSqlDatabase::addDatabase("QSQLITE", "store_write_probe");
        probe.setDatabaseName(m_dir->path() + "/doob.db");
        QVERIFY(probe.open());
        {
            QSqlQuery t(probe);
            QVERIFY(t.exec(QStringLiteral(
                "CREATE TRIGGER fail_insert BEFORE INSERT ON media_items "
                "BEGIN SELECT RAISE(ABORT, 'forced failure'); END")));
            QVERIFY(t.exec(QStringLiteral(
                "CREATE TRIGGER fail_update BEFORE UPDATE ON media_items "
                "BEGIN SELECT RAISE(ABORT, 'forced failure'); END")));
        }
        probe.close();
    }
    QSqlDatabase::removeDatabase("store_write_probe");

    MediaItem b = makeItem("B", "b.mp3");
    QVERIFY(!m_store->insert(b));
    QCOMPARE(m_store->count(), 1);
    QVERIFY(!MediaItemStore::resolve(b.id).isValid());

    // The update of an existing item rolls back the old metadata.
    MediaItem updated = a;
    updated.title = QStringLiteral("Never persisted");
    QVERIFY(!m_store->updateItem(updated));
    QCOMPARE(m_store->item(a.id).title, QStringLiteral("A"));

    {
        QSqlDatabase probe = QSqlDatabase::addDatabase("QSQLITE", "store_write_probe");
        probe.setDatabaseName(m_dir->path() + "/doob.db");
        QVERIFY(probe.open());
        {
            QSqlQuery t(probe);
            QVERIFY(t.exec(QStringLiteral("DROP TRIGGER fail_insert")));
            QVERIFY(t.exec(QStringLiteral("DROP TRIGGER fail_update")));
        }
        probe.close();
    }
    QSqlDatabase::removeDatabase("store_write_probe");

    // The store is consistent and writable again.
    MediaItem c = makeItem("C", "c.mp3");
    QVERIFY(m_store->insert(c));
    QCOMPARE(m_store->count(), 2);
}

void MediaItemStoreTest::resolveFindsLibraryItem()
{
    const MediaItem libItem = addLibraryItem("lib.mp3");
    QVERIFY(libItem.isValid());

    // Library items live in the store with the flag set.
    QVERIFY(m_store->item(libItem.id).inLibrary);
    const MediaItem resolved = MediaItemStore::resolve(libItem.id);
    QCOMPARE(resolved.toVariantMap(), libItem.toVariantMap());
}

void MediaItemStoreTest::resolveFindsStoreItem()
{
    MediaItem item = makeItem("T", "a.mp3");
    QVERIFY(m_store->insert(item));

    const MediaItem resolved = MediaItemStore::resolve(item.id);
    QCOMPARE(resolved.toVariantMap(), item.toVariantMap());
}

void MediaItemStoreTest::resolveMissIsInvalid()
{
    // An id the store has never allocated (the tests allocate ids far
    // below this).
    const MediaItem resolved = MediaItemStore::resolve(999999);
    QVERIFY(!resolved.isValid());
}

void MediaItemStoreTest::setInLibraryFlipsFlag()
{
    MediaItem item = makeItem("T", "a.mp3");
    QVERIFY(m_store->insert(item));
    QVERIFY(!item.inLibrary);

    QVERIFY(m_store->setInLibrary(item.id, true));
    QVERIFY(m_store->item(item.id).inLibrary);
    QVERIFY(m_store->setInLibrary(item.id, false));
    QVERIFY(!m_store->item(item.id).inLibrary);

    // Unknown ids are refused.
    QVERIFY(!m_store->setInLibrary(999999, true));
}

void MediaItemStoreTest::updateItemReplacesInPlaceAndEmits()
{
    MediaItem item = makeItem("T", "a.mp3");
    item.inLibrary = true;
    QVERIFY(m_store->insert(item));

    QSignalSpy changedSpy(m_store, &MediaItemStore::itemChanged);

    MediaItem updated = item;
    updated.title = QStringLiteral("Retagged");
    updated.year = 2004;
    updated.inLibrary = false;
    QVERIFY(m_store->updateItem(updated));

    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(changedSpy.at(0).at(0).value<MediaItem>().toVariantMap(),
             updated.toVariantMap());
    QCOMPARE(m_store->item(item.id).toVariantMap(), updated.toVariantMap());
    QCOMPARE(m_store->findBySource(item.source).id, item.id);

    // Unknown ids are refused.
    MediaItem unknown = updated;
    unknown.id = 999999;
    QVERIFY(!m_store->updateItem(unknown));
    QCOMPARE(changedSpy.count(), 1);
}

void MediaItemStoreTest::libraryRemovalKeepsItemInStore()
{
    const MediaItem libItem = addLibraryItem("referenced.mp3");
    QVERIFY(libItem.isValid());

    // A playlist keeps the reference by the item's id.
    m_pl->addItems({ libItem.id });
    QVERIFY(m_pl->referencedIds().contains(libItem.id));

    QSignalSpy removedSpy(m_lib, &LibraryManager::itemRemoved);
    QVERIFY(m_lib->removeItem(libItem.id));
    QCOMPARE(removedSpy.count(), 1);
    QCOMPARE(removedSpy.at(0).at(0).value<MediaItem>().id, libItem.id);
    QCOMPARE(removedSpy.at(0).at(0).value<MediaItem>().title, libItem.title);
    QVERIFY(!removedSpy.at(0).at(0).value<MediaItem>().inLibrary);
    QVERIFY(!m_lib->contains(libItem.id));

    // No promotion needed: the item was always in the store, the flag is
    // just off, and the playlist reference keeps resolving.
    QCOMPARE(m_store->count(), 1);
    const MediaItem kept = m_store->item(libItem.id);
    QVERIFY(!kept.inLibrary);
    QCOMPARE(kept.title, libItem.title);
    QCOMPARE(m_pl->itemAt(0).toVariantMap(), kept.toVariantMap());
}

void MediaItemStoreTest::libraryRemovalPrunesItemImmediately()
{
    const MediaItem libItem = addLibraryItem("unreferenced.mp3");
    QVERIFY(libItem.isValid());

    QVERIFY(m_pl->referencedIds().isEmpty());
    QVERIFY(m_lib->removeItem(libItem.id));

    // Prune-at-mutation: not in the library, referenced nowhere, local —
    // the rows are gone right away, in memory and in the database.
    QCOMPARE(m_store->count(), 0);
    QVERIFY(!MediaItemStore::resolve(libItem.id).isValid());
    {
        QSqlDatabase probe = QSqlDatabase::addDatabase("QSQLITE", "store_prune_probe");
        probe.setDatabaseName(m_dir->path() + "/doob.db");
        QVERIFY(probe.open());
        QSqlQuery q(probe);
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM media_items")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);
        probe.close();
    }
    QSqlDatabase::removeDatabase("store_prune_probe");
}

void MediaItemStoreTest::playlistRowRemovalPrunesItemImmediately()
{
    // The other prune call site: an item referenced by a playlist row only
    // (not in the library) is pruned when the row is removed.
    MediaItem item = makeItem("Solo", "solo.mp3");
    QVERIFY(m_store->insert(item));
    m_pl->addItems({ item.id });
    QCOMPARE(m_store->count(), 1);

    m_pl->removeRowsInPlaylist(m_pl->currentPlaylistId(), { 0 });

    QCOMPARE(m_store->count(), 0);
    QVERIFY(!MediaItemStore::resolve(item.id).isValid());
}

void MediaItemStoreTest::ensureItemCreatesStoreItemForNewFile()
{
    const QUrl source = makeTempFile("ensure.mp3");
    TrackMeta meta;
    meta.title = QStringLiteral("Ensured");
    meta.artist = QStringLiteral("Ensured Artist");

    const MediaItem item = m_store->ensureItem(source, meta);

    QVERIFY(item.isValid());
    QCOMPARE(m_store->count(), 1);
    QCOMPARE(m_store->item(item.id).title, QString("Ensured"));
    QCOMPARE(m_store->item(item.id).artist, QString("Ensured Artist"));
    QCOMPARE(m_store->item(item.id).source, source);
    QCOMPARE(m_lib->count(), 0);
}

void MediaItemStoreTest::ensureItemReusesLibraryItemIgnoringMeta()
{
    const MediaItem libItem = addLibraryItem("lib-ensure.mp3");
    QVERIFY(libItem.isValid());

    TrackMeta meta;
    meta.title = QStringLiteral("Different");
    const MediaItem item = m_store->ensureItem(libItem.source, meta);

    QCOMPARE(item.id, libItem.id);
    QCOMPARE(item.title, libItem.title);  // the stored one, not the given meta
    QVERIFY(item.inLibrary);
    QCOMPARE(m_store->count(), 1);
}

void MediaItemStoreTest::ensureItemReusesStoreItemIgnoringMeta()
{
    const QUrl source = makeTempFile("stored.mp3");
    const MediaItem first = m_store->ensureItem(source, TrackMeta());
    QVERIFY(first.isValid());

    TrackMeta meta;
    meta.title = QStringLiteral("Refreshed");
    const MediaItem second = m_store->ensureItem(source, meta);

    QCOMPARE(second.id, first.id);
    QCOMPARE(m_store->count(), 1);
    // The existing item is returned as-is (no metadata refresh).
    QCOMPARE(m_store->item(first.id).toVariantMap(), first.toVariantMap());
}

void MediaItemStoreTest::ensureItemRejectsMissingAndUnclaimedSources()
{
    const QUrl missing = QUrl::fromLocalFile(m_dir->path() + "/nope.mp3");
    QVERIFY(!m_store->ensureItem(missing, TrackMeta()).isValid());
    // A non-local source that is neither http(s) nor claimed by a
    // registered provider is rejected.
    QVERIFY(!m_store->ensureItem(QUrl("ftp://example.com/a.mp3"),
                                 TrackMeta()).isValid());
    QVERIFY(!m_store->ensureItem(QUrl("spotify:track:abc"),
                                 TrackMeta()).isValid());
    QCOMPARE(m_store->count(), 0);
}

void MediaItemStoreTest::ensureItemCreatesRemoteItemForHttpUrl()
{
    TrackMeta meta;
    meta.title = QStringLiteral("Stream");
    const MediaItem item =
        m_store->ensureItem(QUrl("https://example.com/audio/t.mp3"), meta);

    QVERIFY(item.isValid());
    QCOMPARE(item.kind, MediaItem::Remote);
    QVERIFY(item.provider.isEmpty());
    QCOMPARE(item.title, QString("Stream"));
    QCOMPARE(item.source, QUrl("https://example.com/audio/t.mp3"));
    QVERIFY(!item.importedAt.isNull());
    QCOMPARE(m_store->count(), 1);
}

void MediaItemStoreTest::ensureItemCreatesPluginItemForProviderSource()
{
    const QUrl source = QUrl("https://music.example.com/watch?v=1");
    m_provider.m_urls << source.toString();
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    TrackMeta meta;
    meta.title = QStringLiteral("Remote Track");
    meta.artist = QStringLiteral("Remote Artist");
    const MediaItem item = m_store->ensureItem(source, meta);

    QVERIFY(item.isValid());
    QCOMPARE(item.kind, MediaItem::Plugin);
    QCOMPARE(item.provider, m_provider.id());
    QCOMPARE(item.title, QString("Remote Track"));
    QCOMPARE(item.artist, QString("Remote Artist"));
    // The source is stored in the provider's canonical form (the
    // default canonicalUrl is the identity: as pasted).
    QCOMPARE(item.source, source);
    QCOMPARE(m_store->count(), 1);
}

void MediaItemStoreTest::ensureItemStoresCanonicalProviderSource()
{
    // The provider claims the pasted forms (with their incidental
    // arguments) and canonicalizes them to the bare video id.
    const QUrl pasted("https://music.example.com/watch?v=9&list=L1&start_radio=1");
    const QUrl rePasted("https://music.example.com/watch?v=9&i=2");
    m_provider.m_urls = { pasted.toString(), rePasted.toString() };
    m_provider.m_canonical = [](const QUrl &url) {
        return QUrl(QStringLiteral("https://music.example.com/watch?v=")
                    + QUrlQuery(url).queryItemValue("v"));
    };
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    const MediaItem item = m_store->ensureItem(pasted, TrackMeta());
    QVERIFY(item.isValid());
    QCOMPARE(item.source.toString(),
             QString("https://music.example.com/watch?v=9"));

    // A re-paste with different extra arguments hits the same item.
    const MediaItem again = m_store->ensureItem(rePasted, TrackMeta());
    QCOMPARE(again.id, item.id);
    QCOMPARE(m_store->count(), 1);
}

void MediaItemStoreTest::insertStampsImportedAt()
{
    MediaItem item = makeItem("T", "a.mp3");
    QVERIFY(item.importedAt.isNull());
    QVERIFY(m_store->insert(item));

    // Written back into the argument, like the allocated id.
    QVERIFY(!item.importedAt.isNull());
    QCOMPARE(m_store->item(item.id).importedAt, item.importedAt);
}

void MediaItemStoreTest::registerPlayIncrementsAndTimestamps()
{
    MediaItem item = makeItem("T", "a.mp3");
    QVERIFY(m_store->insert(item));

    QSignalSpy changedSpy(m_store, &MediaItemStore::itemChanged);
    QVERIFY(m_store->registerPlay(item.id));

    const MediaItem played = m_store->item(item.id);
    QCOMPARE(played.playCount, quint64(1));
    QVERIFY(!played.lastPlayedAt.isNull());
    // The rest of the item is untouched.
    QCOMPARE(played.title, item.title);
    QCOMPARE(played.importedAt, item.importedAt);

    QVERIFY(m_store->registerPlay(item.id));
    QCOMPARE(m_store->item(item.id).playCount, quint64(2));
    QCOMPARE(changedSpy.count(), 2);
}

void MediaItemStoreTest::registerPlayIsNoopForUnknownId()
{
    QVERIFY(!m_store->registerPlay(424242));
}

void MediaItemStoreTest::registerPlaySurvivesRestart()
{
    MediaItem item = makeItem("T", "a.mp3");
    item.inLibrary = true;  // a plain local item would be pruned by save()
    QVERIFY(m_store->insert(item));
    QVERIFY(m_store->registerPlay(item.id));
    const MediaItem played = m_store->item(item.id);

    m_store->save();
    m_store->clear();
    m_store->load();

    const MediaItem reloaded = m_store->item(item.id);
    QCOMPARE(reloaded.playCount, quint64(1));
    QCOMPARE(reloaded.lastPlayedAt, played.lastPlayedAt);
}

void MediaItemStoreTest::setArtworkStoresRetrievesAndReplaces()
{
    MediaItem item = makeItem("T", "a.mp3");
    QVERIFY(m_store->insert(item));
    QSignalSpy spy(m_store, &MediaItemStore::itemArtworkChanged);

    QVERIFY(!m_store->hasArtwork(item.id));
    QString mime;
    QByteArray data;
    QVERIFY(!m_store->artwork(item.id, &mime, &data));

    QVERIFY(m_store->setArtwork(item.id, "image/jpeg", QByteArray("one")));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<MediaItem>().id, item.id);
    QVERIFY(m_store->hasArtwork(item.id));
    QVERIFY(m_store->artwork(item.id, &mime, &data));
    QCOMPARE(mime, QString("image/jpeg"));
    QCOMPARE(data, QByteArray("one"));

    // A replacement: same id, new mime and bytes, one more signal.
    QVERIFY(m_store->setArtwork(item.id, "image/png", QByteArray("two")));
    QCOMPARE(spy.count(), 2);
    QVERIFY(m_store->artwork(item.id, &mime, &data));
    QCOMPARE(mime, QString("image/png"));
    QCOMPARE(data, QByteArray("two"));
}

void MediaItemStoreTest::setArtworkEmptyDataRemoves()
{
    MediaItem item = makeItem("T", "a.mp3");
    QVERIFY(m_store->insert(item));
    QVERIFY(m_store->setArtwork(item.id, "image/jpeg", QByteArray("one")));
    QVERIFY(m_store->hasArtwork(item.id));

    QSignalSpy spy(m_store, &MediaItemStore::itemArtworkChanged);
    QVERIFY(m_store->setArtwork(item.id, "image/jpeg", QByteArray()));
    QCOMPARE(spy.count(), 1);
    QVERIFY(!m_store->hasArtwork(item.id));
    QString mime;
    QByteArray data;
    QVERIFY(!m_store->artwork(item.id, &mime, &data));
}

void MediaItemStoreTest::setArtworkUnknownIdIsNoop()
{
    QSignalSpy spy(m_store, &MediaItemStore::itemArtworkChanged);
    QVERIFY(!m_store->setArtwork(424242, "image/jpeg", QByteArray("x")));
    QCOMPARE(spy.count(), 0);
}

void MediaItemStoreTest::artworkSurvivesRestart()
{
    MediaItem item = makeItem("T", "a.mp3");
    item.inLibrary = true;  // a plain local item would be pruned by save()
    QVERIFY(m_store->insert(item));
    QVERIFY(m_store->setArtwork(item.id, "image/png", QByteArray("cover")));

    m_store->save();
    m_store->clear();
    m_store->load();

    QVERIFY(m_store->hasArtwork(item.id));
    QString mime;
    QByteArray data;
    QVERIFY(m_store->artwork(item.id, &mime, &data));
    QCOMPARE(mime, QString("image/png"));
    QCOMPARE(data, QByteArray("cover"));
}

void MediaItemStoreTest::removeDeletesArtworkRow()
{
    MediaItem item = makeItem("T", "a.mp3");
    QVERIFY(m_store->insert(item));
    QVERIFY(m_store->setArtwork(item.id, "image/png", QByteArray("art")));
    {
        QSqlDatabase probe = QSqlDatabase::addDatabase("QSQLITE", "store_art_probe");
        probe.setDatabaseName(m_dir->path() + "/doob.db");
        QVERIFY(probe.open());
        QSqlQuery q(probe);
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM item_artwork")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1);
        probe.close();
    }
    QSqlDatabase::removeDatabase("store_art_probe");

    QVERIFY(m_store->remove(item.id));
    {
        QSqlDatabase probe = QSqlDatabase::addDatabase("QSQLITE", "store_art_probe");
        probe.setDatabaseName(m_dir->path() + "/doob.db");
        QVERIFY(probe.open());
        QSqlQuery q(probe);
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM media_items")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);
        q.prepare(QStringLiteral("SELECT COUNT(*) FROM item_artwork"));
        QVERIFY(q.exec());
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);
        probe.close();
    }
    QSqlDatabase::removeDatabase("store_art_probe");
}

void MediaItemStoreTest::savePrunesArtworkOfPrunedItems()
{
    MediaItem kept = makeItem("Kept", "kept.mp3");
    kept.inLibrary = true;
    QVERIFY(m_store->insert(kept));
    MediaItem dead = makeItem("Dead", "dead.mp3");
    QVERIFY(m_store->insert(dead));
    QVERIFY(m_store->setArtwork(kept.id, "image/jpeg", QByteArray("kept-art")));
    QVERIFY(m_store->setArtwork(dead.id, "image/jpeg", QByteArray("dead-art")));

    // dead is pruned from the snapshot (local, unreferenced): its art row
    // goes with it, the survivor's stays.
    QVERIFY(m_store->save());
    {
        QSqlDatabase probe = QSqlDatabase::addDatabase("QSQLITE", "store_art_probe");
        probe.setDatabaseName(m_dir->path() + "/doob.db");
        QVERIFY(probe.open());
        QSqlQuery q(probe);
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM media_items")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1);
        q.exec(QStringLiteral("SELECT item_id FROM item_artwork"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toULongLong(), kept.id);
        QVERIFY(!q.next());
        probe.close();
    }
    QSqlDatabase::removeDatabase("store_art_probe");
}

void MediaItemStoreTest::ensureItemStoresArtForNewItem()
{
    const QUrl source = makeTempFile("art-ensure.mp3");
    TrackMeta meta;
    meta.title = QStringLiteral("Arted");
    meta.artMime = "image/jpeg";
    meta.artData = QByteArray("cover-bytes");

    const MediaItem item = m_store->ensureItem(source, meta);

    QVERIFY(item.isValid());
    QVERIFY(m_store->hasArtwork(item.id));
    QString mime;
    QByteArray data;
    QVERIFY(m_store->artwork(item.id, &mime, &data));
    QCOMPARE(mime, QString("image/jpeg"));
    QCOMPARE(data, QByteArray("cover-bytes"));
}

void MediaItemStoreTest::ensureItemKeepsStoredArtOnSourceHit()
{
    const QUrl source = makeTempFile("keep-art.mp3");
    const MediaItem first = m_store->ensureItem(source, TrackMeta());
    QVERIFY(first.isValid());
    QVERIFY(m_store->setArtwork(first.id, "image/jpeg", QByteArray("stored")));

    // A re-import of the same source (its read carrying fresh art) hits
    // the stored item: its art is left as stored.
    TrackMeta meta;
    meta.artMime = "image/png";
    meta.artData = QByteArray("fresh");
    const MediaItem second = m_store->ensureItem(source, meta);

    QCOMPARE(second.id, first.id);
    QString mime;
    QByteArray data;
    QVERIFY(m_store->artwork(second.id, &mime, &data));
    QCOMPARE(mime, QString("image/jpeg"));
    QCOMPARE(data, QByteArray("stored"));
}

void MediaItemStoreTest::v1DatabaseMigratesToV2()
{
    // Hand-craft the database a v1 doob left behind: the schema_version
    // row at 1, the v1 tables (no item_artwork) and one item.
    {
        QSqlDatabase craft = QSqlDatabase::addDatabase("QSQLITE", "store_v1_craft");
        craft.setDatabaseName(m_dir->path() + "/doob.db");
        QVERIFY(craft.open());
        QSqlQuery q(craft);
        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE schema_version ("
            "component TEXT NOT NULL PRIMARY KEY, "
            "version INTEGER NOT NULL)")));
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO schema_version (component, version) VALUES ('items', 1)")));
        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE media_items ("
            "id INTEGER PRIMARY KEY, "
            "source TEXT NOT NULL UNIQUE, "
            "in_library INTEGER NOT NULL DEFAULT 0, "
            "kind INTEGER NOT NULL DEFAULT 0, "
            "title TEXT NOT NULL DEFAULT '', "
            "artist TEXT NOT NULL DEFAULT '', "
            "album TEXT NOT NULL DEFAULT '', "
            "album_artist TEXT NOT NULL DEFAULT '', "
            "composer TEXT NOT NULL DEFAULT '', "
            "genre TEXT NOT NULL DEFAULT '', "
            "comment TEXT NOT NULL DEFAULT '', "
            "track_number INTEGER NOT NULL DEFAULT 0, "
            "track_total INTEGER NOT NULL DEFAULT 0, "
            "disc_number INTEGER NOT NULL DEFAULT 0, "
            "disc_total INTEGER NOT NULL DEFAULT 0, "
            "year INTEGER NOT NULL DEFAULT 0, "
            "duration_ms INTEGER NOT NULL DEFAULT 0, "
            "bitrate INTEGER NOT NULL DEFAULT 0, "
            "sample_rate INTEGER NOT NULL DEFAULT 0, "
            "channels INTEGER NOT NULL DEFAULT 0, "
            "provider TEXT NOT NULL DEFAULT '', "
            "imported_at INTEGER NOT NULL DEFAULT 0, "
            "last_played_at INTEGER NOT NULL DEFAULT 0, "
            "play_count INTEGER NOT NULL DEFAULT 0, "
            "metadata_json TEXT NOT NULL DEFAULT '{}')")));
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO media_items (id, source, title) VALUES (1, 'v1source', 'V1 Title')")));
        craft.close();
    }
    QSqlDatabase::removeDatabase("store_v1_craft");

    // The next open runs the v2 migration: the art table appears, the
    // recorded version bumps, the v1 item survives.
    m_store->setStoragePath(m_dir->path() + "/doob.db");
    m_store->load();

    QCOMPARE(m_store->count(), 1);
    QCOMPARE(m_store->item(1).title, QString("V1 Title"));
    {
        QSqlDatabase probe = QSqlDatabase::addDatabase("QSQLITE", "store_v1_probe");
        probe.setDatabaseName(m_dir->path() + "/doob.db");
        QVERIFY(probe.open());
        QSqlQuery q(probe);
        QVERIFY(q.exec(QStringLiteral(
            "SELECT name FROM sqlite_master WHERE type='table' AND name='item_artwork'")));
        QVERIFY(q.next());
        q.exec(QStringLiteral("SELECT version FROM schema_version WHERE component='items'"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 2);
        probe.close();
    }
    QSqlDatabase::removeDatabase("store_v1_probe");
}

}

QTEST_MAIN(MediaItemStoreTest)
#include "mediaitemstore_test.moc"
