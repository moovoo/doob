#include <QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <memory>

#include "playlistfile.h"
#include "settingsmanager.h"
#include "sourcemanager.h"
#include "testproviders.h"
#include "taggedfile.h"
#include "testsupport.h"

#include <utility>

namespace {

class PlaylistManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void addItemsAppendsAndSetsCurrent();
    void addItemsAllowsDuplicates();
    void insertItemsAtPosition();
    void insertItemsClampsIndex();
    void insertItemsAllowsDuplicates();
    void removeAtAdjustsCurrentIndex();
    void removeItemById();
    void clearResets();
    void moveRowsReorders();
    void moveRowsMultiSelection();
    void moveRowsNoOpWhenDroppedInPlace();
    void moveRowsNoOpWhenDroppedInOwnSpan();
    void moveRowsMovesExactlyTheDraggedRows();
    void removeRowsInPlaylistRemovesRows();
    void removeRowsInPlaylistTracksCurrentIndex();
    void removeRowsInPlaylistIgnoresOutOfRangeAndDuplicates();
    void setCurrentIndexClampsAndEmits();
    void currentItemFollowsIndex();
    void saveLoadRoundtrip();
    void saveFailureRollsBackAndKeepsPreviousSnapshot();
    void mutationsSurviveRestartWithoutSave();
    void writeFailureRollsBackMemory();
    void saveLoadKeepsDanglingIds();
    void referencedIdsSeesAllPlaylists();
    void currentPlaylistPersistedByIdAndName();
    void loadResolvesCurrentPlaylistById();
    void loadResolvesCurrentPlaylistByName();
    void sessionStateWrittenOnChange();
    void currentIndexLoadIgnoresStaleAndOutOfRange();
    void removePlaylistDropsIndexEntry();
    void databaseHoldsNoAppState();
    void startsWithDefaultPlaylist();
    void addPlaylistCreatesPlaylistWithUniqueIds();
    void addPlaylistDoesNotChangeCurrent();
    void removePlaylistRemovesPlaylist();
    void removingCurrentPlaylistSwitchesToFirst();
    void removingLastPlaylistCreatesNewEmptyOne();
    void renamePlaylistRenames();
    void setCurrentPlaylistSwitches();
    void setCurrentPlaylistNoOpForUnknownId();
    void nextPlaylistWalksInOrderAndWraps();
    void previousPlaylistWalksInReverseAndWraps();
    void nextPreviousPlaylistNoOpWithSinglePlaylist();
    void playlistsKeepOwnCurrentIndex();
    void resetRestoresDefaultState();

    void importUnknownFileCreatesStandaloneItem();
    void importLibrarySourceReusesLibraryItem();
    void importAppendsDuplicateRows();
    void importIsSerialized();
    void importOrderFollowsSourceOrder();
    void importProviderSourceCreatesPluginItem();
    void importSkipsFailedProviderFetch();
    void importRemoteStreamUrlCreatesRemoteItem();
    void importMixedSourcesFollowSourceOrder();
    void importRejectsUnclaimedNonLocalSource();
    void importSkipsSourceWhenProviderVanishes();
    void importSourcesAtPositionInsertsBlock();
    void importSourcesAtPositionTracksCurrentIndex();
    void importSourcesPastTheEndAppends();

    void v1DatabaseMigratesToV2();
    void movePlaylistReordersAndPersists();
    void movePlaylistNoOpForUnknownOrSamePosition();
    void markImportedSetsFlagsAndClearsDirty();
    void contentMutationMarksDirty();
    void renameAndCursorDoNotMarkDirty();
    void dirtySurvivesRestart();
    void importPlaylistFileCreatesImportedPlaylist();
    void importPlaylistFileFailsOnUnparseable();
    void exportPlaylistWritesM3U();
    void savePlaylistClearsDirty();

private:
    MediaItem makeItem(quint64 id, const QString &title);
    QJsonObject readSettings();
    // Spins the event loop until the running import (if any) has finished.
    void waitForImport();

    LibraryManager* m_lib;
    PlaylistManager* m_pl;
    MediaItemStore* m_store;
    std::unique_ptr<QTemporaryDir> m_dir;
    MetaProvider m_provider{this};
};

void PlaylistManagerTest::init()
{
    m_lib = LibraryManager::getInstance();
    m_pl = PlaylistManager::getInstance();
    m_store = MediaItemStore::getInstance();
    m_dir = std::make_unique<QTemporaryDir>();
    test::usePrivateDatabase(m_dir->path());
    // The session state lives in the SettingsManager, private to this
    // test's directory.
    SettingsManager* settings = SettingsManager::getInstance();
    settings->setSettingsPath(m_dir->path() + "/settings.json");
    settings->load();
    m_lib->clear();
    m_pl->reset();
    m_store->clear();
    SourceManager::getInstance()->clear();
    m_provider.m_urls.clear();
    m_provider.m_meta = TrackMeta();
    m_provider.m_defer = false;
    m_provider.m_pending.clear();
}

void PlaylistManagerTest::cleanup()
{
    m_lib->clear();
    m_pl->reset();
    m_store->clear();
    SourceManager::getInstance()->clear();
    SettingsManager::reset();
}

// Builds a fully populated item and registers it in the standalone item
// store so itemAt()/currentItem() can resolve it. Re-registering an id
// replaces the stored data.
MediaItem PlaylistManagerTest::makeItem(quint64 id, const QString &title)
{
    MediaItem item;
    item.id = id;
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
    // A real (dummy) file: playback is checked against the disk.
    const QString path = m_dir->path() + "/" + title + ".mp3";
    {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.write("\x00\x01\x02");
    }
    item.source = QUrl::fromLocalFile(path);
    m_store->remove(id);
    m_store->insert(item);
    return item;
}

void PlaylistManagerTest::addItemsAppendsAndSetsCurrent()
{
    QSignalSpy changedSpy(m_pl, &PlaylistManager::playlistChanged);

    m_pl->addItems({ makeItem(1, "A").id, makeItem(2, "B").id });

    QCOMPARE(m_pl->count(), 2);
    QCOMPARE(m_pl->itemIds(), QList<quint64>({ 1, 2 }));
    QCOMPARE(m_pl->currentIndex(), 0);
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(changedSpy.at(0).at(0).toULongLong(), m_pl->currentPlaylistId());
}

void PlaylistManagerTest::addItemsAllowsDuplicates()
{
    // A playlist may hold the same item any number of times: re-adding an
    // item that is already there (a double-clicked library row, a re-drag)
    // appends another row.
    QSignalSpy changedSpy(m_pl, &PlaylistManager::playlistChanged);

    m_pl->addItems({ makeItem(1, "A").id, makeItem(2, "B").id });
    QCOMPARE(changedSpy.count(), 1);

    // Every id is appended, in order — including ids the playlist already
    // holds and duplicates inside the payload itself.
    m_pl->addItems({ makeItem(2, "B").id, makeItem(3, "C").id, makeItem(3, "C").id });
    QCOMPARE(changedSpy.count(), 2);
    QCOMPARE(m_pl->itemIds(), QList<quint64>({ 1, 2, 2, 3, 3 }));

    // A re-add of an existing id appends another row and still emits.
    m_pl->addItems({ 2 });
    QCOMPARE(changedSpy.count(), 3);
    QCOMPARE(m_pl->itemIds(), QList<quint64>({ 1, 2, 2, 3, 3, 2 }));
}

void PlaylistManagerTest::insertItemsAtPosition()
{
    m_pl->addItems({ makeItem(1, "A").id, makeItem(3, "C").id });
    m_pl->setCurrentIndex(1);

    m_pl->insertItems(1, { makeItem(2, "B").id });

    QCOMPARE(m_pl->count(), 3);
    QCOMPARE(m_pl->itemIds().at(1), 2ULL);
    QCOMPARE(m_pl->currentIndex(), 2);
}

