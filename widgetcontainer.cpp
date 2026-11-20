#include "layoutmanager.h"
#include "widgetcontainer.h"
#include "widgetcontaineroverlay.h"

#include <QMouseEvent>
#include <QResizeEvent>

#include <functional>

WidgetContainer::WidgetContainer(QWidget *parent)
    : QFrame{parent}
{
    setFrameShadow(QFrame::Sunken);
    setFrameShape(QFrame::StyledPanel);

    m_overlayWidget = new WidgetContainerOverlay(this);
    m_overlayWidget->show();

    m_layout = new QHBoxLayout();
    m_layout->setContentsMargins(0, 0, 0, 0);
    setLayout(m_layout);

    installEventFilter(this);
}

void WidgetContainer::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (!m_childWidget)
    {
        m_overlayWidget->grayed(true);
        LayoutManager::getInstance()->popupContainerMenu(this);
    }
}

void WidgetContainer::resizeEvent(QResizeEvent * event)
{
    m_overlayWidget->resize(event->size());
}

void WidgetContainer::setWidget(BaseWidget* widget, bool deleteOld)
{
    if (m_childWidget)
    {
        m_childWidget->hide();
        m_layout->removeWidget(m_childWidget);
        if (deleteOld)
        {
            m_childWidget->deleteLater();
        }
    }
    m_childWidget = widget;
    if (m_childWidget)
    {
        m_layout->addWidget(m_childWidget);
    }
    m_overlayWidget->raise();
}

BaseWidget* WidgetContainer::getWidget() const
{
    return m_childWidget;
}

void WidgetContainer::paintEvent(QPaintEvent* event)
{
    if (!m_childWidget)
    {
        QFrame::paintEvent(event);
    }
}

WidgetContainerOverlay* WidgetContainer::getOverlayWidget() const
{
    return m_overlayWidget;
}

namespace {
// The event filter's walker: every object under `o` gets `apply` run on
// it with the current filter — a container within the subtree becomes
// the filter for its own children.
void walkEventFilter(QObject *o, QObject *p,
                     const std::function<void(QObject *, QObject *)> &apply)
{
    // if a child also a widget container, than use it as event filter instead
    if (qobject_cast<WidgetContainer*>(o) != nullptr)
    {
        p = o;
    }
    for (QObject *child : o->children())
    {
        walkEventFilter(child, p, apply);
    }
    apply(o, p);
}
} // namespace

void WidgetContainer::forceInstallEventFilter() {
    for (QObject *child : children()) {
        walkEventFilter(child, this, [](QObject *object, QObject *filter) {
            object->installEventFilter(filter);
        });
    }
}

void WidgetContainer::childEvent(QChildEvent * event)
{
    if (event->added())
    {
        walkEventFilter(event->child(), this, [](QObject *object, QObject *filter) {
            object->installEventFilter(filter);
        });
    }
    else if (event->removed())
    {
        walkEventFilter(event->child(), this, [](QObject *object, QObject *filter) {
            object->removeEventFilter(filter);
        });
    }
}

bool WidgetContainer::eventFilter(QObject *obj, QEvent *event)
{
    if (!LayoutManager::getInstance()->isEditMode())
        return QFrame::eventFilter(obj, event);

    // The mouse entered this container (one of its child widgets): the
    // flip-flop resets to "edit next" if it is a different container than
    // the mouse was in before, so the first right-click on a widget is
    // always the edit menu (see LayoutManager::mouseEnteredContainer).
    if (event->type() == QEvent::Enter)
        LayoutManager::getInstance()->mouseEnteredContainer(this);

    // The edit-mode right-click flip-flop (7d). The flip-flop's state is
    // driven by the MENUS, not the clicks: the edit menu closing flips it
    // to "native next"; a pass-through (the widget's native menu) flips it
    // back to "edit next". The clicks just follow the state: the press
    // records the click's decision, the release and the synthesized
    // context-menu event act on it.
    if (event->type() == QEvent::MouseButtonPress
        && static_cast<QMouseEvent*>(event)->button() == Qt::RightButton)
    {
        // The press (a clean one — a dismissal's press goes to the
        // popup's grab, never here) records the click's decision: the
        // flip-flop's current state. It must be set before the
        // context-menu event, which a
        // QStyleHints::contextMenuTrigger() == Press platform
        // synthesizes from this very press. It also arms the mouse-entry
        // tracker (a no-op — the mouse is already here), so the
        // QEvent::Enter that fires when the mouse re-enters the widget
        // after a menu closes is seen as a re-entry, not a fresh one.
        LayoutManager::getInstance()->mouseEnteredContainer(this);
        m_thisClickPassThrough =
            LayoutManager::getInstance()->editRightClickPassThrough();
        return QFrame::eventFilter(obj, event);
    }

    if (event->type() == QEvent::MouseButtonRelease)
    {
        QMouseEvent *mouse_event = static_cast<QMouseEvent*>(event);
        if (mouse_event->button() == Qt::RightButton)
        {
            // A click whose press landed on an open popup (the edit menu,
            // a native menu from a previous pass-through) is a dismissal,
            // not a new right-click: the press went to the popup's mouse
            // grab (so it set no decision above), this release is just
            // its tail. Swallow it whole — the flip-flop already moved
            // when the popup closed (the edit menu's aboutToHide; the
            // native menu's close flips it back on the next pass-
            // through), so a dismissal must not act on a stale decision.
            if (LayoutManager::getInstance()->takePressOnPopup())
                return true;

            if (m_thisClickPassThrough)
            {
                // Pass-through to the widget's own native context menu
                // (the synthesized context-menu event goes through with
                // it, the widget's Qt::CustomContextMenu slot shows its
                // menu as in normal mode). The flip-flop flips back to
                // "edit next" now — the native menu's disappearance is
                // not observed.
                LayoutManager::getInstance()->setEditRightClickPassThrough(false);
                return QFrame::eventFilter(obj, event);
            }
            // The edit menu owns the click.
            m_overlayWidget->grayed(true);
            LayoutManager::getInstance()->popupContainerMenu(this);
            return true;
        }
    }

    // The synthesized context-menu event (see
    // QWindowPrivate::maybeSynthesizeContextMenuEvent) — from the press
    // on a QStyleHints::contextMenuTrigger() == Press platform, from the
    // release on a Release one. It follows the click's decision (set by
    // the press): a pass-through goes to the widget's native menu
    // untouched (and flips the flip-flop back), an edit-menu click is
    // swallowed (a pass-through here would pop the widget's native menu
    // over the edit menu, or — on a Press-trigger platform — grab the
    // mouse and eat the release that shows it).
    if (event->type() == QEvent::ContextMenu)
    {
        if (m_thisClickPassThrough)
        {
            LayoutManager::getInstance()->setEditRightClickPassThrough(false);
            return QFrame::eventFilter(obj, event);
        }
        return true;
    }
    return QFrame::eventFilter(obj, event);
}

WidgetConfiguration WidgetContainer::storeContainer() {
    WidgetConfiguration config;
    if (m_childWidget) {
        config = m_childWidget->storeWidget();
        config["type"] = m_childWidget->widgetId();
    }
    return config;
}

void WidgetContainer::restoreContainer(WidgetConfiguration& config) {
    QString typeValue = config.value("type", QVariant()).toString();
    if (!typeValue.isEmpty()) {
        setWidget(LayoutManager::getInstance()->createWidget(typeValue, config));
    } else {
        setWidget(nullptr);
    }
}


