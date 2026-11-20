#include <QtTest>
#include <QApplication>
#include <QTemporaryDir>

#include <QAction>
#include <QFile>
#include <QJsonObject>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QUrl>

#include <memory>

#include "layoutmanager.h"
#include "mainwindow.h"
#include "currentplaylistwidget.h"
#include "fakeplayer.h"
#include "librarymanager.h"
#include "mediaitemstore.h"
#include "medialistview.h"
#include "playbackmanager.h"
#include "playlistmanager.h"
#include "playlistswidget.h"
#include "settingsmanager.h"
#include "shortcutmanager.h"
#include "taggedfile.h"
#include "trayicon.h"
#include "testsupport.h"
#include "titlebar.h"
#include "widgetcontainer.h"

namespace {

// The "Open..." chooser seam (the QFileDialog statics are untestable
// offscreen, the LibraryWidget pattern).
class TestMainWindow : public MainWindow
{
public:
    using MainWindow::MainWindow;

    QStringList m_chosenFiles;
    QStringList chooseAudioFiles() override { return m_chosenFiles; }

    // The focused view for the import target's resolution (the
    // offscreen platform never sets a focus widget).
    QWidget* m_focusedView = nullptr;
    QWidget* focusSource() const override { return m_focusedView; }

    // The "Exit doob" action's app quit (the test seam: the test
    // counts it, the app quits).
    int m_quits = 0;
    void quitApp() override { ++m_quits; }
};

class MainWindowTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void openActionImportsChosenFilesIntoCurrentPlaylist();
    void openActionEmptyChoiceImportsNothing();
    void importTargetDefaultIsTheCurrentPlaylist();
    void importTargetFocusedUsesTheFocusedTabView();
    void importTargetFocusedUsesTheCurrentPlaylistView();
    void importTargetFocusedFallsBackWithoutFocus();
    void focusedPlaylistIdWalksUpToTheNearestView();
    void titleShowsPlayingTrack();
    void titleKeepsTheTrackWhenPaused();
    void titleShowsBareNameOnStop();
    void fileMenuHasTheReorderedActions();
    void exitActionQuitsTheApp();
    void theViewMenuTogglesTheMenuBar();
    void theWindowSubmenuTogglesFrameless();
    void f10ShortcutShowsTheHiddenMenuBar();
    void theFirstHideShowsTheOneShotHint();
    void framelessTogglesTheWindowFlag();
    void theFallbackStripFollowsFramelessAndTheTitlebar();
    void theResizeEdgeHitTest();
    void theLayoutDocumentRoundTripsTheWindowChrome();

private:
    QString makeTempFile(const QString &name);
    QMenu* findFileMenu();
    QMenu* findViewMenu();
    QAction* findAction(QMenu *menu, const QString &text);
    // The File menu's "Open..." (the tests' import trigger).
    void openAction();
    // The import is asynchronous (the tag reading runs on a worker
    // thread): true when the row has landed in the named playlist.
    bool importLanded(quint64 playlistId);

    FakePlayer m_fake{this};
    TestMainWindow* m_window;
    PlaylistManager* m_pl;
    PlaybackManager* m_pm;
    MediaItemStore* m_store;
    std::unique_ptr<QTemporaryDir> m_dir;
};

// The store's window (the document round-trip and the F10 registration
// run against it; the layoutmanager_test's pattern — the window lives
// until the test process ends).
void MainWindowTest::initTestCase()
{
    LayoutManager::getInstance()->show();
}

