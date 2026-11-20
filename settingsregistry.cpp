#include "settingsregistry.h"

#include <QDebug>

#include <algorithm>

SettingsRegistry* SettingsRegistry::getInstance()
{
    if (m_instance == nullptr)
        m_instance = new SettingsRegistry;
    return m_instance;
}

bool SettingsRegistry::registerTopic(const QString &id, const QString &title,
                                     const QString &owner, PageCreator creator)
{
    if (!creator) {
        qWarning("SettingsRegistry::registerTopic: empty creator for %s",
                 qPrintable(id));
        return false;
    }
    if (contains(id)) {
        qWarning("SettingsRegistry::registerTopic: id %s is already taken",
                 qPrintable(id));
        return false;
    }
    m_topics.append(SettingsTopic{ id, title, owner, std::move(creator) });
    return true;
}

void SettingsRegistry::unregisterTopic(const QString &id)
{
    m_topics.erase(
        std::remove_if(m_topics.begin(), m_topics.end(),
                       [&id](const SettingsTopic &topic) {
                           return topic.id == id;
                       }),
        m_topics.end());
}

bool SettingsRegistry::contains(const QString &id) const
{
    return std::any_of(m_topics.constBegin(), m_topics.constEnd(),
                       [&id](const SettingsTopic &topic) {
                           return topic.id == id;
                       });
}

void SettingsRegistry::registerOwner(const QString &owner, const QString &title)
{
    if (owner.isEmpty())
        return;
    // A declaration without a title (or a re-declaration) keeps what is
    // already stored; an empty map entry means "display the owner id".
    if (!m_owners.contains(owner) || (!title.isEmpty() && m_owners.value(owner).isEmpty()))
        m_owners.insert(owner, title);
}

void SettingsRegistry::unregisterOwner(const QString &owner)
{
    m_owners.remove(owner);
}

QString SettingsRegistry::ownerTitle(const QString &owner) const
{
    const QString title = m_owners.value(owner);
    return title.isEmpty() ? owner : title;
}

QList<SettingsTopic> SettingsRegistry::topics() const
{
    return m_topics;
}

QList<SettingsTopic> SettingsRegistry::topicsForOwner(const QString &owner) const
{
    QList<SettingsTopic> out;
    for (const SettingsTopic &topic : m_topics)
        if (topic.owner == owner)
            out.append(topic);
    return out;
}

QStringList SettingsRegistry::owners() const
{
    QStringList out = m_owners.keys();
    for (const SettingsTopic &topic : m_topics)
        if (!topic.owner.isEmpty() && !out.contains(topic.owner))
            out.append(topic.owner);
    out.sort();
    return out;
}

void SettingsRegistry::clear()
{
    m_topics.clear();
    m_owners.clear();
}
