#include "mainwindow.h"
#include "widgetcontainer.h"
#include "layoutmanager.h"
#include "medialistview.h"
#include "settingsmanager.h"
#include "stylemanager.h"
#include "playbackmanager.h"
#include "playlistmanager.h"
#include "librarymanager.h"
#include "playlistfile.h"
#include "playlisttoolbar.h"
#include "shortcutmanager.h"
#include "trayicon.h"
#include "titlebar.h"
#include <QApplication>
#include <QChildEvent>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMenu>
#include <QFileDialog>
#include <QMouseEvent>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QWindow>

#include <algorithm>

namespace {

// The per-edge cursor shape (a corner is the diagonal's).
Qt::CursorShape cursorForEdges(Qt::Edges edges)
{
    switch (edges) {
    case Qt::LeftEdge:
    case Qt::RightEdge:
        return Qt::SizeHorCursor;
    case Qt::TopEdge:
    case Qt::BottomEdge:
        return Qt::SizeVerCursor;
    case Qt::LeftEdge | Qt::TopEdge:
    case Qt::RightEdge | Qt::BottomEdge:
        return Qt::SizeBDiagCursor;
    case Qt::RightEdge | Qt::TopEdge:
    case Qt::LeftEdge | Qt::BottomEdge:
        return Qt::SizeFDiagCursor;
    default:
        return Qt::ArrowCursor;
    }
}

}  // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_mainContainer = new WidgetContainer(this);
    setCentralWidget(m_mainContainer);

    buildMenus();

    // User-visible playback failures (a provider that resolved no
    // stream): a transient status bar message, so a silent stop is not
    // read as "the track ended".
    connect(PlaybackManager::getInstance(), &PlaybackManager::playbackError,
            this, [this](const QString &message) {
        statusBar()->showMessage(message, 5000);
    });

    // User-visible import skips (a provider fetch that failed — a
    // not-logged-in provider's hint carries the reason): the same
    // transient treatment, so a silent no-op is not read as "imported".
    connect(LibraryManager::getInstance(), &LibraryManager::sourceSkipped,
            this, [this](const QUrl &source, const QString &reason) {
        const QString message = reason.isEmpty()
            ? QStringLiteral(
                  "Cannot import %1: the source's metadata could not be "
                  "read.")
                  .arg(source.toString())
            : QStringLiteral("Cannot import %1: %2")
                  .arg(source.toString(), reason);
        statusBar()->showMessage(message, 5000);
    });

    // The registered global shortcuts bind to this window: the global
    // ones are app-wide, the non-global ones scoped to it.
    ShortcutManager* shortcuts = ShortcutManager::getInstance();
    shortcuts->attach(this);

    // The F10 recovery (the menu bar's hiding guarantee): registered
    // after the attach (the manager creates the shortcut on registration
    // when attached), global, show-only (a recovery key must not be able
    // to hide the menu bar again — on a platform where F10 is reserved
    // for the native menu focus, the no-op means no clash).
    shortcuts->registerAction({
        QStringLiteral("window.show_menu_bar"),
        QStringLiteral("Show menu bar"),
        QStringLiteral("Window"),
        QKeySequence(Qt::Key_F10),
        true,
        [this] { showMenuBar(); },
    });

    // The one-shot hint's flag (registered like the tray's close-action
    // setting, idempotent).
    SettingsManager::getInstance()->registerSetting(
        { QStringLiteral("hints.menu_bar_hidden"),
          QStringLiteral("Menu bar hidden hint shown"),
          QStringLiteral("The one-shot status-bar hint (naming the "
                         "F10 recovery) has been shown."),
          false,
          QString(), });

    // The now playing in the window title: "Title — Artist" while
    // playing or paused, the bare app name on stop (playingItemChanged
    // fires on stop too, with the invalid item — the state watch covers
    // a pause/resume of the same item).
    setWindowTitle(QStringLiteral("doob"));
    PlaybackManager* pm = PlaybackManager::getInstance();
    connect(pm, &PlaybackManager::playingItemChanged, this,
            &MainWindow::updateWindowTitle);
    connect(pm, &PlaybackManager::stateChanged, this,
            &MainWindow::updateWindowTitle);

    // The fallback strip (the frameless chrome, fixed — it cannot be
    // deleted away): the minimal icon + title + close, so a frameless
    // window without a titlebar widget can still be moved and closed.
    m_fallbackStripBar = new QToolBar(this);
    m_fallbackStripBar->setObjectName("fallbackTitleStripBar");
    m_fallbackStripBar->setMovable(false);
    m_fallbackStripBar->setFloatable(false);
    m_fallbackStrip = new QWidget(m_fallbackStripBar);
    m_fallbackStrip->setObjectName("fallbackTitleStrip");
    QHBoxLayout* stripLayout = new QHBoxLayout(m_fallbackStrip);
    stripLayout->setContentsMargins(4, 0, 4, 0);
    stripLayout->setSpacing(6);
    QIcon appIcon = windowIcon();
    if (appIcon.isNull())
        appIcon = QIcon(QStringLiteral(":/icons/doob.svg"));
    m_fallbackStripIcon = new QLabel(m_fallbackStrip);
    m_fallbackStripIcon->setObjectName("fallbackStripIcon");
    m_fallbackStripIcon->setPixmap(appIcon.pixmap(QSize(16, 16)));
    m_fallbackStripTitle = new QLabel(m_fallbackStrip);
    m_fallbackStripTitle->setObjectName("fallbackStripTitle");
    m_fallbackStripTitle->setText(QStringLiteral("doob"));
    m_fallbackStripClose = new QPushButton(QString::fromUtf8("\xE2\x9C\x95"),
                                           m_fallbackStrip);
    m_fallbackStripClose->setObjectName("fallbackStripClose");
    m_fallbackStripClose->setToolTip(QStringLiteral("Close"));
    stripLayout->addWidget(m_fallbackStripIcon);
    stripLayout->addWidget(m_fallbackStripTitle, 1);
    stripLayout->addWidget(m_fallbackStripClose);
    m_fallbackStripBar->addWidget(m_fallbackStrip);
    addToolBar(Qt::TopToolBarArea, m_fallbackStripBar);
    m_fallbackStripBar->hide();
    connect(m_fallbackStripClose, &QPushButton::clicked, this, [this] {
        close();
    });
    // The strip's drag (the frameless window's move): a left press on
    // the strip starts the system move (the window's event filter,
    // installed on the strip).
    m_fallbackStrip->installEventFilter(this);

    // The menu bar's Show/Hide (QMenuBar has no visibility signal): the
    // View toggle's and the hamburger's entries follow the live state.
    menuBar()->installEventFilter(this);

    // The main container's subtree: the child events (the tree's shape
    // changed — the fallback strip's walk) and the frameless resize
    // edges (the press's start, the move's cursor).
    installTreeFilter(m_mainContainer);

    updateFallbackStrip();
}