void PlaylistManagerTest::insertItemsClampsIndex()
{
    m_pl->addItems({ makeItem(1, "A").id });

    m_pl->insertItems(99, { makeItem(2, "B").id });
    QCOMPARE(m_pl->itemIds().at(1), 2ULL);

    m_pl->insertItems(-5, { makeItem(3, "C").id });
    QCOMPARE(m_pl->itemIds().at(0), 3ULL);
}

void PlaylistManagerTest::insertItemsAllowsDuplicates()
{
    m_pl->addItems({ makeItem(1, "A").id, makeItem(3, "C").id });
    m_pl->setCurrentIndex(1);

    // One row per id, in order: the first id is already in the playlist
    // and is added again, and both inserted rows shift the current index.
    m_pl->insertItems(0, { makeItem(1, "A").id, makeItem(2, "B").id });

    QCOMPARE(m_pl->itemIds(), QList<quint64>({ 1, 2, 1, 3 }));
    QCOMPARE(m_pl->currentIndex(), 3);
}

void PlaylistManagerTest::removeAtAdjustsCurrentIndex()
{
    m_pl->addItems({ makeItem(1, "A").id, makeItem(2, "B").id, makeItem(3, "C").id });
    m_pl->setCurrentIndex(2);

    m_pl->removeAt(0);
    QCOMPARE(m_pl->count(), 2);
    QCOMPARE(m_pl->currentIndex(), 1);

    m_pl->removeAt(1);
    QCOMPARE(m_pl->count(), 1);
    QCOMPARE(m_pl->currentIndex(), 0);

    m_pl->removeAt(0);
    QCOMPARE(m_pl->count(), 0);
    QCOMPARE(m_pl->currentIndex(), -1);
}

void PlaylistManagerTest::removeItemById()
{
    m_pl->addItems({ makeItem(1, "A").id, makeItem(2, "B").id });

    QVERIFY(m_pl->removeItem(1));
    QCOMPARE(m_pl->count(), 1);
    QCOMPARE(m_pl->itemIds().at(0), 2ULL);
    QVERIFY(!m_pl->removeItem(1));
}

void PlaylistManagerTest::clearResets()
{
    m_pl->addItems({ makeItem(1, "A").id, makeItem(2, "B").id });

    m_pl->clear();

    QCOMPARE(m_pl->count(), 0);
    QCOMPARE(m_pl->currentIndex(), -1);
}

void PlaylistManagerTest::moveRowsReorders()
{
    m_pl->addItems({ makeItem(1, "A").id, makeItem(2, "B").id,
                     makeItem(3, "C").id, makeItem(4, "D").id, });

    m_pl->moveRows({ 0 }, 3);
    QCOMPARE(m_pl->itemIds(), QList<quint64>({ 2, 3, 1, 4 }));
}

void PlaylistManagerTest::moveRowsMultiSelection()
{
    m_pl->addItems({ makeItem(1, "A").id, makeItem(2, "B").id,
                     makeItem(3, "C").id, makeItem(4, "D").id,
                     makeItem(5, "E").id, });

    m_pl->moveRows({ 0, 2 }, 4);
    QCOMPARE(m_pl->itemIds(), QList<quint64>({ 2, 4, 1, 3, 5 }));
}

void PlaylistManagerTest::moveRowsNoOpWhenDroppedInPlace()
{
    m_pl->addItems({ makeItem(1, "A").id, makeItem(2, "B").id, makeItem(3, "C").id });

    m_pl->moveRows({ 1 }, 1);
    QCOMPARE(m_pl->itemIds(), QList<quint64>({ 1, 2, 3 }));

    m_pl->moveRows({ 1 }, 2);
    QCOMPARE(m_pl->itemIds(), QList<quint64>({ 1, 2, 3 }));
}

void PlaylistManagerTest::moveRowsNoOpWhenDroppedInOwnSpan()
{
    // Dropping a non-contiguous selection onto a position inside its own
    // span is a no-op: the rows return to their original places (a single
    // row's in-place drop is already a no-op through the position math).
    m_pl->addItems({ makeItem(1, "A").id, makeItem(2, "B").id,
                     makeItem(3, "C").id, makeItem(4, "D").id, });

    m_pl->moveRows({ 0, 2 }, 1); // drop A and C onto B (inside their span)
    QCOMPARE(m_pl->itemIds(), QList<quint64>({ 1, 2, 3, 4 }));

    m_pl->moveRows({ 0, 2 }, 2);
    QCOMPARE(m_pl->itemIds(), QList<quint64>({ 1, 2, 3, 4 }));
}

void PlaylistManagerTest::moveRowsMovesExactlyTheDraggedRows()
{
    // A playlist may hold the same item in several rows: the move names
    // the dragged rows, so dragging one copy of a duplicated item moves
    // exactly that row — and the current row is tracked through the move.
    m_pl->addItems({ 1, 2, 1, 3 });
    QCOMPARE(m_pl->currentIndex(), 0);

    // Drag the second copy of id 1 (row 2) to the front: the first copy
    // (row 0, the current row) shifts to row 1.
    m_pl->moveRows({ 2 }, 0);
    QCOMPARE(m_pl->itemIds(), QList<quint64>({ 1, 1, 2, 3 }));
    QCOMPARE(m_pl->currentIndex(), 1);

    // Drag the first copy of id 1 (row 0) to the end: the current row
    // (row 1, the other copy) is left in place at row 0.
    m_pl->moveRows({ 0 }, 4);
    QCOMPARE(m_pl->itemIds(), QList<quint64>({ 1, 2, 3, 1 }));
    QCOMPARE(m_pl->currentIndex(), 0);
}

void PlaylistManagerTest::removeRowsInPlaylistRemovesRows()
{
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->setCurrentPlaylist(road);
    m_pl->addItems({ makeItem(1, "A").id, makeItem(2, "B").id,
                     makeItem(3, "C").id, makeItem(4, "D").id, });

    // The middle rows of an arbitrary playlist are removed in one call.
    m_pl->removeRowsInPlaylist(road, { 1, 2 });

    QCOMPARE(m_pl->playlist(road).items, QList<quint64>({ 1ULL, 4ULL }));
}

void PlaylistManagerTest::removeRowsInPlaylistTracksCurrentIndex()
{
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->setCurrentPlaylist(road);
    m_pl->addItems({ makeItem(1, "A").id, makeItem(2, "B").id,
                     makeItem(3, "C").id, makeItem(4, "D").id, });
    m_pl->setCurrentIndex(2); // C

    // Removing a row before the current row shifts it down.
    m_pl->removeRowsInPlaylist(road, { 0 });
    QCOMPARE(m_pl->currentIndex(), 1);

    // Removing the current row: the next row slides into its place.
    m_pl->removeRowsInPlaylist(road, { 1 });
    QCOMPARE(m_pl->itemIds(), QList<quint64>({ 2ULL, 4ULL }));
    QCOMPARE(m_pl->currentIndex(), 1);

    // Removing all remaining rows empties the playlist.
    m_pl->removeRowsInPlaylist(road, { 0, 1 });
    QCOMPARE(m_pl->count(), 0);
    QCOMPARE(m_pl->currentIndex(), -1);
}

void PlaylistManagerTest::removeRowsInPlaylistIgnoresOutOfRangeAndDuplicates()
{
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->setCurrentPlaylist(road);
    m_pl->addItems({ makeItem(1, "A").id, makeItem(2, "B").id });

    // Out-of-range rows are dropped; a repeated row is removed only once.
    m_pl->removeRowsInPlaylist(road, { 0, 0, 5, -1 });

    QCOMPARE(m_pl->playlist(road).items, QList<quint64>({ 2ULL }));
}

void PlaylistManagerTest::setCurrentIndexClampsAndEmits()
{
    m_pl->addItems({ makeItem(1, "A").id, makeItem(2, "B").id });

    QSignalSpy spy(m_pl, &PlaylistManager::currentIndexChanged);

    m_pl->setCurrentIndex(5);
    QCOMPARE(m_pl->currentIndex(), 1);
    m_pl->setCurrentIndex(1);
    QCOMPARE(spy.count(), 1);
    m_pl->setCurrentIndex(-5);
    QCOMPARE(m_pl->currentIndex(), -1);
    QCOMPARE(spy.count(), 2);
}

