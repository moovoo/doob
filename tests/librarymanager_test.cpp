#include <QtTest>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <QSqlDatabase>
#include <QSqlQuery>

#include <memory>

#include "librarymanager.h"
#include "sourcemanager.h"
#include "testproviders.h"
#include "taggedfile.h"
#include "testsupport.h"

namespace {

class LibraryManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void addFileCreatesLocalItem();
    void addFileDerivesTitleFromFileName();
    void addFileReadsTagsWhenPresent();
    void addFileCarriesExtendedFields();
    void addFileDeduplicatesByPath();
    void addFileRejectsMissingFile();
    void addFilesAddsOnlyExistingFiles();
    void addFileOfStandaloneItemReusesItem();
    void addFileWithProviderSource();
    void addFileSkipsFailedProviderFetch();
    void addFileSkippedSourceCarriesProviderHint();
    void addFileUsesProvidedMetadataWithoutFetching();
    void addFileProvidedMetadataIsUsedAsIs();
    void addFileTrimsWhitespaceFromMetadata();
    void addFileWithRemoteStreamUrl();
    void addFileRejectsUnclaimedNonLocalSource();
    void scanDirectoryFindsNestedAudio();
    void scanDirectorySkipsUnknownExtensions();
    void syncDirectoryImportsNewFiles();
    void syncDirectoryKeepsExistingItems();
    void syncDirectoryRemovesGoneFiles();
    void syncDirectoryOfMissingDirRemovesItsItems();
    void syncDirectorySkipsUnknownExtensions();
    void syncDirectoryWhileBusyIsRefused();
    void onlyOneOperationAtATime();
    void readMetadataReturnsFileTags();
    void readMetadataSkipsMissingFiles();
    void readMetadataSkipsNonLocalItems();
    void readMetadataWhileBusyIsRefused();
    void readMetadataEmptyIdsIsNoOp();
    void removeItemEmitsAndRemoves();
    void addItemsFlagsStoreItems();
    void addItemsOfLibraryItemsIsNoOp();
    void addItemsSkipsUnknownIds();
    void clearEmitsAndEmpties();
    void saveLoadRoundtrip();
    void saveWritesExtendedColumnsAndMeta();
    void idsContinueAfterLoad();
    void schemaCreatesTablesAndIndexes();
    void findBySourceFindsItem();
    void findBySourceNormalizesRelativePath();
    void idsFollowItemOrder();

private:
    QUrl makeTempFile(const QString &name);

    LibraryManager* m_lib;
    MediaItemStore* m_store;
    std::unique_ptr<QTemporaryDir> m_dir;
    MetaProvider m_provider{this};
};

void LibraryManagerTest::init()
{
    m_lib = LibraryManager::getInstance();
    m_store = MediaItemStore::getInstance();
    m_dir = std::make_unique<QTemporaryDir>();
    // The library allocates ids from the store counter and persists through
    // the store's save; keep the store pointed at the temp dir and empty
    // for the whole test.
    m_store->setStoragePath(m_dir->path() + "/doob.db");
    PlaylistManager::getInstance()->reset();
    m_store->clear();
    m_lib->clear();
    SourceManager::getInstance()->clear();
    m_provider.m_urls.clear();
    m_provider.m_meta = TrackMeta();
    m_provider.m_metaCalls = 0;
}

void LibraryManagerTest::cleanup()
{
    m_lib->clear();
    PlaylistManager::getInstance()->reset();
    m_store->clear();
    SourceManager::getInstance()->clear();
}

QUrl LibraryManagerTest::makeTempFile(const QString &name)
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

void LibraryManagerTest::addFileCreatesLocalItem()
{
    QSignalSpy addedSpy(m_lib, &LibraryManager::itemAdded);

    const QUrl url = makeTempFile("song.mp3");
    m_lib->addFile(url);
    QVERIFY(addedSpy.wait(10000));
    test::waitForIdle(m_lib);

    const MediaItem item = addedSpy.at(0).at(0).value<MediaItem>();
    QVERIFY(item.isValid());
    QCOMPARE(item.kind, MediaItem::Local);
    QCOMPARE(item.source, QUrl::fromLocalFile(m_dir->path() + "/song.mp3"));
    QVERIFY(item.id > 0);
    QVERIFY(item.inLibrary);
    QCOMPARE(m_lib->count(), 1);
    QVERIFY(m_lib->contains(item.id));
    QCOMPARE(m_lib->item(item.id).toVariantMap(), item.toVariantMap());
    QCOMPARE(addedSpy.count(), 1);
}

void LibraryManagerTest::addFileDerivesTitleFromFileName()
{
    const QUrl url = makeTempFile("My_Cool_Song.mp3");
    const MediaItem item = test::addLibraryFileAndWait(m_lib, url);

    QCOMPARE(item.title, QString("My Cool Song"));
}

