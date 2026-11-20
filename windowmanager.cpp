#include "windowmanager.h"

#include "layoutmanager.h"
#include "mainwindow.h"
#include "uistate.h"
#include "widgetcontainer.h"

#include <QDockWidget>
#include <QEvent>
#include <QHBoxLayout>
#include <QWidget>

#include <functional>

namespace {

// A fresh standalone window's size (the user resizes it afterwards).
const QSize kDefaultStandaloneSize(480, 360);

// Follows a carrier's show/hide (the window's own close button hides
// it, a show anywhere re-shows it) — this Qt's QWidget has no
// visibilityChanged signal (only QDockWidget and QToolBar do), so the
// show/hide events are the generic surface. The move/resize of a
// standalone carrier are followed the same way (the record's geometry
// must match the widget the user drags and resizes). The watcher is
// parented to the carrier and dies with it.
class VisibilityWatcher : public QObject
{
public:
    VisibilityWatcher(QObject *parent,
                      std::function<void(bool)> notify,
                      std::function<void(const QRect&)> geometry)
        : QObject(parent),
          m_notify(std::move(notify)),
          m_geometry(std::move(geometry)) {}

    bool eventFilter(QObject *obj, QEvent *event) override
    {
        if (event->type() == QEvent::Show || event->type() == QEvent::Hide)
            m_notify(event->type() == QEvent::Show);
        else if (event->type() == QEvent::Move || event->type() == QEvent::Resize)
            if (QWidget* widget = qobject_cast<QWidget*>(obj))
                m_geometry(widget->geometry());
        return QObject::eventFilter(obj, event);
    }

private:
    std::function<void(bool)> m_notify;
    std::function<void(const QRect&)> m_geometry;
};

// The top-level widget a standalone window is carried in (the
// container is added to its layout by the caller).
QWidget* makeStandaloneWidget(const QString &title, const QRect &geometry)
{
    QWidget* top = new QWidget(nullptr, Qt::Window);
    top->setWindowTitle(title);
    // The main window is the app's anchor. Without this, closing it
    // while a standalone window is open would keep the app alive in the
    // orphaned window, and the user's closing of that window — the
    // final one — is what quits the app and saves the layout, recording
    // the window as closed: a restart would then restore it hidden.
    // With the attribute off, closing the main window quits the app and
    // the layout is saved with the windows in the state they were in.
    top->setAttribute(Qt::WA_QuitOnClose, false);
    QHBoxLayout* layout = new QHBoxLayout(top);
    layout->setContentsMargins(0, 0, 0, 0);
    if (geometry.isValid() && geometry.width() > 0 && geometry.height() > 0)
        top->setGeometry(geometry);
    else
        top->resize(kDefaultStandaloneSize);
    return top;
}
}

WindowManager::WindowManager(QObject *parent)
    : QObject{parent}
{
}

WindowManager::~WindowManager() = default;

WindowManager* WindowManager::getInstance(QObject *parent)
{
    if (m_windowManager == nullptr) {
        m_windowManager = new WindowManager(parent);
    }
    return m_windowManager;
}

WindowManager::Window* WindowManager::find(const QString &id)
{
    for (Window &w : m_windows)
        if (w.id == id)
            return &w;
    return nullptr;
}

const WindowManager::Window* WindowManager::find(const QString &id) const
{
    for (const Window &w : m_windows)
        if (w.id == id)
            return &w;
    return nullptr;
}

QWidget* WindowManager::carrier(const Window &w)
{
    return w.dock != nullptr ? static_cast<QWidget*>(w.dock) : w.top;
}

