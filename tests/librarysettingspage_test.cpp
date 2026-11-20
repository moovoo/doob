#include <QtTest>

#include <QDir>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QTemporaryDir>

#include "librarymanager.h"
#include "librarysettingspage.h"
#include "librarywatcher.h"
#include "mediaitemstore.h"
#include "settings.h"

namespace {

// The page with the directory chooser replaced (QFileDialog is
// untestable offscreen).
class TestLibrarySettingsPage : public LibrarySettingsPage
{
public:
    using LibrarySettingsPage::LibrarySettingsPage;

    QString chosen;
    QString chooseDir() override { return chosen; }
};

class LibrarySettingsPageTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void showsTheCurrentWatchDirs();
    void addAddsTheChosenDir();
    void removeRemovesTheSelectedDir();
    void removeIsDisabledWithoutADir();

private:
    QListWidget* list(const LibrarySettingsPage& page) const
    {
        return page.findChild<QListWidget*>("watchDirs");
    }
    QPushButton* button(const LibrarySettingsPage& page, const QString& name) const
    {
        return page.findChild<QPushButton*>(name);
    }

    LibraryWatcher* m_watcher;
    LibraryManager* m_lib;
    MediaItemStore* m_store;
    QTemporaryDir m_dataDir;
    QTemporaryDir m_dir1;
    QTemporaryDir m_dir2;

    inline static QString s_dataPath;
};

void LibrarySettingsPageTest::initTestCase()
{
    s_dataPath = m_dataDir.path();
    qputenv("DOOB_DATA_DIR", s_dataPath.toUtf8());
    Settings::reset();
}

void LibrarySettingsPageTest::cleanupTestCase()
{
    qunsetenv("DOOB_DATA_DIR");
    Settings::reset();
}

void LibrarySettingsPageTest::init()
{
    m_lib = LibraryManager::getInstance();
    m_store = MediaItemStore::getInstance();
    m_store->setStoragePath(s_dataPath + "/doob.db");
    m_watcher = LibraryWatcher::getInstance();
    m_watcher->clear();
    m_store->clear();
    m_lib->clear();
}

void LibrarySettingsPageTest::cleanup()
{
    m_watcher->clear();
    m_lib->clear();
    m_store->clear();
}

void LibrarySettingsPageTest::showsTheCurrentWatchDirs()
{
    // A watched directory set up before the page opens: the page must
    // start from the current watcher state, not an empty list.
    QVERIFY(m_watcher->addWatchDir(m_dir1.path()));

    TestLibrarySettingsPage page;
    page.show();

    QCOMPARE(list(page)->count(), 1);
    QCOMPARE(list(page)->item(0)->text(), m_dir1.path());
    QCOMPARE(page.watchDirs(), QStringList() << m_dir1.path());
}

void LibrarySettingsPageTest::addAddsTheChosenDir()
{
    TestLibrarySettingsPage page;
    page.show();

    page.chosen = m_dir1.path();
    button(page, "addWatchDirButton")->click();

    QCOMPARE(list(page)->count(), 1);
    QCOMPARE(list(page)->item(0)->text(), m_dir1.path());
    QCOMPARE(m_watcher->watchDirs(), QStringList() << m_dir1.path());
}

void LibrarySettingsPageTest::removeRemovesTheSelectedDir()
{
    QVERIFY(m_watcher->addWatchDir(m_dir1.path()));
    QVERIFY(m_watcher->addWatchDir(m_dir2.path()));

    TestLibrarySettingsPage page;
    page.show();
    QCOMPARE(list(page)->count(), 2);

    // The watcher adds in call order: dir1 is the first row.
    list(page)->setCurrentRow(0);
    button(page, "removeWatchDirButton")->click();

    QCOMPARE(list(page)->count(), 1);
    QCOMPARE(list(page)->item(0)->text(), m_dir2.path());
    QCOMPARE(m_watcher->watchDirs(), QStringList() << m_dir2.path());
}

void LibrarySettingsPageTest::removeIsDisabledWithoutADir()
{
    TestLibrarySettingsPage page;
    page.show();

    QVERIFY(!button(page, "removeWatchDirButton")->isEnabled());

    QVERIFY(m_watcher->addWatchDir(m_dir1.path()));
    QVERIFY(button(page, "removeWatchDirButton")->isEnabled());
}

} // namespace

QTEST_MAIN(LibrarySettingsPageTest)
#include "librarysettingspage_test.moc"