void LibraryManagerTest::addFileReadsTagsWhenPresent()
{
    const QUrl url = makeTaggedFile(m_dir->path(), "tagged.flac",
                                    "Real Title", "Real Artist", "Real Album");
    if (url.isEmpty())
        QSKIP("ffmpeg not available to generate a tagged file");

    const MediaItem item = test::addLibraryFileAndWait(m_lib, url);

    QCOMPARE(item.title, QString("Real Title"));
    QCOMPARE(item.artist, QString("Real Artist"));
    QCOMPARE(item.album, QString("Real Album"));
    QVERIFY(item.durationMs > 500 && item.durationMs < 2000);
    QVERIFY(item.bitrate > 0);
    QVERIFY(item.sampleRate > 0);
    QVERIFY(item.channels > 0);
}

void LibraryManagerTest::addFileCarriesExtendedFields()
{
    const QUrl url = makeTaggedFileWith(m_dir->path(), "full.flac",
        { { "TITLE", "Full Title" }, { "ARTIST", "Full Artist" },
          { "ALBUMARTIST", "Full Album Artist" }, { "COMPOSER", "Full Composer" },
          { "GENRE", "Jazz" }, { "COMMENT", "A comment" }, { "DATE", "1999" },
          { "TRACKNUMBER", "3/10" }, { "DISCNUMBER", "1/2" }, { "BPM", "128" }, });
    if (url.isEmpty())
        QSKIP("ffmpeg not available to generate a tagged file");

    const MediaItem item = test::addLibraryFileAndWait(m_lib, url);

    QCOMPARE(item.albumArtist, QString("Full Album Artist"));
    QCOMPARE(item.composer, QString("Full Composer"));
    QCOMPARE(item.genre, QString("Jazz"));
    QCOMPARE(item.comment, QString("A comment"));
    QCOMPARE(item.year, 1999);
    QCOMPARE(item.trackNumber, 3);
    QCOMPARE(item.trackTotal, 10);
    QCOMPARE(item.discNumber, 1);
    QCOMPARE(item.discTotal, 2);
    QCOMPARE(item.extra.value("BPM"), QString("128"));
    QVERIFY(!item.extra.contains("TITLE"));
}

void LibraryManagerTest::addFileDeduplicatesByPath()
{
    const QUrl url = makeTempFile("dup.mp3");
    const MediaItem first = test::addLibraryFileAndWait(m_lib, url);

    // A second add of the same source is rejected before the worker thread
    // is even involved: no signal, no busy period.
    QSignalSpy addedSpy(m_lib, &LibraryManager::itemAdded);
    m_lib->addFile(url);
    QCoreApplication::processEvents();
    QCOMPARE(addedSpy.count(), 0);
    QVERIFY(!m_lib->busy());
    QCOMPARE(m_lib->count(), 1);
    QVERIFY(m_lib->contains(first.id));
}

void LibraryManagerTest::addFileRejectsMissingFile()
{
    QSignalSpy addedSpy(m_lib, &LibraryManager::itemAdded);

    m_lib->addFile(QUrl::fromLocalFile(m_dir->path() + "/does_not_exist.mp3"));
    QCoreApplication::processEvents();

    QCOMPARE(m_lib->count(), 0);
    QCOMPARE(addedSpy.count(), 0);
    QVERIFY(!m_lib->busy());
}

void LibraryManagerTest::addFilesAddsOnlyExistingFiles()
{
    const QUrl a = makeTempFile("a.mp3");
    const QUrl b = makeTempFile("b.flac");
    const QUrl missing = QUrl::fromLocalFile(m_dir->path() + "/missing.ogg");

    QSignalSpy addedSpy(m_lib, &LibraryManager::itemAdded);
    m_lib->addFiles({ a, b, missing });

    QTRY_COMPARE_WITH_TIMEOUT(addedSpy.count(), 2, 10000);
    test::waitForIdle(m_lib);
    QCOMPARE(addedSpy.count(), 2);
    QCOMPARE(m_lib->count(), 2);
    QVERIFY(m_lib->contains(addedSpy.at(0).at(0).value<MediaItem>().id));
    QVERIFY(m_lib->contains(addedSpy.at(1).at(0).value<MediaItem>().id));
}

void LibraryManagerTest::addFileOfStandaloneItemReusesItem()
{
    // A file the store already holds without the library flag (imported
    // into a playlist before) must not create a second item: adding it to
    // the library flags the existing item under the same id.
    const QUrl url = makeTempFile("shared.mp3");
    TrackMeta meta;
    meta.title = QStringLiteral("Standalone");
    const MediaItem solo = m_store->ensureItem(url, meta);
    QVERIFY(solo.isValid());
    QCOMPARE(m_lib->count(), 0);

    const MediaItem item = test::addLibraryFileAndWait(m_lib, url);

    QCOMPARE(item.id, solo.id);
    QCOMPARE(item.title, QString("Standalone"));
    QCOMPARE(m_lib->count(), 1);
    QVERIFY(m_lib->contains(item.id));
    QVERIFY(m_store->item(item.id).inLibrary);
    QCOMPARE(m_store->count(), 1);
}

