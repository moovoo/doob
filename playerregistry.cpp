#include "playerregistry.h"

#include <QDebug>
#include <QPair>

#include <algorithm>

#include "audioplayer.h"

PlayerRegistry* PlayerRegistry::getInstance()
{
    if (m_instance == nullptr)
        m_instance = new PlayerRegistry;
    return m_instance;
}

bool PlayerRegistry::registerPlayer(const QString &id, const QString &title,
                                    Creator creator)
{
    if (!creator) {
        qWarning("PlayerRegistry::registerPlayer: empty creator for %s",
                 qPrintable(id));
        return false;
    }
    if (contains(id)) {
        qWarning("PlayerRegistry::registerPlayer: id %s is already taken",
                 qPrintable(id));
        return false;
    }
    m_players.append(Entry{ id, title, std::move(creator) });
    return true;
}

void PlayerRegistry::unregisterPlayer(const QString &id)
{
    m_players.erase(
        std::remove_if(m_players.begin(), m_players.end(),
                       [&id](const Entry &entry) { return entry.id == id; }),
        m_players.end());
}

bool PlayerRegistry::setPreferredDefault(const QString &id)
{
    if (!id.isEmpty() && !contains(id)) {
        qWarning("PlayerRegistry::setPreferredDefault: unknown player id %s",
                 qPrintable(id));
        return false;
    }
    m_preferredDefault = id;
    return true;
}

QList<QPair<QString, QString>> PlayerRegistry::options() const
{
    QList<QPair<QString, QString>> out;
    out.reserve(m_players.size());
    for (const Entry &entry : m_players)
        out.append(qMakePair(entry.id, entry.title));
    return out;
}

bool PlayerRegistry::contains(const QString &id) const
{
    return std::any_of(m_players.constBegin(), m_players.constEnd(),
                       [&id](const Entry &entry) {
                           return entry.id == id;
                       });
}

AudioPlayer* PlayerRegistry::create(const QString &id, QObject *parent) const
{
    for (const Entry &entry : m_players)
        if (entry.id == id)
            return entry.creator(parent);
    return nullptr;
}
