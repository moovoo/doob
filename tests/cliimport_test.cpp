#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <memory>

#include "cliimport.h"
#include "mediaitem.h"
#include "mediaitemstore.h"
#include "playlist.h"
#include "playlistfile.h"
#include "settingsmanager.h"
#include "sourcemanager.h"
#include "taggedfile.h"
#include "testsupport.h"

namespace {

class CliImportTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void sourceFromArgumentResolvesPathsAndUrls();
    void splitSourcesSeparatesPlaylistFiles();
    void importAppendsToCurrentPlaylist();
    void importNewPlaylistCreatesNamedPlaylist();
    void importNewPlaylistTakesDefaultName();
    void importPlaylistFileBecomesItsOwnPlaylist();
    void importMixedRoutesEachHalf();
    void importMediaBeforePlaylistFileChains();
    void importIsSerialized();
    void importPlayStartsFirstImportedTrack();
    void importWithoutPlayLeavesPlaybackAlone();
    void importPlayPlaysFirstOfNewPlaylist();
    void importPlayOnlyPlaylistFilePlaysItsFirstEntry();
    void importWithoutSourcesIsNoOp();

private:
    // Spins the event loop until the running import (if any) has finished.
    void waitForImport();

    LibraryManager* m_lib;
    PlaylistManager* m_pl;
    MediaItemStore* m_store;
    PlaybackManager* m_pm;
    FakePlayer m_fake{this};
    std::unique_ptr<QTemporaryDir> m_dir;
};

void CliImportTest::init()
{
    m_lib = LibraryManager::getInstance();
    m_pl = PlaylistManager::getInstance();
    m_store = MediaItemStore::getInstance();
    m_pm = PlaybackManager::getInstance();
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
    test::installFakePlayer(m_pm, &m_fake);
}

void CliImportTest::cleanup()
{
    m_pm->stop();
    m_lib->clear();
    m_pl->reset();
    m_store->clear();
    SettingsManager::reset();
}

void CliImportTest::sourceFromArgumentResolvesPathsAndUrls()
{
    const QUrl file = makeTaggedFile(m_dir->path(), "arg.flac", "Arg", "A", "B");
    if (file.isEmpty())
        QSKIP("ffmpeg not available");
    const QString path = file.toLocalFile();

    // An absolute path resolves to its file: URL.
    QCOMPARE(CliImport::sourceFromArgument(path), file);
    // An existing relative path resolves against the current directory.
    const QString cwd = QDir::currentPath();
    QDir::setCurrent(m_dir->path());
    QCOMPARE(CliImport::sourceFromArgument(QStringLiteral("arg.flac")), file);
    QDir::setCurrent(cwd);
    // A scheme-bearing argument is a URL as pasted.
    const QUrl stream = QUrl(QStringLiteral("https://example.com/stream.mp3"));
    QCOMPARE(CliImport::sourceFromArgument(stream.toString()), stream);
    // A host-like argument (no scheme, no local file) takes the
    // user-input resolution (http is assumed).
    QCOMPARE(CliImport::sourceFromArgument(QStringLiteral("example.com/stream.mp3")),
             QUrl(QStringLiteral("http://example.com/stream.mp3")));
}

void CliImportTest::splitSourcesSeparatesPlaylistFiles()
{
    const QUrl file = makeTaggedFile(m_dir->path(), "sp.flac", "SP", "A", "B");
    if (file.isEmpty())
        QSKIP("ffmpeg not available");
    const QUrl stream = QUrl(QStringLiteral("https://example.com/stream.mp3"));
    // A playlist file: a local file with a playlist extension.
    const QString m3uPath = m_dir->path() + "/mix.m3u";
    QVERIFY(writePlaylistFile(m3uPath, { file }));
    const QUrl m3u = QUrl::fromLocalFile(m3uPath);

    const CliImport::Split split =
        CliImport::splitSources({ file, stream, m3u });
    QCOMPARE(split.media, QList<QUrl>({ file, stream }));
    QCOMPARE(split.playlistFiles, QList<QUrl>({ m3u }));
}