void LibraryManagerTest::addFileWithProviderSource()
{
    const QUrl source = QUrl("https://music.example.com/watch?v=1");
    m_provider.m_urls << source.toString();
    TrackMeta meta;
    meta.title = QStringLiteral("Streamed");
    meta.artist = QStringLiteral("Streamer");
    m_provider.m_meta = meta;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    const MediaItem item = test::addLibraryFileAndWait(m_lib, source);

    QVERIFY(item.isValid());
    QCOMPARE(item.kind, MediaItem::Plugin);
    QCOMPARE(item.provider, m_provider.id());
    QCOMPARE(item.title, QString("Streamed"));
    QCOMPARE(item.artist, QString("Streamer"));
    // The source is stored as pasted (never rewritten).
    QCOMPARE(item.source, source);
    QVERIFY(item.inLibrary);
    QCOMPARE(m_lib->count(), 1);
}

void LibraryManagerTest::addFileSkipsFailedProviderFetch()
{
    // A failed provider fetch (an empty TrackMeta) contributes no item —
    // a re-add can retry it, and the operation finishes (never an
    // empty-metadata item, never a stuck spinner).
    const QUrl source = QUrl("https://music.example.com/watch?v=7");
    m_provider.m_urls << source.toString();
    m_provider.m_meta = TrackMeta();  // the fetch fails
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    QSignalSpy addedSpy(m_lib, &LibraryManager::itemAdded);
    QSignalSpy skippedSpy(m_lib, &LibraryManager::sourceSkipped);
    m_lib->addFile(source);
    test::waitForIdle(m_lib);

    QCOMPARE(addedSpy.count(), 0);
    // The skip is reported (the provider gives no specific reason).
    QCOMPARE(skippedSpy.count(), 1);
    QCOMPARE(skippedSpy.first().at(0).toUrl(), source);
    QVERIFY(skippedSpy.first().at(1).toString().isEmpty());
    QCOMPARE(m_lib->count(), 0);
    QCOMPARE(m_store->count(), 0);
}

void LibraryManagerTest::addFileSkippedSourceCarriesProviderHint()
{
    // The skip carries the provider's failure hint (a not-logged-in
    // provider's sign-in hint), so the UI can explain the no-op.
    const QUrl source = QUrl("https://music.example.com/watch?v=77");
    m_provider.m_urls << source.toString();
    m_provider.m_meta = TrackMeta();  // the fetch fails
    m_provider.m_hint = QStringLiteral("Sign in first.");
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    QSignalSpy skippedSpy(m_lib, &LibraryManager::sourceSkipped);
    m_lib->addFile(source);
    test::waitForIdle(m_lib);

    QCOMPARE(skippedSpy.count(), 1);
    QCOMPARE(skippedSpy.first().at(0).toUrl(), source);
    QCOMPARE(skippedSpy.first().at(1).toString(),
             QStringLiteral("Sign in first."));
    QCOMPARE(m_lib->count(), 0);
}

void LibraryManagerTest::addFileUsesProvidedMetadataWithoutFetching()
{
    // Metadata the caller already holds (the import wizard's list
    // listing) is imported straight from it — the provider is not asked
    // to fetch again, even if its fetch would fail.
    const QUrl source = QUrl("https://music.example.com/watch?v=8");
    m_provider.m_urls << source.toString();
    m_provider.m_meta = TrackMeta();  // the fetch would fail
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    TrackMeta provided;
    provided.title = QStringLiteral("From The List");
    provided.artist = QStringLiteral("Lister");
    provided.durationMs = 133000;
    QHash<QString, TrackMeta> metadata;
    metadata.insert(source.toString(), provided);

    const MediaItem item = test::addLibraryFileAndWait(m_lib, source, metadata);

    QVERIFY(item.isValid());
    QCOMPARE(item.kind, MediaItem::Plugin);
    QCOMPARE(item.title, QString("From The List"));
    QCOMPARE(item.artist, QString("Lister"));
    QCOMPARE(item.durationMs, 133000);
    QCOMPARE(m_lib->count(), 1);
    QCOMPARE(m_provider.m_metaCalls, 0);
}

void LibraryManagerTest::addFileProvidedMetadataIsUsedAsIs()
{
    // The provided metadata is used as-is: a source with it is not
    // fetched again, so the provider's (fresher) answer never wins.
    // Without provided metadata, a source is fetched as usual.
    const QUrl source = QUrl("https://music.example.com/watch?v=9");
    m_provider.m_urls << source.toString();
    TrackMeta fresh;
    fresh.title = QStringLiteral("Fresh");
    m_provider.m_meta = fresh;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    TrackMeta provided;
    provided.title = QStringLiteral("Listed");
    QHash<QString, TrackMeta> metadata;
    metadata.insert(source.toString(), provided);

    const MediaItem item = test::addLibraryFileAndWait(m_lib, source, metadata);

    QVERIFY(item.isValid());
    QCOMPARE(item.title, QString("Listed"));
    QCOMPARE(m_lib->count(), 1);
    QCOMPARE(m_provider.m_metaCalls, 0);
}