// A visibility change of the carrier is the single source of truth:
// the record and the observers follow it, never a direct write. The id
// is captured by value: the record is looked up again on every event,
// so a re-created window never meets a stale one.
void WindowManager::connectVisibility(const QString &id, QWidget *widget)
{
    if (widget == nullptr)
        return;
    auto* watcher = new VisibilityWatcher(
        widget,
        [this, id](bool visible) {
            Window* w = find(id);
            if (w == nullptr)
                return;
            if (!visible && w->kind == Kind::Docked) {
                // Hiding the main window cascades a hide to its docked
                // child windows (the app's exit does exactly that, and
                // the layout is saved afterwards): that is the app
                // going away, not the window being closed, so the
                // record keeps the state the window was in — a
                // restarted dock restored from a cascade-flipped flag
                // would come back hidden. A show always applies: by
                // then the main window is visible again.
                MainWindow* mw = LayoutManager::getInstance()->window();
                if (mw != nullptr && !mw->isVisible())
                    return;
            }
            w->visible = visible;
            emit windowVisibleChanged(id, visible);
        },
        [this, id](const QRect &geometry) {
            Window* w = find(id);
            if (w != nullptr && w->kind == Kind::Standalone)
                w->geometry = geometry;
        });
    widget->installEventFilter(watcher);
}

// Hides the carrier before deleting it: the deletion of a visible
// top-level widget is deferred until it is hidden (and never lands in
// the offscreen platform at all). A synchronous delete is safe: the
// carrier is destroyed from an outside call (the manager, the
// editor's dialog), never from its own signal handler.
void WindowManager::destroyCarrier(Window &w)
{
    MainWindow* mw = LayoutManager::getInstance()->window();
    if (mw != nullptr && w.dock != nullptr)
        mw->removeDockWidget(w.dock);
    if (w.dock != nullptr) {
        w.dock->hide();
        delete w.dock;
    }
    if (w.top != nullptr) {
        w.top->hide();
        delete w.top;
    }
}

void WindowManager::showWindow(const Window &w)
{
    QWidget* c = carrier(w);
    if (c == nullptr)
        return;
    c->show();
    if (w.kind == Kind::Standalone && w.geometry.isValid())
        c->setGeometry(w.geometry);
}

QString WindowManager::addWindow(Kind kind, const QString &title)
{
    MainWindow* mw = LayoutManager::getInstance()->window();
    if (kind == Kind::Docked && mw == nullptr)
        return {};

    Window w;
    {
        // A stable id of its own ("" would never do: the toggle widget
        // references windows by id). Bumped past the ids in use.
        int n = 1;
        while (find(QStringLiteral("window-%1").arg(n)) != nullptr)
            ++n;
        w.id = QStringLiteral("window-%1").arg(n);
    }
    w.kind = kind;
    w.title = title;
    w.visible = true;
    w.container = new WidgetContainer();

    if (kind == Kind::Docked) {
        w.dock = new QDockWidget(title, mw);
        // saveState/restoreState key the dock by its objectName (and
        // warn without one); the window's id is stable across a
        // save/restore, so the saved state maps onto the recreated dock.
        w.dock->setObjectName(w.id);
        w.dock->setWidget(w.container);
        mw->addDockWidget(Qt::TopDockWidgetArea, w.dock);
    } else {
        w.top = makeStandaloneWidget(title, QRect());
        w.top->layout()->addWidget(w.container);
        w.geometry = w.top->geometry();
    }

    m_windows.append(w);
    connectVisibility(w.id, carrier(w));

    // A fresh window is visible; the carrier is shown after the record
    // is in place (the visibility signal keeps it in sync).
    carrier(w)->show();

    emit windowsChanged();
    return w.id;
}

void WindowManager::removeWindow(const QString &id)
{
    Window* w = find(id);
    if (w == nullptr)
        return;

    // The container dies with its carrier.
    destroyCarrier(*w);

    for (int i = 0; i < m_windows.size(); ++i)
        if (m_windows.at(i).id == id) {
            m_windows.removeAt(i);
            break;
        }
    emit windowsChanged();
}

void WindowManager::setWindowTitle(const QString &id, const QString &title)
{
    Window* w = find(id);
    if (w == nullptr || w->title == title)
        return;
    w->title = title;
    if (w->dock != nullptr)
        w->dock->setWindowTitle(title);
    if (w->top != nullptr)
        w->top->setWindowTitle(title);
    emit windowsChanged();
}

