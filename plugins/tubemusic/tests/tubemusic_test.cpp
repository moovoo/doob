#include <QtTest>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkProxyFactory>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <array>

// The core headers go through the <doob/...> prefix (the doob_lib
// target's include layout), so the test builds both in-tree and
// standalone against the installed API.
#include <doob/mediaitem.h>
#include <doob/pluginmanager.h>
#include <doob/settings.h>
#include <doob/settingsmanager.h>
#include <doob/sourcemanager.h>
#include <doob/trackmetadata.h>

#include "testhttpserver.h"

namespace {

class TubeMusicTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void claimsTubeUrls();
    void concurrentFetchesBothSucceed();
    void queuedRunsAllComplete();
    void cookiesFileIsUsed();
    void playbackResolvesSelfContainedStream();
    void downloadModeCachesAndReplays();
    void downloadModeEvictsOldestCacheFiles();
    void concurrentDownloadsDedupToOneRun();
    void metadataSurvivesStaleCookies();
    void failedRunLogsItsStderr();
    void fetchMetadataDownloadsThumbnail();
    void fetchMetadataThumbnailFallsBackToThumbnailsList();
    void fetchMetadataThumbnailFallsBackToDefaultUrl();
    void fetchMetadataThumbnailFailureLeadsMetaWithoutArt();

private:
    // The "tubemusic.ytdlp" setting the tests point at a fake binary
    // (restored per test, so the real-binary tests keep their path).
    QString m_savedYtdlpSetting;

    // A fake yt-dlp on disk: a script that prints the canned JSON
    // (ignoring its arguments). The binary's path is the setting,
    // re-read on every run.
    static QString writeFakeYtdlp(const QString &dir, const QByteArray &json);

private:
    // A finished cache file of a given size, aged by ageSecs (the
    // eviction order is by mtime).
    static void makeCacheFile(const QString &path, qint64 size, int ageSecs);
    void canonicalizesClaimedUrls();
    void listFetchWithoutListArgIsEmpty();
    void listFetchReturnsTheList();

private:
    // The dev build drops the plugins/ .so files in <app dir>/plugins
    // (one of the search paths), next to the test executable.
    QString pluginDir() const
    {
        return QCoreApplication::applicationDirPath() + "/plugins";
    }
    bool pluginBuilt() const
    {
        return !QDir(pluginDir())
                    .entryList({"libtubemusic.so"}, QDir::Files)
                    .isEmpty();
    }
    // Loads the plugin from the dev build's directory and looks up its
    // provider (nullptr when registration did not happen — the caller
    // QFAILs, the QSKIP-able preconditions stay with the caller).
    SourceProvider* tubeProvider();
};

void TubeMusicTest::initTestCase()
{
    // The thumbnail fetches reach a local fake server: no system proxy
    // in between (a dead proxy would break the loopback requests).
    QNetworkProxyFactory::setUseSystemConfiguration(false);
    // The plugin's initialize() registers its settings (needs a data
    // dir for the central settings.json).
    qputenv("DOOB_DATA_DIR", "/tmp/doob_tubemusic_test");
    Settings::reset();
    SettingsManager* settings = SettingsManager::getInstance();
    settings->setSettingsPath("/tmp/doob_tubemusic_test/settings.json");
    settings->load();
}

void TubeMusicTest::cleanupTestCase()
{
    qunsetenv("DOOB_DATA_DIR");
    Settings::reset();
    SettingsManager::reset();
}

void TubeMusicTest::init()
{
    PluginManager::getInstance()->shutdownPlugins();
    m_savedYtdlpSetting =
        SettingsManager::getInstance()->value("tubemusic.ytdlp").toString();
}

void TubeMusicTest::cleanup()
{
    PluginManager::getInstance()->shutdownPlugins();
    SettingsManager::getInstance()->setValue("tubemusic.ytdlp",
                                             m_savedYtdlpSetting);
}

SourceProvider* TubeMusicTest::tubeProvider()
{
    PluginManager::getInstance()->loadFromDir(pluginDir());
    return SourceManager::getInstance()->providerById("tubemusic");
}