void PlaylistManagerTest::currentItemFollowsIndex()
{
    m_pl->addItems({ makeItem(1, "A").id, makeItem(2, "B").id });

    // itemAt()/currentItem() resolve the ids against the stores.
    QCOMPARE(m_pl->currentItem().id, 1ULL);
    m_pl->setCurrentIndex(1);
    QCOMPARE(m_pl->currentItem().id, 2ULL);
}

void PlaylistManagerTest::saveLoadRoundtrip()
{
    const quint64 def = m_pl->currentPlaylistId();
    const MediaItem a = makeItem(1, "A");
    const MediaItem b = makeItem(2, "B");
    const MediaItem c = makeItem(3, "C");
    m_pl->addItems({ a.id, b.id, c.id });
    m_pl->setCurrentIndex(1);

    const quint64 p = m_pl->addPlaylist("Second");
    m_pl->setCurrentPlaylist(p);
    const MediaItem d = makeItem(4, "D");
    const MediaItem e = makeItem(5, "E");
    m_pl->addItems({ d.id, e.id });
    m_pl->setCurrentIndex(0);

    m_pl->save();
    m_store->save();

    // Wipe in-memory state, then reload everything from the databases.
    m_pl->reset();
    m_store->clear();
    m_store->load();
    m_pl->load();

    QCOMPARE(m_pl->playlistCount(), 2);
    QCOMPARE(m_pl->currentPlaylistId(), p);

    // The current ("Second") playlist kept its items and current index, and
    // the references resolve to the full item data.
    QCOMPARE(m_pl->count(), 2);
    QCOMPARE(m_pl->currentIndex(), 0);
    QCOMPARE(m_pl->itemAt(0).toVariantMap(), d.toVariantMap());
    QCOMPARE(m_pl->itemAt(1).toVariantMap(), e.toVariantMap());

    // The other playlist kept its own items and current index too.
    m_pl->setCurrentPlaylist(def);
    QCOMPARE(m_pl->count(), 3);
    QCOMPARE(m_pl->currentIndex(), 1);
    QCOMPARE(m_pl->itemAt(0).toVariantMap(), a.toVariantMap());
    QCOMPARE(m_pl->itemAt(2).toVariantMap(), c.toVariantMap());
}

void PlaylistManagerTest::saveFailureRollsBackAndKeepsPreviousSnapshot()
{
    // A save that fails mid-snapshot must roll back completely: the previous
    // snapshot stays intact (no truncated/committed partial state).
    const MediaItem a = makeItem(1, "A");
    m_pl->addItems({ a.id });
    const quint64 p = m_pl->addPlaylist("Second");
    QVERIFY(m_pl->save());

    // A second connection to the same file: a trigger that aborts every
    // insert into playlists forces the snapshot to fail after the DELETEs.
    const MediaItem b = makeItem(2, "B");
    {
        QSqlDatabase probe = QSqlDatabase::addDatabase("QSQLITE", "pl_save_probe");
        probe.setDatabaseName(m_dir->path() + "/doob.db");
        QVERIFY(probe.open());
        {
            QSqlQuery t(probe);
            QVERIFY(t.exec(QStringLiteral(
                "CREATE TRIGGER fail_insert BEFORE INSERT ON playlists "
                "BEGIN SELECT RAISE(ABORT, 'forced failure'); END")));
        }

        // With the trigger in place, a write-through mutation fails and
        // rolls its memory change back (the row sync upserts the playlist
        // row), and the snapshot fails the same way.
        m_pl->setCurrentPlaylist(p);
        m_pl->addItems({ b.id });
        QCOMPARE(m_pl->count(), 0);  // the append was rolled back
        QVERIFY(!m_pl->save());

        // The failed save rolled back cleanly: both playlists are there and
        // the state is exactly what was written through before the trigger
        // (a in the default playlist, nothing in "Second").
        {
            QSqlQuery q(probe);
            QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM playlists")));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 2);
            q.prepare(QStringLiteral(
                "SELECT COUNT(*) FROM playlist_items WHERE playlist_id = ?"));
            q.bindValue(0, p);
            QVERIFY(q.exec());
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 0);
            q.bindValue(0, 1ULL);
            QVERIFY(q.exec());
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 1);
            QVERIFY(q.exec(QStringLiteral("DROP TRIGGER fail_insert")));
        }
        probe.close();
    }
    QSqlDatabase::removeDatabase("pl_save_probe");

    // In RAM the state is intact; once the failure goes away the writes
    // succeed again and the snapshot round-trips.
    m_pl->addItems({ b.id });
    QVERIFY(m_pl->save());
    m_pl->reset();
    m_pl->load();
    QCOMPARE(m_pl->playlistCount(), 2);
    m_pl->setCurrentPlaylist(p);
    QCOMPARE(m_pl->count(), 1);
    QCOMPARE(m_pl->itemAt(0).id, 2ULL);
}

void PlaylistManagerTest::mutationsSurviveRestartWithoutSave()
{
    // Write-through: every mutation is durable the moment it completes —
    // there is no save() call anywhere in this test.
    const MediaItem a = makeItem(1, "A");
    const MediaItem b = makeItem(2, "B");
    const MediaItem c = makeItem(3, "C");
    m_pl->addItems({ a.id, b.id, c.id });
    m_pl->setCurrentIndex(1);

    const quint64 p = m_pl->addPlaylist("Second");
    m_pl->setCurrentPlaylist(p);
    m_pl->addItems({ b.id, a.id });  // the same items, a different order
    m_pl->moveRowsInPlaylist(p, { 0 }, 2);  // b after a
    m_pl->renamePlaylist(p, "Renamed");

    // Simulate a crash: the connection drops with no save, the next start
    // reloads from the file (and settings.json).
    m_pl->setStoragePath(m_dir->path() + "/doob.db");  // closes
    m_pl->load();

    QCOMPARE(m_pl->playlistCount(), 2);
    QCOMPARE(m_pl->currentPlaylistId(), p);
    QCOMPARE(m_pl->playlist(p).name, QString("Renamed"));
    QCOMPARE(m_pl->itemIds(), QList<quint64>({ a.id, b.id }));
    // The move dragged the current row along with it.
    QCOMPARE(m_pl->currentIndex(), 1);

    m_pl->setCurrentPlaylist(1);
    QCOMPARE(m_pl->itemIds(), QList<quint64>({ a.id, b.id, c.id }));
    QCOMPARE(m_pl->currentIndex(), 1);
}

void PlaylistManagerTest::writeFailureRollsBackMemory()
{
    // A mutation whose database write fails must roll the in-memory change
    // back: RAM and the file must never drift.
    const MediaItem a = makeItem(1, "A");
    m_pl->addItems({ a.id });

    // A trigger that aborts every insert into playlist_items.
    {
        QSqlDatabase probe = QSqlDatabase::addDatabase("QSQLITE", "pl_write_probe");
        probe.setDatabaseName(m_dir->path() + "/doob.db");
        QVERIFY(probe.open());
        {
            QSqlQuery t(probe);
            QVERIFY(t.exec(QStringLiteral(
                "CREATE TRIGGER fail_insert BEFORE INSERT ON playlist_items "
                "BEGIN SELECT RAISE(ABORT, 'forced failure'); END")));
        }
        probe.close();
    }
    QSqlDatabase::removeDatabase("pl_write_probe");

    QSignalSpy changedSpy(m_pl, &PlaylistManager::playlistChanged);
    const MediaItem b = makeItem(2, "B");
    m_pl->addItems({ b.id });
    // The failed write rolled the append back: no change is announced.
    QCOMPARE(changedSpy.count(), 0);
    QCOMPARE(m_pl->count(), 1);
    QCOMPARE(m_pl->itemIds(), QList<quint64>({ a.id }));

    {
        QSqlDatabase probe = QSqlDatabase::addDatabase("QSQLITE", "pl_write_probe");
        probe.setDatabaseName(m_dir->path() + "/doob.db");
        QVERIFY(probe.open());
        {
            QSqlQuery t(probe);
            QVERIFY(t.exec(QStringLiteral("DROP TRIGGER fail_insert")));
        }
        probe.close();
    }
    QSqlDatabase::removeDatabase("pl_write_probe");

    // The playlist is consistent and writable again.
    m_pl->addItems({ b.id });
    QCOMPARE(m_pl->count(), 2);
}