void MainWindowTest::init()
{
    m_pl = PlaylistManager::getInstance();
    m_pm = PlaybackManager::getInstance();
    m_store = MediaItemStore::getInstance();
    m_dir = std::make_unique<QTemporaryDir>();
    test::usePrivateDatabase(m_dir->path());
    LibraryManager::getInstance()->clear();
    m_pl->reset();
    m_store->clear();

    test::installFakePlayer(m_pm, &m_fake);

    // The import.target setting writes through to the SettingsManager:
    // keep that store private too, so the test does not touch the
    // user's settings.json.
    SettingsManager* settings = SettingsManager::getInstance();
    settings->setSettingsPath(m_dir->path() + "/settings.json");
    settings->load();

    m_window = new TestMainWindow();
    m_window->show();

    // The F10 registration belongs to the window that last registered
    // it (the store's, from initTestCase — the idempotent registration
    // is refused here): take it over, so the shortcut's trigger runs
    // against this test's window.
    ShortcutManager* shortcuts = ShortcutManager::getInstance();
    shortcuts->unregisterAction("window.show_menu_bar");
    shortcuts->registerAction({
        QStringLiteral("window.show_menu_bar"),
        QStringLiteral("Show menu bar"),
        QStringLiteral("Window"),
        QKeySequence(Qt::Key_F10),
        true,
        [this] { m_window->showMenuBar(); },
    });
}

void MainWindowTest::cleanup()
{
    delete m_window;
    m_pm->stop();
    LibraryManager::getInstance()->clear();
    m_pl->reset();
    m_store->clear();
    SettingsManager::reset();
}

QString MainWindowTest::makeTempFile(const QString &name)
{
    QString path = m_dir->path() + "/" + name;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("makeTempFile: cannot open %s", qPrintable(path));
        return {};
    }
    f.write("\x00\x01\x02");
    f.close();
    return { path };
}

QMenu* MainWindowTest::findFileMenu()
{
    for (QAction *a : m_window->menuBar()->actions()) {
        QMenu *menu = a->menu();
        if (menu != nullptr && menu->title() == "File")
            return menu;
    }
    return nullptr;
}

QAction* MainWindowTest::findAction(QMenu *menu, const QString &text)
{
    for (QAction *a : menu->actions())
        if (a->text() == text)
            return a;
    return nullptr;
}

void MainWindowTest::openAction()
{
    QMenu *fileMenu = findFileMenu();
    if (fileMenu == nullptr) {
        QFAIL("no File menu");
        return;
    }
    QAction *open = findAction(fileMenu, "Open...");
    if (open == nullptr) {
        QFAIL("no \"Open...\" action");
        return;
    }
    open->trigger();
}

void MainWindowTest::openActionImportsChosenFilesIntoCurrentPlaylist()
{
    // A real audio file (the import reads its tags; an unreadable file
    // contributes no row).
    const QUrl url = makeTaggedFile(m_dir->path(), "song.flac",
                                    "Song Title", "The Artist", "Album");
    if (url.isEmpty())
        QSKIP("ffmpeg not available to generate a tagged file");

    QMenu *fileMenu = findFileMenu();
    if (fileMenu == nullptr) {
        QFAIL("no File menu");
        return;
    }
    QAction *open = findAction(fileMenu, "Open...");
    if (open == nullptr) {
        QFAIL("no \"Open...\" action");
        return;
    }
    QVERIFY(open->isEnabled());

    m_window->m_chosenFiles = { url.toLocalFile() };
    open->trigger();

    // The import is asynchronous (the tag reading runs on a worker
    // thread): wait for the row to land in the current playlist.
    QVERIFY(test::waitUntil([this] {
        return m_pl->playlist(m_pl->currentPlaylistId()).items.size() == 1;
    }));
    const quint64 id = m_pl->playlist(m_pl->currentPlaylistId()).items.first();
    const MediaItem item = MediaItemStore::resolve(id);
    QVERIFY(item.isValid());
    QCOMPARE(item.source, url);
    QCOMPARE(item.title, QString("Song Title"));
}

void MainWindowTest::openActionEmptyChoiceImportsNothing()
{
    QMenu *fileMenu = findFileMenu();
    if (fileMenu == nullptr) {
        QFAIL("no File menu");
        return;
    }
    QAction *open = findAction(fileMenu, "Open...");
    if (open == nullptr) {
        QFAIL("no \"Open...\" action");
        return;
    }

    m_window->m_chosenFiles = {};
    open->trigger();
    // Give a (mistaken) import time to start: none may.
    QTest::qWait(300);
    QCOMPARE(m_pl->playlist(m_pl->currentPlaylistId()).items.size(), 0);
}

