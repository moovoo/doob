#include <QtTest>
#include <QApplication>
#include <QTemporaryDir>

#include <QAction>
#include <QFile>
#include <QMenu>
#include <QMenuBar>
#include <QSystemTrayIcon>
#include <QUrl>

#include <memory>

#include "fakeplayer.h"
#include "librarymanager.h"
#include "mainwindow.h"
#include "mediaitemstore.h"
#include "playbackmanager.h"
#include "playlistmanager.h"
#include "settingsmanager.h"
#include "testsupport.h"
#include "trayicon.h"

namespace {

// The decision's seams: the tray's availability (offscreen it is
// unavailable, the tests simulate it) and the Quit action's app quit
// (the tests count it, the app quits). The subclass registers itself
// as the singleton, so the closeEvent integration (which asks
// TrayIcon::getInstance()) runs against it.
class TestTrayIcon : public TrayIcon
{
public:
    explicit TestTrayIcon(QObject *parent = nullptr)
        : TrayIcon(parent)
    {
        m_instance = this;
    }
    ~TestTrayIcon() override { m_instance = nullptr; }

    bool m_available = true;
    bool trayAvailable() const override { return m_available; }

    int m_quits = 0;
    void quit() override { ++m_quits; }

    // The protected activation seam (offscreen the QPA never emits
    // activated).
    void activate(QSystemTrayIcon::ActivationReason reason)
    {
        onActivated(reason);
    }
};

// The platform's menu-bar-hiding support seam (the macOS case: the
// native global menu bar — a per-window hide is a no-op): the tray's
// "Show menu bar" entry is not created.
class NoMenuBarWindow : public MainWindow
{
public:
    using MainWindow::MainWindow;

    bool menuBarSupported() const override { return false; }
};

class TrayIconTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void theCloseActionSettingDefaultsToTray();
    void theDecisionNeedsATrayAndTheSetting();
    void theDecisionIsOffWhileQuitting();
    void thePlayPauseTracksThePlaybackState();
    void theNextPreviousActionsDriveThePlayback();
    void theShowActionRestoresTheWindow();
    void theTrayClickShowsTheWindow();
    void theQuitActionPreparesTheQuit();
    void theCloseHidesToTheTrayByDefault();
    void theCloseQuitsWhenTheActionIsExit();
    void theMenuBarEntryShowsTheHiddenMenuBar();
    void theMenuBarEntryIsGatedOnThePlatform();

private:
    // The menu's action by objectName (nullptr when absent — the
    // QFAIL goes in the test, the helpers cannot carry it).
    QAction* action(const QString &objectName);
    // A playable local item (a real file — a local file that is gone
    // is unplayable before the player is even asked), added to the
    // current playlist.
    MediaItem addItem(const QString &title);

    FakePlayer m_fake{this};
    TestTrayIcon* m_tray = nullptr;
    MainWindow* m_window = nullptr;
    PlaylistManager* m_pl = nullptr;
    PlaybackManager* m_pm = nullptr;
    MediaItemStore* m_store = nullptr;
    std::unique_ptr<QTemporaryDir> m_dir;
};

void TrayIconTest::init()
{
    m_pl = PlaylistManager::getInstance();
    m_pm = PlaybackManager::getInstance();
    m_store = MediaItemStore::getInstance();
    m_dir = std::make_unique<QTemporaryDir>();
    test::usePrivateDatabase(m_dir->path());
    LibraryManager::getInstance()->clear();
    m_pl->reset();
    m_store->clear();

    // The tray's setting writes through to the SettingsManager: keep
    // that store private too, so the test does not touch the user's
    // settings.json.
    SettingsManager* settings = SettingsManager::getInstance();
    settings->setSettingsPath(m_dir->path() + "/settings.json");
    settings->load();

    test::installFakePlayer(m_pm, &m_fake);

    m_tray = new TestTrayIcon();
    m_window = new MainWindow();
}

void TrayIconTest::cleanup()
{
    delete m_tray;
    m_tray = nullptr;
    delete m_window;
    m_window = nullptr;
    m_pm->stop();
    LibraryManager::getInstance()->clear();
    m_pl->reset();
    m_store->clear();
    SettingsManager::reset();
}

QAction* TrayIconTest::action(const QString &objectName)
{
    return m_tray->menu()->findChild<QAction*>(objectName);
}

MediaItem TrayIconTest::addItem(const QString &title)
{
    const QString path = m_dir->path() + "/" + title + ".mp3";
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("addItem: cannot open %s", qPrintable(path));
        return {};
    }
    f.write("\x00\x01\x02");
    f.close();

    MediaItem item;
    item.source = QUrl::fromLocalFile(path);
    item.title = title;
    if (!m_store->insert(item))
        return {};
    m_pl->addItems({ item.id });
    return item;
}