MainWindow::~MainWindow()
{
    // The F10 registration: the shortcut is a child of this window —
    // unregistered in the body, before the base class destroys the
    // children (the stored settings survive unregistration).
    ShortcutManager::getInstance()->unregisterAction("window.show_menu_bar");
}

void MainWindow::buildMenus()
{
    m_fileMenu = new QMenu(QStringLiteral("File"), this);

    // Open media files into the current playlist (the same import path
    // a file drop takes).
    QAction* open = m_fileMenu->addAction(QStringLiteral("Open..."));
    open->setObjectName("openAction");
    connect(open, &QAction::triggered, this, &MainWindow::onOpenFiles);

    // Export the current playlist to an M3U8 file.
    QAction* exportPlaylist =
        m_fileMenu->addAction(QStringLiteral("Export current playlist..."));
    exportPlaylist->setObjectName("exportPlaylistAction");
    connect(exportPlaylist, &QAction::triggered, this, [this] {
        PlaylistManager *pl = PlaylistManager::getInstance();
        const quint64 id = pl->currentPlaylistId();
        const QString path = QFileDialog::getSaveFileName(
            this, "Export playlist", pl->playlist(id).name + QStringLiteral(".m3u"),
            "Playlist files (*.m3u *.m3u8 *.pls)");
        if (path.isEmpty())
            return;
        if (!pl->exportPlaylist(id, path))
            statusBar()->showMessage("Could not export the playlist.", 5000);
    });

    // Import an external playlist file (M3U/M3U8, PLS) as a new playlist.
    QAction* importPlaylist = m_fileMenu->addAction("Import playlist...");
    importPlaylist->setObjectName("importPlaylistAction");
    connect(importPlaylist, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, "Import playlist", QString(),
            "Playlist files (*.m3u *.m3u8 *.pls)");
        if (path.isEmpty())
            return;
        if (PlaylistManager::getInstance()->importPlaylistFile(path) == 0)
            statusBar()->showMessage("Could not import the playlist file.", 5000);
    });

    m_fileMenu->addSeparator();

    QAction* preferences = m_fileMenu->addAction("Preferences");
    preferences->setObjectName("preferencesAction");
    connect(preferences, &QAction::triggered, LayoutManager::getInstance(),
            &LayoutManager::showPreferences);

    m_fileMenu->addSeparator();

    // The explicit exit: the real-quit path (the tray's Quit and the
    // closeEvent's "exit" branch show the shape), behind the seam.
    QAction* exit = m_fileMenu->addAction("Exit doob");
    exit->setObjectName("exitAction");
    connect(exit, &QAction::triggered, this, [this] {
        TrayIcon::getInstance()->prepareQuit();
        quitApp();
    });

    menuBar()->addMenu(m_fileMenu);

    m_viewMenu = new QMenu(QStringLiteral("View"), this);
    QAction* editMode = m_viewMenu->addAction("Edit mode");
    editMode->setCheckable(true);
    editMode->setChecked(LayoutManager::getInstance()->isEditMode());
    connect(editMode, &QAction::toggled, LayoutManager::getInstance(),
            &LayoutManager::setEditMode);

    // The window's chrome (the layout document's "window" map keys —
    // arrangement, persisted with the layout on its save): the View
    // menu's "Window" submenu, one checkable toggle per supported
    // state. Each entry is platform-gated (on macOS the menu bar is
    // the native global menu bar and the frame takes the middle path),
    // a platform without either gets no submenu.
    QMenu* windowMenu = nullptr;
    if (menuBarSupported()) {
        windowMenu = m_viewMenu->addMenu(QStringLiteral("Window"));
        // The menu bar's visibility: the checkable toggle, following
        // the live state like the Edit-mode action (the sync from the
        // menu bar's Show/Hide events, not a signal — QMenuBar has
        // none).
        m_showMenuBarAction = windowMenu->addAction(QStringLiteral("Menu bar"));
        m_showMenuBarAction->setObjectName("menuBarToggleAction");
        m_showMenuBarAction->setCheckable(true);
        m_showMenuBarAction->setChecked(!menuBar()->isHidden());
        connect(m_showMenuBarAction, &QAction::toggled, this,
                [this](bool visible) { setMenuBarVisible(visible); });
    }
    if (framelessSupported()) {
        if (windowMenu == nullptr)
            windowMenu = m_viewMenu->addMenu(QStringLiteral("Window"));
        m_framelessAction = windowMenu->addAction(QStringLiteral("Frameless"));
        m_framelessAction->setObjectName("framelessToggleAction");
        m_framelessAction->setCheckable(true);
        m_framelessAction->setChecked(isFrameless());
        connect(m_framelessAction, &QAction::toggled, this,
                &MainWindow::setFrameless);
    }

    // The layouts: the whole menu is re-built on open (the user's
    // layouts, the current checked, a click switches, then "Manage
    // windows" and "Manage layouts" — the picker dialog, the store's
    // New/Copy to/Delete/Import/Export operations being its per-pane
    // buttons), so a layout added elsewhere shows up on the next open.
    QMenu* layoutMenu = m_viewMenu->addMenu("Layout");
    connect(layoutMenu, &QMenu::aboutToShow, this, [layoutMenu] {
        LayoutManager::getInstance()->fillLayoutMenu(layoutMenu);
    });

    // The styles: the same shape as the layout menu (all the styles,
    // the current checked, then "Manage styles" and "Edit style
    // sheet"), re-built on open.
    QMenu* stylesMenu = m_viewMenu->addMenu("Styles");
    connect(stylesMenu, &QMenu::aboutToShow, this, [stylesMenu] {
        StyleManager::getInstance()->fillStylesMenu(stylesMenu);
    });

    menuBar()->addMenu(m_viewMenu);
}