// The import is asynchronous (the tag reading runs on a worker thread):
// wait for the row to land in the named playlist.
bool MainWindowTest::importLanded(quint64 playlistId)
{
    return test::waitUntil([this, playlistId] {
        return m_pl->playlist(playlistId).items.size() == 1;
    });
}

void MainWindowTest::importTargetDefaultIsTheCurrentPlaylist()
{
    const QUrl url = makeTaggedFile(m_dir->path(), "song.flac",
                                    "Song Title", "The Artist", "Album");
    if (url.isEmpty())
        QSKIP("ffmpeg not available to generate a tagged file");
    const quint64 other = m_pl->addPlaylist("Other");

    // A focused view on the other playlist: the default target (the
    // current playlist) ignores it.
    PlaylistsWidget* tabs = new PlaylistsWidget(m_window);
    tabs->show();
    m_window->m_focusedView =
        qobject_cast<MediaListView*>(tabs->tabWidget()->widget(1));
    QVERIFY(m_window->m_focusedView != nullptr);

    m_window->m_chosenFiles = { url.toLocalFile() };
    openAction();

    QVERIFY(importLanded(m_pl->currentPlaylistId()));
    QCOMPARE(m_pl->playlist(other).items.size(), 0);
}

void MainWindowTest::importTargetFocusedUsesTheFocusedTabView()
{
    const QUrl url = makeTaggedFile(m_dir->path(), "song.flac",
                                    "Song Title", "The Artist", "Album");
    if (url.isEmpty())
        QSKIP("ffmpeg not available to generate a tagged file");
    const quint64 other = m_pl->addPlaylist("Other");

    PlaylistsWidget* tabs = new PlaylistsWidget(m_window);
    tabs->show();
    m_window->m_focusedView =
        qobject_cast<MediaListView*>(tabs->tabWidget()->widget(1));
    QVERIFY(m_window->m_focusedView != nullptr);
    SettingsManager::getInstance()->setValue("import.target", "focused");

    m_window->m_chosenFiles = { url.toLocalFile() };
    openAction();

    // The import lands in the focused tab's playlist, not the current
    // one.
    QVERIFY(importLanded(other));
    QCOMPARE(m_pl->playlist(m_pl->currentPlaylistId()).items.size(), 0);
}

void MainWindowTest::importTargetFocusedUsesTheCurrentPlaylistView()
{
    const QUrl url = makeTaggedFile(m_dir->path(), "song.flac",
                                    "Song Title", "The Artist", "Album");
    if (url.isEmpty())
        QSKIP("ffmpeg not available to generate a tagged file");

    // The current playlist's view carries no playlistId property: it
    // resolves to the current playlist.
    CurrentPlaylistWidget* view = new CurrentPlaylistWidget(m_window);
    view->show();
    m_window->m_focusedView = view->listView();
    SettingsManager::getInstance()->setValue("import.target", "focused");

    m_window->m_chosenFiles = { url.toLocalFile() };
    openAction();

    QVERIFY(importLanded(m_pl->currentPlaylistId()));
}

void MainWindowTest::importTargetFocusedFallsBackWithoutFocus()
{
    const QUrl url = makeTaggedFile(m_dir->path(), "song.flac",
                                    "Song Title", "The Artist", "Album");
    if (url.isEmpty())
        QSKIP("ffmpeg not available to generate a tagged file");

    // No focused view: the focused target falls back to the current
    // playlist (the CLI's case — a forwarded invocation has no focus).
    SettingsManager::getInstance()->setValue("import.target", "focused");

    m_window->m_chosenFiles = { url.toLocalFile() };
    openAction();

    QVERIFY(importLanded(m_pl->currentPlaylistId()));
}

