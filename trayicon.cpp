#include "trayicon.h"

#include "mainwindow.h"
#include "playbackmanager.h"
#include "settingsmanager.h"

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QIcon>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QSystemTrayIcon>

TrayIcon* TrayIcon::getInstance(QObject *parent)
{
    if (m_instance == nullptr) {
        m_instance = new TrayIcon(parent);
    }
    return m_instance;
}

TrayIcon::~TrayIcon()
{
    // The menu is not a child (TrayIcon is a QObject, not a QWidget,
    // so QMenu has no parent to take it): delete it here. The icon and
    // the actions die with the menu.
    delete m_menu;
}

void TrayIcon::setWindow(QMainWindow *window)
{
    m_window = window;

    // The "Show menu bar" entry (the menu bar's hiding recovery, the
    // third layer after F10 and the menu button widget): the platforms
    // where the menu bar can be hidden only (the same gate as the View
    // toggle), inserted before "Show window", its check following the
    // live visibility (the menu bar's Show/Hide events — QMenuBar has
    // no visibility signal).
    if (m_menuBarAction == nullptr) {
        MainWindow *main = qobject_cast<MainWindow*>(window);
        if (main != nullptr && main->menuBarSupported()) {
            m_menuBarAction = m_menu->addAction(QStringLiteral("Show menu bar"));
            m_menuBarAction->setObjectName("menuBarAction");
            m_menuBarAction->setCheckable(true);
            m_menu->insertAction(m_showAction, m_menuBarAction);
            connect(m_menuBarAction, &QAction::triggered, main,
                    &MainWindow::showMenuBar);
            main->menuBar()->installEventFilter(this);
            updateMenuBarEntry();
        }
    }
}

TrayIcon::TrayIcon(QObject *parent)
    : QObject(parent)
{
    // The close-action setting (registered by its owner, idempotent
    // like the other components' registrations).
    SettingsManager::getInstance()->registerSetting(
        { QStringLiteral("window.close_action"),
          QStringLiteral("Close action"),
          QStringLiteral("What closing the main window does: \"tray\" "
                         "hides it (a tray click, or the tray menu's "
                         "\"Show window\", brings it back) and keeps "
                         "the app alive; \"exit\" quits doob. \"tray\" "
                         "degrades to \"exit\" when the system has no "
                         "tray."),
          QStringLiteral("tray"),
          QString(), });

    // The context menu (built even when the tray is unavailable —
    // offscreen, for the tests): Play/Pause (state-tracked), Next,
    // Previous, a separator, Show window, Quit.
    PlaybackManager* pm = PlaybackManager::getInstance();
    m_menu = new QMenu();

    m_playPause = m_menu->addAction(QStringLiteral("Play"));
    m_playPause->setObjectName("playPauseAction");
    m_playPause->setIcon(QIcon(QStringLiteral(":/icons/play.svg")));
    connect(m_playPause, &QAction::triggered, pm,
            &PlaybackManager::togglePlayPause);
    connect(pm, &PlaybackManager::stateChanged, this,
            &TrayIcon::updatePlayPause);

    QAction* next = m_menu->addAction(QStringLiteral("Next"));
    next->setObjectName("nextAction");
    next->setIcon(QIcon(QStringLiteral(":/icons/next.svg")));
    connect(next, &QAction::triggered, pm, &PlaybackManager::next);

    QAction* previous = m_menu->addAction(QStringLiteral("Previous"));
    previous->setObjectName("previousAction");
    previous->setIcon(QIcon(QStringLiteral(":/icons/previous.svg")));
    connect(previous, &QAction::triggered, pm, &PlaybackManager::previous);

    m_menu->addSeparator();

    m_showAction = m_menu->addAction(QStringLiteral("Show window"));
    m_showAction->setObjectName("showAction");
    connect(m_showAction, &QAction::triggered, this, &TrayIcon::showWindow);

    QAction* quitAction = m_menu->addAction(QStringLiteral("Quit"));
    quitAction->setObjectName("quitAction");
    connect(quitAction, &QAction::triggered, this, [this] {
        // A real quit (the teardown's close must not hide to the tray).
        prepareQuit();
        quit();
    });

    // The icon, only where a tray is — one warning when there is none
    // (the close-to-tray setting is ignored with it).
    if (trayAvailable()) {
        buildIcon();
    } else {
        qWarning("trayicon: no system tray available — the tray icon "
                 "is off");
    }

    // The Play/Pause label from the current playback state (the
    // signals only fire for future changes).
    updatePlayPause(pm->state());
}