QMenu* MainWindow::mainMenu()
{
    if (m_mainMenu == nullptr) {
        // The menu bar's File and View submenus as-is (a Qt6 action may
        // belong to several menus).
        m_mainMenu = new QMenu(this);
        m_mainMenu->addMenu(m_fileMenu);
        m_mainMenu->addMenu(m_viewMenu);
        if (menuBarSupported()) {
            m_mainMenuShowMenuBarAction =
                m_mainMenu->addAction(QStringLiteral("Show menu bar"));
            m_mainMenuShowMenuBarAction->setObjectName("mainMenuShowMenuBarAction");
            m_mainMenuShowMenuBarAction->setCheckable(true);
            // A true toggle (the View "Menu bar" entry's pattern): the
            // hamburger is a menu access, not a recovery layer — it
            // hides as well as shows (F10 and the tray stay show-only).
            connect(m_mainMenuShowMenuBarAction, &QAction::toggled, this,
                    [this](bool visible) { setMenuBarVisible(visible); });
        }
    }
    updateMenuBarEntries();
    return m_mainMenu;
}

void MainWindow::setMenuBarVisible(bool visible)
{
    if (!menuBarSupported() || menuBar()->isVisible() == visible)
        return;
    menuBar()->setVisible(visible);
    if (!visible)
        showMenuBarHint();
}