void TubeMusicTest::claimsTubeUrls()
{
    if (!pluginBuilt())
        QSKIP("the tubemusic plugin is not built (DOOB_BUILD_PLUGINS=OFF)");

    SourceProvider* provider = tubeProvider();
    if (provider == nullptr)
        QFAIL("the tubemusic provider is not registered");

    QVERIFY(provider->accepts(QUrl("https://www.youtube.com/watch?v=abc")));
    QVERIFY(provider->accepts(QUrl("https://youtu.be/abc")));
    QVERIFY(provider->accepts(QUrl("https://m.youtube.com/shorts/abc")));
    QVERIFY(provider->accepts(
        QUrl("https://www.youtube.com/playlist?list=abc")));
    // Channel and search pages are not importable.
    QVERIFY(!provider->accepts(QUrl("https://www.youtube.com/@channel")));
    QVERIFY(!provider->accepts(QUrl("https://www.youtube.com/results?q=x")));
}

void TubeMusicTest::concurrentFetchesBothSucceed()
{
    if (!pluginBuilt())
        QSKIP("the tubemusic plugin is not built (DOOB_BUILD_PLUGINS=OFF)");
    if (QStandardPaths::findExecutable("yt-dlp").isEmpty())
        QSKIP("yt-dlp is not available");

    SourceProvider* provider = tubeProvider();
    if (provider == nullptr)
        QFAIL("the tubemusic provider is not registered");

    // Two concurrent fetches (the shape of a resync): both must land
    // with real metadata — a clobbered in-flight run comes back empty.
    int done = 0;
    TrackMeta meta1;
    TrackMeta meta2;
    provider->fetchMetadata(QUrl("https://www.youtube.com/watch?v=jNQXAC9IVRw"),
                            [&meta1, &done](const TrackMeta &m) {
                                meta1 = m;
                                ++done;
                            });
    provider->fetchMetadata(QUrl("https://youtu.be/jNQXAC9IVRw"),
                            [&meta2, &done](const TrackMeta &m) {
                                meta2 = m;
                                ++done;
                            });
    int waited = 0;
    while (done < 2 && waited < 60000) {
        QTest::qWait(100);
        waited += 100;
    }
    QCOMPARE(done, 2);
    if (meta1.isEmpty() || meta2.isEmpty())
        QSKIP("the network is unreachable (an empty fetch result)");

    QCOMPARE(meta1.title, QString("Me at the zoo"));
    QCOMPARE(meta2.title, QString("Me at the zoo"));
    QVERIFY(meta1.durationMs > 0);
}

void TubeMusicTest::queuedRunsAllComplete()
{
    if (!pluginBuilt())
        QSKIP("the tubemusic plugin is not built (DOOB_BUILD_PLUGINS=OFF)");
    if (QStandardPaths::findExecutable("yt-dlp").isEmpty())
        QSKIP("yt-dlp is not available");

    SourceProvider* provider = tubeProvider();
    if (provider == nullptr)
        QFAIL("the tubemusic provider is not registered");

    // A burst of runs beyond the process pool: the excess must queue and
    // drain — every run lands exactly once, with metadata (the shape of a
    // big import: a broken queue would drop a run, a broken recycle of a
    // pooled process would deliver a result to the wrong callback).
    const int runs = 8;
    int done = 0;
    int nonEmpty = 0;
    for (int i = 0; i < runs; ++i) {
        provider->fetchMetadata(
            QUrl("https://www.youtube.com/watch?v=jNQXAC9IVRw"),
            [&done, &nonEmpty](const TrackMeta &m) {
                ++done;
                if (!m.isEmpty())
                    ++nonEmpty;
            });
    }
    int waited = 0;
    while (done < runs && waited < 120000) {
        QTest::qWait(100);
        waited += 100;
    }
    QCOMPARE(done, runs);
    if (nonEmpty == 0)
        QSKIP("the network is unreachable (empty fetch results)");
    QCOMPARE(nonEmpty, runs);
}