void CliImportTest::importAppendsToCurrentPlaylist()
{
    const QUrl a = makeTaggedFile(m_dir->path(), "ia.flac", "IA", "A", "B");
    const QUrl b = makeTaggedFile(m_dir->path(), "ib.flac", "IB", "A", "B");
    if (a.isEmpty() || b.isEmpty())
        QSKIP("ffmpeg not available");

    CliRequest request;
    request.sources = { a.toString(), b.toString() };
    CliImport cli;
    cli.import(request);
    waitForImport();

    // The rows land in the current playlist, in source order.
    QCOMPARE(m_pl->count(), 2);
    QCOMPARE(m_pl->itemAt(0).title, QString("IA"));
    QCOMPARE(m_pl->itemAt(1).title, QString("IB"));
    // No new playlist.
    QCOMPARE(m_pl->playlistCount(), 1);
}

void CliImportTest::importNewPlaylistCreatesNamedPlaylist()
{
    const QUrl a = makeTaggedFile(m_dir->path(), "in.flac", "IN", "A", "B");
    if (a.isEmpty())
        QSKIP("ffmpeg not available");

    const quint64 currentId = m_pl->currentPlaylistId();
    CliRequest request;
    request.sources = { a.toString() };
    request.newPlaylist = true;
    request.newPlaylistName = "Mix";
    CliImport cli;
    cli.import(request);
    waitForImport();

    QCOMPARE(m_pl->playlistCount(), 2);
    const Playlist newPlaylist = m_pl->playlists().last();
    QCOMPARE(newPlaylist.name, QString("Mix"));
    QCOMPARE(newPlaylist.items.size(), 1);
    QCOMPARE(MediaItemStore::resolve(newPlaylist.items.at(0)).title, QString("IN"));
    // The current playlist is untouched and keeps its identity.
    QCOMPARE(m_pl->currentPlaylistId(), currentId);
    QVERIFY(m_pl->playlist(currentId).items.isEmpty());
}

void CliImportTest::importNewPlaylistTakesDefaultName()
{
    const QUrl a = makeTaggedFile(m_dir->path(), "ind.flac", "IND", "A", "B");
    if (a.isEmpty())
        QSKIP("ffmpeg not available");

    CliRequest request;
    request.sources = { a.toString() };
    request.newPlaylist = true;  // no name
    CliImport cli;
    cli.import(request);
    waitForImport();

    QCOMPARE(m_pl->playlistCount(), 2);
    QCOMPARE(m_pl->playlists().last().name, QString("Playlist"));
}

void CliImportTest::importPlaylistFileBecomesItsOwnPlaylist()
{
    const QUrl a = makeTaggedFile(m_dir->path(), "ipa.flac", "IPA", "A", "B");
    const QUrl b = makeTaggedFile(m_dir->path(), "ipb.flac", "IPB", "A", "B");
    if (a.isEmpty() || b.isEmpty())
        QSKIP("ffmpeg not available");
    const QString m3uPath = m_dir->path() + "/mixlist.m3u8";
    QVERIFY(writePlaylistFile(m3uPath, { a, b }));

    CliRequest request;
    request.sources = { m3uPath };
    CliImport cli;
    cli.import(request);
    waitForImport();

    // A new playlist named after the file, marked imported, its entries
    // in file order.
    QCOMPARE(m_pl->playlistCount(), 2);
    const Playlist filePlaylist = m_pl->playlists().last();
    QCOMPARE(filePlaylist.name, QString("mixlist"));
    QVERIFY(filePlaylist.imported);
    QCOMPARE(filePlaylist.sourceFile, QUrl::fromLocalFile(m3uPath).toString());
    QCOMPARE(filePlaylist.items.size(), 2);
    QCOMPARE(MediaItemStore::resolve(filePlaylist.items.at(0)).title, QString("IPA"));
    QCOMPARE(MediaItemStore::resolve(filePlaylist.items.at(1)).title, QString("IPB"));
    // The current playlist is untouched.
    QVERIFY(m_pl->playlist(m_pl->currentPlaylistId()).items.isEmpty());
}