void TrayIcon::buildIcon()
{
    // The app's icon (main sets it on the application): the embedded
    // one when the application has none (a test's app).
    QIcon appIcon = QApplication::windowIcon();
    if (appIcon.isNull())
        appIcon = QIcon(QStringLiteral(":/icons/doob.svg"));
    m_icon = new QSystemTrayIcon(appIcon, this);
    m_icon->setContextMenu(m_menu);
    connect(m_icon, &QSystemTrayIcon::activated, this,
            &TrayIcon::onActivated);
    m_icon->show();
}

bool TrayIcon::trayAvailable() const
{
    return QSystemTrayIcon::isSystemTrayAvailable();
}

TrayIcon::CloseAction TrayIcon::closeAction() const
{
    // With no tray, hiding would be unreachable: "tray" degrades to
    // "exit" (the setting is ignored there).
    if (!trayAvailable())
        return CloseAction::Exit;
    return SettingsManager::getInstance()
        ->value(QStringLiteral("window.close_action")).toString()
        == QStringLiteral("exit")
        ? CloseAction::Exit
        : CloseAction::Tray;
}

bool TrayIcon::shouldHideOnClose() const
{
    // A quit's teardown close never hides (prepareQuit).
    return !m_quitting && closeAction() == CloseAction::Tray;
}

bool TrayIcon::shouldQuitOnClose() const
{
    // The teardown's close never re-quits (prepareQuit).
    return !m_quitting && closeAction() == CloseAction::Exit;
}

void TrayIcon::prepareQuit()
{
    m_quitting = true;
}

void TrayIcon::quit()
{
    QCoreApplication::quit();
}

void TrayIcon::updatePlayPause(PlaybackManager::State state)
{
    const bool playing = state == PlaybackManager::Playing;
    m_playPause->setText(playing
        ? QStringLiteral("Pause")
        : QStringLiteral("Play"));
    m_playPause->setIcon(QIcon(playing
        ? QStringLiteral(":/icons/pause.svg")
        : QStringLiteral(":/icons/play.svg")));
}

void TrayIcon::showWindow()
{
    if (m_window == nullptr)
        return;
    m_window->show();
    m_window->raise();
    m_window->activateWindow();
}

void TrayIcon::updateMenuBarEntry()
{
    if (m_menuBarAction == nullptr || m_window == nullptr)
        return;
    m_menuBarAction->setChecked(!m_window->menuBar()->isHidden());
}

bool TrayIcon::eventFilter(QObject *obj, QEvent *event)
{
    // The menu bar's Show/Hide (QMenuBar has no visibility signal): the
    // entry's check follows the live state (a setChecked on a matching
    // state is a no-op — no loop).
    if (m_menuBarAction != nullptr
            && m_window != nullptr
            && obj == m_window->menuBar()
            && (event->type() == QEvent::Show
                || event->type() == QEvent::Hide))
        updateMenuBarEntry();
    return QObject::eventFilter(obj, event);
}

void TrayIcon::onActivated(QSystemTrayIcon::ActivationReason reason)
{
    // A click (or a double-click) brings the window forward; the other
    // reasons (the context menu's open, a middle click) do nothing.
    if (reason == QSystemTrayIcon::Trigger
            || reason == QSystemTrayIcon::DoubleClick)
        showWindow();
}