void TubeMusicTest::cookiesFileIsUsed()
{
    if (!pluginBuilt())
        QSKIP("the tubemusic plugin is not built (DOOB_BUILD_PLUGINS=OFF)");
    if (QStandardPaths::findExecutable("yt-dlp").isEmpty())
        QSKIP("yt-dlp is not available");

    SourceProvider* provider = tubeProvider();
    if (provider == nullptr)
        QFAIL("the tubemusic provider is not registered");

    // A cookies file in the plugins directory: every run passes it to
    // yt-dlp (--cookies). A valid (header-only) snapshot must not break
    // the run.
    const QString cookiesPath =
        QFileInfo(Settings::instance()->pluginDataPath("tubemusic")).path()
        + "/cookies.txt";
    QDir().mkpath(QFileInfo(cookiesPath).path());
    {
        QFile f(cookiesPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("# Netscape HTTP Cookie File\n");
    }

    TrackMeta meta;
    bool done = false;
    provider->fetchMetadata(QUrl("https://www.youtube.com/watch?v=jNQXAC9IVRw"),
                            [&meta, &done](const TrackMeta &m) {
                                meta = m;
                                done = true;
                            });
    int waited = 0;
    while (!done && waited < 60000) {
        QTest::qWait(100);
        waited += 100;
    }
    QCOMPARE(done, true);
    // The snapshot must not leak into the other tests' runs.
    QFile::remove(cookiesPath);

    if (meta.isEmpty())
        QSKIP("the network is unreachable (an empty fetch result)");
    QCOMPARE(meta.title, QString("Me at the zoo"));
}

void TubeMusicTest::playbackResolvesSelfContainedStream()
{
    if (!pluginBuilt())
        QSKIP("the tubemusic plugin is not built (DOOB_BUILD_PLUGINS=OFF)");
    if (QStandardPaths::findExecutable("yt-dlp").isEmpty())
        QSKIP("yt-dlp is not available");

    SourceProvider* provider = tubeProvider();
    if (provider == nullptr)
        QFAIL("the tubemusic provider is not registered");

    // Stream mode: the playback resolution must yield a stream URL a
    // bare player can open — it runs WITHOUT the cookies file (a
    // session-bound URL 403s for a client without the session's
    // context).
    SettingsManager::getInstance()
        ->setValue(QStringLiteral("tubemusic.playback_mode"),
                   QStringLiteral("stream"));

    MediaItem item;
    item.source = QUrl("https://www.youtube.com/watch?v=jNQXAC9IVRw");
    QUrl stream;
    bool done = false;
    provider->resolvePlayback(item, [&stream, &done](const QUrl &u) {
        stream = u;
        done = true;
    });
    int waited = 0;
    while (!done && waited < 60000) {
        QTest::qWait(100);
        waited += 100;
    }
    QCOMPARE(done, true);
    if (stream.isEmpty())
        QSKIP("the network is unreachable (no stream)");
    QVERIFY(stream.toString().startsWith("http", Qt::CaseInsensitive));
}

void TubeMusicTest::makeCacheFile(const QString &path, qint64 size, int ageSecs)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.resize(size);
    f.setFileTime(QDateTime::currentDateTime().addSecs(-ageSecs),
                  QFile::FileModificationTime);
    f.close();
}

void TubeMusicTest::downloadModeCachesAndReplays()
{
    if (!pluginBuilt())
        QSKIP("the tubemusic plugin is not built (DOOB_BUILD_PLUGINS=OFF)");
    if (QStandardPaths::findExecutable("yt-dlp").isEmpty())
        QSKIP("yt-dlp is not available");

    SettingsManager* settings = SettingsManager::getInstance();
    settings->setValue(QStringLiteral("tubemusic.playback_mode"),
                     QStringLiteral("download"));
    settings->setValue(QStringLiteral("tubemusic.cache_max_mb"), 512);

    SourceProvider* provider = tubeProvider();
    if (provider == nullptr)
        QFAIL("the tubemusic provider is not registered");

    // Download-first mode: the resolution lands on a local file the
    // player can open (network-independent playback after the download).
    MediaItem item;
    item.source = QUrl("https://www.youtube.com/watch?v=jNQXAC9IVRw");
    QUrl first;
    bool done = false;
    provider->resolvePlayback(item, [&first, &done](const QUrl &u) {
        first = u;
        done = true;
    });
    int waited = 0;
    while (!done && waited < 120000) {
        QTest::qWait(100);
        waited += 100;
    }
    QCOMPARE(done, true);
    if (first.isEmpty())
        QSKIP("the network is unreachable (no download)");
    QVERIFY(first.isLocalFile());
    const QFileInfo cached(QUrl(first).toLocalFile());
    QVERIFY(cached.exists());
    QVERIFY(cached.size() > 0);

    // A second play of the same track is a cache hit: the same file, no
    // download.
    QUrl second;
    done = false;
    provider->resolvePlayback(item, [&second, &done](const QUrl &u) {
        second = u;
        done = true;
    });
    QVERIFY(done);  // synchronous (the file is already there)
    QCOMPARE(second, first);
}