void PlaylistManagerTest::saveLoadKeepsDanglingIds()
{
    // An id that resolves to nothing (in neither the library nor the store)
    // is kept across a save/load: a failed library load must not silently
    // wipe playlist rows on the next save.
    m_pl->addItems({ 999999 });
    m_pl->save();

    m_pl->reset();
    m_pl->load();

    QCOMPARE(m_pl->itemIds(), QList<quint64>({ 999999 }));
    QVERIFY(!m_pl->itemAt(0).isValid());
}

void PlaylistManagerTest::referencedIdsSeesAllPlaylists()
{
    const MediaItem a = makeItem(1, "A");
    m_pl->addItems({ a.id });

    const quint64 p = m_pl->addPlaylist("P");
    m_pl->setCurrentPlaylist(p);
    const MediaItem b = makeItem(2, "B");
    m_pl->addItems({ b.id });

    const QSet<quint64> refs = m_pl->referencedIds();
    QVERIFY(refs.contains(a.id));  // referenced from the other playlist
    QVERIFY(refs.contains(b.id));
    QVERIFY(!refs.contains(999));
}

QJsonObject PlaylistManagerTest::readSettings()
{
    QFile f(m_dir->path() + "/settings.json");
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

void PlaylistManagerTest::currentPlaylistPersistedByIdAndName()
{
    const quint64 p = m_pl->addPlaylist("Second");
    m_pl->setCurrentPlaylist(p);
    m_pl->save();

    // settings.json stores both the id (exact) and the name (readable).
    QCOMPARE(readSettings().value("playlist.current_id").toVariant().toULongLong(),
             static_cast<qulonglong>(p));
    QCOMPARE(readSettings().value("playlist.current_name").toString(),
             QString("Second"));

    // Renaming the current playlist updates the persisted name...
    m_pl->renamePlaylist(p, "Renamed");
    m_pl->save();
    QCOMPARE(readSettings().value("playlist.current_id").toVariant().toULongLong(),
             static_cast<qulonglong>(p));
    QCOMPARE(readSettings().value("playlist.current_name").toString(),
             QString("Renamed"));

    // ...and load resolves the id back to the same playlist.
    m_pl->reset();
    m_pl->load();
    QCOMPARE(m_pl->currentPlaylistId(), p);
    QCOMPARE(m_pl->currentPlaylistName(), QString("Renamed"));
}

void PlaylistManagerTest::loadResolvesCurrentPlaylistById()
{
    // Two playlists share a name: only the id in settings.json can decide
    // which one is current.
    const quint64 first = m_pl->addPlaylist("Mix");
    const quint64 second = m_pl->addPlaylist("Mix");
    m_pl->setCurrentPlaylist(second);
    m_pl->save();

    m_pl->reset();
    m_pl->load();
    QCOMPARE(m_pl->currentPlaylistId(), second);

    // A stored id that matches nothing falls back to the stored name
    // (first playlist with that name wins).
    QJsonObject obj;
    obj.insert("playlist.current_id", 99);
    obj.insert("playlist.current_name", "Mix");
    QFile f(m_dir->path() + "/settings.json");
    f.open(QIODevice::WriteOnly);
    f.write(QJsonDocument(obj).toJson());
    f.close();
    SettingsManager::getInstance()->load();  // the manager reads the store

    m_pl->load();
    QCOMPARE(m_pl->currentPlaylistId(), first);
}

void PlaylistManagerTest::loadResolvesCurrentPlaylistByName()
{
    m_pl->addPlaylist("First Mix");
    const quint64 b = m_pl->addPlaylist("Second Mix");

    // Hand-write settings.json (no save() involved): load must pick the
    // playlist by its stored name.
    QJsonObject obj;
    obj.insert("playlist.current_name", "Second Mix");
    QFile f(m_dir->path() + "/settings.json");
    f.open(QIODevice::WriteOnly);
    f.write(QJsonDocument(obj).toJson());
    f.close();
    SettingsManager::getInstance()->load();  // the manager reads the store

    m_pl->load();
    QCOMPARE(m_pl->currentPlaylistId(), b);

    // A name that matches nothing falls back to the first playlist. The
    // default "Playlist" (id 1) exists in memory only — this test never
    // persisted it — so load() resolves against the persisted playlists
    // and the fallback is the first of those.
    obj.insert("playlist.current_name", "Gone");
    f.open(QIODevice::WriteOnly);
    f.write(QJsonDocument(obj).toJson());
    f.close();
    SettingsManager::getInstance()->load();  // the manager reads the store

    m_pl->load();
    QCOMPARE(m_pl->currentPlaylistId(), 2ULL);
}

void PlaylistManagerTest::sessionStateWrittenOnChange()
{
    const quint64 def = m_pl->currentPlaylistId();
    m_pl->addItems({ makeItem(1, "A").id, makeItem(2, "B").id });
    m_pl->setCurrentIndex(1);

    const quint64 p = m_pl->addPlaylist("Second");
    m_pl->setCurrentPlaylist(p);
    m_pl->addItems({ makeItem(3, "C").id, makeItem(4, "D").id });

    // No save() anywhere: every change wrote settings.json directly.
    const QJsonObject obj = readSettings();
    QCOMPARE(obj.value("playlist.current_id").toVariant().toULongLong(),
             static_cast<qulonglong>(p));
    QCOMPARE(obj.value("playlist.current_name").toString(), QString("Second"));
    const QJsonObject indices = obj.value("playlist.indexes").toObject();
    QCOMPARE(indices.value(QString::number(def)).toInt(), 1);
    QCOMPARE(indices.value(QString::number(p)).toInt(), 0);
}

void PlaylistManagerTest::currentIndexLoadIgnoresStaleAndOutOfRange()
{
    m_pl->addItems({ makeItem(1, "A").id, makeItem(2, "B").id });
    const quint64 p2 = m_pl->addPlaylist("P2");  // empty
    m_pl->save();

    // Hand-written settings.json: a valid index, an out-of-range one, and
    // one for a playlist that does not exist.
    QJsonObject indices;
    indices.insert("1", 1);   // valid
    indices.insert("2", 99);  // out of range: P2 has no rows
    indices.insert("99", 0);  // unknown playlist
    QJsonObject obj;
    obj.insert("playlist.current_id", 1);
    obj.insert("playlist.current_name", "Playlist");
    obj.insert("playlist.indexes", indices);
    QFile f(m_dir->path() + "/settings.json");
    f.open(QIODevice::WriteOnly);
    f.write(QJsonDocument(obj).toJson());
    f.close();
    SettingsManager::getInstance()->load();  // the manager reads the store

    m_pl->load();
    QCOMPARE(m_pl->currentIndex(), 1);

    // The out-of-range entry stays at the default (the stale one names a
    // playlist that load() does not build, so it is ignored by absence).
    m_pl->setCurrentPlaylist(p2);
    QCOMPARE(m_pl->currentIndex(), -1);
}

void PlaylistManagerTest::removePlaylistDropsIndexEntry()
{
    m_pl->addItems({ makeItem(1, "A").id, makeItem(2, "B").id });
    m_pl->setCurrentIndex(1);

    const quint64 p = m_pl->addPlaylist("Second");
    m_pl->setCurrentPlaylist(p);
    m_pl->addItems({ makeItem(3, "C").id });

    QJsonObject indices = readSettings().value("playlist.indexes").toObject();
    QCOMPARE(indices.value(QString::number(p)).toInt(), 0);

    // Removing the current playlist switches to the first one and rewrites
    // the state without the removed playlist's entry.
    QVERIFY(m_pl->removePlaylist(p));
    indices = readSettings().value("playlist.indexes").toObject();
    QCOMPARE(indices.size(), 1);
    QCOMPARE(indices.value(QString::number(1)).toInt(), 1);
    QCOMPARE(readSettings().value("playlist.current_id").toVariant().toULongLong(),
             1ULL);
}

void PlaylistManagerTest::databaseHoldsNoAppState()
{
    m_pl->addPlaylist("Second");
    m_pl->save();

    // The database is content only (plus the schema_version record): the
    // old app_state table is gone.
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "doob_state_check");
    db.setDatabaseName(m_dir->path() + "/doob.db");
    QVERIFY(db.open());

    QStringList tables;
    {
        QSqlQuery q(db);
        q.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name"));
        while (q.next())
            tables << q.value(0).toString();
    }
    db.close();
    QSqlDatabase::removeDatabase("doob_state_check");

    const QStringList expected{ "playlist_items", "playlists", "schema_version" };
    QCOMPARE(tables, expected);
}