void WindowManager::setWindowKind(const QString &id, Kind kind)
{
    Window* w = find(id);
    if (w == nullptr || w->kind == kind)
        return;
    MainWindow* mw = LayoutManager::getInstance()->window();
    if (kind == Kind::Docked && mw == nullptr)
        return;

    const bool wasVisible = w->visible;
    const QRect oldGeometry = w->geometry;

    if (kind == Kind::Docked) {
        QDockWidget* dock = new QDockWidget(w->title, mw);
        dock->setObjectName(w->id); // saveState keys the dock by it
        dock->setWidget(w->container); // reparents the container
        mw->addDockWidget(Qt::TopDockWidgetArea, dock);
        // The old top-level (the record already names the new dock, so
        // it is destroyed directly, not through destroyCarrier). Its
        // final geometry is captured (not the watcher's, whose move/
        // resize events may still be queued) for a conversion back:
        // the record's geometry is written only while the window is
        // standalone, and a conversion back restores it.
        w->geometry = w->top->geometry();
        w->top->hide();
        delete w->top;
        w->top = nullptr;
        w->dock = dock;
    } else {
        QDockWidget* oldDock = w->dock;
        w->dock = nullptr;
        w->top = makeStandaloneWidget(w->title, oldGeometry);
        w->top->layout()->addWidget(w->container); // out of the dock first:
        // the synchronous delete of the old carrier would take the
        // container (its child) with it. The old dock is destroyed
        // directly (destroyCarrier would take the new top with it).
        if (oldDock != nullptr) {
            if (mw != nullptr)
                mw->removeDockWidget(oldDock);
            oldDock->hide();
            delete oldDock;
        }
        w->geometry = w->top->geometry();
    }
    w->kind = kind;
    w->visible = wasVisible;

    // The old carrier's watcher died with the carrier (it is a child),
    // the new one takes over.
    connectVisibility(w->id, carrier(*w));

    if (wasVisible)
        showWindow(*w);

    emit windowsChanged();
}

void WindowManager::setWindowVisible(const QString &id, bool visible)
{
    Window* w = find(id);
    if (w == nullptr)
        return;
    QWidget* c = carrier(*w);
    if (c == nullptr || c->isVisible() == visible)
        return;
    // The visibility signal updates the record and emits
    // windowVisibleChanged.
    if (visible)
        showWindow(*w);
    else
        c->hide();
}

QList<WindowManager::Info> WindowManager::windows() const
{
    QList<Info> list;
    for (const Window &w : m_windows)
        list.append(Info{w.id, w.kind, w.title, w.visible});
    return list;
}

bool WindowManager::hasWindow(const QString &id) const
{
    return find(id) != nullptr;
}

WindowManager::Info WindowManager::window(const QString &id) const
{
    const Window* w = find(id);
    return w != nullptr ? Info{w->id, w->kind, w->title, w->visible} : Info{};
}

WindowManager::Kind WindowManager::windowKind(const QString &id) const
{
    return window(id).kind;
}

QString WindowManager::windowTitle(const QString &id) const
{
    return window(id).title;
}

bool WindowManager::windowVisible(const QString &id) const
{
    const Window* w = find(id);
    return w != nullptr && w->visible;
}

WidgetContainer* WindowManager::containerOf(const QString &id) const
{
    const Window* w = find(id);
    return w != nullptr ? w->container : nullptr;
}

QWidget* WindowManager::widgetOf(const QString &id) const
{
    const Window* w = find(id);
    return w != nullptr ? carrier(*w) : nullptr;
}

QList<WidgetContainer*> WindowManager::containers() const
{
    QList<WidgetContainer*> list;
    for (const Window &w : m_windows)
        list.append(w.container);
    return list;
}