void LibraryManagerTest::addFileTrimsWhitespaceFromMetadata()
{
    // Every import path trims the metadata fields of surrounding
    // whitespace (the provider's answer goes through the same item
    // building as the worker's file reads).
    const QUrl source = QUrl("https://music.example.com/watch?v=10");
    m_provider.m_urls << source.toString();
    TrackMeta meta;
    meta.title = QStringLiteral("  Padded Title  ");
    meta.artist = QStringLiteral("\tPadded Artist\n");
    meta.album = QStringLiteral("  Padded Album  ");
    m_provider.m_meta = meta;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    const MediaItem item = test::addLibraryFileAndWait(m_lib, source);

    QVERIFY(item.isValid());
    QCOMPARE(item.title, QString("Padded Title"));
    QCOMPARE(item.artist, QString("Padded Artist"));
    QCOMPARE(item.album, QString("Padded Album"));
    QCOMPARE(m_lib->count(), 1);
}

void LibraryManagerTest::addFileWithRemoteStreamUrl()
{
    const QUrl source = QUrl("https://example.com/audio/t.mp3");

    const MediaItem item = test::addLibraryFileAndWait(m_lib, source);

    QVERIFY(item.isValid());
    QCOMPARE(item.kind, MediaItem::Remote);
    QVERIFY(item.provider.isEmpty());
    QCOMPARE(item.source, source);
    // No metadata to read: the title falls back to host + path.
    QCOMPARE(item.title, QString("example.com/audio/t.mp3"));
}

void LibraryManagerTest::addFileRejectsUnclaimedNonLocalSource()
{
    QSignalSpy addedSpy(m_lib, &LibraryManager::itemAdded);

    m_lib->addFile(QUrl("spotify:track:abc"));
    QTest::qWait(100);

    QCOMPARE(addedSpy.count(), 0);
    QVERIFY(!m_lib->busy());
    QCOMPARE(m_store->count(), 0);
}

void LibraryManagerTest::scanDirectoryFindsNestedAudio()
{
    QVERIFY(QDir(m_dir->path()).mkdir("sub"));
    QVERIFY(QDir(m_dir->path() + "/sub").mkdir("deep"));

    const QStringList names = { "one.mp3", "sub/two.flac", "sub/deep/three.OGG", "four.wav" };
    for (const QString &name : names)
        makeTempFile(name);

    QSignalSpy addedSpy(m_lib, &LibraryManager::itemAdded);
    m_lib->scanDirectory(QUrl::fromLocalFile(m_dir->path()));

    // One wait for all four: a single processEvents round drains every
    // posted event, so "one signal per wait" is a race (the worker can
    // queue several before the loop wakes).
    QTRY_COMPARE_WITH_TIMEOUT(addedSpy.count(), 4, 10000);
    test::waitForIdle(m_lib);
    QCOMPARE(addedSpy.count(), 4);
    QCOMPARE(m_lib->count(), 4);
}

void LibraryManagerTest::scanDirectorySkipsUnknownExtensions()
{
    makeTempFile("notes.txt");
    makeTempFile("video.mp4");
    makeTempFile("audio.mp3");

    QSignalSpy addedSpy(m_lib, &LibraryManager::itemAdded);
    m_lib->scanDirectory(QUrl::fromLocalFile(m_dir->path()));

    QVERIFY(addedSpy.wait(10000));
    test::waitForIdle(m_lib);
    QCOMPARE(addedSpy.count(), 1);
    QCOMPARE(addedSpy.at(0).at(0).value<MediaItem>().title, QString("audio"));
}

void LibraryManagerTest::syncDirectoryImportsNewFiles()
{
    makeTempFile("one.mp3");
    makeTempFile("two.flac");
    makeTempFile("notes.txt");

    QSignalSpy addedSpy(m_lib, &LibraryManager::itemAdded);
    m_lib->syncDirectory(QUrl::fromLocalFile(m_dir->path()));

    QTRY_COMPARE_WITH_TIMEOUT(addedSpy.count(), 2, 10000);
    test::waitForIdle(m_lib);
    QCOMPARE(m_lib->count(), 2);
}

void LibraryManagerTest::syncDirectoryKeepsExistingItems()
{
    const MediaItem kept = test::addLibraryFileAndWait(m_lib, makeTempFile("kept.mp3"));
    makeTempFile("fresh.wav");

    QSignalSpy addedSpy(m_lib, &LibraryManager::itemAdded);
    QSignalSpy removedSpy(m_lib, &LibraryManager::itemRemoved);
    m_lib->syncDirectory(QUrl::fromLocalFile(m_dir->path()));

    QTRY_COMPARE_WITH_TIMEOUT(addedSpy.count(), 1, 10000);
    test::waitForIdle(m_lib);
    QCOMPARE(removedSpy.count(), 0);
    QCOMPARE(m_lib->count(), 2);
    QVERIFY(m_lib->contains(kept.id));
    QCOMPARE(addedSpy.at(0).at(0).value<MediaItem>().title, QString("fresh"));
}

