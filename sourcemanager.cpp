#include "sourcemanager.h"

#include <QDebug>
#include <QFileInfo>

SourceManager* SourceManager::getInstance()
{
    if (m_instance == nullptr)
        m_instance = new SourceManager;
    return m_instance;
}

bool SourceManager::registerProvider(SourceProvider *provider)
{
    if (provider == nullptr)
        return false;
    if (providerById(provider->id()) != nullptr) {
        qWarning("SourceManager::registerProvider: id %s is already taken",
                 qPrintable(provider->id()));
        return false;
    }
    m_providers.append(provider);
    return true;
}

void SourceManager::unregisterProvider(SourceProvider *provider)
{
    m_providers.removeAll(provider);
}

SourceProvider* SourceManager::providerById(const QString &id) const
{
    for (SourceProvider *provider : m_providers)
        if (provider->id() == id)
            return provider;
    return nullptr;
}

SourceProvider* SourceManager::providerFor(const QUrl &url) const
{
    for (SourceProvider *provider : m_providers)
        if (provider->accepts(url))
            return provider;
    return nullptr;
}

SourceManager::ClassifiedSource SourceManager::classify(const QUrl &source) const
{
    ClassifiedSource out;
    if (source.isEmpty())
        return out;
    if (source.isLocalFile()) {
        // Only an existing local file is importable; the url is stored in
        // its absolute form so the same file hits the same item.
        const QFileInfo info(source.toLocalFile());
        if (!info.exists() || !info.isFile())
            return out;
        out.kind = SourceKind::Local;
        out.url = QUrl::fromLocalFile(info.absoluteFilePath());
        return out;
    }
    if (SourceProvider *provider = providerFor(source)) {
        // Stored as pasted; the claimant's canonical form is applied at
        // insert and lookup by the store.
        out.kind = SourceKind::Provider;
        out.provider = provider;
        out.url = source;
        return out;
    }
    if (source.scheme() == QLatin1String("http")
            || source.scheme() == QLatin1String("https")) {
        out.kind = SourceKind::Remote;
        out.url = source;
        return out;
    }
    return out;
}