void MainWindowTest::focusedPlaylistIdWalksUpToTheNearestView()
{
    const quint64 other = m_pl->addPlaylist("Other");

    // A tab view resolves by its playlistId property...
    PlaylistsWidget* tabs = new PlaylistsWidget(m_window);
    tabs->show();
    MediaListView* tabView =
        qobject_cast<MediaListView*>(tabs->tabWidget()->widget(1));
    QVERIFY(tabView != nullptr);
    QCOMPARE(MediaListView::focusedPlaylistId(tabView), other);
    // ...even when the focus sits on the view's viewport (the walk goes
    // up the ancestor chain).
    QCOMPARE(MediaListView::focusedPlaylistId(tabView->viewport()), other);

    // A view without the property (the current playlist's) resolves to
    // the current playlist...
    CurrentPlaylistWidget* view = new CurrentPlaylistWidget(m_window);
    view->show();
    QCOMPARE(MediaListView::focusedPlaylistId(view->listView()),
             m_pl->currentPlaylistId());

    // ...and a focus with no media view in its chain is none.
    QCOMPARE(MediaListView::focusedPlaylistId(m_window), 0ULL);
}

void MainWindowTest::titleShowsPlayingTrack()
{
    MediaItem item;
    item.source = QUrl::fromLocalFile(makeTempFile("song.mp3"));
    item.title = "Song Title";
    item.artist = "The Artist";
    QVERIFY(m_store->insert(item));
    m_pl->addItems({ item.id });

    QCOMPARE(m_window->windowTitle(), QString("doob"));

    m_pm->playFromPlaylist(m_pl->currentPlaylistId(), 0);

    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(m_window->windowTitle(), QString("Song Title — The Artist"));
}

void MainWindowTest::titleKeepsTheTrackWhenPaused()
{
    MediaItem item;
    item.source = QUrl::fromLocalFile(makeTempFile("song.mp3"));
    item.title = "Song Title";
    item.artist = "The Artist";
    QVERIFY(m_store->insert(item));
    m_pl->addItems({ item.id });

    m_pm->playFromPlaylist(m_pl->currentPlaylistId(), 0);
    m_pm->pause();

    QCOMPARE(m_pm->state(), PlaybackManager::Paused);
    QCOMPARE(m_window->windowTitle(), QString("Song Title — The Artist"));
}

void MainWindowTest::titleShowsBareNameOnStop()
{
    MediaItem item;
    item.source = QUrl::fromLocalFile(makeTempFile("song.mp3"));
    item.title = "Song Title";
    item.artist = "The Artist";
    QVERIFY(m_store->insert(item));
    m_pl->addItems({ item.id });

    m_pm->playFromPlaylist(m_pl->currentPlaylistId(), 0);
    QCOMPARE(m_window->windowTitle(), QString("Song Title — The Artist"));

    m_pm->stop();

    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);
    QCOMPARE(m_window->windowTitle(), QString("doob"));
}

QMenu* MainWindowTest::findViewMenu()
{
    for (QAction *a : m_window->menuBar()->actions()) {
        QMenu *menu = a->menu();
        if (menu != nullptr && menu->title() == "View")
            return menu;
    }
    return nullptr;
}

// The File menu's order (7d): Open / Export / Import, separator,
// Preferences, separator, "Exit doob" — the objectNames are stable.
void MainWindowTest::fileMenuHasTheReorderedActions()
{
    QMenu *fileMenu = findFileMenu();
    if (fileMenu == nullptr) {
        QFAIL("no File menu");
        return;
    }

    QStringList order;
    for (QAction *a : fileMenu->actions())
        order << (a->isSeparator() ? QStringLiteral("--") : a->objectName());

    QCOMPARE(order, (QStringList{
        QStringLiteral("openAction"),
        QStringLiteral("exportPlaylistAction"),
        QStringLiteral("importPlaylistAction"),
        QStringLiteral("--"),
        QStringLiteral("preferencesAction"),
        QStringLiteral("--"),
        QStringLiteral("exitAction"),
    }));
}