void LibraryManagerTest::syncDirectoryRemovesGoneFiles()
{
    const MediaItem gone = test::addLibraryFileAndWait(m_lib, makeTempFile("gone.mp3"));
    const MediaItem kept = test::addLibraryFileAndWait(m_lib, makeTempFile("kept.mp3"));
    QVERIFY(QFile::remove(m_dir->path() + "/gone.mp3"));

    QSignalSpy removedSpy(m_lib, &LibraryManager::itemRemoved);
    m_lib->syncDirectory(QUrl::fromLocalFile(m_dir->path()));

    QTRY_COMPARE_WITH_TIMEOUT(removedSpy.count(), 1, 10000);
    test::waitForIdle(m_lib);
    QCOMPARE(removedSpy.at(0).at(0).value<MediaItem>().id, gone.id);
    QCOMPARE(m_lib->count(), 1);
    QVERIFY(m_lib->contains(kept.id));
    // An unreferenced local file that left the library is pruned from
    // the store entirely.
    QVERIFY(!m_store->item(gone.id).isValid());
}

void LibraryManagerTest::syncDirectoryOfMissingDirRemovesItsItems()
{
    QVERIFY(QDir(m_dir->path()).mkdir("dir"));
    test::addLibraryFileAndWait(m_lib, makeTempFile("dir/only.mp3"));
    const MediaItem outside = test::addLibraryFileAndWait(m_lib, makeTempFile("outside.mp3"));
    QVERIFY(QFile::remove(m_dir->path() + "/dir/only.mp3"));
    QVERIFY(QDir(m_dir->path()).rmdir("dir"));

    QSignalSpy removedSpy(m_lib, &LibraryManager::itemRemoved);
    m_lib->syncDirectory(QUrl::fromLocalFile(m_dir->path() + "/dir"));

    QTRY_COMPARE_WITH_TIMEOUT(removedSpy.count(), 1, 10000);
    test::waitForIdle(m_lib);
    QCOMPARE(m_lib->count(), 1);
    QVERIFY(m_lib->contains(outside.id));
}

void LibraryManagerTest::syncDirectorySkipsUnknownExtensions()
{
    makeTempFile("notes.txt");
    makeTempFile("video.mp4");
    makeTempFile("audio.mp3");

    QSignalSpy addedSpy(m_lib, &LibraryManager::itemAdded);
    m_lib->syncDirectory(QUrl::fromLocalFile(m_dir->path()));

    QTRY_COMPARE_WITH_TIMEOUT(addedSpy.count(), 1, 10000);
    test::waitForIdle(m_lib);
    QCOMPARE(m_lib->count(), 1);
    QCOMPARE(addedSpy.at(0).at(0).value<MediaItem>().title, QString("audio"));
}

void LibraryManagerTest::syncDirectoryWhileBusyIsRefused()
{
    const QUrl url = makeTaggedFile(m_dir->path(), "busy.flac",
                                    "Busy Title", "Busy Artist", "Busy Album");
    if (url.isEmpty())
        QSKIP("ffmpeg not available to generate a tagged file");

    m_lib->addFile(url);
    QVERIFY(m_lib->busy());

    QSignalSpy removedSpy(m_lib, &LibraryManager::itemRemoved);
    m_lib->syncDirectory(QUrl::fromLocalFile(m_dir->path()));
    QCOMPARE(removedSpy.count(), 0);
    QVERIFY(m_lib->busy());

    test::waitForIdle(m_lib);
    QCOMPARE(m_lib->count(), 1);
}

void LibraryManagerTest::onlyOneOperationAtATime()
{
    const QUrl url = makeTaggedFile(m_dir->path(), "busy.flac",
                                    "Busy Title", "Busy Artist", "Busy Album");
    if (url.isEmpty())
        QSKIP("ffmpeg not available to generate a tagged file");

    QSignalSpy addedSpy(m_lib, &LibraryManager::itemAdded);
    m_lib->addFile(url);
    // The synchronous part of addFile has posted the job: the manager is
    // busy until the worker thread reports back.
    QVERIFY(m_lib->busy());

    // While busy, another operation is refused entirely (the read's
    // result is empty).
    QSignalSpy busySpy(m_lib, &LibraryManager::busyChanged);
    QHash<quint64, TrackMeta> results;
    m_lib->readMetadata({ 99999 },
                        [](int, int) {},
                        [&results](const QHash<quint64, TrackMeta> &res) {
                            results = res;
                        });
    QCOMPARE(busySpy.count(), 0);
    QVERIFY(m_lib->busy());
    QVERIFY(results.isEmpty());

    QVERIFY(addedSpy.wait(10000));
    test::waitForIdle(m_lib);
    QCOMPARE(m_lib->count(), 1);
    QCOMPARE(addedSpy.at(0).at(0).value<MediaItem>().title, QString("Busy Title"));
}