void PlaylistManagerTest::startsWithDefaultPlaylist()
{
    QCOMPARE(m_pl->playlistCount(), 1);
    QCOMPARE(m_pl->currentPlaylistId(), 1ULL);
    QCOMPARE(m_pl->currentPlaylistName(), QString("Playlist"));
}

void PlaylistManagerTest::addPlaylistCreatesPlaylistWithUniqueIds()
{
    QSignalSpy spy(m_pl, &PlaylistManager::playlistsChanged);

    const quint64 a = m_pl->addPlaylist("A");
    const quint64 b = m_pl->addPlaylist("B");

    QCOMPARE(m_pl->playlistCount(), 3);
    QVERIFY(a != b);
    QVERIFY(a != 1);
    QVERIFY(b != 1);
    QCOMPARE(m_pl->playlist(a).name, QString("A"));
    QCOMPARE(m_pl->playlist(b).name, QString("B"));
    QCOMPARE(spy.count(), 2);
}

void PlaylistManagerTest::addPlaylistDoesNotChangeCurrent()
{
    const quint64 def = m_pl->currentPlaylistId();
    m_pl->addPlaylist("A");
    QCOMPARE(m_pl->currentPlaylistId(), def);
}

void PlaylistManagerTest::removePlaylistRemovesPlaylist()
{
    const quint64 a = m_pl->addPlaylist("A");

    QSignalSpy spy(m_pl, &PlaylistManager::playlistsChanged);
    QVERIFY(m_pl->removePlaylist(a));

    QCOMPARE(m_pl->playlistCount(), 1);
    QCOMPARE(m_pl->playlistIndexOf(a), -1);
    QCOMPARE(spy.count(), 1);
}

void PlaylistManagerTest::removingCurrentPlaylistSwitchesToFirst()
{
    const quint64 def = m_pl->currentPlaylistId();
    const quint64 a = m_pl->addPlaylist("A");
    m_pl->setCurrentPlaylist(a);

    QSignalSpy spy(m_pl, &PlaylistManager::currentPlaylistChanged);
    m_pl->removePlaylist(a);

    QCOMPARE(m_pl->currentPlaylistId(), def);
    QCOMPARE(spy.count(), 1);
}

void PlaylistManagerTest::removingLastPlaylistCreatesNewEmptyOne()
{
    const quint64 def = m_pl->currentPlaylistId();

    QVERIFY(m_pl->removePlaylist(def));

    QCOMPARE(m_pl->playlistCount(), 1);
    QVERIFY(m_pl->currentPlaylistId() != def);
    QCOMPARE(m_pl->count(), 0);
    QCOMPARE(m_pl->currentIndex(), -1);
}

void PlaylistManagerTest::renamePlaylistRenames()
{
    const quint64 a = m_pl->addPlaylist("A");

    QVERIFY(m_pl->renamePlaylist(a, "B"));
    QCOMPARE(m_pl->playlist(a).name, QString("B"));
    QVERIFY(!m_pl->renamePlaylist(999, "X"));
}

void PlaylistManagerTest::setCurrentPlaylistSwitches()
{
    QSignalSpy spy(m_pl, &PlaylistManager::currentPlaylistChanged);
    const quint64 def = m_pl->currentPlaylistId();
    const quint64 a = m_pl->addPlaylist("A");
    m_pl->setCurrentPlaylist(a);
    m_pl->addItems({ makeItem(1, "A1").id });
    m_pl->setCurrentPlaylist(def);

    QCOMPARE(m_pl->currentPlaylistId(), def);
    QCOMPARE(m_pl->count(), 0);
    QCOMPARE(spy.count(), 2);

    m_pl->setCurrentPlaylist(a);
    QCOMPARE(m_pl->currentPlaylistId(), a);
    QCOMPARE(m_pl->count(), 1);
    QCOMPARE(m_pl->itemIds().at(0), 1ULL);
}

void PlaylistManagerTest::setCurrentPlaylistNoOpForUnknownId()
{
    QSignalSpy spy(m_pl, &PlaylistManager::currentPlaylistChanged);
    const quint64 def = m_pl->currentPlaylistId();

    m_pl->setCurrentPlaylist(999);
    QCOMPARE(m_pl->currentPlaylistId(), def);
    QCOMPARE(spy.count(), 0);

    m_pl->setCurrentPlaylist(def);
    QCOMPARE(spy.count(), 0);
}

void PlaylistManagerTest::nextPlaylistWalksInOrderAndWraps()
{
    const quint64 def = m_pl->currentPlaylistId();
    const quint64 a = m_pl->addPlaylist("A");
    const quint64 b = m_pl->addPlaylist("B");

    QSignalSpy spy(m_pl, &PlaylistManager::currentPlaylistChanged);
    m_pl->nextPlaylist();
    QCOMPARE(m_pl->currentPlaylistId(), a);
    m_pl->nextPlaylist();
    QCOMPARE(m_pl->currentPlaylistId(), b);
    // Wraps back to the first.
    m_pl->nextPlaylist();
    QCOMPARE(m_pl->currentPlaylistId(), def);
    QCOMPARE(spy.count(), 3);
}

void PlaylistManagerTest::previousPlaylistWalksInReverseAndWraps()
{
    const quint64 def = m_pl->currentPlaylistId();
    const quint64 a = m_pl->addPlaylist("A");
    const quint64 b = m_pl->addPlaylist("B");
    m_pl->setCurrentPlaylist(b);

    m_pl->previousPlaylist();
    QCOMPARE(m_pl->currentPlaylistId(), a);
    m_pl->previousPlaylist();
    QCOMPARE(m_pl->currentPlaylistId(), def);
    // Wraps to the last.
    m_pl->previousPlaylist();
    QCOMPARE(m_pl->currentPlaylistId(), b);
}

void PlaylistManagerTest::nextPreviousPlaylistNoOpWithSinglePlaylist()
{
    const quint64 def = m_pl->currentPlaylistId();
    QSignalSpy spy(m_pl, &PlaylistManager::currentPlaylistChanged);

    m_pl->nextPlaylist();
    m_pl->previousPlaylist();

    QCOMPARE(m_pl->currentPlaylistId(), def);
    QCOMPARE(spy.count(), 0);
}

void PlaylistManagerTest::playlistsKeepOwnCurrentIndex()
{
    const quint64 def = m_pl->currentPlaylistId();
    m_pl->addItems({ makeItem(1, "A").id, makeItem(2, "B").id });
    m_pl->setCurrentIndex(1);

    const quint64 p = m_pl->addPlaylist("P");
    m_pl->setCurrentPlaylist(p);
    m_pl->addItems({ makeItem(3, "C").id, makeItem(4, "D").id });
    QCOMPARE(m_pl->currentIndex(), 0);
    m_pl->setCurrentIndex(1);

    m_pl->setCurrentPlaylist(def);
    QCOMPARE(m_pl->currentIndex(), 1);

    m_pl->setCurrentPlaylist(p);
    QCOMPARE(m_pl->currentIndex(), 1);
}

