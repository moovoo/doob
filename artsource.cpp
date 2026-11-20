#include "artsource.h"

#include <QDebug>

#include <algorithm>

#include "itunesartsource.h"

ArtRegistry* ArtRegistry::getInstance(QObject *parent)
{
    if (m_instance == nullptr)
        m_instance = new ArtRegistry(parent);
    return m_instance;
}

void ArtRegistry::reset()
{
    delete m_instance;
    m_instance = nullptr;
}

ArtRegistry::ArtRegistry(QObject *parent)
    : QObject(parent)
{
    registerBuiltins();
}

void ArtRegistry::registerBuiltins()
{
    // The core's built-in, parented to the registry (it lives for the
    // whole session, like the registry itself). The analyzer does not
    // see Qt's parent-child ownership, so it reports a leak: the parent
    // owns the source, so there is none.
    registerSource(new iTunesArtSource(this), QString());
}  // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

bool ArtRegistry::registerSource(ArtSource *source, const QString &owner)
{
    if (source == nullptr) {
        qWarning("ArtRegistry: registerSource with a null source");
        return false;
    }
    const QString id = source->id();
    if (contains(id)) {
        qWarning("ArtRegistry: the source id %s is already taken",
                 qPrintable(id));
        return false;
    }
    m_entries.append(Entry{ source, owner });
    emit sourcesChanged();
    return true;
}

void ArtRegistry::unregisterSource(const QString &id)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).source->id() == id) {
            m_entries.removeAt(i);
            emit sourcesChanged();
            return;
        }
    }
}

bool ArtRegistry::contains(const QString &id) const
{
    return std::any_of(m_entries.cbegin(), m_entries.cend(),
                       [&id](const Entry &entry) {
                           return entry.source->id() == id;
                       });
}

ArtSource* ArtRegistry::source(const QString &id) const
{
    for (const Entry &entry : m_entries)
        if (entry.source->id() == id)
            return entry.source;
    return nullptr;
}

QList<QPair<QString, QString>> ArtRegistry::options() const
{
    QList<QPair<QString, QString>> out;
    for (const Entry &entry : m_entries)
        out.append(qMakePair(entry.source->id(), entry.source->title()));
    return out;
}

QString ArtRegistry::owner(const QString &id) const
{
    for (const Entry &entry : m_entries)
        if (entry.source->id() == id)
            return entry.owner;
    return {};
}

void ArtRegistry::clear()
{
    m_entries.clear();
}