void LibraryManagerTest::readMetadataReturnsFileTags()
{
    const QUrl url = makeTaggedFileWith(m_dir->path(), "song.flac",
        { { "TITLE", "Old Title" }, { "ARTIST", "Old Artist" },
          { "ALBUM", "Old Album" }, });
    if (url.isEmpty())
        QSKIP("ffmpeg not available to generate a tagged file");

    const MediaItem item = test::addLibraryFileAndWait(m_lib, url);
    QCOMPARE(item.title, QString("Old Title"));

    // The tags on disk change; the read must pick up the new values.
    makeTaggedFileWith(m_dir->path(), "song.flac",
        { { "TITLE", "New Title" }, { "ARTIST", "New Artist" },
          { "ALBUM", "New Album" }, });

    QHash<quint64, TrackMeta> results;
    int lastResolved = -1;
    int lastTotal = -1;
    m_lib->readMetadata(
        { item.id },
        [&lastResolved, &lastTotal](int resolved, int total) {
            lastResolved = resolved;
            lastTotal = total;
        },
        [&results](const QHash<quint64, TrackMeta> &res) { results = res; });

    QVERIFY(test::waitUntil([&results] { return !results.isEmpty(); }));
    test::waitForIdle(m_lib);

    QVERIFY(results.contains(item.id));
    QCOMPARE(results.value(item.id).title, QString("New Title"));
    QCOMPARE(results.value(item.id).artist, QString("New Artist"));
    QCOMPARE(results.value(item.id).album, QString("New Album"));
    // The progress ran (0..1 of 1 files).
    QCOMPARE(lastTotal, 1);
    QCOMPARE(lastResolved, 1);
    // Read-only: the store is untouched.
    QCOMPARE(m_store->item(item.id).title, QString("Old Title"));
    QSignalSpy changedSpy(m_lib, &LibraryManager::itemChanged);
    QCOMPARE(changedSpy.count(), 0);
}

void LibraryManagerTest::readMetadataSkipsMissingFiles()
{
    const MediaItem item = test::addLibraryFileAndWait(m_lib, makeTempFile("gone.mp3"));

    QVERIFY(QFile::remove(item.source.toLocalFile()));

    QHash<quint64, TrackMeta> results;
    m_lib->readMetadata({ item.id },
                        [](int, int) {},
                        [&results](const QHash<quint64, TrackMeta> &res) { results = res; });

    QVERIFY(test::waitUntil([&results, this] {
        return !results.isEmpty() || !m_lib->busy();
    }));
    test::waitForIdle(m_lib);

    // The missing file is absent from the result, the store is untouched.
    QVERIFY(!results.contains(item.id));
    QCOMPARE(m_lib->count(), 1);
    QCOMPARE(m_store->item(item.id).title, QString("gone"));
}

void LibraryManagerTest::readMetadataSkipsNonLocalItems()
{
    // A provider item (no local file)...
    const QUrl source = QUrl("https://music.example.com/watch?v=2");
    m_provider.m_urls << source.toString();
    TrackMeta meta;
    meta.title = QStringLiteral("Fetched");
    m_provider.m_meta = meta;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));
    const MediaItem providerItem = test::addLibraryFileAndWait(m_lib, source);
    QVERIFY(providerItem.isValid());

    // ...and a plain stream URL (no source to re-read).
    MediaItem remote;
    remote.kind = MediaItem::Remote;
    remote.source = QUrl("https://stream.example.com/live");
    remote.title = QStringLiteral("Stream");
    QVERIFY(m_store->insert(remote));

    // Nothing is local: the read finishes immediately with an empty
    // result and no operation runs at all.
    QSignalSpy busySpy(m_lib, &LibraryManager::busyChanged);
    QHash<quint64, TrackMeta> results;
    m_lib->readMetadata({ providerItem.id, remote.id },
                        [](int, int) {},
                        [&results](const QHash<quint64, TrackMeta> &res) { results = res; });

    QCOMPARE(busySpy.count(), 0);
    QVERIFY(!m_lib->busy());
    QVERIFY(results.isEmpty());
}

void LibraryManagerTest::readMetadataWhileBusyIsRefused()
{
    const QUrl url = makeTaggedFile(m_dir->path(), "busy.flac",
                                    "Busy Title", "Busy Artist", "Busy Album");
    if (url.isEmpty())
        QSKIP("ffmpeg not available to generate a tagged file");

    QSignalSpy addedSpy(m_lib, &LibraryManager::itemAdded);
    m_lib->addFile(url);
    QVERIFY(m_lib->busy());

    // While busy, the read is refused: the result is empty, the store
    // stays as it is.
    QHash<quint64, TrackMeta> results;
    int progressCalls = 0;
    m_lib->readMetadata({ 99999 },
                        [&progressCalls](int, int) { ++progressCalls; },
                        [&results](const QHash<quint64, TrackMeta> &res) { results = res; });

    QVERIFY(results.isEmpty());
    QCOMPARE(progressCalls, 0);

    QVERIFY(addedSpy.wait(10000));
    test::waitForIdle(m_lib);
}

void LibraryManagerTest::readMetadataEmptyIdsIsNoOp()
{
    QSignalSpy busySpy(m_lib, &LibraryManager::busyChanged);
    QHash<quint64, TrackMeta> results;
    m_lib->readMetadata({},
                        [](int, int) {},
                        [&results](const QHash<quint64, TrackMeta> &res) { results = res; });

    QCOMPARE(busySpy.count(), 0);
    QVERIFY(!m_lib->busy());
    QVERIFY(results.isEmpty());
}