void TrayIconTest::theCloseActionSettingDefaultsToTray()
{
    SettingsManager* settings = SettingsManager::getInstance();
    const Setting setting = settings->setting("window.close_action");
    QVERIFY(setting.isValid());
    QCOMPARE(setting.defaultValue.toString(), QString("tray"));
    QCOMPARE(settings->value("window.close_action").toString(),
             QString("tray"));
}

void TrayIconTest::theDecisionNeedsATrayAndTheSetting()
{
    // Without a tray (the offscreen reality): "tray" degrades to
    // "exit" (a hidden window would be unreachable).
    m_tray->m_available = false;
    SettingsManager::getInstance()->setValue("window.close_action", "tray");
    QCOMPARE(m_tray->closeAction(), TrayIcon::CloseAction::Exit);
    QVERIFY(!m_tray->shouldHideOnClose());
    QVERIFY(m_tray->shouldQuitOnClose());

    // With a tray: the default is "tray", "exit" quits.
    m_tray->m_available = true;
    SettingsManager::getInstance()->resetToDefault("window.close_action");
    QCOMPARE(m_tray->closeAction(), TrayIcon::CloseAction::Tray);
    QVERIFY(m_tray->shouldHideOnClose());
    QVERIFY(!m_tray->shouldQuitOnClose());
    SettingsManager::getInstance()->setValue("window.close_action", "exit");
    QCOMPARE(m_tray->closeAction(), TrayIcon::CloseAction::Exit);
    QVERIFY(!m_tray->shouldHideOnClose());
    QVERIFY(m_tray->shouldQuitOnClose());
}

void TrayIconTest::theDecisionIsOffWhileQuitting()
{
    m_tray->m_available = true;
    SettingsManager::getInstance()->setValue("window.close_action", "tray");
    QVERIFY(m_tray->shouldHideOnClose());

    m_tray->prepareQuit();
    QVERIFY(!m_tray->shouldHideOnClose());
    // ...and no re-quit from the teardown's close.
    SettingsManager::getInstance()->setValue("window.close_action", "exit");
    QVERIFY(!m_tray->shouldQuitOnClose());
}

void TrayIconTest::thePlayPauseTracksThePlaybackState()
{
    QAction* playPause = action("playPauseAction");
    if (playPause == nullptr) {
        QFAIL("no playPauseAction in the tray menu");
        return;
    }
    const MediaItem item = addItem("Song");
    if (!item.isValid()) {
        QFAIL("no playable item");
        return;
    }

    // Stopped: "Play".
    QCOMPARE(playPause->text(), QString("Play"));

    // Playing: "Pause" — and the trigger pauses.
    m_pm->playFromPlaylist(m_pl->currentPlaylistId(), 0);
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(playPause->text(), QString("Pause"));

    playPause->trigger();
    QCOMPARE(m_pm->state(), PlaybackManager::Paused);
    QCOMPARE(playPause->text(), QString("Play"));

    // Paused: the trigger resumes.
    playPause->trigger();
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(playPause->text(), QString("Pause"));
}

void TrayIconTest::theNextPreviousActionsDriveThePlayback()
{
    QAction* next = action("nextAction");
    QAction* previous = action("previousAction");
    if (next == nullptr || previous == nullptr) {
        QFAIL("no next/previous actions in the tray menu");
        return;
    }
    const MediaItem first = addItem("First");
    const MediaItem second = addItem("Second");
    if (!first.isValid() || !second.isValid()) {
        QFAIL("no playable items");
        return;
    }

    m_pm->playFromPlaylist(m_pl->currentPlaylistId(), 0);
    QCOMPARE(m_pm->playingItem().id, first.id);

    next->trigger();
    QCOMPARE(m_pm->playingItem().id, second.id);

    previous->trigger();
    QCOMPARE(m_pm->playingItem().id, first.id);
}

void TrayIconTest::theShowActionRestoresTheWindow()
{
    QAction* show = action("showAction");
    if (show == nullptr) {
        QFAIL("no showAction in the tray menu");
        return;
    }

    m_tray->setWindow(m_window);
    m_window->show();
    qApp->processEvents();
    m_window->hide();
    qApp->processEvents();
    QVERIFY(!m_window->isVisible());

    show->trigger();
    qApp->processEvents();

    QVERIFY(m_window->isVisible());
}

void TrayIconTest::theTrayClickShowsTheWindow()
{
    m_tray->setWindow(m_window);
    m_window->show();
    qApp->processEvents();
    m_window->hide();
    qApp->processEvents();
    QVERIFY(!m_window->isVisible());

    // The context menu's open does not bring the window forward.
    m_tray->activate(QSystemTrayIcon::Context);
    qApp->processEvents();
    QVERIFY(!m_window->isVisible());

    // A click does.
    m_tray->activate(QSystemTrayIcon::Trigger);
    qApp->processEvents();
    QVERIFY(m_window->isVisible());

    // ...and so does a double-click.
    m_window->hide();
    qApp->processEvents();
    m_tray->activate(QSystemTrayIcon::DoubleClick);
    qApp->processEvents();
    QVERIFY(m_window->isVisible());
}

