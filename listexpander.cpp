#include "listexpander.h"

#include "sourcemanager.h"

#include <QSharedPointer>

namespace {

// The discovery's state: the provider callbacks are asynchronous (a
// fetchList may be a network round-trip), so the pending count and the
// gathered entries must outlive discover() — a shared payload, not stack
// locals the callbacks would dangle into. The count alone cannot decide
// completion: a provider may answer synchronously, landing a callback
// before the next fetch has even started. done fires only once all
// fetches have been started and the count has run down to zero.
struct DiscoveryState
{
    int pending = 0;
    bool allStarted = false;
    QHash<QString, SourceProvider::ListEntries> lists;
    ListExpander::EachDiscovered each;
    ListExpander::Discovered done;

    void finishedOne()
    {
        if (--pending == 0 && allStarted)
            done(lists);
    }
    void markAllStarted()
    {
        allStarted = true;
        if (pending == 0)
            done(lists);
    }
};

} // namespace

void ListExpander::discover(const QList<QUrl> &urls, const EachDiscovered &each,
                            const Discovered &done)
{
    QSharedPointer<DiscoveryState> state = QSharedPointer<DiscoveryState>::create();
    state->each = each;
    state->done = done;

    SourceManager* sources = SourceManager::getInstance();
    for (const QUrl &url : urls) {
        SourceProvider* provider = sources->providerFor(url);
        if (provider == nullptr)
            continue; // local files and plain streams carry no collection
        ++state->pending;
        provider->fetchList(
            url, [state, url](const SourceProvider::ListEntries &entries) {
                if (!entries.isEmpty())
                    state->lists[url.toString()] = entries;
                if (state->each)
                    state->each(url, entries);
                state->finishedOne();
            });
    }

    state->markAllStarted();
}

QList<QUrl> ListExpander::apply(const QList<QUrl> &urls,
                                const QHash<QString, SourceProvider::ListEntries> &lists,
                                const QSet<QString> &selected)
{
    QList<QUrl> out;
    QSet<QString> seen;
    for (const QUrl &url : urls) {
        const QString key = url.toString();
        const auto it = lists.constFind(key);
        if (it != lists.constEnd()) {
            // The collection contributes its checked videos, in list order.
            for (const SourceProvider::ListVideo &video : it.value().videos) {
                const QString videoKey = video.url.toString();
                if (selected.contains(videoKey) && !seen.contains(videoKey)) {
                    seen.insert(videoKey);
                    out.append(video.url);
                }
            }
            continue;
        }
        if (selected.contains(key) && !seen.contains(key)) {
            seen.insert(key);
            out.append(url);
        }
    }
    return out;
}