void LibraryManagerTest::removeItemEmitsAndRemoves()
{
    const MediaItem item = test::addLibraryFileAndWait(m_lib, makeTempFile("gone.mp3"));

    QSignalSpy removedSpy(m_lib, &LibraryManager::itemRemoved);
    QVERIFY(m_lib->removeItem(item.id));
    QCOMPARE(removedSpy.count(), 1);
    // The signal carries the item with the flag already cleared (captured
    // before the prune, which removes the row: the item is referenced by
    // no playlist).
    const MediaItem removed = removedSpy.at(0).at(0).value<MediaItem>();
    QCOMPARE(removed.id, item.id);
    QCOMPARE(removed.title, item.title);
    QVERIFY(!removed.inLibrary);
    QCOMPARE(m_lib->count(), 0);
    QCOMPARE(m_store->count(), 0);
    QVERIFY(!MediaItemStore::resolve(item.id).isValid());
    QVERIFY(!m_lib->removeItem(item.id));
}

void LibraryManagerTest::addItemsFlagsStoreItems()
{
    // A store item that is not in the library (a playlist row keeps it
    // from being pruned): addItems flags it in place.
    const MediaItem a = test::addLibraryFileAndWait(m_lib, makeTempFile("one.mp3"));
    const MediaItem b = test::addLibraryFileAndWait(m_lib, makeTempFile("two.mp3"));
    PlaylistManager* pl = PlaylistManager::getInstance();
    pl->addItems({ a.id, b.id });
    QVERIFY(m_lib->removeItem(a.id));
    QVERIFY(m_lib->removeItem(b.id));
    QVERIFY(!m_lib->contains(a.id));
    QVERIFY(!m_lib->contains(b.id));
    // The rows survive the removal: the playlist references them.
    QVERIFY(MediaItemStore::resolve(a.id).isValid());
    QVERIFY(MediaItemStore::resolve(b.id).isValid());

    QSignalSpy addedSpy(m_lib, &LibraryManager::itemAdded);
    m_lib->addItems({ a.id, b.id });
    QCOMPARE(addedSpy.count(), 2);
    QCOMPARE(addedSpy.at(0).at(0).value<MediaItem>().id, a.id);
    QCOMPARE(addedSpy.at(1).at(0).value<MediaItem>().id, b.id);
    QVERIFY(m_lib->contains(a.id));
    QVERIFY(m_lib->contains(b.id));
    // The flag is write-through: the store's items carry it.
    QVERIFY(MediaItemStore::resolve(a.id).inLibrary);
    QVERIFY(MediaItemStore::resolve(b.id).inLibrary);
}

void LibraryManagerTest::addItemsOfLibraryItemsIsNoOp()
{
    const MediaItem a = test::addLibraryFileAndWait(m_lib, makeTempFile("one.mp3"));

    QSignalSpy addedSpy(m_lib, &LibraryManager::itemAdded);
    m_lib->addItems({ a.id });
    QCOMPARE(addedSpy.count(), 0);
    QCOMPARE(m_lib->count(), 1);
    QVERIFY(m_lib->contains(a.id));
}

void LibraryManagerTest::addItemsSkipsUnknownIds()
{
    // An id the store has never issued: refused, no signal, no crash.
    QSignalSpy addedSpy(m_lib, &LibraryManager::itemAdded);
    m_lib->addItems({ 424242 });
    QCOMPARE(addedSpy.count(), 0);
    QCOMPARE(m_lib->count(), 0);
}

void LibraryManagerTest::clearEmitsAndEmpties()
{
    const MediaItem a = test::addLibraryFileAndWait(m_lib, makeTempFile("x1.mp3"));
    const MediaItem b = test::addLibraryFileAndWait(m_lib, makeTempFile("x2.mp3"));

    QSignalSpy clearedSpy(m_lib, &LibraryManager::itemsCleared);
    m_lib->clear();

    QCOMPARE(clearedSpy.count(), 1);
    QCOMPARE(m_lib->count(), 0);
    // Pruned at once: neither item is in the library, referenced nowhere,
    // and local — the rows are gone from the store right away.
    QCOMPARE(m_store->count(), 0);
    QVERIFY(!MediaItemStore::resolve(a.id).isValid());
    QVERIFY(!MediaItemStore::resolve(b.id).isValid());
}

void LibraryManagerTest::saveLoadRoundtrip()
{
    const MediaItem a = test::addLibraryFileAndWait(m_lib, makeTempFile("alpha.mp3"));
    const MediaItem b = test::addLibraryFileAndWait(m_lib, makeTempFile("beta.flac"));

    m_store->save();
    m_store->clear();
    QCOMPARE(m_lib->count(), 2);  // the mirror is untouched by the store's clear

    m_store->load();
    m_lib->load();

    QCOMPARE(m_lib->count(), 2);
    QVERIFY(m_lib->contains(a.id));
    QVERIFY(m_lib->contains(b.id));
    QCOMPARE(m_lib->item(a.id).toVariantMap(), a.toVariantMap());
    QCOMPARE(m_lib->item(b.id).toVariantMap(), b.toVariantMap());
}