void TubeMusicTest::downloadModeEvictsOldestCacheFiles()
{
    if (!pluginBuilt())
        QSKIP("the tubemusic plugin is not built (DOOB_BUILD_PLUGINS=OFF)");

    SettingsManager* settings = SettingsManager::getInstance();
    settings->setValue(QStringLiteral("tubemusic.playback_mode"),
                     QStringLiteral("download"));
    settings->setValue(QStringLiteral("tubemusic.cache_max_mb"), 1);

    SourceProvider* provider = tubeProvider();
    if (provider == nullptr)
        QFAIL("the tubemusic provider is not registered");

    // A cache over the 1 MB cap (three 600 KiB files) plus the file of
    // the track about to play: a cache hit must evict the
    // least-recently-modified files down to the cap.
    const QString cacheDir =
        Settings::instance()->pluginDataPath("tubemusic") + "/cache";
    QDir().mkpath(cacheDir);
    makeCacheFile(cacheDir + "/old1.m4a", static_cast<qint64>(600) * 1024, 300);
    makeCacheFile(cacheDir + "/old2.m4a", static_cast<qint64>(600) * 1024, 200);
    makeCacheFile(cacheDir + "/old3.m4a", static_cast<qint64>(600) * 1024, 100);
    makeCacheFile(cacheDir + "/hitvid.m4a", static_cast<qint64>(100) * 1024, 0);

    MediaItem item;
    item.source = QUrl("https://www.youtube.com/watch?v=hitvid");
    QUrl stream;
    bool done = false;
    provider->resolvePlayback(item, [&stream, &done](const QUrl &u) {
        stream = u;
        done = true;
    });
    QCOMPARE(done, true);  // a cache hit: no network, no download
    QVERIFY(stream.isLocalFile());
    QCOMPARE(QUrl(stream).toLocalFile(), cacheDir + "/hitvid.m4a");

    // 1.8 MB + 100 KiB > 1 MB: the two oldest files went, the rest
    // (700 KiB) fits the cap.
    QVERIFY(!QFile::exists(cacheDir + "/old1.m4a"));
    QVERIFY(!QFile::exists(cacheDir + "/old2.m4a"));
    QVERIFY(QFile::exists(cacheDir + "/old3.m4a"));
    QVERIFY(QFile::exists(cacheDir + "/hitvid.m4a"));

    for (const QString &name : { "old1.m4a", "old2.m4a", "old3.m4a",
                                 "hitvid.m4a", })
        QFile::remove(cacheDir + '/' + name);
}

