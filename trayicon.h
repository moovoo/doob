#ifndef TRAYICON_H
#define TRAYICON_H

#include <QObject>
#include <QPointer>
#include <QSystemTrayIcon>

#include "playbackmanager.h"

class QAction;
class QEvent;
class QMenu;
class QMainWindow;

// The system tray (the desktop's tray icon): a QSystemTrayIcon (the
// app's icon) with a context menu — Play/Pause (state-tracked), Next,
// Previous, a separator, Show window, Quit. A click (or a
// double-click) on the icon brings the window forward. A singleton,
// created in main.cpp after the window (the Mpris pattern, parented to
// the app so the quit tears it down); the main window's closeEvent
// asks it for the close decision.
//
// The close action: the "window.close_action" setting (default "tray").
// "tray" hides the window and keeps the app alive — a tray click (or
// the menu's Show window, or any re-open, e.g. MPRIS's Raise) brings
// it back. "exit" is a real quit (prepareQuit() + QCoreApplication::
// quit(), so every top-level — the child windows among them — closes
// with the app). A real quit (the tray's Quit, MPRIS's Quit) calls
// prepareQuit() first, so the teardown's close never hides.
//
// A system without a tray (Wayland without a status-notifier host)
// runs without the icon: one warning, and "tray" degrades to "exit"
// (a hidden window would be unreachable). The menu is built either
// way, so its wiring is testable offscreen.
class TrayIcon : public QObject
{
    Q_OBJECT
public:
    static TrayIcon* getInstance(QObject *parent = nullptr);
    ~TrayIcon() override;

    // The window "Show window" brings forward (main sets it once the
    // window exists).
    void setWindow(QMainWindow *window);

    // What closing the main window does (the "window.close_action"
    // setting, default Tray): with no tray, Tray degrades to Exit (a
    // hidden window would be unreachable).
    enum class CloseAction { Tray, Exit };
    CloseAction closeAction() const;

    // The main window's closeEvent asks: hide the window and keep the
    // app alive? True when the close is not part of a quit
    // (prepareQuit()) and the close action is Tray. A virtual (the
    // test seam: offscreen the tray is unavailable, the tests
    // exercise the decision, not the icon).
    virtual bool shouldHideOnClose() const;
    // The closeEvent's other branch: a real quit is due (the action is
    // Exit and the close is not the teardown of a quit already in
    // flight — prepareQuit()).
    bool shouldQuitOnClose() const;

    // A real quit is on its way (the tray's Quit, MPRIS's Quit): the
    // teardown's close must close for real, not hide to the tray.
    void prepareQuit();

    // The tray icon (nullptr when the system has no tray) and the
    // context menu (always built; the tests reach the actions through
    // it).
    QSystemTrayIcon* icon() const { return m_icon; }
    QMenu* menu() const { return m_menu; }

    // The menu bar's Show/Hide (QMenuBar has no visibility signal): the
    // "Show menu bar" entry's check follows the live visibility. Public
    // to match QObject::eventFilter() (it is public in Qt 6).
    bool eventFilter(QObject *obj, QEvent *event) override;

protected:
    explicit TrayIcon(QObject *parent = nullptr);

    // The tray's availability (the test seam: offscreen the tray is
    // unavailable, the tests simulate it).
    virtual bool trayAvailable() const;
    // The Quit action's app quit (the test seam: the tests count it,
    // the app quits).
    virtual void quit();
    // The icon's activation (the QPA's click; the test seam —
    // offscreen the QPA never emits it, the tests call it): a click
    // or a double-click brings the window forward, the other reasons
    // (the context menu's open, a middle click) do nothing.
    void onActivated(QSystemTrayIcon::ActivationReason reason);

private:
    // The icon and its context menu (the app's icon).
    void buildIcon();
    // The Play/Pause action's label and icon from the playback state
    // (the constructor runs it once with the current state — the
    // signals only fire for future changes).
    void updatePlayPause(PlaybackManager::State state);
    void showWindow();

    // The "Show menu bar" entry's live-visibility sync (the menu bar's
    // Show/Hide events, QMenuBar has no visibility signal).
    void updateMenuBarEntry();

    QPointer<QMainWindow> m_window;
    QSystemTrayIcon* m_icon = nullptr;
    QMenu* m_menu = nullptr;
    QAction* m_playPause = nullptr;
    // The "Show window" action (the "Show menu bar" entry is inserted
    // before it).
    QAction* m_showAction = nullptr;
    // The checkable "Show menu bar" entry (nullptr on a platform where
    // the menu bar cannot be hidden — the same gate as the View toggle).
    QAction* m_menuBarAction = nullptr;
    bool m_quitting = false;

protected:
    // The singleton (protected: the test seam — a subclass registers
    // itself, so the closeEvent integration runs against it).
    inline static TrayIcon* m_instance;
};

#endif // TRAYICON_H