void LibraryManagerTest::saveWritesExtendedColumnsAndMeta()
{
    const QUrl url = makeTaggedFileWith(m_dir->path(), "song.flac",
        { { "TITLE", "Song Title" }, { "ARTIST", "Song Artist" },
          { "ALBUM", "Song Album" }, { "GENRE", "Jazz" }, { "BPM", "128" }, });
    if (url.isEmpty())
        QSKIP("ffmpeg not available to generate a tagged file");
    const MediaItem item = test::addLibraryFileAndWait(m_lib, url);

    m_store->save();

    // Inspect the file through a second connection.
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(m_dir->path() + "/doob.db");
    if (!db.open()) {
        QFAIL("cannot open doob.db for inspection");
        return;
    }
    QSqlQuery q(db);

    // Standard fields are real columns.
    q.exec(QStringLiteral("SELECT genre FROM media_items WHERE id = %1").arg(item.id));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QString("Jazz"));

    // The library flag is persisted on the item.
    q.exec(QStringLiteral("SELECT in_library FROM media_items WHERE id = %1").arg(item.id));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 1);

    // Arbitrary tags are EAV rows.
    q.exec(QStringLiteral(
        "SELECT value FROM item_meta WHERE item_id = %1 AND key = 'BPM'")
        .arg(item.id));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QString("128"));

    db.close();
    QSqlDatabase::removeDatabase(db.connectionName());
}

void LibraryManagerTest::idsContinueAfterLoad()
{
    const MediaItem a = test::addLibraryFileAndWait(m_lib, makeTempFile("keep.mp3"));
    test::addLibraryFileAndWait(m_lib, makeTempFile("keep2.mp3"));

    m_store->save();
    m_store->clear();
    m_store->load();
    m_lib->load();

    const MediaItem next = test::addLibraryFileAndWait(m_lib, makeTempFile("new.mp3"));

    QVERIFY(next.id > a.id);
    QVERIFY(m_lib->contains(a.id));
    QVERIFY(m_lib->contains(next.id));
    QCOMPARE(m_lib->count(), 3);
}

void LibraryManagerTest::schemaCreatesTablesAndIndexes()
{
    test::addLibraryFileAndWait(m_lib, makeTempFile("x.mp3"));
    m_store->save();

    // Inspect the file through a second connection: the schema applied on
    // open must include the metadata indexes (for future SQL-side
    // filtering).
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(m_dir->path() + "/doob.db");
    if (!db.open()) {
        QFAIL("cannot open doob.db for inspection");
        return;
    }
    QSqlQuery q(db);

    q.exec("SELECT name FROM sqlite_master WHERE type = 'index' "
           "AND tbl_name IN ('media_items', 'item_meta')");
    QStringList objects;
    while (q.next())
        objects << q.value(0).toString();

    q.exec("SELECT name FROM sqlite_master WHERE type = 'table'");
    QStringList tables;
    while (q.next())
        tables << q.value(0).toString();

    db.close();
    QSqlDatabase::removeDatabase(db.connectionName());

    // No full-text index: the store is filtered in memory (see the note in
    // kV1ItemsStatements).
    QVERIFY(!tables.contains("item_fts"));
    QVERIFY(objects.contains("idx_items_title"));
    QVERIFY(objects.contains("idx_items_artist"));
    QVERIFY(objects.contains("idx_items_album"));
    // The composites a tree representation (artist -> album -> track)
    // groups on.
    QVERIFY(objects.contains("idx_items_artist_album"));
    QVERIFY(objects.contains("idx_items_album_artist_album"));
    QVERIFY(objects.contains("idx_item_meta_key"));
    QVERIFY(objects.contains("idx_item_meta_value"));
}

void LibraryManagerTest::findBySourceFindsItem()
{
    const MediaItem item = test::addLibraryFileAndWait(m_lib, makeTempFile("findable.mp3"));

    QCOMPARE(m_lib->findBySource(item.source).id, item.id);

    QUrl other = item.source;
    other.setPath(m_dir->path() + "/other.mp3");
    QVERIFY(!m_lib->findBySource(other).isValid());
}

void LibraryManagerTest::findBySourceNormalizesRelativePath()
{
    const MediaItem item = test::addLibraryFileAndWait(m_lib, makeTempFile("findable.mp3"));

    // A CWD-relative local URL names the same file as the stored absolute
    // source; the lookup must normalize it before comparing.
    const QString cwd = QDir::currentPath();
    QDir::setCurrent(m_dir->path());
    const QUrl relative = QUrl::fromLocalFile("findable.mp3");
    QCOMPARE(m_lib->findBySource(relative).id, item.id);
    QDir::setCurrent(cwd);
}

void LibraryManagerTest::idsFollowItemOrder()
{
    const MediaItem a = test::addLibraryFileAndWait(m_lib, makeTempFile("o1.mp3"));
    const MediaItem b = test::addLibraryFileAndWait(m_lib, makeTempFile("o2.mp3"));
    QCOMPARE(m_lib->ids(), QList<quint64>({ a.id, b.id }));
}
}


QTEST_MAIN(LibraryManagerTest)
#include "librarymanager_test.moc"