// The "Exit doob" action: the real-quit path through the seam (the
// tray's Quit is prepared first, so the teardown's close closes for
// real).
void MainWindowTest::exitActionQuitsTheApp()
{
    QMenu *fileMenu = findFileMenu();
    if (fileMenu == nullptr) {
        QFAIL("no File menu");
        return;
    }
    QAction *exit = fileMenu->findChild<QAction*>(QStringLiteral("exitAction"));
    if (exit == nullptr) {
        QFAIL("no exitAction in the File menu");
        return;
    }

    exit->trigger();

    QCOMPARE(m_window->m_quits, 1);
    // prepareQuit ran (the trayicon_test's pattern — its effect): the
    // teardown's close closes for real, never hides to the tray.
    QVERIFY(!TrayIcon::getInstance()->shouldHideOnClose());
}

// The View menu's "Window" submenu's checkable "Menu bar" toggle (7a):
// it follows the live visibility (the menu bar's Show/Hide events —
// QMenuBar has no visibility signal), and hides/shows the bar.
void MainWindowTest::theViewMenuTogglesTheMenuBar()
{
    QMenu *viewMenu = findViewMenu();
    if (viewMenu == nullptr) {
        QFAIL("no View menu");
        return;
    }
    QAction *toggle = viewMenu->findChild<QAction*>(QStringLiteral("menuBarToggleAction"));
    if (toggle == nullptr) {
        QFAIL("no menuBarToggleAction in the View menu's Window submenu");
        return;
    }
    QVERIFY(toggle->isCheckable());

    // Shown by default.
    QVERIFY(!m_window->menuBar()->isHidden());
    QVERIFY(toggle->isChecked());

    // A toggle hides the bar and the check follows.
    toggle->setChecked(false);
    qApp->processEvents();
    QVERIFY(m_window->menuBar()->isHidden());
    QVERIFY(!toggle->isChecked());

    // The live sync: an outside hide (the F10's counterpart) is
    // followed on a re-access.
    m_window->showMenuBar();
    qApp->processEvents();
    QVERIFY(!m_window->menuBar()->isHidden());
    toggle = viewMenu->findChild<QAction*>(QStringLiteral("menuBarToggleAction"));
    QVERIFY(toggle->isChecked());
}

// The View menu's "Window" submenu's checkable "Frameless" toggle (the
// runtime chrome switch, any layout): it drives the live frameless
// state (the FramelessWindowHint — the platform window's re-creation is
// not observable offscreen) and follows it, like the menu bar's entry.
// The state persists with the layout on its save (the document's
// window.frameless key, theLayoutDocumentRoundTripsTheWindowChrome's
// territory).
void MainWindowTest::theWindowSubmenuTogglesFrameless()
{
    QMenu *viewMenu = findViewMenu();
    if (viewMenu == nullptr) {
        QFAIL("no View menu");
        return;
    }
    QAction *toggle = viewMenu->findChild<QAction*>(QStringLiteral("framelessToggleAction"));
    if (toggle == nullptr) {
        QFAIL("no framelessToggleAction in the View menu's Window submenu");
        return;
    }
    QVERIFY(toggle->isCheckable());

    // Framed by default.
    QVERIFY(!m_window->isFrameless());
    QVERIFY(toggle->isChecked() == false);

    // A toggle goes frameless: the flag is the observable, the check
    // follows.
    toggle->setChecked(true);
    qApp->processEvents();
    QVERIFY(m_window->isFrameless());
    QVERIFY(m_window->windowFlags() & Qt::FramelessWindowHint);
    QVERIFY(toggle->isChecked());

    // Back framed again.
    toggle->setChecked(false);
    qApp->processEvents();
    QVERIFY(!m_window->isFrameless());
    QVERIFY(!(m_window->windowFlags() & Qt::FramelessWindowHint));
    QVERIFY(!toggle->isChecked());

    // The live sync: an outside change (the layout document's apply)
    // is followed.
    m_window->setFrameless(true);
    qApp->processEvents();
    QVERIFY(toggle->isChecked());
}

