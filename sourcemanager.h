#ifndef SOURCEMANAGER_H
#define SOURCEMANAGER_H

#include <QList>
#include <QString>
#include <QUrl>

#include "sourceprovider.h"

// The registry of source providers (the core-side half of source plugins).
// Non-owning: a provider is created and destroyed by its owner (a plugin,
// a test); the registry only records it. Registration order is the
// providerFor() claim order (first registered claimant wins).
class SourceManager
{
public:
    // What a pasted/selected source is: an existing local file (the url
    // normalized to its absolute path), a source the first registered
    // claimant owns (the provider named), a plain http(s) stream URL, or
    // nothing usable (empty url, a missing local file, an unknown scheme
    // no provider claims).
    enum class SourceKind { Rejected, Local, Provider, Remote };
    struct ClassifiedSource {
        SourceKind kind = SourceKind::Rejected;
        QUrl url;  // normalized for Local, as pasted for Provider/Remote
        SourceProvider* provider = nullptr;  // Provider kind only
    };

    static SourceManager* getInstance();

    // Records the provider (kept alive by its owner). Fails (false, a
    // qWarning) when the id is already taken.
    bool registerProvider(SourceProvider *provider);
    void unregisterProvider(SourceProvider *provider);

    QList<SourceProvider*> providers() const { return m_providers; }
    // The provider with the id, or nullptr (e.g. the plugin is gone).
    SourceProvider* providerById(const QString &id) const;
    // The first registered provider that claims the url, or nullptr when
    // none does.
    SourceProvider* providerFor(const QUrl &url) const;
    // The one classification every import path uses (library adds,
    // playlist imports, the store's ensureItem) — the local-file check,
    // the provider claim and the plain-stream fallback live here once.
    ClassifiedSource classify(const QUrl &source) const;

    // Test seam: drops the registrations (does not delete the providers).
    void clear() { m_providers.clear(); }

private:
    SourceManager() = default;

    QList<SourceProvider*> m_providers;
    inline static SourceManager* m_instance;
};

#endif // SOURCEMANAGER_H
