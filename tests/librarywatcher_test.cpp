#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "librarymanager.h"
#include "librarywatcher.h"
#include "settings.h"
#include "settingsmanager.h"
#include "testsupport.h"

namespace {

class LibraryWatcherTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void addWatchDirPersistsAndImports();
    void addWatchDirRejectsNonDirectory();
    void addWatchDirRejectsDuplicate();
    void removeWatchDirPersistsAndKeepsItems();
    void loadRestoresDirsAndImports();
    void copiedFileIsImported();
    void rapidChangesCoalesceToOneSync();
    void deletedFileIsRemoved();
    void deletedTopLevelDirLeavesTheList();
    void fileInNewSubdirectoryIsImported();

private:
    bool makeFile(const QString &name);
    MediaItem itemFor(const QString &name) const;

    // The persisted watch list as stored in settings.json.
    QStringList persistedDirs() const;

    LibraryWatcher* m_watcher;
    LibraryManager* m_lib;
    MediaItemStore* m_store;
    // Unique per run: the persisted watch list (settings.json) must not
    // carry over from an earlier run.
    QTemporaryDir m_dataDir;
    QTemporaryDir m_files;  // the watched directory

    inline static QString s_dataPath;
};

void LibraryWatcherTest::initTestCase()
{
    s_dataPath = m_dataDir.path();
    qputenv("DOOB_DATA_DIR", s_dataPath.toUtf8());
    Settings::reset();
    // The SettingsManager's store, private to this test's data dir.
    SettingsManager* settings = SettingsManager::getInstance();
    settings->setSettingsPath(s_dataPath + "/settings.json");
    settings->load();
}

void LibraryWatcherTest::cleanupTestCase()
{
    qunsetenv("DOOB_DATA_DIR");
    Settings::reset();
    SettingsManager::reset();
}

void LibraryWatcherTest::init()
{
    m_lib = LibraryManager::getInstance();
    m_store = MediaItemStore::getInstance();
    m_store->setStoragePath(s_dataPath + "/doob.db");
    m_watcher = LibraryWatcher::getInstance();
    m_watcher->clear();
    m_store->clear();
    m_lib->clear();
    // The watched directory must start empty (earlier tests leave files).
    QDir(m_files.path()).removeRecursively();
    QDir().mkpath(m_files.path());
}

void LibraryWatcherTest::cleanup()
{
    m_watcher->clear();
    m_lib->clear();
    m_store->clear();
}

bool LibraryWatcherTest::makeFile(const QString &name)
{
    const QString path = m_files.path() + "/" + name;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write("\x00\x01\x02");
    f.close();
    return true;
}

MediaItem LibraryWatcherTest::itemFor(const QString &name) const
{
    for (const MediaItem &item : m_lib->items())
        if (item.source.toLocalFile().endsWith("/" + name))
            return item;
    return {};
}

QStringList LibraryWatcherTest::persistedDirs() const
{
    QFile f(s_dataPath + "/settings.json");
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll())
        .object()
        .value("library.watch.dirs")
        .toVariant()
        .toStringList();
}

void LibraryWatcherTest::addWatchDirPersistsAndImports()
{
    QVERIFY(makeFile("one.mp3"));
    QVERIFY(makeFile("sub/two.flac"));

    QSignalSpy changed(m_watcher, &LibraryWatcher::watchDirsChanged);
    QVERIFY(m_watcher->addWatchDir(m_files.path()));
    QCOMPARE(changed.count(), 1);

    // The initial import runs through the library's operation queue.
    QTRY_COMPARE_WITH_TIMEOUT(m_lib->count(), 2, 10000);
    test::waitForIdle(m_lib);
    QVERIFY(itemFor("one.mp3").isValid());
    QVERIFY(itemFor("two.flac").isValid());

    // The list is persisted as the "library.watch.dirs" setting.
    QCOMPARE(persistedDirs(), QStringList() << m_files.path());
    QCOMPARE(m_watcher->watchDirs(), QStringList() << m_files.path());
}

void LibraryWatcherTest::addWatchDirRejectsNonDirectory()
{
    QVERIFY(makeFile("afile.mp3"));

    QVERIFY(!m_watcher->addWatchDir(m_files.path() + "/afile.mp3"));
    QVERIFY(!m_watcher->addWatchDir(m_files.path() + "/missing"));
    QCOMPARE(m_watcher->watchDirs().size(), 0);
    QCOMPARE(m_lib->count(), 0);
}

void LibraryWatcherTest::addWatchDirRejectsDuplicate()
{
    QVERIFY(m_watcher->addWatchDir(m_files.path()));
    test::waitForIdle(m_lib);

    QVERIFY(!m_watcher->addWatchDir(m_files.path()));
    QCOMPARE(m_watcher->watchDirs().size(), 1);
}