// The F10 recovery (7a): registered global with the F10 default,
// show-only (a recovery key must not be able to hide the menu bar
// again).
void MainWindowTest::f10ShortcutShowsTheHiddenMenuBar()
{
    ShortcutManager *shortcuts = ShortcutManager::getInstance();
    QCOMPARE(shortcuts->sequence(QStringLiteral("window.show_menu_bar")),
             QKeySequence(Qt::Key_F10));
    QVERIFY(shortcuts->isGlobal(QStringLiteral("window.show_menu_bar")));

    m_window->setMenuBarVisible(false);
    QVERIFY(m_window->menuBar()->isHidden());

    shortcuts->trigger(QStringLiteral("window.show_menu_bar"));

    QVERIFY(!m_window->menuBar()->isHidden());

    // Show-only: the trigger on a visible bar is a no-op.
    shortcuts->trigger(QStringLiteral("window.show_menu_bar"));
    QVERIFY(!m_window->menuBar()->isHidden());
}

// The one-shot hint (7a): the first hide shows the transient status-bar
// line (the key read live from the ShortcutManager's effective value),
// the flag is set, the later hides are silent.
void MainWindowTest::theFirstHideShowsTheOneShotHint()
{
    SettingsManager *settings = SettingsManager::getInstance();
    QVERIFY(!settings->value("hints.menu_bar_hidden").toBool());

    QSignalSpy spy(m_window->statusBar(), &QStatusBar::messageChanged);
    m_window->setMenuBarVisible(false);

    QCOMPARE(spy.count(), 1);
    const QString key = ShortcutManager::getInstance()
                            ->sequence(QStringLiteral("window.show_menu_bar"))
                            .toString();
    QVERIFY(spy.first().at(0).toString().contains(key));
    QVERIFY(settings->value("hints.menu_bar_hidden").toBool());

    // The second hide is silent.
    m_window->setMenuBarVisible(true);
    QSignalSpy spy2(m_window->statusBar(), &QStatusBar::messageChanged);
    m_window->setMenuBarVisible(false);
    QCOMPARE(spy2.count(), 0);
}

// The frameless state (7b): the X11/Wayland path takes the
// FramelessWindowHint (the flag's presence is the observable — the
// platform window re-creation is not offscreen).
void MainWindowTest::framelessTogglesTheWindowFlag()
{
    QVERIFY(!(m_window->windowFlags() & Qt::FramelessWindowHint));
    QVERIFY(!m_window->isFrameless());

    m_window->setFrameless(true);
    QVERIFY(m_window->isFrameless());
    QVERIFY(m_window->windowFlags() & Qt::FramelessWindowHint);

    m_window->setFrameless(false);
    QVERIFY(!m_window->isFrameless());
    QVERIFY(!(m_window->windowFlags() & Qt::FramelessWindowHint));
}

// The fallback strip (7b): fixed chrome shown when frameless is on and
// no titlebar widget is in the live main container's tree (the window
// can never be trapped); hidden when the flag is off or a titlebar is
// present.
void MainWindowTest::theFallbackStripFollowsFramelessAndTheTitlebar()
{
    // Framed: hidden.
    m_window->setFrameless(false);
    qApp->processEvents();
    QVERIFY(!m_window->fallbackStripVisible());

    // Frameless, no titlebar: shown (the child events are deferred to
    // the event loop's turn).
    m_window->setFrameless(true);
    QVERIFY(test::waitUntil([this] { return m_window->fallbackStripVisible(); }));

    // A titlebar widget in the live tree: the strip gives way.
    WidgetConfiguration cfg;
    BaseWidget *titlebar =
        LayoutManager::getInstance()->createWidget(TitleBarWidget::id, cfg);
    QVERIFY(titlebar != nullptr);
    m_window->mainContainer()->setWidget(titlebar);
    QVERIFY(test::waitUntil([this] { return !m_window->fallbackStripVisible(); }));

    // Framed again: hidden, titlebar or not.
    m_window->setFrameless(false);
    qApp->processEvents();
    QVERIFY(!m_window->fallbackStripVisible());
}

