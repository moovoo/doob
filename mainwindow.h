#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QApplication>
#include <QMainWindow>
#include <QPair>
#include <QPointer>
#include <QSize>
#include <QStringList>
#include <Qt>

class LayoutManager;
class WidgetContainer;
class QAction;
class QLabel;
class QMenu;
class QPushButton;
class QToolBar;
class QWindow;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    WidgetContainer* mainContainer();

    // The main menu (the hamburger's): the menu bar's File and View
    // submenus as-is (a Qt6 action may belong to several menus), plus
    // the checkable "Show menu bar" entry (the platforms where the menu
    // bar can be hidden only), a true toggle following the live
    // visibility. Built lazily on the first access.
    QMenu* mainMenu();

    // The menu bar's visibility (the platforms where the menu bar can
    // be hidden only — the platform gate is a virtual seam, the
    // trayAvailable pattern). A no-op when the state matches.
    void setMenuBarVisible(bool visible);
    // Show-only (F10 and the tray's entry — the recovery's layers: a
    // recovery key must not be able to hide the menu bar again; the
    // hamburger's entry is a true toggle). A no-op when the menu bar
    // is already visible.
    void showMenuBar();

    // The frameless state (the layout document's "window.frameless"):
    // X11/Wayland take the FramelessWindowHint (setWindowFlags hides
    // the window — a re-show), macOS takes the middle path (the title
    // bar's content hidden, the native traffic lights and drag area
    // kept — Qt >= 6.5). The platform decision is a virtual seam.
    void setFrameless(bool frameless);
    bool isFrameless() const { return m_frameless; }

    // The frameless fallback strip (the fixed chrome shown when
    // frameless is on and no titlebar widget is in the live main
    // container's tree — the window can never be trapped).
    bool fallbackStripVisible() const;

    // The resize edges at a window-local point (the frameless edge
    // zones; framed mode is Qt::NoEdge — the zones and the cursors are
    // a no-op there): the hit-test behind a virtual seam (the offscreen
    // tests exercise the decision, not the platform).
    virtual Qt::Edges resizeEdgesAt(const QPoint &pos, const QSize &size) const;
    // The pure hit-test: the `margin` pixels from each of the window's
    // edges (a corner is the two edges' flags).
    static Qt::Edges edgeAt(const QPoint &pos, const QSize &size, int margin);
    // True when a titlebar widget is in the live tree under `root`
    // (the fallback strip's walk).
    static bool hasTitlebarWidget(const QWidget *root);

    // The window's system operations (the test seams: offscreen the
    // QWindow calls are no-ops — the tests count the calls).
    virtual void startSystemMove();
    virtual void startSystemResize(Qt::Edges edges);
    // The "Exit doob" action's app quit (the test seam: the tests count
    // it, the app quits — the tray's Quit pattern).
    virtual void quitApp();

    // The resize edge zone's width, in pixels.
    static constexpr int kResizeEdgeWidth = 6;

    // The main container's subtree's events (the menu bar's Show/Hide
    // sync, the fallback strip's drag, the child events' tree re-walk,
    // the frameless edge zones' press/move).
    // Public to match QObject::eventFilter() (it is public in Qt 6).
    bool eventFilter(QObject *obj, QEvent *event) override;

    // The platform's menu-bar-hiding support (the test seam; on macOS
    // the menu bar is the native global menu bar and a per-window hide
    // is a no-op). Public: the LayoutManager (the document's keys) and
    // the TrayIcon (the "Show menu bar" entry) consult it too.
    virtual bool menuBarSupported() const;
    // The platform's full-frameless support (the test seam; macOS takes
    // the middle path — setTitleBarVisibility — instead). Public: the
    // LayoutManager consults it too.
    virtual bool framelessSupported() const;

protected:
    void closeEvent(QCloseEvent *event) override;

    // The "Open..." files (a test seam: the QFileDialog statics are
    // untestable offscreen, the LibraryWidget pattern).
    virtual QStringList chooseAudioFiles();
    // The playlist the "Open..." import lands in: the import.target
    // setting — the current playlist (the default) or the playlist of
    // the focused view (falling back to the current one when no view
    // has focus).
    quint64 importTargetPlaylistId();
    // The app's focus widget (a test seam: the offscreen platform never
    // sets one, so the focused resolution is untestable without it).
    virtual QWidget* focusSource() const { return QApplication::focusWidget(); }

    WidgetContainer* m_mainContainer;

private:
    // The File and View menus (the menu bar's, re-used by mainMenu()).
    void buildMenus();
    // The "Open..." files' import (the chooseAudioFiles seam's sink).
    void onOpenFiles();
    // The window title's live update ("Title — Artist").
    void updateWindowTitle();
    // The menu bar's Show/Hide (QMenuBar has no visibility signal):
    // sync the View toggle's and the hamburger's "Show menu bar"
    // entries (both follow the live visibility).
    void updateMenuBarEntries();
    // The one-shot hint (the "hints.menu_bar_hidden" setting): the
    // first hide shows a transient status-bar line (the key read live
    // from the ShortcutManager's effective value), later hides are
    // silent.
    void showMenuBarHint();
    // The fallback strip's state (frameless on and no titlebar widget
    // in the live main container's tree).
    void updateFallbackStrip();
    // The main container's subtree gets the window's filter (the child
    // events are watched at their parent, so the filter goes on every
    // object of the subtree, the root included).
    void installTreeFilter(QObject *root);
    // The frameless edge zones' half of the filter: a left press on an
    // edge starts the system resize (swallowed), a move sets the
    // per-edge cursor (and records the widget's previous one).
    bool handleResizeEdgeEvent(QObject *obj, QEvent *event);
    // True when `widget`'s previous cursor is already recorded in
    // m_edgeCursors (the restore's payload, recorded once).
    bool edgeCursorRecorded(const QWidget *widget) const;

    QMenu* m_fileMenu = nullptr;
    QMenu* m_viewMenu = nullptr;
    QMenu* m_mainMenu = nullptr;
    // The View menu's "Window" submenu's checkable "Menu bar" toggle
    // (nullptr on a platform without menu-bar hiding).
    QAction* m_showMenuBarAction = nullptr;
    // The View menu's "Window" submenu's checkable "Frameless" toggle
    // (nullptr on a platform without full frameless), following the
    // live state like the menu bar's entry.
    QAction* m_framelessAction = nullptr;
    // The main menu's checkable "Show menu bar" entry (same gate).
    QAction* m_mainMenuShowMenuBarAction = nullptr;
    // The fallback strip's fixed chrome (icon + title + close).
    QToolBar* m_fallbackStripBar = nullptr;
    QWidget* m_fallbackStrip = nullptr;
    QLabel* m_fallbackStripIcon = nullptr;
    QLabel* m_fallbackStripTitle = nullptr;
    QPushButton* m_fallbackStripClose = nullptr;
    // The edge-zone cursors (a widget the edge zones cursor-changed,
    // and its previous cursor — the QPointer keys a deleted widget safe;
    // a list, not a hash: this Qt's QPointer has no qHash, and the
    // list holds at most a handful of entries).
    QList<QPair<QPointer<QWidget>, Qt::CursorShape>> m_edgeCursors;
    bool m_frameless = false;
};

#endif // MAINWINDOW_H