void PlaylistManagerTest::resetRestoresDefaultState()
{
    m_pl->addItems({ makeItem(1, "A").id });
    m_pl->addPlaylist("X");

    m_pl->reset();

    QCOMPARE(m_pl->playlistCount(), 1);
    QCOMPARE(m_pl->currentPlaylistId(), 1ULL);
    QCOMPARE(m_pl->count(), 0);
    QCOMPARE(m_pl->currentIndex(), -1);
}

void PlaylistManagerTest::waitForImport()
{
    int waited = 0;
    while (m_pl->importing() && waited < 10000) {
        QTest::qWait(25);
        waited += 25;
    }
    QVERIFY(!m_pl->importing());
}

void PlaylistManagerTest::importUnknownFileCreatesStandaloneItem()
{
    const QUrl file = makeTaggedFile(m_dir->path(), "standalone.flac",
                                     "Solo Title", "Solo Artist", "Solo Album");
    if (file.isEmpty())
        QSKIP("ffmpeg not available");

    m_pl->importSources(m_pl->currentPlaylistId(), { file });
    waitForImport();

    QCOMPARE(m_pl->count(), 1);
    const quint64 storeId = m_pl->itemIds().at(0);
    QCOMPARE(m_lib->count(), 0);  // the library is not polluted
    QCOMPARE(m_store->count(), 1);
    QCOMPARE(m_store->item(storeId).title, QString("Solo Title"));
    QCOMPARE(m_store->item(storeId).artist, QString("Solo Artist"));
    QCOMPARE(m_store->item(storeId).album, QString("Solo Album"));
    QCOMPARE(MediaItemStore::resolve(storeId).title, QString("Solo Title"));

    // The import saved both databases; the row and the standalone item
    // survive a restart.
    m_pl->reset();
    m_store->clear();
    m_pl->load();
    m_store->load();
    QCOMPARE(m_pl->count(), 1);
    QCOMPARE(m_pl->itemIds(), QList<quint64>({ storeId }));
    QCOMPARE(m_pl->itemAt(0).title, QString("Solo Title"));
}

void PlaylistManagerTest::importLibrarySourceReusesLibraryItem()
{
    const QUrl file = makeTaggedFile(m_dir->path(), "lib.mp3",
                                     "Old Title", "Old Artist", "Old Album");
    if (file.isEmpty())
        QSKIP("ffmpeg not available");

    QSignalSpy addedSpy(m_lib, &LibraryManager::itemAdded);
    m_lib->addFile(file);
    QVERIFY(addedSpy.wait(10000));
    test::waitForIdle(m_lib);
    QCOMPARE(m_lib->count(), 1);
    const quint64 libId = m_lib->ids().at(0);

    // Re-tag the file after the library import: an import that re-read the
    // file would pick up the new title.
    const QUrl retagged = makeTaggedFile(m_dir->path(), "lib.mp3",
                                         "New Title", "New Artist", "New Album");
    QVERIFY(retagged.isValid());

    m_pl->importSources(m_pl->currentPlaylistId(), { file });
    waitForImport();

    QCOMPARE(m_pl->count(), 1);
    QCOMPARE(m_pl->itemIds(), QList<quint64>({ libId }));
    QCOMPARE(m_store->count(), 1);  // the library item lives in the store
    QVERIFY(m_store->item(libId).inLibrary);
    QCOMPARE(m_pl->itemAt(0).title, QString("Old Title"));  // not re-read
}

void PlaylistManagerTest::importAppendsDuplicateRows()
{
    const QUrl file = makeTaggedFile(m_dir->path(), "dup.flac",
                                     "Dup", "Dup Artist", "Dup Album");
    if (file.isEmpty())
        QSKIP("ffmpeg not available");

    // A first import creates the standalone item.
    m_pl->importSources(m_pl->currentPlaylistId(), { file });
    waitForImport();
    QCOMPARE(m_pl->count(), 1);
    const quint64 storeId = m_pl->itemIds().at(0);
    const int storeCount = m_store->count();

    // Re-importing appends another row: a source list may repeat an item
    // (an external playlist, a re-run of the same folder), and the
    // repetition is kept. A second playlist gets the same item.
    const quint64 otherId = m_pl->addPlaylist("Other");
    m_pl->importSources(m_pl->currentPlaylistId(), { file });
    waitForImport();
    QCOMPARE(m_pl->itemIds(), QList<quint64>({ storeId, storeId }));
    m_pl->importSources(otherId, { file });
    waitForImport();
    QCOMPARE(m_pl->playlist(otherId).items, QList<quint64>({ storeId }));
    QCOMPARE(m_store->count(), storeCount);  // idempotent: no new item
}

void PlaylistManagerTest::importIsSerialized()
{
    const QUrl a = makeTaggedFile(m_dir->path(), "s1.flac", "S1", "A", "B");
    const QUrl b = makeTaggedFile(m_dir->path(), "s2.flac", "S2", "A", "B");
    if (a.isEmpty() || b.isEmpty())
        QSKIP("ffmpeg not available");

    QSignalSpy finishedSpy(m_pl, &PlaylistManager::importFinished);

    m_pl->importSources(m_pl->currentPlaylistId(), { a, b });
    QVERIFY(m_pl->importing());
    // A second import while one is running is refused.
    m_pl->importSources(m_pl->currentPlaylistId(), { a, b });
    QVERIFY(m_pl->importing());
    waitForImport();

    QCOMPARE(m_pl->count(), 2);
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.at(0).at(0).toULongLong(), m_pl->currentPlaylistId());
}

void PlaylistManagerTest::importOrderFollowsSourceOrder()
{
    const QUrl a = makeTaggedFile(m_dir->path(), "o1.flac", "O1", "A", "B");
    const QUrl b = makeTaggedFile(m_dir->path(), "o2.flac", "O2", "A", "B");
    const QUrl c = makeTaggedFile(m_dir->path(), "o3.flac", "O3", "A", "B");
    if (a.isEmpty() || b.isEmpty() || c.isEmpty())
        QSKIP("ffmpeg not available");

    QSignalSpy finishedSpy(m_pl, &PlaylistManager::importFinished);

    // Out-of-name order: the rows must follow the given source order.
    m_pl->importSources(m_pl->currentPlaylistId(), { c, a, b });
    waitForImport();

    QCOMPARE(m_pl->count(), 3);
    QCOMPARE(MediaItemStore::resolve(m_pl->itemIds().at(0)).title, QString("O3"));
    QCOMPARE(MediaItemStore::resolve(m_pl->itemIds().at(1)).title, QString("O1"));
    QCOMPARE(MediaItemStore::resolve(m_pl->itemIds().at(2)).title, QString("O2"));

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.at(0).at(0).toULongLong(), m_pl->currentPlaylistId());

    // An unknown playlist id is a no-op.
    m_pl->importSources(9999, { a });
    QVERIFY(!m_pl->importing());
    QCOMPARE(m_pl->count(), 3);
    QCOMPARE(finishedSpy.count(), 1);
}

void PlaylistManagerTest::importProviderSourceCreatesPluginItem()
{
    const QUrl source = QUrl("https://music.example.com/watch?v=1");
    m_provider.m_urls << source.toString();
    TrackMeta meta;
    meta.title = QStringLiteral("Streamed");
    meta.artist = QStringLiteral("Streamer");
    m_provider.m_meta = meta;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    m_pl->importSources(m_pl->currentPlaylistId(), { source });
    waitForImport();

    QCOMPARE(m_pl->count(), 1);
    const quint64 storeId = m_pl->itemIds().at(0);
    QCOMPARE(m_store->count(), 1);
    const MediaItem item = m_store->item(storeId);
    QCOMPARE(item.kind, MediaItem::Plugin);
    QCOMPARE(item.provider, m_provider.id());
    QCOMPARE(item.title, QString("Streamed"));
    QCOMPARE(item.artist, QString("Streamer"));
    // The source is stored as pasted (never rewritten).
    QCOMPARE(item.source, source);
    QCOMPARE(m_lib->count(), 0);  // the library is not polluted

    // Re-importing appends another row of the same item.
    m_pl->importSources(m_pl->currentPlaylistId(), { source });
    waitForImport();
    QCOMPARE(m_pl->itemIds(), QList<quint64>({ storeId, storeId }));
    QCOMPARE(m_store->count(), 1);
}