void CliImportTest::importMixedRoutesEachHalf()
{
    const QUrl a = makeTaggedFile(m_dir->path(), "ima.flac", "IMA", "A", "B");
    const QUrl b = makeTaggedFile(m_dir->path(), "imb.flac", "IMB", "A", "B");
    if (a.isEmpty() || b.isEmpty())
        QSKIP("ffmpeg not available");
    const QString m3uPath = m_dir->path() + "/mixed.m3u";
    QVERIFY(writePlaylistFile(m3uPath, { a, b }));

    // The m3u becomes its own playlist, the bare file goes to the
    // current one.
    CliRequest request;
    request.sources = { m3uPath, a.toString() };
    CliImport cli;
    cli.import(request);
    waitForImport();

    QCOMPARE(m_pl->playlistCount(), 2);
    const Playlist filePlaylist = m_pl->playlists().last();
    QCOMPARE(filePlaylist.name, QString("mixed"));
    QCOMPARE(filePlaylist.items.size(), 2);
    QCOMPARE(m_pl->playlist(m_pl->currentPlaylistId()).items.size(), 1);
    QCOMPARE(MediaItemStore::resolve(m_pl->itemIds().at(0)).title, QString("IMA"));
}

void CliImportTest::importMediaBeforePlaylistFileChains()
{
    const QUrl a = makeTaggedFile(m_dir->path(), "cmb.flac", "CMB", "A", "B");
    const QUrl b = makeTaggedFile(m_dir->path(), "cma1.flac", "CMA1", "A", "B");
    const QUrl c = makeTaggedFile(m_dir->path(), "cma2.flac", "CMA2", "A", "B");
    if (a.isEmpty() || b.isEmpty() || c.isEmpty())
        QSKIP("ffmpeg not available");
    const QString m3uPath = m_dir->path() + "/chain.m3u";
    QVERIFY(writePlaylistFile(m3uPath, { b, c }));

    // The media's block precedes the playlist file in the request: the
    // media lands first (the file's import chains on its finish).
    CliRequest request;
    request.sources = { a.toString(), m3uPath };
    CliImport cli;
    cli.import(request);
    waitForImport();

    QCOMPARE(m_pl->playlistCount(), 2);
    QCOMPARE(m_pl->playlist(m_pl->currentPlaylistId()).items.size(), 1);
    QCOMPARE(MediaItemStore::resolve(m_pl->itemIds().at(0)).title, QString("CMB"));
    const Playlist filePlaylist = m_pl->playlists().last();
    QCOMPARE(filePlaylist.name, QString("chain"));
    QCOMPARE(filePlaylist.items.size(), 2);
    QCOMPARE(MediaItemStore::resolve(filePlaylist.items.at(0)).title, QString("CMA1"));
    QCOMPARE(MediaItemStore::resolve(filePlaylist.items.at(1)).title, QString("CMA2"));
}

void CliImportTest::importIsSerialized()
{
    const QUrl a = makeTaggedFile(m_dir->path(), "ser.flac", "SER", "A", "B");
    if (a.isEmpty())
        QSKIP("ffmpeg not available");

    CliImport cli;
    CliRequest first;
    first.sources = { a.toString() };
    cli.import(first);
    // The fresh file's tag read runs on the worker: the import is in
    // flight.
    QVERIFY(m_pl->importing());
    // A second import while one is running is refused (the jobs of one
    // invocation chain on the manager's single import).
    CliRequest second;
    second.sources = { a.toString() };
    cli.import(second);
    waitForImport();

    QCOMPARE(m_pl->count(), 1);
    QVERIFY(!m_pl->importing());
}

