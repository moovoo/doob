#include "widgetregistry.h"

#include <QDebug>

#include <algorithm>

#include "basewidget.h"
#include "widgetconfiguration.h"

WidgetRegistry* WidgetRegistry::getInstance()
{
    if (m_instance == nullptr)
        m_instance = new WidgetRegistry;
    return m_instance;
}

bool WidgetRegistry::registerWidget(const QString &id, const QString &title,
                                    Creator creator, const QString &tag)
{
    if (!creator) {
        qWarning("WidgetRegistry::registerWidget: empty creator for %s",
                 qPrintable(id));
        return false;
    }
    if (std::any_of(m_widgets.constBegin(), m_widgets.constEnd(),
                    [&id](const Entry &entry) { return entry.id == id; })) {
        qWarning("WidgetRegistry::registerWidget: id %s is already taken",
                 qPrintable(id));
        return false;
    }
    m_widgets.append(Entry{ id, title,
                            tag.isEmpty() ? widgetTags::other : tag,
                            std::move(creator), });
    return true;
}

void WidgetRegistry::unregisterWidget(const QString &id)
{
    m_widgets.erase(
        std::remove_if(m_widgets.begin(), m_widgets.end(),
                       [&id](const Entry &entry) { return entry.id == id; }),
        m_widgets.end());
}

QList<WidgetRegistry::WidgetOption> WidgetRegistry::options() const
{
    QList<WidgetOption> out;
    out.reserve(m_widgets.size());
    for (const Entry &entry : m_widgets)
        out.append(WidgetOption{ entry.id, entry.title, entry.tag });
    return out;
}

BaseWidget* WidgetRegistry::create(const QString &id,
                                   WidgetConfiguration &config) const
{
    for (const Entry &entry : m_widgets)
        if (entry.id == id)
            return entry.creator(config);
    return nullptr;
}