void PlaylistManagerTest::importSkipsFailedProviderFetch()
{
    // A failed provider fetch (an empty TrackMeta) contributes no row
    // and no item — never an empty-metadata row.
    const QUrl source = QUrl("https://music.example.com/watch?v=8");
    m_provider.m_urls << source.toString();
    m_provider.m_meta = TrackMeta();
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    m_pl->importSources(m_pl->currentPlaylistId(), { source });
    waitForImport();

    QCOMPARE(m_pl->count(), 0);
    QCOMPARE(m_store->count(), 0);
}

void PlaylistManagerTest::importRemoteStreamUrlCreatesRemoteItem()
{
    const QUrl source = QUrl("https://example.com/audio/t.mp3");

    m_pl->importSources(m_pl->currentPlaylistId(), { source });
    waitForImport();

    QCOMPARE(m_pl->count(), 1);
    const MediaItem item = m_store->item(m_pl->itemIds().at(0));
    QCOMPARE(item.kind, MediaItem::Remote);
    QVERIFY(item.provider.isEmpty());
    QCOMPARE(item.source, source);
    // No metadata to read: the title falls back to host + path.
    QCOMPARE(item.title, QString("example.com/audio/t.mp3"));
}

void PlaylistManagerTest::importMixedSourcesFollowSourceOrder()
{
    const QUrl a = makeTaggedFile(m_dir->path(), "m1.flac", "MA", "A", "B");
    const QUrl b = makeTaggedFile(m_dir->path(), "m2.flac", "MB", "A", "B");
    if (a.isEmpty() || b.isEmpty())
        QSKIP("ffmpeg not available");
    const QUrl p = QUrl("https://music.example.com/watch?v=9");
    m_provider.m_urls << p.toString();
    TrackMeta meta;
    meta.title = QStringLiteral("MP");
    m_provider.m_meta = meta;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));
    const QUrl r = QUrl("https://example.com/audio/m.mp3");

    // local, provider, remote, local: the rows must follow the given
    // source order (local reads on the worker, the provider fetch in
    // cursor order, the remote resolved up front).
    m_pl->importSources(m_pl->currentPlaylistId(), { a, p, r, b });
    waitForImport();

    QCOMPARE(m_pl->count(), 4);
    QCOMPARE(MediaItemStore::resolve(m_pl->itemIds().at(0)).title, QString("MA"));
    QCOMPARE(MediaItemStore::resolve(m_pl->itemIds().at(1)).title, QString("MP"));
    QCOMPARE(MediaItemStore::resolve(m_pl->itemIds().at(2)).title,
               QString("example.com/audio/m.mp3"));
    QCOMPARE(MediaItemStore::resolve(m_pl->itemIds().at(3)).title, QString("MB"));
}

void PlaylistManagerTest::importRejectsUnclaimedNonLocalSource()
{
    QSignalSpy finishedSpy(m_pl, &PlaylistManager::importFinished);

    m_pl->importSources(m_pl->currentPlaylistId(), { QUrl("spotify:track:abc") });
    QVERIFY(!m_pl->importing());  // nothing usable: the import never started

    QCOMPARE(m_pl->count(), 0);
    QCOMPARE(m_store->count(), 0);
    QCOMPARE(finishedSpy.count(), 0);
}

void PlaylistManagerTest::importSkipsSourceWhenProviderVanishes()
{
    // A provider-claimed custom-scheme source (an https one would fall
    // back to a Remote item when the provider is gone).
    const QUrl source = QUrl("music:track:123");
    m_provider.m_urls << source.toString();
    m_provider.m_defer = true;  // hold the fetch until we say so
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    m_pl->importSources(m_pl->currentPlaylistId(), { source });
    QVERIFY(m_pl->importing());
    QCOMPARE(m_provider.m_pending.size(), 1);

    // The plugin is removed before the fetch lands: the source cannot be
    // read anymore (it is neither http nor claimed) and contributes no row.
    SourceManager::getInstance()->unregisterProvider(&m_provider);
    m_provider.deliver();
    waitForImport();

    QCOMPARE(m_pl->count(), 0);
    QCOMPARE(m_store->count(), 0);
}

void PlaylistManagerTest::importSourcesAtPositionInsertsBlock()
{
    // Sources the store already holds: no file read, the import lands
    // synchronously.
    const MediaItem a = makeItem(1, "A");
    const MediaItem b = makeItem(2, "B");
    const MediaItem c = makeItem(3, "C");
    const MediaItem d = makeItem(4, "D");
    m_pl->addItems({ a.id, b.id, c.id });

    m_pl->importSources(m_pl->currentPlaylistId(), { d.source }, 1);
    waitForImport();

    QCOMPARE(m_pl->itemIds(), QList<quint64>({ a.id, d.id, b.id, c.id }));
}

void PlaylistManagerTest::importSourcesAtPositionTracksCurrentIndex()
{
    const MediaItem a = makeItem(1, "A");
    const MediaItem b = makeItem(2, "B");
    const MediaItem c = makeItem(3, "C");
    const MediaItem d = makeItem(4, "D");
    const MediaItem e = makeItem(5, "E");
    m_pl->addItems({ a.id, b.id, c.id });
    m_pl->setCurrentIndex(1);  // b

    // Two items land at the anchor, one after the other: the current row
    // shifts by the rows that crossed it.
    m_pl->importSources(m_pl->currentPlaylistId(), { d.source, e.source }, 1);
    waitForImport();

    QCOMPARE(m_pl->itemIds(), QList<quint64>({ a.id, d.id, e.id, b.id, c.id }));
    QCOMPARE(m_pl->currentIndex(), 3);  // b moved from 1 to 3
}

void PlaylistManagerTest::importSourcesPastTheEndAppends()
{
    const MediaItem a = makeItem(1, "A");
    const MediaItem b = makeItem(2, "B");
    const MediaItem c = makeItem(3, "C");
    m_pl->addItems({ a.id, b.id });

    m_pl->importSources(m_pl->currentPlaylistId(), { c.source }, 99);
    waitForImport();

    QCOMPARE(m_pl->itemIds(), QList<quint64>({ a.id, b.id, c.id }));
}

void PlaylistManagerTest::v1DatabaseMigratesToV2()
{
    // A v1 database by hand: the playlists component at version 1 (no
    // position/import columns), three playlists in id order.
    const QString path = m_dir->path() + "/migrate.db";
    {
        QSqlDatabase raw = QSqlDatabase::addDatabase("QSQLITE", "pl_migrate_craft");
        raw.setDatabaseName(path);
        QVERIFY(raw.open());
        QSqlQuery q(raw);
        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE schema_version (component TEXT NOT NULL PRIMARY KEY, "
            "version INTEGER NOT NULL)")));
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO schema_version VALUES ('playlists', 1)")));
        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE playlists (id INTEGER PRIMARY KEY, name TEXT NOT NULL)")));
        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE playlist_items (playlist_id INTEGER NOT NULL, "
            "position INTEGER NOT NULL, item_id INTEGER NOT NULL, "
            "PRIMARY KEY (playlist_id, position))")));
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO playlists VALUES (1, 'A'), (2, 'B'), (3, 'C')")));
        raw.close();
        QSqlDatabase::removeDatabase("pl_migrate_craft");
    }

    m_pl->setStoragePath(path);
    m_pl->load();

    // The migration ran: the order is the id order, made explicit.
    QCOMPARE(m_pl->playlistCount(), 3);
    QCOMPARE(m_pl->playlists().at(0).name, QString("A"));
    QCOMPARE(m_pl->playlists().at(1).name, QString("B"));
    QCOMPARE(m_pl->playlists().at(2).name, QString("C"));
    // The import bookkeeping is absent for a v1 playlist.
    for (const Playlist &pl : m_pl->playlists()) {
        QVERIFY(!pl.imported);
        QVERIFY(pl.sourceFile.isEmpty());
        QVERIFY(!pl.dirty);
    }
    // The schema is at v2 now.
    {
        QSqlDatabase probe = QSqlDatabase::addDatabase("QSQLITE", "pl_migrate_probe");
        probe.setDatabaseName(path);
        QVERIFY(probe.open());
        QSqlQuery q(probe);
        q.prepare(QStringLiteral("SELECT version FROM schema_version WHERE component = ?"));
        q.bindValue(0, QStringLiteral("playlists"));
        QVERIFY(q.exec() && q.next());
        QCOMPARE(q.value(0).toInt(), 2);
        probe.close();
        QSqlDatabase::removeDatabase("pl_migrate_probe");
    }
}