void TrayIconTest::theQuitActionPreparesTheQuit()
{
    QAction* quit = action("quitAction");
    if (quit == nullptr) {
        QFAIL("no quitAction in the tray menu");
        return;
    }

    m_tray->m_available = true;
    SettingsManager::getInstance()->setValue("window.close_action", "tray");
    QVERIFY(m_tray->shouldHideOnClose());

    quit->trigger();

    QCOMPARE(m_tray->m_quits, 1);
    // The quit is prepared: the teardown's close closes for real,
    // not to the tray.
    QVERIFY(!m_tray->shouldHideOnClose());
}

// The closeEvent integration, the default ("tray") setting: the window
// hides and the app lives on — the close is ignored, not closed.
void TrayIconTest::theCloseHidesToTheTrayByDefault()
{
    m_tray->m_available = true;
    SettingsManager::getInstance()->resetToDefault("window.close_action");
    m_tray->setWindow(m_window);
    m_window->show();
    qApp->processEvents();
    QVERIFY(m_window->isVisible());

    const bool quitOnLast = qApp->quitOnLastWindowClosed();
    qApp->setQuitOnLastWindowClosed(false);
    m_window->close();
    qApp->processEvents();
    qApp->setQuitOnLastWindowClosed(quitOnLast);

    // Hidden to the tray, not closed (a re-show works).
    QVERIFY(!m_window->isVisible());
    m_tray->activate(QSystemTrayIcon::Trigger);
    qApp->processEvents();
    QVERIFY(m_window->isVisible());
}

// The closeEvent integration, the "exit" action: the window closes for
// real, and the quit is prepared (the teardown's close must not
// re-quit, nor hide).
void TrayIconTest::theCloseQuitsWhenTheActionIsExit()
{
    m_tray->m_available = true;
    SettingsManager::getInstance()->setValue("window.close_action", "exit");
    QVERIFY(m_tray->shouldQuitOnClose());

    m_window->show();
    qApp->processEvents();
    const bool quitOnLast = qApp->quitOnLastWindowClosed();
    qApp->setQuitOnLastWindowClosed(false);
    m_window->close();
    qApp->processEvents();
    qApp->setQuitOnLastWindowClosed(quitOnLast);

    QVERIFY(!m_window->isVisible());
    QVERIFY(!m_tray->shouldQuitOnClose());
}

// The "Show menu bar" entry (7a's third recovery layer): created for a
// window whose platform supports the menu bar's hiding, inserted before
// "Show window", its check following the live visibility (the menu
// bar's Show/Hide events), and showing the bar on a trigger (show-only).
void TrayIconTest::theMenuBarEntryShowsTheHiddenMenuBar()
{
    m_tray->setWindow(m_window);
    m_window->show();
    qApp->processEvents();

    QAction* entry = action("menuBarAction");
    if (entry == nullptr) {
        QFAIL("no menuBarAction in the tray menu");
        return;
    }
    QVERIFY(entry->isCheckable());

    // Inserted before "Show window".
    int entryIndex = -1;
    int showIndex = -1;
    int i = 0;
    for (QAction* a : m_tray->menu()->actions()) {
        if (a->objectName() == "menuBarAction")
            entryIndex = i;
        if (a->objectName() == "showAction")
            showIndex = i;
        ++i;
    }
    QVERIFY(entryIndex >= 0);
    QVERIFY(showIndex > entryIndex);

    // Shown by default.
    QVERIFY(!m_window->menuBar()->isHidden());
    QVERIFY(entry->isChecked());

    // A hide is followed (the menu bar's Hide event).
    m_window->setMenuBarVisible(false);
    qApp->processEvents();
    QVERIFY(m_window->menuBar()->isHidden());
    QVERIFY(!entry->isChecked());

    // The trigger shows the bar (show-only) and the check follows.
    entry->trigger();
    qApp->processEvents();
    QVERIFY(!m_window->menuBar()->isHidden());
    QVERIFY(entry->isChecked());
}

// The entry's platform gate: a window whose platform does not support
// the menu bar's hiding (macOS) gets no entry.
void TrayIconTest::theMenuBarEntryIsGatedOnThePlatform()
{
    NoMenuBarWindow* window = new NoMenuBarWindow();
    TestTrayIcon* tray = new TestTrayIcon();
    tray->setWindow(window);
    qApp->processEvents();

    QVERIFY(tray->menu()->findChild<QAction*>("menuBarAction") == nullptr);

    delete tray;
    delete window;
}

}  // namespace

QTEST_MAIN(TrayIconTest)
#include "trayicon_test.moc"