void MainWindow::showMenuBar()
{
    setMenuBarVisible(true);
}

void MainWindow::showMenuBarHint()
{
    // The one-shot: the first hide shows the line (the key read live
    // from the ShortcutManager's effective value, so a rebound key is
    // shown), the flag is set, the later hides are silent.
    SettingsManager* settings = SettingsManager::getInstance();
    if (settings->value("hints.menu_bar_hidden").toBool())
        return;
    settings->setValue("hints.menu_bar_hidden", true);
    const QString key =
        ShortcutManager::getInstance()->sequence("window.show_menu_bar").toString();
    statusBar()->showMessage(
        QStringLiteral("Menu bar hidden — press %1 to bring it back").arg(key), 5000);
}

void MainWindow::updateMenuBarEntries()
{
    const bool visible = !menuBar()->isHidden();
    if (m_showMenuBarAction != nullptr)
        m_showMenuBarAction->setChecked(visible);
    if (m_mainMenuShowMenuBarAction != nullptr)
        m_mainMenuShowMenuBarAction->setChecked(visible);
}

void MainWindow::setFrameless(bool frameless)
{
    if (m_frameless == frameless)
        return;
    m_frameless = frameless;

    // The "Window" submenu's entry follows the live state (a setChecked
    // on a matching state is a no-op — no loop).
    if (m_framelessAction != nullptr)
        m_framelessAction->setChecked(frameless);

    if (framelessSupported()) {
        // X11/Wayland: the frameless hint. setWindowFlags re-creates the
        // platform window (it hides it): the re-show.
        const Qt::WindowFlags flags = windowFlags();
        setWindowFlags(frameless
            ? flags | Qt::FramelessWindowHint
            : flags & ~Qt::FramelessWindowHint);
        show();
    }
    // The macOS middle path (the title bar's content hidden, the native
    // traffic lights and drag area kept) wants Qt >= 6.5's
    // setTitleBarVisibility, which this Qt build does not expose: the
    // frameless state is recorded and the fallback strip carries the
    // window (the native frame stays).
    updateFallbackStrip();
}

bool MainWindow::fallbackStripVisible() const
{
    return m_fallbackStripBar != nullptr && m_fallbackStripBar->isVisible();
}

Qt::Edges MainWindow::resizeEdgesAt(const QPoint &pos, const QSize &size) const
{
    // Framed mode is a no-op (no zones, no cursor).
    if (!isFrameless() || !framelessSupported())
        return {};
    return edgeAt(pos, size, kResizeEdgeWidth);
}

