#ifndef WIDGETREGISTRY_H
#define WIDGETREGISTRY_H

#include <QList>
#include <QString>

#include <functional>

#include "basewidget.h"
#include "widgetconfiguration.h"

class BaseWidget;

// The registry of widget types (the core-side half of a widget plugin):
// id -> (title, tag, creator). The built-in widgets register from the
// LayoutManager's constructor; a plugin registers its own in
// initialize(). LayoutManager's widgetOptions()/createWidget() are thin
// lookups into this registry, so the "Set widget..." dialog, the
// clipboard paste validation and the layout restore all pick up
// plugin widgets for free.
class WidgetRegistry
{
public:
    // A registered widget type: the id (the layout document's "type"),
    // the display title, and the tag (the "Set widget..." dialog's
    // group — a free-form string, see widgetTags).
    struct WidgetOption
    {
        QString id;
        QString title;
        QString tag;
    };

    using Creator = std::function<BaseWidget*(WidgetConfiguration &)>;

    static WidgetRegistry* getInstance();

    // Fails (false, a qWarning) when the id is already taken or the
    // creator is empty. `tag` groups the type in the "Set widget..."
    // dialog; a widget that does not name one falls back to
    // widgetTags::other.
    bool registerWidget(const QString &id, const QString &title,
                        Creator creator,
                        const QString &tag = widgetTags::other);
    void unregisterWidget(const QString &id);

    // The registered types, registration order.
    QList<WidgetOption> options() const;
    // Creates the widget with the id, or nullptr when unknown.
    BaseWidget* create(const QString &id, WidgetConfiguration &config) const;

    // Test seam: drops the registrations.
    void clear() { m_widgets.clear(); }

private:
    WidgetRegistry() = default;

    struct Entry
    {
        QString id;
        QString title;
        QString tag;
        Creator creator;
    };
    QList<Entry> m_widgets;  // registration order
    inline static WidgetRegistry* m_instance;
};

#endif // WIDGETREGISTRY_H