QVariantList WindowManager::storeWindows() const
{
    QVariantList list;
    for (const Window &w : m_windows) {
        QVariantMap map;
        map.insert("id", w.id);
        map.insert("kind", w.kind == Kind::Docked ? "docked" : "standalone");
        map.insert("title", w.title);
        map.insert("visible", w.visible);
        if (w.kind == Kind::Standalone && w.geometry.isValid()) {
            QVariantMap geo;
            geo.insert("x", w.geometry.x());
            geo.insert("y", w.geometry.y());
            geo.insert("width", w.geometry.width());
            geo.insert("height", w.geometry.height());
            geo.insert("maximized", w.maximized);
            map.insert("geometry", geo);
        }
        // The window's own widget hierarchy (an empty container is a
        // valid saved state).
        map.insert("container", w.container->storeContainer());
        list.append(map);
    }
    return list;
}

QByteArray WindowManager::dockState() const
{
    MainWindow* mw = LayoutManager::getInstance()->window();
    if (mw == nullptr)
        return {};
    for (const Window &w : m_windows)
        if (w.kind == Kind::Docked)
            return mw->saveState();
    return {};
}

void WindowManager::restoreWindows(const QVariantList &windows,
                                   const QByteArray &dockState)
{
    clear();

    MainWindow* mw = LayoutManager::getInstance()->window();
    if (mw == nullptr)
        return;

    for (const QVariant &entry : windows) {
        const QVariantMap map = entry.toMap();
        const QString id = map.value("id").toString();
        if (id.isEmpty())
            continue;

        Window w;
        w.id = id;
        w.kind = map.value("kind").toString() == "docked"
            ? Kind::Docked
            : Kind::Standalone;
        w.title = map.value("title").toString();
        if (w.title.isEmpty())
            w.title = w.id;
        w.visible = map.value("visible", true).toBool();
        if (w.kind == Kind::Standalone) {
            // The saved state is sanitized (UiState's restore rule —
            // the main window's state is treated the same way in the
            // LayoutManager): a degenerate or oversized size keeps the
            // fresh default, a position off every screen is centered on
            // the primary screen.
            const QVariantMap geo = map.value("geometry").toMap();
            w.geometry = UiState::restoredGeometry(geo);
            w.maximized = geo.value("maximized", false).toBool();
        }

        w.container = new WidgetContainer();
        if (w.kind == Kind::Docked) {
            w.dock = new QDockWidget(w.title, mw);
            w.dock->setObjectName(w.id); // saveState keys the dock by it
            w.dock->setWidget(w.container);
            mw->addDockWidget(Qt::TopDockWidgetArea, w.dock);
        } else {
            w.top = makeStandaloneWidget(w.title, w.geometry);
            w.top->layout()->addWidget(w.container);
            w.geometry = w.top->geometry();
        }

        m_windows.append(w);
        connectVisibility(w.id, carrier(w));

        // The window's own widget hierarchy; an unknown widget type
        // leaves the container empty (the LayoutManager already walked
        // the document and warned / refused it).
        WidgetConfiguration cfg = map.value("container").toMap();
        find(w.id)->container->restoreContainer(cfg);
    }

    // The dock arrangement (which edge, tab, size, float) is part of
    // the main window's state: it is applied once all the docks are
    // back, and it restores which docks were hidden too.
    if (!dockState.isEmpty())
        mw->restoreState(dockState);

    for (Window &w : m_windows) {
        if (w.kind == Kind::Docked) {
            // The saved flag is authoritative: a dock's live visibility
            // may still be settling (a pending show), and the flag and
            // the dock state were captured at the same moment.
            w.dock->setVisible(w.visible);
            continue;
        }
        if (w.maximized)
            w.top->showMaximized();
        else if (w.visible)
            showWindow(w);
        // A restored hidden window stays hidden; when it is shown later
        // (a toggle, the editor) the same post-show geometry
        // application applies.
    }

    emit windowsChanged();
}

void WindowManager::clear()
{
    for (Window &w : m_windows)
        destroyCarrier(w);
    m_windows.clear();
}