Qt::Edges MainWindow::edgeAt(const QPoint &pos, const QSize &size, int margin)
{
    Qt::Edges edges = Qt::Edges();
    if (pos.x() < margin)
        edges |= Qt::LeftEdge;
    if (pos.x() >= size.width() - margin)
        edges |= Qt::RightEdge;
    if (pos.y() < margin)
        edges |= Qt::TopEdge;
    if (pos.y() >= size.height() - margin)
        edges |= Qt::BottomEdge;
    return edges;
}

bool MainWindow::hasTitlebarWidget(const QWidget *root)
{
    const QList<BaseWidget*> widgets =
        root->findChildren<BaseWidget*>(Qt::FindChildrenRecursively);
    return std::any_of(widgets.cbegin(), widgets.cend(), [](BaseWidget* widget) {
        return widget->widgetId() == TitleBarWidget::id;
    });
}

void MainWindow::startSystemMove()
{
    if (windowHandle() != nullptr)
        windowHandle()->startSystemMove();
}

void MainWindow::startSystemResize(Qt::Edges edges)
{
    if (windowHandle() != nullptr)
        windowHandle()->startSystemResize(edges);
}

void MainWindow::quitApp()
{
    QCoreApplication::quit();
}

bool MainWindow::menuBarSupported() const
{
#ifndef Q_OS_MACOS
    return true;
#else
    return false;
#endif
}

bool MainWindow::framelessSupported() const
{
#ifndef Q_OS_MACOS
    return true;
#else
    return false;
#endif
}

void MainWindow::updateFallbackStrip()
{
    const bool show = isFrameless() && !hasTitlebarWidget(m_mainContainer);
    m_fallbackStripBar->setVisible(show);
}

void MainWindow::installTreeFilter(QObject *root)
{
    // The child events are watched at their parent: the filter goes on
    // the root and on every object beneath it (a container within the
    // subtree is not special — the window filters all of them).
    root->installEventFilter(this);
    for (QObject *child : root->children())
        installTreeFilter(child);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    // The menu bar's Show/Hide (QMenuBar has no visibility signal): the
    // View toggle's and the hamburger's entries follow the live state
    // (a setChecked on a matching state is a no-op — no loop).
    if (obj == menuBar()
        && (event->type() == QEvent::Show || event->type() == QEvent::Hide))
        updateMenuBarEntries();

    // The fallback strip's drag (the frameless window's move).
    if (obj == m_fallbackStrip
        && event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *press = static_cast<QMouseEvent*>(event);
        if (press->button() == Qt::LeftButton)
            startSystemMove();
    }

    // The main container's subtree: the child events (the tree's shape
    // changed — the fallback strip's walk, deferred to the event loop's
    // turn so a (re)store finishes first).
    if (event->type() == QEvent::ChildAdded
        || event->type() == QEvent::ChildRemoved) {
        QChildEvent *childEvent = static_cast<QChildEvent*>(event);
        if (childEvent->added())
            installTreeFilter(childEvent->child());
        QTimer::singleShot(0, this, [this] { updateFallbackStrip(); });
        return QMainWindow::eventFilter(obj, event);
    }

    // The frameless resize edges (the mouse events on the window's
    // other widgets — the fallback strip's events are the drag's).
    if (event->type() == QEvent::MouseButtonPress
        || event->type() == QEvent::MouseMove
        || event->type() == QEvent::MouseButtonRelease)
        return handleResizeEdgeEvent(obj, event);

    return QMainWindow::eventFilter(obj, event);
}