void CliImportTest::importPlayStartsFirstImportedTrack()
{
    const QUrl a = makeTaggedFile(m_dir->path(), "ip1.flac", "IP1", "A", "B");
    const QUrl b = makeTaggedFile(m_dir->path(), "ip2.flac", "IP2", "A", "B");
    if (a.isEmpty() || b.isEmpty())
        QSKIP("ffmpeg not available");

    // The current playlist already has a row: the play starts at the
    // first imported row, not at row 0.
    const QUrl existingUrl = makeTaggedFile(m_dir->path(), "existing.flac",
                                            "Existing", "A", "B");
    if (existingUrl.isEmpty())
        QSKIP("ffmpeg not available");
    TrackMeta meta;
    meta.title = "Existing";
    meta.artist = "A";
    meta.album = "B";
    const MediaItem existing = m_store->ensureItem(existingUrl, meta);
    QVERIFY(existing.isValid());
    m_pl->addItems({ existing.id });

    CliRequest request;
    request.sources = { a.toString(), b.toString() };
    request.play = true;
    CliImport cli;
    cli.import(request);
    waitForImport();

    QCOMPARE(m_pl->count(), 3);
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(m_pm->playingItem().title, QString("IP1"));  // the first imported
    QCOMPARE(m_pl->currentIndex(), 1);
}

void CliImportTest::importWithoutPlayLeavesPlaybackAlone()
{
    const QUrl a = makeTaggedFile(m_dir->path(), "ipn.flac", "IPN", "A", "B");
    if (a.isEmpty())
        QSKIP("ffmpeg not available");

    CliRequest request;
    request.sources = { a.toString() };  // no play flag
    CliImport cli;
    cli.import(request);
    waitForImport();

    QCOMPARE(m_pl->count(), 1);
    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);
    QCOMPARE(m_fake.m_playCount, 0);
}

void CliImportTest::importPlayPlaysFirstOfNewPlaylist()
{
    const QUrl a = makeTaggedFile(m_dir->path(), "ipnp.flac", "IPNP", "A", "B");
    if (a.isEmpty())
        QSKIP("ffmpeg not available");

    CliRequest request;
    request.sources = { a.toString() };
    request.newPlaylist = true;
    request.newPlaylistName = "Mix";
    request.play = true;
    CliImport cli;
    cli.import(request);
    waitForImport();

    // The play lands on the new playlist's first row (and switches the
    // current playlist to it).
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(m_pm->playingItem().title, QString("IPNP"));
    QCOMPARE(m_pl->currentPlaylistName(), QString("Mix"));
    QCOMPARE(m_pl->currentIndex(), 0);
}

void CliImportTest::importPlayOnlyPlaylistFilePlaysItsFirstEntry()
{
    const QUrl a = makeTaggedFile(m_dir->path(), "ipf1.flac", "IPF1", "A", "B");
    const QUrl b = makeTaggedFile(m_dir->path(), "ipf2.flac", "IPF2", "A", "B");
    if (a.isEmpty() || b.isEmpty())
        QSKIP("ffmpeg not available");
    const QString m3uPath = m_dir->path() + "/onlyfile.m3u8";
    QVERIFY(writePlaylistFile(m3uPath, { a, b }));

    // No media sources: the play starts at the imported file's first
    // entry.
    CliRequest request;
    request.sources = { m3uPath };
    request.play = true;
    CliImport cli;
    cli.import(request);
    waitForImport();

    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(m_pm->playingItem().title, QString("IPF1"));
    QCOMPARE(m_pl->currentPlaylistName(), QString("onlyfile"));
    QCOMPARE(m_pl->currentIndex(), 0);
}

void CliImportTest::importWithoutSourcesIsNoOp()
{
    CliImport cli;
    cli.import(CliRequest());  // no sources, no flags

    QCOMPARE(m_pl->count(), 0);
    QCOMPARE(m_pl->playlistCount(), 1);
    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);
}

void CliImportTest::waitForImport()
{
    int waited = 0;
    while (m_pl->importing() && waited < 10000) {
        QTest::qWait(25);
        waited += 25;
    }
    QVERIFY(!m_pl->importing());
}

} // namespace

QTEST_MAIN(CliImportTest)
#include "cliimport_test.moc"