void PlaylistManagerTest::movePlaylistReordersAndPersists()
{
    const quint64 a = m_pl->currentPlaylistId();
    const quint64 b = m_pl->addPlaylist("B");
    const quint64 c = m_pl->addPlaylist("C");

    QVERIFY(m_pl->movePlaylist(a, 2));

    QCOMPARE(m_pl->playlists().at(0).id, b);
    QCOMPARE(m_pl->playlists().at(1).id, c);
    QCOMPARE(m_pl->playlists().at(2).id, a);

    // The order is write-through: it survives a restart without save().
    m_pl->setStoragePath(m_dir->path() + "/doob.db");  // closes
    m_pl->load();
    QCOMPARE(m_pl->playlists().at(0).id, b);
    QCOMPARE(m_pl->playlists().at(1).id, c);
    QCOMPARE(m_pl->playlists().at(2).id, a);
}

void PlaylistManagerTest::movePlaylistNoOpForUnknownOrSamePosition()
{
    const quint64 a = m_pl->currentPlaylistId();
    const quint64 b = m_pl->addPlaylist("B");

    QVERIFY(!m_pl->movePlaylist(99, 0));
    QVERIFY(!m_pl->movePlaylist(a, 0));
    QVERIFY(!m_pl->movePlaylist(b, 1));
    QVERIFY(!m_pl->movePlaylist(a, -1));
    QVERIFY(!m_pl->movePlaylist(a, 5));

    QCOMPARE(m_pl->playlists().at(0).id, a);
    QCOMPARE(m_pl->playlists().at(1).id, b);
}

void PlaylistManagerTest::markImportedSetsFlagsAndClearsDirty()
{
    const quint64 def = m_pl->currentPlaylistId();
    const MediaItem a = makeItem(1, "A");
    m_pl->addItems({ a.id });
    QVERIFY(m_pl->playlist(def).dirty);

    m_pl->markImported(def, QStringLiteral("file:///tmp/x.m3u"));

    const Playlist pl = m_pl->playlist(def);
    QVERIFY(pl.imported);
    QCOMPARE(pl.sourceFile, QString("file:///tmp/x.m3u"));
    QVERIFY(!pl.dirty);
}

void PlaylistManagerTest::contentMutationMarksDirty()
{
    const quint64 def = m_pl->currentPlaylistId();
    m_pl->markImported(def, QStringLiteral("file:///tmp/x.m3u"));
    QVERIFY(!m_pl->playlist(def).dirty);

    m_pl->addItems({ makeItem(1, "A").id, makeItem(2, "B").id });
    QVERIFY(m_pl->playlist(def).dirty);

    m_pl->removeAt(0);
    QVERIFY(m_pl->playlist(def).dirty);

    m_pl->addItems({ makeItem(3, "C").id });
    m_pl->moveRows({ 0 }, 1);
    QVERIFY(m_pl->playlist(def).dirty);
}

void PlaylistManagerTest::renameAndCursorDoNotMarkDirty()
{
    const quint64 def = m_pl->currentPlaylistId();
    m_pl->markImported(def, QStringLiteral("file:///tmp/x.m3u"));

    m_pl->renamePlaylist(def, "Renamed");
    QVERIFY(!m_pl->playlist(def).dirty);

    const MediaItem a = makeItem(1, "A");
    m_pl->addItems({ a.id });
    m_pl->setCurrentIndex(0);
    QVERIFY(m_pl->playlist(def).dirty);  // from the add

    // A fresh playlist: cursor moves alone never mark it.
    const quint64 other = m_pl->addPlaylist("Other");
    m_pl->setCurrentPlaylist(other);
    m_pl->setCurrentIndex(5);
    QVERIFY(!m_pl->playlist(other).dirty);
}

void PlaylistManagerTest::dirtySurvivesRestart()
{
    const quint64 def = m_pl->currentPlaylistId();
    m_pl->markImported(def, QStringLiteral("file:///tmp/x.m3u"));
    m_pl->addItems({ makeItem(1, "A").id });

    m_pl->setStoragePath(m_dir->path() + "/doob.db");  // closes
    m_pl->load();

    const Playlist pl = m_pl->playlist(def);
    QVERIFY(pl.imported);
    QCOMPARE(pl.sourceFile, QString("file:///tmp/x.m3u"));
    QVERIFY(pl.dirty);
}

void PlaylistManagerTest::importPlaylistFileCreatesImportedPlaylist()
{
    // Sources the store already holds: the import lands synchronously.
    const MediaItem a = makeItem(1, "A");
    const MediaItem b = makeItem(2, "B");
    const QString path = m_dir->path() + "/road.m3u";
    QVERIFY(writePlaylistFile(path, { a.source, b.source }));

    const int before = m_pl->playlistCount();
    const quint64 id = m_pl->importPlaylistFile(path);
    QVERIFY(id != 0);
    QCOMPARE(m_pl->playlistCount(), before + 1);

    waitForImport();

    const Playlist pl = m_pl->playlist(id);
    QCOMPARE(pl.name, QString("road"));
    QVERIFY(pl.imported);
    QCOMPARE(pl.sourceFile, QUrl::fromLocalFile(path).toString());
    // The import is the content arriving, not a modification of it.
    QVERIFY(!pl.dirty);
    QCOMPARE(pl.items, QList<quint64>({ a.id, b.id }));
}

void PlaylistManagerTest::importPlaylistFileFailsOnUnparseable()
{
    const QString path = m_dir->path() + "/empty.m3u";
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("#EXTM3U\n");
    }

    QCOMPARE(m_pl->importPlaylistFile(path), quint64(0));
    QCOMPARE(m_pl->playlistCount(), 1);  // nothing was created
}

void PlaylistManagerTest::exportPlaylistWritesM3U()
{
    const MediaItem a = makeItem(1, "A");
    const MediaItem b = makeItem(2, "B");
    m_pl->addItems({ a.id, b.id });
    // A dangling id (the store does not hold it) is skipped.
    m_pl->addItems({ 999 });

    const QString out = m_dir->path() + "/out.m3u";
    QVERIFY(m_pl->exportPlaylist(m_pl->currentPlaylistId(), out));

    PlaylistFile pl;
    QVERIFY(parsePlaylistFile(out, pl));
    QCOMPARE(pl.entries, QList<QUrl>({ a.source, b.source }));
}

void PlaylistManagerTest::savePlaylistClearsDirty()
{
    const quint64 def = m_pl->currentPlaylistId();
    const MediaItem a = makeItem(1, "A");
    m_pl->markImported(def, QStringLiteral("file:///tmp/x.m3u"));
    m_pl->addItems({ a.id });
    QVERIFY(m_pl->playlist(def).dirty);

    const QString out = m_dir->path() + "/saved.m3u";
    QVERIFY(m_pl->savePlaylist(def, out));
    QVERIFY(!m_pl->playlist(def).dirty);

    PlaylistFile pl;
    QVERIFY(parsePlaylistFile(out, pl));
    QCOMPARE(pl.entries, QList<QUrl>({ a.source }));

    // The cleared marker persists.
    m_pl->setStoragePath(m_dir->path() + "/doob.db");  // closes
    m_pl->load();
    QVERIFY(!m_pl->playlist(def).dirty);
    QVERIFY(m_pl->playlist(def).imported);
}
}


QTEST_MAIN(PlaylistManagerTest)
#include "playlistmanager_test.moc"