bool MainWindow::handleResizeEdgeEvent(QObject *obj, QEvent *event)
{
    QWidget* widget = qobject_cast<QWidget*>(obj);
    if (widget == nullptr)
        return QMainWindow::eventFilter(obj, event);

    QMouseEvent *mouse = static_cast<QMouseEvent*>(event);
    const Qt::Edges edges = resizeEdgesAt(
        widget->mapTo(this, mouse->position().toPoint()), size());

    // A left press on an edge zone starts the system resize (swallowed).
    if (event->type() == QEvent::MouseButtonPress && edges != Qt::Edges()
        && mouse->button() == Qt::LeftButton) {
        startSystemResize(edges);
        return true;
    }

    if (edges == Qt::Edges()) {
        // The move left every edge zone: restore the cursors (the
        // QPointers keep a deleted widget's entry inert).
        for (const auto &entry : m_edgeCursors)
            if (entry.first)
                entry.first->setCursor(entry.second);
        m_edgeCursors.clear();
    } else {
        // Record the widget's previous cursor once (the restore's
        // payload), then the edge's cursor (a re-set on a moved edge —
        // the first entry's original is kept).
        if (!edgeCursorRecorded(widget))
            m_edgeCursors.append(
                QPair<QPointer<QWidget>, Qt::CursorShape>(
                    QPointer<QWidget>(widget), widget->cursor().shape()));
        widget->setCursor(cursorForEdges(edges));
    }

    return QMainWindow::eventFilter(obj, event);
}

bool MainWindow::edgeCursorRecorded(const QWidget *widget) const
{
    return std::any_of(m_edgeCursors.cbegin(), m_edgeCursors.cend(),
                       [widget](const auto &entry) {
                           return entry.first == widget;
                       });
}

void MainWindow::onOpenFiles()
{
    const QStringList paths = chooseAudioFiles();
    if (paths.isEmpty())
        return;
    // The same import path a file drop takes: flatten (a chosen playlist
    // file becomes its entries) into the import target.
    PlaylistManager* pl = PlaylistManager::getInstance();
    pl->importSources(importTargetPlaylistId(), flattenLocalFiles(paths));
}

quint64 MainWindow::importTargetPlaylistId()
{
    PlaylistManager* pl = PlaylistManager::getInstance();
    quint64 id = pl->currentPlaylistId();
    // The import.target setting: the focused view's playlist when the
    // setting says so and a playlist view has focus (no focus falls back
    // to the current one — the CLI path has no focus and a drop targets
    // the view it lands on, so neither comes here).
    if (SettingsManager::getInstance()->value("import.target").toString()
            == QStringLiteral("focused")) {
        const quint64 focused = MediaListView::focusedPlaylistId(focusSource());
        if (focused != 0)
            id = focused;
    }
    return id;
}

QStringList MainWindow::chooseAudioFiles()
{
    return ::chooseAudioFiles(this);
}

void MainWindow::updateWindowTitle()
{
    PlaybackManager* pm = PlaybackManager::getInstance();
    const MediaItem item = pm->playingItem();
    QString title = QStringLiteral("doob");
    if (item.isValid() && pm->state() != PlaybackManager::Stopped) {
        title = item.title;
        if (title.isEmpty())
            title = QFileInfo(item.source.toLocalFile()).fileName();
        if (!item.artist.isEmpty())
            title += QStringLiteral(" — ") + item.artist;
    }
    setWindowTitle(title);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // The layout is saved before the base class hides the window: the
    // quit that follows tears the windows down (the close cascades a
    // hide to the docked children, and QApplication's quit handler
    // closes the top-levels), and a save after the teardown would
    // record every window as closed. The save is guarded by the
    // window's visibility, because the teardown closes this window a
    // second time (already hidden, it must not re-save the torn-down
    // state).
    if (isVisible())
        LayoutManager::getInstance()->saveLayout();
    // The close action (the "window.close_action" setting, the
    // TrayIcon's decision): "tray" hides the window and keeps the app
    // alive — a tray click (or the menu's Show window, or any re-open)
    // brings it back. The teardown's double close arrives already
    // hidden (and a quit has prepared itself), so it falls through to
    // the normal close.
    if (isVisible() && TrayIcon::getInstance()->shouldHideOnClose()) {
        event->ignore();
        hide();
        return;
    }
    // "exit" (or a trayless system's degraded "tray"): a real quit —
    // QCoreApplication::quit() closes every top-level (the child
    // windows among them), where quitOnLastWindowClosed alone would
    // leave the app up behind an open child window.
    if (isVisible() && TrayIcon::getInstance()->shouldQuitOnClose()) {
        TrayIcon::getInstance()->prepareQuit();
        qApp->quit();
    }
    QMainWindow::closeEvent(event);
}

WidgetContainer* MainWindow::mainContainer() {
    return m_mainContainer;
}
