#ifndef BASEWIDGET_H
#define BASEWIDGET_H

#include <QWidget>
#include <QMenu>
#include <QLayout>
#include <QPointer>
#include <QHash>
#include <QString>
#include <QVariant>

#include "widgetconfiguration.h"

class QPainter;
class QToolBar;
class WidgetContainer;

// A context menu that deletes itself when it hides: the wiring the media
// widgets' context menus share. The caller adds the actions and pops the
// menu up last — an empty menu does not show, so the popup must come
// after the actions are in.
QMenu *transientMenu(QWidget *parent);

// A widget type's tag (the "Set widget..." dialog's group): a free-form
// string — a plugin may register its own group (a tag none of the core's
// constants names), and a widget that names no tag falls back to
// "other". The core's groups, in the dialog's order ("other" last, an
// unknown group sorted in before it).
namespace widgetTags
{
inline const QString layout = QStringLiteral("layout");
inline const QString playback = QStringLiteral("playback");
inline const QString visualization = QStringLiteral("visualization");
inline const QString library = QStringLiteral("library");
inline const QString other = QStringLiteral("other");
}

// The bar display's styling surface (the peak meter's and the
// spectrum's shared "x-" properties, see barpaint): the background is
// filled and the surface carries the track (a bar's unfilled part),
// the bar color ("color" — the theme's roles stand in without a
// rule), the segments count and the margin (0: the auto share, like
// barSlot's default).
struct BarSurface
{
    QColor track;
    QColor bar;
    int segments;
    int margin;
};

class BaseWidget : public QWidget
{
    Q_OBJECT
public:
    explicit BaseWidget(QWidget *parent = nullptr);
    virtual QMenu* widgetContextMenu();

    virtual QString widgetId() const = 0;
    virtual QString widgetTitle() const = 0;

    virtual WidgetConfiguration storeWidget();
    virtual void restoreWidget(WidgetConfiguration& config);

    WidgetContainer* parentWidgetContainer();

    // Toolbar state (empty/no-op when the widget has no toolbar).
    enum class ToolbarPosition { Top, Bottom };
    QToolBar* toolbar() const { return m_toolbar; }
    bool toolbarVisible() const { return m_toolbarVisible; }
    ToolbarPosition toolbarPosition() const { return m_toolbarPosition; }

protected:
    // Toolbar support: a widget that wants a toolbar calls createToolbar()
    // in its constructor (after it installed its own layout) and adds
    // QToolButtons (optionally with QMenus) to the returned toolbar. The
    // base class owns the visibility and the position (a top/bottom strip),
    // adds the edit-mode context-menu entries and persists the state;
    // visibleByDefault is the state of a freshly created widget and only
    // states different from it are stored.
    QToolBar* createToolbar(bool visibleByDefault = false);

    void setToolbarVisible(bool visible);
    void setToolbarPosition(ToolbarPosition position);

    // Call from storeWidget()/restoreWidget() to persist/apply the toolbar
    // state as a "toolbar" map with "visible" (bool) and "position"
    // ("top"/"bottom"). storeToolbar writes the key only when the state
    // differs from the default; restoreToolbar is a no-op without it.
    void storeToolbar(WidgetConfiguration &config) const;
    void restoreToolbar(const WidgetConfiguration &config);

    // The instance name (the widget's objectName): the key the
    // per-instance "#name" rules of the style sheet target, assigned
    // lazily by "Style this widget..." (see the base's
    // widgetContextMenu()). storeName writes the "name" key only when
    // the widget is named; restoreName applies it (a missing key leaves
    // the name untouched). Subclasses that override
    // storeWidget()/restoreWidget() chain to the base so the name
    // persists with the widget.
    void storeName(WidgetConfiguration &config) const;
    void restoreName(const WidgetConfiguration &config);

    // True when the widget paints its own visible surface (a
    // custom-painted widget like the oscillogram): the "Style this
    // widget..." scaffold is the bare "#name" rule. The default is
    // false: a composite widget's visible parts are its children, which
    // carry no name of their own and which a "#name" rule does not
    // reach (Qt's QSS does not cascade it down), so the scaffold adds a
    // "#name *" rule beside it (see editOwnStyle()).
    virtual bool selfPainted() const { return false; }

    // The style editor the "Style this widget..." action opens: the
    // application's style sheet dialog seeded on `selector` (the cursor
    // lands in that rule); `revertTo` is what Cancel restores — the
    // sheet as it was before the action ran, so a rule appended for a
    // freshly named widget that the user abandons leaves nothing
    // behind. A protected seam: tests override it to record the call
    // instead of driving the modal (the chooseFiles() pattern).
    virtual void openStyleEditor(const QString &selector, const QString &revertTo);

    // A color for custom painting. A stylesheet rule matching the widget
    // is the styling surface: QStyleSheetStyle merges the rule's
    // "background" into the widget's Base/Window/Button palette roles and
    // its "color" into Text/WindowText/ButtonText. `styled` is such a
    // QSS-reachable role and `fallback` the theme role the paint code
    // used before styles existed: when no rule named the role for this
    // widget, the theme stands in and the widget paints exactly as
    // unstyled.
    QColor styleColor(QPalette::ColorRole styled, QPalette::ColorRole fallback) const;

    // The "x-" custom properties of the application style sheet (see
    // StyleManager) — the styling surface for custom painting next to
    // the palette roles: a rule matching this widget (or an ancestor,
    // by objectName or class name) overrides the coded default, and an
    // unstyled widget uses the default. The sheet's value is an opaque
    // string; the helpers interpret it.
    QString styleValue(const QString &name, const QString &fallback) const;
    QColor styleColorValue(const QString &name, const QColor &fallback) const;
    // An integer: "12" or "12px", clamped to [min, max].
    int styleInt(const QString &name, int fallback, int min, int max) const;
    // A pixel length: "12" or "12px" absolute, "50%" of `reference`.
    int styleLength(const QString &name, int fallback, int reference) const;

    // Fills the widget's background (a stylesheet's "background"
    // recolors it, the track follows) and resolves the bar display's
    // shared "x-" properties (see BarSurface).
    BarSurface barSurface(QPainter &p) const;

private:
    // The "Style this widget..." action: ensure the widget has a name
    // (auto-assign one when it does not), start an empty "#name" rule
    // when the sheet has none, and open the editor seeded on it.
    void editOwnStyle();

    QToolBar* m_toolbar = nullptr;
    bool m_toolbarVisible = false;
    bool m_toolbarDefaultVisible = false;
    ToolbarPosition m_toolbarPosition = ToolbarPosition::Top;

signals:
};

#endif // BASEWIDGET_H