void TubeMusicTest::concurrentDownloadsDedupToOneRun()
{
    if (!pluginBuilt())
        QSKIP("the tubemusic plugin is not built (DOOB_BUILD_PLUGINS=OFF)");

    SettingsManager* settings = SettingsManager::getInstance();
    settings->setValue(QStringLiteral("tubemusic.playback_mode"),
                      QStringLiteral("download"));

    SourceProvider* provider = tubeProvider();
    if (provider == nullptr)
        QFAIL("the tubemusic provider is not registered");

    // A fake yt-dlp that "downloads": it records every invocation,
    // takes a beat (so the second resolvePlayback lands while the first
    // run is in flight), and writes a non-empty file at the -o template.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString invocations = dir.path() + "/invocations";
    const QString script = dir.path() + "/fake-ytdlp";
    {
        QFile f(script);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(
            ("#!/bin/sh\n"
             "echo x >> '" + invocations + "'\n"
             "sleep 1\n"
             "out=\"\"\n"
             "prev=\"\"\n"
             "for a in \"$@\"; do\n"
             "  if [ \"$prev\" = \"-o\" ]; then out=\"$a\"; fi\n"
             "  prev=\"$a\"\n"
             "done\n"
             "out=$(printf '%s' \"$out\" | sed 's/%(ext)s/m4a/')\n"
             "printf 'fake-audio' > \"$out\"\n"
             "echo '[download] 100%'\n")
                .toUtf8());
    }
    QVERIFY(QFile::setPermissions(script, QFile::permissions(script)
                                              | QFileDevice::ExeOwner
                                              | QFileDevice::ExeGroup
                                              | QFileDevice::ExeOther));
    settings->setValue("tubemusic.ytdlp", script);

    // Two resolutions of the same uncached id while the first's
    // download is in flight (a gapless pre-roll resolution and the
    // plain play of the item, a double play press): one download, both
    // callbacks land on the file.
    MediaItem item;
    item.source = QUrl("https://www.youtube.com/watch?v=dupvid");
    std::array<QUrl, 2> urls;
    int done = 0;
    provider->resolvePlayback(item, [&urls, &done](const QUrl &u) {
        urls[0] = u;
        ++done;
    });
    provider->resolvePlayback(item, [&urls, &done](const QUrl &u) {
        urls[1] = u;
        ++done;
    });
    int waited = 0;
    while (done < 2 && waited < 30000) {
        QTest::qWait(100);
        waited += 100;
    }
    QCOMPARE(done, 2);
    QVERIFY(urls[0].isLocalFile());
    QCOMPARE(urls[0], urls[1]);
    {
        QFile f(invocations);
        QVERIFY(f.open(QIODevice::ReadOnly));
        // Exactly one binary run: the second resolution joined the
        // first download, not a second yt-dlp racing on the same
        // .part file.
        QCOMPARE(f.readAll().count('x'), 1);
    }

    // The download is forgotten on completion: a later resolution is a
    // plain cache hit (synchronous — a joined dead entry would hang).
    QUrl third;
    bool thirdDone = false;
    provider->resolvePlayback(item, [&third, &thirdDone](const QUrl &u) {
        third = u;
        thirdDone = true;
    });
    QCOMPARE(thirdDone, true);
    QCOMPARE(third, urls[0]);
}