void LibraryWatcherTest::removeWatchDirPersistsAndKeepsItems()
{
    QVERIFY(makeFile("one.mp3"));
    QVERIFY(m_watcher->addWatchDir(m_files.path()));
    QTRY_VERIFY_WITH_TIMEOUT(itemFor("one.mp3").isValid(), 10000);
    test::waitForIdle(m_lib);

    QVERIFY(m_watcher->removeWatchDir(m_files.path()));

    QCOMPARE(m_watcher->watchDirs().size(), 0);
    QCOMPARE(m_lib->count(), 1);  // the items stay in the library

    QCOMPARE(persistedDirs(), QStringList());
}

void LibraryWatcherTest::loadRestoresDirsAndImports()
{
    QVERIFY(makeFile("one.mp3"));
    {
        QJsonObject obj;
        obj.insert("library.watch.dirs",
                   QJsonArray::fromVariantList(QVariantList{ m_files.path() }));
        QFile f(s_dataPath + "/settings.json");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(obj).toJson());
        f.close();
    }
    SettingsManager::getInstance()->load();  // the watcher reads the store

    m_watcher->load();

    QTRY_VERIFY_WITH_TIMEOUT(itemFor("one.mp3").isValid(), 10000);
    test::waitForIdle(m_lib);
    QCOMPARE(m_watcher->watchDirs(), QStringList() << m_files.path());
}

void LibraryWatcherTest::copiedFileIsImported()
{
    QVERIFY(m_watcher->addWatchDir(m_files.path()));
    test::waitForIdle(m_lib);
    QCOMPARE(m_lib->count(), 0);

    // A file lands in the watched directory: the watcher's resync
    // imports it.
    QVERIFY(makeFile("copied.mp3"));
    QTRY_VERIFY_WITH_TIMEOUT(itemFor("copied.mp3").isValid(), 10000);
    test::waitForIdle(m_lib);
    QCOMPARE(m_lib->count(), 1);
}

void LibraryWatcherTest::rapidChangesCoalesceToOneSync()
{
    QVERIFY(m_watcher->addWatchDir(m_files.path()));
    test::waitForIdle(m_lib);

    QSignalSpy busy(m_lib, &LibraryManager::busyChanged);
    // A churn (a sync tool swapping files in place), paced so the
    // events straddle the rescans ...
    for (int i = 0; i < 10; ++i) {
        QVERIFY(makeFile("churn.mp3"));
        QVERIFY(QFile::remove(m_files.path() + "/churn.mp3"));
        QTest::qWait(50);
    }
    // ... and must trigger one rescan after the quiet, not one per
    // event (a rescan per event keeps the library permanently busy).
    QTRY_VERIFY_WITH_TIMEOUT(busy.count() > 0, 10000);
    test::waitForIdle(m_lib);
    int starts = 0;
    for (const QList<QVariant> &args : busy)
        if (args.at(0).toBool())
            ++starts;
    QCOMPARE(starts, 1);
    QCOMPARE(m_lib->count(), 0);  // the churned file never settled
}

void LibraryWatcherTest::deletedFileIsRemoved()
{
    QVERIFY(makeFile("doomed.mp3"));
    QVERIFY(m_watcher->addWatchDir(m_files.path()));
    QTRY_VERIFY_WITH_TIMEOUT(itemFor("doomed.mp3").isValid(), 10000);
    test::waitForIdle(m_lib);

    // The file disappears: the watcher's resync removes the item.
    QVERIFY(QFile::remove(m_files.path() + "/doomed.mp3"));
    QTRY_VERIFY_WITH_TIMEOUT(!itemFor("doomed.mp3").isValid(), 10000);
    test::waitForIdle(m_lib);
    QCOMPARE(m_lib->count(), 0);
}

void LibraryWatcherTest::deletedTopLevelDirLeavesTheList()
{
    QVERIFY(makeFile("one.mp3"));
    QVERIFY(m_watcher->addWatchDir(m_files.path()));
    QTRY_VERIFY_WITH_TIMEOUT(itemFor("one.mp3").isValid(), 10000);
    test::waitForIdle(m_lib);

    // The watched directory itself disappears: the items under it are
    // removed and the dead directory leaves the list (the QTemporaryDir
    // member's path is recreated by the next init()). The removal runs
    // on the debounced rescan, so wait it out.
    QVERIFY(QDir(m_files.path()).removeRecursively());

    QTRY_COMPARE_WITH_TIMEOUT(m_watcher->watchDirs().size(), 0, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(m_lib->count() == 0, 10000);
    test::waitForIdle(m_lib);
}

void LibraryWatcherTest::fileInNewSubdirectoryIsImported()
{
    QVERIFY(m_watcher->addWatchDir(m_files.path()));
    test::waitForIdle(m_lib);

    // A new subdirectory appears with a file in it: the watcher
    // re-registers the tree and the resync finds the file.
    QVERIFY(QDir(m_files.path()).mkdir("newsub"));
    QVERIFY(makeFile("newsub/inner.flac"));
    QTRY_VERIFY_WITH_TIMEOUT(itemFor("inner.flac").isValid(), 10000);
    test::waitForIdle(m_lib);
    QCOMPARE(m_lib->count(), 1);
}

} // namespace

QTEST_MAIN(LibraryWatcherTest)
#include "librarywatcher_test.moc"
