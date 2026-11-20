#ifndef WINDOWMANAGER_H
#define WINDOWMANAGER_H

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QRect>
#include <QString>

class MainWindow;
class QDockWidget;
class QWidget;
class WidgetContainer;

// Owns the layout's child windows: standalone top-level windows and
// docked windows (QDockWidgets on the main window). Each window hosts
// its own widget hierarchy (a WidgetContainer) and lives in the layout
// document under the "windows" key, next to the main container, the
// main window's state and the style sheet. Closing a window hides it
// (the state persists, the window is not destroyed); the application
// quits via the main window only.
class WindowManager : public QObject
{
    Q_OBJECT
public:
    enum class Kind { Standalone, Docked };

    static WindowManager* getInstance(QObject *parent = nullptr);

    // A window's identity (the live widgets live on the manager).
    struct Info {
        QString id; // empty when the window is unknown
        Kind kind = Kind::Standalone;
        QString title;
        bool visible = true;
    };

    // The windows, in document order.
    QList<Info> windows() const;

    bool hasWindow(const QString &id) const;
    Info window(const QString &id) const;
    Kind windowKind(const QString &id) const;
    QString windowTitle(const QString &id) const;
    bool windowVisible(const QString &id) const;

    // The window's live widgets: the container it hosts and the carrier
    // widget (a QDockWidget when docked, a top-level QWidget when
    // standalone) — nullptr when the id is unknown.
    WidgetContainer* containerOf(const QString &id) const;
    QWidget* widgetOf(const QString &id) const;

    // The containers of all windows (the free-name scan of the
    // LayoutManager includes the widgets they host).
    QList<WidgetContainer*> containers() const;

    // Mutations. Each emits windowsChanged(); a visibility change also
    // emits windowVisibleChanged(id, visible). addWindow returns the
    // new window's id ("" when a docked window is requested without a
    // main window to host it).
    QString addWindow(Kind kind, const QString &title);
    void removeWindow(const QString &id);
    void setWindowTitle(const QString &id, const QString &title);
    // Converts the window to the other kind, keeping its container and
    // its visibility.
    void setWindowKind(const QString &id, Kind kind);
    void setWindowVisible(const QString &id, bool visible);

    // Persistence (called from the LayoutManager): the layout document's
    // "windows" list (empty when there are no windows), the main
    // window's dock arrangement (QMainWindow::saveState, empty when
    // there are no docked windows), and the restore: it tears down all
    // live windows and rebuilds them from the list (dockState is
    // applied once the docked windows are back, so it also restores
    // which docks were hidden).
    QVariantList storeWindows() const;
    QByteArray dockState() const;
    void restoreWindows(const QVariantList &windows, const QByteArray &dockState);

    // Test seam: tears down all live windows (a reset, like the other
    // singletons').
    void clear();

signals:
    void windowsChanged();
    void windowVisibleChanged(const QString &id, bool visible);

protected:
    ~WindowManager() override;

private:
    // A live window: the record (what storeWindows writes) plus the
    // carrier widgets (what holds the container).
    struct Window {
        QString id;
        Kind kind = Kind::Standalone;
        QString title;
        bool visible = true;
        QRect geometry; // standalone only
        bool maximized = false; // standalone only
        WidgetContainer* container = nullptr;
        QDockWidget* dock = nullptr; // kind == Docked
        QWidget* top = nullptr; // kind == Standalone
    };

    inline static WindowManager* m_windowManager;
    explicit WindowManager(QObject *parent = nullptr);

    QList<Window> m_windows;

    Window* find(const QString &id);
    const Window* find(const QString &id) const;
    // The carrier widget (the QDockWidget or the top-level QWidget) of a
    // window — nullptr when it has none.
    static QWidget* carrier(const Window &w);
    // Follows the carrier's visibility (the window's own close button
    // hides it): the record and the observers follow the signal, never
    // a direct write.
    void connectVisibility(const QString &id, QWidget *widget);
    // Removes the window's carrier from the layout and deletes it
    // (the container dies with it).
    void destroyCarrier(Window &w);
    // Shows the carrier, and — for a visible standalone — applies the
    // recorded geometry once it is shown: a window manager may
    // re-place a window on the (re-)map, so the position set before
    // the show is only a request (the main window's position is
    // applied the same way, after the show).
    void showWindow(const Window &w);
};

#endif // WINDOWMANAGER_H