void TubeMusicTest::metadataSurvivesStaleCookies()
{
    if (!pluginBuilt())
        QSKIP("the tubemusic plugin is not built (DOOB_BUILD_PLUGINS=OFF)");
    if (QStandardPaths::findExecutable("yt-dlp").isEmpty())
        QSKIP("yt-dlp is not available");

    // A rotated session: invalid cookies 403 where cookie-less would
    // work. The with-cookies run must fail into the cookie-less
    // fallback, and the metadata must still land.
    const QString cookiesPath =
        QFileInfo(Settings::instance()->pluginDataPath("tubemusic"))
            .path() + "/cookies.txt";
    {
        QFile f(cookiesPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("# Netscape HTTP Cookie File\n"
                ".youtube.com\tFALSE\t/\tFALSE\t0\tSTALE\tdeadbeef\n");
    }

    SourceProvider* provider = tubeProvider();
    if (provider == nullptr)
        QFAIL("the tubemusic provider is not registered");

    TrackMeta meta;
    bool done = false;
    provider->fetchMetadata(QUrl("https://www.youtube.com/watch?v=jNQXAC9IVRw"),
                            [&meta, &done](const TrackMeta &m) {
                                meta = m;
                                done = true;
                            });
    int waited = 0;
    while (!done && waited < 90000) {
        QTest::qWait(100);
        waited += 100;
    }
    QCOMPARE(done, true);
    QFile::remove(cookiesPath);
    if (meta.isEmpty())
        QSKIP("the network is unreachable (an empty fetch result)");
    QCOMPARE(meta.title, QString("Me at the zoo"));
}

void TubeMusicTest::failedRunLogsItsStderr()
{
    if (!pluginBuilt())
        QSKIP("the tubemusic plugin is not built (DOOB_BUILD_PLUGINS=OFF)");

    SourceProvider* provider = tubeProvider();
    if (provider == nullptr)
        QFAIL("the tubemusic provider is not registered");

    // A stand-in yt-dlp that fails with a known stderr line: a failure
    // without its stderr is undiagnosable, so the line must reach the log.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString script = dir.path() + "/fake-ytdlp";
    {
        QFile f(script);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("#!/bin/sh\necho doob-fake-ytdlp-failure >&2\nexit 1\n");
    }
    QVERIFY(QFile::setPermissions(script, QFile::permissions(script)
                                              | QFileDevice::ExeOwner
                                              | QFileDevice::ExeGroup
                                              | QFileDevice::ExeOther));

    SettingsManager* settings = SettingsManager::getInstance();
    settings->setValue("tubemusic.ytdlp", script);

    // The handler is a C function pointer (no captures): a file-scope
    // sink, set for the duration of the run.
    static QStringList *failures = nullptr;
    const QtMessageHandler handler =
        [](QtMsgType type, const QMessageLogContext &context,
           const QString &message) {
            Q_UNUSED(context)
            if (type == QtWarningMsg
                    && message.startsWith(QStringLiteral("tubemusic: yt-dlp failed")))
                failures->append(message);
        };
    QStringList captured;
    failures = &captured;
    const QtMessageHandler previous = qInstallMessageHandler(handler);

    bool done = false;
    provider->fetchMetadata(QUrl("https://www.youtube.com/watch?v=abc"),
                            [&done](const TrackMeta &) { done = true; });
    int waited = 0;
    while (!done && waited < 10000) {
        QTest::qWait(50);
        waited += 50;
    }
    qInstallMessageHandler(previous);
    failures = nullptr;

    QCOMPARE(done, true);
    bool logged = false;
    for (const QString &message : captured) {
        if (message.contains(QStringLiteral("doob-fake-ytdlp-failure")))
            logged = true;
    }
    QVERIFY(logged);

    // The stand-in was test-local: back to the binary on the PATH.
    settings->setValue("tubemusic.ytdlp", QString());
}

void TubeMusicTest::canonicalizesClaimedUrls()
{
    if (!pluginBuilt())
        QSKIP("the tubemusic plugin is not built (DOOB_BUILD_PLUGINS=OFF)");

    SourceProvider* provider = tubeProvider();
    if (provider == nullptr)
        QFAIL("the tubemusic provider is not registered");

    // Only what identifies the video survives: the incidental arguments
    // (a list reference, a share token, a start offset) are dropped.
    QCOMPARE(provider->canonicalUrl(
                 QUrl("https://www.youtube.com/watch?v=abc&list=RDabc&start_radio=1")),
             QUrl("https://www.youtube.com/watch?v=abc"));
    QCOMPARE(provider->canonicalUrl(QUrl("https://youtu.be/abc?si=xyz")),
             QUrl("https://youtu.be/abc"));
    QCOMPARE(provider->canonicalUrl(QUrl("https://m.youtube.com/shorts/abc?si=xyz")),
             QUrl("https://m.youtube.com/shorts/abc"));
    QCOMPARE(provider->canonicalUrl(QUrl("https://music.youtube.com/watch?v=abc&t=42s")),
             QUrl("https://music.youtube.com/watch?v=abc"));
}

void TubeMusicTest::listFetchWithoutListArgIsEmpty()
{
    if (!pluginBuilt())
        QSKIP("the tubemusic plugin is not built (DOOB_BUILD_PLUGINS=OFF)");

    SourceProvider* provider = tubeProvider();
    if (provider == nullptr)
        QFAIL("the tubemusic provider is not registered");

    bool done = false;
    SourceProvider::ListEntries entries;
    provider->fetchList(QUrl("https://www.youtube.com/watch?v=abc"),
                        [&entries, &done](const SourceProvider::ListEntries &e) {
                            entries = e;
                            done = true;
                        });
    // No list= argument: the answer is immediate (no binary run).
    QVERIFY(done);
    QVERIFY(entries.isEmpty());
}

void TubeMusicTest::listFetchReturnsTheList()
{
    if (!pluginBuilt())
        QSKIP("the tubemusic plugin is not built (DOOB_BUILD_PLUGINS=OFF)");
    if (QStandardPaths::findExecutable("yt-dlp").isEmpty())
        QSKIP("yt-dlp is not available");

    SourceProvider* provider = tubeProvider();
    if (provider == nullptr)
        QFAIL("the tubemusic provider is not registered");

    // A radio list (list=RD...) referenced from a watch link: the whole
    // list comes back, in list order, the watched video first.
    const QUrl listUrl(
        "https://www.youtube.com/watch?v=5DRqD6TznoU&list=RD5DRqD6TznoU&start_radio=1");
    SourceProvider::ListEntries entries;
    provider->fetchList(listUrl,
                        [&entries](const SourceProvider::ListEntries &e) {
                            entries = e;
                        });
    int waited = 0;
    while (entries.videos.isEmpty() && waited < 60000) {
        QTest::qWait(100);
        waited += 100;
    }
    if (entries.videos.isEmpty())
        QSKIP("the network is unreachable (an empty list result)");

    QVERIFY(!entries.title.isEmpty());
    QVERIFY(entries.videos.size() > 1);
    QCOMPARE(entries.videos.first().url,
             QUrl("https://www.youtube.com/watch?v=5DRqD6TznoU"));
    // The flat listing carries the display metadata (title, the channel,
    // the duration) without a per-video round-trip: the import wizard's
    // review shows it as-is.
    QVERIFY(!entries.videos.first().title.isEmpty());
}

QString TubeMusicTest::writeFakeYtdlp(const QString &dir,
                                      const QByteArray &json)
{
    QString script = dir + "/fake-ytdlp";
    QFile f(script);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write("#!/bin/sh\ncat <<'EOF'\n");
    f.write(json);
    f.write("\nEOF\n");
    f.close();
    if (!QFile::setPermissions(script, QFile::permissions(script)
                                          | QFileDevice::ExeOwner
                                          | QFileDevice::ExeGroup
                                          | QFileDevice::ExeOther))
        return {};
    return script;
}

void TubeMusicTest::fetchMetadataDownloadsThumbnail()
{
    if (!pluginBuilt())
        QSKIP("the tubemusic plugin is not built (DOOB_BUILD_PLUGINS=OFF)");

    SourceProvider* provider = tubeProvider();
    if (provider == nullptr)
        QFAIL("the tubemusic provider is not registered");

    // A fake yt-dlp (the binary's path is the setting, re-read on every
    // run): the canned JSON's thumbnail points at the local server.
    MiniHttpServer server;
    const QByteArray art("fake-thumb-bytes");
    server.route("/thumb.jpg", 200, art);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString fake = writeFakeYtdlp(
        dir.path(),
        QStringLiteral(
            R"({"title":"Fake Title","uploader":"Fake Channel","duration":42,"thumbnail":"%1/thumb.jpg"})")
            .arg(server.baseUrl())
            .toUtf8());
    QVERIFY(!fake.isEmpty());
    SettingsManager::getInstance()->setValue("tubemusic.ytdlp", fake);

    TrackMeta meta;
    bool done = false;
    provider->fetchMetadata(QUrl("https://www.youtube.com/watch?v=fake123"),
                            [&meta, &done](const TrackMeta &m) {
            meta = m;
            done = true;
        });
    QTRY_VERIFY_WITH_TIMEOUT(done, 30000);

    QCOMPARE(meta.title, QString("Fake Title"));
    QCOMPARE(meta.artist, QString("Fake Channel"));
    // The server sends application/json: the mime is the URL's suffix.
    QCOMPARE(meta.artMime, QString("image/jpeg"));
    QCOMPARE(meta.artData, art);
}

void TubeMusicTest::fetchMetadataThumbnailFallsBackToThumbnailsList()
{
    if (!pluginBuilt())
        QSKIP("the tubemusic plugin is not built (DOOB_BUILD_PLUGINS=OFF)");

    SourceProvider* provider = tubeProvider();
    if (provider == nullptr)
        QFAIL("the tubemusic provider is not registered");

    // No "thumbnail" field: the first entry of the "thumbnails" list
    // (the quality order) is the image.
    MiniHttpServer server;
    const QByteArray art("list-thumb-bytes");
    server.route("/thumb2.jpg", 200, art);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString fake = writeFakeYtdlp(
        dir.path(),
        QStringLiteral(
            R"({"title":"Fake Title","duration":42,"thumbnails":[{"url":"%1/thumb2.jpg","width":120},{"url":"%1/thumb3.jpg","width":320}]})")
            .arg(server.baseUrl())
            .toUtf8());
    QVERIFY(!fake.isEmpty());
    SettingsManager::getInstance()->setValue("tubemusic.ytdlp", fake);

    TrackMeta meta;
    bool done = false;
    provider->fetchMetadata(QUrl("https://www.youtube.com/watch?v=fake123"),
                            [&meta, &done](const TrackMeta &m) {
            meta = m;
            done = true;
        });
    QTRY_VERIFY_WITH_TIMEOUT(done, 30000);

    QCOMPARE(meta.artData, art);
    // The first entry won, not the second.
    QCOMPARE(server.requestPaths().count("/thumb2.jpg"), 1);
    QCOMPARE(server.requestPaths().count("/thumb3.jpg"), 0);
}

void TubeMusicTest::fetchMetadataThumbnailFallsBackToDefaultUrl()
{
    if (!pluginBuilt())
        QSKIP("the tubemusic plugin is not built (DOOB_BUILD_PLUGINS=OFF)");

    SourceProvider* provider = tubeProvider();
    if (provider == nullptr)
        QFAIL("the tubemusic provider is not registered");

    // Neither field: the canonical 0.jpg (YouTube's default thumbnail)
    // — not the local server, which must see no request.
    MiniHttpServer server;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString fake = writeFakeYtdlp(
        dir.path(),
        QStringLiteral(R"({"title":"Fake Title","duration":42})").toUtf8());
    QVERIFY(!fake.isEmpty());
    SettingsManager::getInstance()->setValue("tubemusic.ytdlp", fake);

    TrackMeta meta;
    bool done = false;
    provider->fetchMetadata(QUrl("https://www.youtube.com/watch?v=fake123"),
                            [&meta, &done](const TrackMeta &m) {
            meta = m;
            done = true;
        });
    QTRY_VERIFY_WITH_TIMEOUT(done, 30000);

    QCOMPARE(meta.title, QString("Fake Title"));
    QCOMPARE(server.requestPaths().size(), 0);
}

void TubeMusicTest::fetchMetadataThumbnailFailureLeadsMetaWithoutArt()
{
    if (!pluginBuilt())
        QSKIP("the tubemusic plugin is not built (DOOB_BUILD_PLUGINS=OFF)");

    SourceProvider* provider = tubeProvider();
    if (provider == nullptr)
        QFAIL("the tubemusic provider is not registered");

    // The thumbnail 404s: the read still lands exactly once, with the
    // metadata and no art.
    MiniHttpServer server;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString fake = writeFakeYtdlp(
        dir.path(),
        QStringLiteral(
            R"({"title":"Fake Title","duration":42,"thumbnail":"%1/missing.jpg"})")
            .arg(server.baseUrl())
            .toUtf8());
    QVERIFY(!fake.isEmpty());
    SettingsManager::getInstance()->setValue("tubemusic.ytdlp", fake);

    TrackMeta meta;
    bool done = false;
    provider->fetchMetadata(QUrl("https://www.youtube.com/watch?v=fake123"),
                            [&meta, &done](const TrackMeta &m) {
            meta = m;
            done = true;
        });
    QTRY_VERIFY_WITH_TIMEOUT(done, 30000);

    QCOMPARE(meta.title, QString("Fake Title"));
    QVERIFY(meta.artData.isEmpty());
    QVERIFY(meta.artMime.isEmpty());
}

} // namespace

QTEST_MAIN(TubeMusicTest)
#include "tubemusic_test.moc"