// The resize edge's pure hit-test (7b): the margin pixels from each
// edge, a corner the two edges' flags.
void MainWindowTest::theResizeEdgeHitTest()
{
    const QSize size(800, 600);
    const int margin = MainWindow::kResizeEdgeWidth;

    QCOMPARE(MainWindow::edgeAt(QPoint(0, 0), size, margin),
             Qt::LeftEdge | Qt::TopEdge);
    QCOMPARE(MainWindow::edgeAt(QPoint(size.width() - 1, 0), size, margin),
             Qt::RightEdge | Qt::TopEdge);
    QCOMPARE(MainWindow::edgeAt(QPoint(size.width() - 1, size.height() - 1), size, margin),
             Qt::RightEdge | Qt::BottomEdge);
    QCOMPARE(MainWindow::edgeAt(QPoint(0, size.height() - 1), size, margin),
             Qt::LeftEdge | Qt::BottomEdge);
    QCOMPARE(MainWindow::edgeAt(QPoint(margin / 2, 300), size, margin),
             Qt::LeftEdge);
    QCOMPARE(MainWindow::edgeAt(QPoint(size.width() - (margin / 2), 300), size, margin),
             Qt::RightEdge);
    QCOMPARE(MainWindow::edgeAt(QPoint(400, margin / 2), size, margin),
             Qt::TopEdge);
    QCOMPARE(MainWindow::edgeAt(QPoint(400, size.height() - (margin / 2)), size, margin),
             Qt::BottomEdge);
    QCOMPARE(MainWindow::edgeAt(QPoint(400, 300), size, margin),
             Qt::Edges());
}

// The layout document's window chrome (7a/7b): the keys are stored
// only when non-default (a missing key takes its default on restore),
// and apply takes them over the live state.
void MainWindowTest::theLayoutDocumentRoundTripsTheWindowChrome()
{
    LayoutManager *lm = LayoutManager::getInstance();
    MainWindow *window = lm->window();
    QVERIFY(window != nullptr);

    // The defaults (shown menu bar, framed window) store no keys.
    window->setFrameless(false);
    window->setMenuBarVisible(true);
    QJsonObject windowDoc = lm->buildLayoutDocument()
                                .value("window").toObject();
    QVERIFY(!windowDoc.contains("frameless"));
    QVERIFY(!windowDoc.contains("menu_bar"));

    // The non-defaults are written.
    window->setFrameless(true);
    window->setMenuBarVisible(false);
    const QJsonObject doc = lm->buildLayoutDocument();
    windowDoc = doc.value("window").toObject();
    QCOMPARE(windowDoc.value("frameless").toBool(), true);
    QCOMPARE(windowDoc.value("menu_bar").toBool(), false);

    // Apply over a differing live state: the keys take effect.
    window->setFrameless(false);
    window->setMenuBarVisible(true);
    QVERIFY(lm->applyLayoutDocument(doc, false, "test"));
    QVERIFY(window->isFrameless());
    QVERIFY(window->menuBar()->isHidden());

    // Missing keys: the defaults (framed, shown).
    QJsonObject plain;
    plain["container"] = doc.value("container");
    plain["window"] = QJsonObject{
        {"x", 0},
        {"y", 0},
        {"width", 640},
        {"height", 480},
        {"maximized", false},
    };
    plain["version"] = 1;
    QVERIFY(lm->applyLayoutDocument(plain, false, "test"));
    QVERIFY(!window->isFrameless());
    QVERIFY(!window->menuBar()->isHidden());

    // The store's window back to its defaults (the later tests'
    // starting point).
    window->setFrameless(false);
    window->setMenuBarVisible(true);
}

}  // namespace

QTEST_MAIN(MainWindowTest)
#include "mainwindow_test.moc"
