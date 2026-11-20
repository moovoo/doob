#ifndef SOURCEPROVIDER_H
#define SOURCEPROVIDER_H

#include <QObject>
#include <QList>
#include <QString>
#include <QUrl>

#include <functional>

#include "mediaitem.h"
#include "trackmetadata.h"

class AudioPlayer;

// A media source the core does not read itself (a streaming service, an
// external catalogue, ...). A provider is the core-side half of a source
// plugin: it claims sources at import time and serves their metadata and
// playback afterwards.
//
// Ownership is decided once, at import: MediaItemStore::ensureItem() asks
// the registered providers via accepts() and records the claimant's id in
// the item's provider field (the source is stored as pasted, never
// rewritten). Everything afterwards (playback, resync, display) looks the
// provider up by that id, so a provider's absence degrades its items
// (unplayable, not re-syncable) without ever making them unresolvable.
//
// Thread contract: providers live on the GUI thread; fetchMetadata may do
// its work anywhere but must invoke the callback on the GUI thread.
class SourceProvider : public QObject
{
    Q_OBJECT
public:
    explicit SourceProvider(QObject *parent = nullptr) : QObject(parent) {}
    ~SourceProvider() override = default;

    // The stable id recorded in the item's provider field.
    virtual QString id() const = 0;
    // The display name (a driver/provider picker).
    virtual QString title() const = 0;

    // Claims the source (a pasted link, a scanned URL). The first
    // registered provider that claims wins.
    virtual bool accepts(const QUrl &url) const = 0;

    // The canonical form of a claimed source (the stored form): the
    // provider strips the URL's incidental arguments (a list reference,
    // a share token, ...), keeping only what identifies the item, so a
    // re-pasted link with extra arguments hits the same item. The store
    // applies it at insert and lookup time. Default: as pasted.
    virtual QUrl canonicalUrl(const QUrl &url) const { return url; }

    // Asynchronous metadata read (a network fetch); cb is invoked on the
    // GUI thread, exactly once per call (an empty TrackMeta on failure).
    using MetadataCallback = std::function<void(const TrackMeta &)>;
    virtual void fetchMetadata(const QUrl &url, MetadataCallback cb) = 0;

    // A human-readable reason this provider cannot fetch its sources
    // right now (e.g. not signed in) — shown when an import skips one of
    // its sources; empty when there is no specific reason.
    virtual QString fetchFailureHint() const { return {}; }

    // Optional playback support: resolve the item to a stream URL the
    // active player can play (default: none — the player is asked for the
    // stored source), or provide the provider's own player driver
    // (default: none). Provider players are plugin-owned: they must not be
    // parented to the PlaybackManager, which deletes only what it created.
    virtual QUrl playableUrl(const MediaItem &item) const
    {
        Q_UNUSED(item);
        return {};
    }
    virtual AudioPlayer* player() const { return nullptr; }

    // Asynchronous playback resolution (the seam playItem goes through):
    // resolve the item to a stream URL the active player can play,
    // invoking cb on the GUI thread exactly once — an empty URL when the
    // item cannot be played (the PlaybackManager stops, so the stored
    // source never reaches a player that cannot play it). The default
    // keeps the synchronous playableUrl behavior; a provider whose
    // resolution takes a network round-trip (a stream-URL fetch)
    // overrides this.
    using PlaybackCallback = std::function<void(const QUrl &stream)>;
    virtual void resolvePlayback(const MediaItem &item,
                                 const PlaybackCallback &cb)
    {
        cb(playableUrl(item));
    }

    // The human/shareable form of the stored source (default: as stored).
    virtual QString shareableUrl(const MediaItem &item) const
    {
        return item.source.toString();
    }

    // A collection the source references (a pasted link with a list
    // argument): its title and its videos, in list order. A provider
    // without collection support — or a source that references no
    // collection — answers with an empty list; cb is invoked on the GUI
    // thread, exactly once per call. The videos carry the display metadata
    // the collection listing itself provides (a flat playlist extraction
    // yields title/artist/album/duration without a per-video round-trip); a
    // provider whose listing has a field leaves it empty.
    //
    // The outcome of a read of a source that DOES reference a collection
    // rides in the fields below, so the import wizard can tell the three
    // apart: the collection's entries (videos non-empty), the collection
    // read failed (error set — the source is shown as failed and not
    // imported), and the collection is legitimately empty (isCollection,
    // no error — shown as an empty section, nothing to import). A source
    // that references no collection (a single track) answers plain: no
    // videos, no error, no isCollection — the wizard imports it as a
    // plain source.
    struct ListVideo
    {
        ListVideo() = default;
        explicit ListVideo(const QUrl &url) : url(url) {}
        QUrl url;
        QString title;
        QString artist;
        QString album;
        qint64 durationMs = 0;
        // The collection listing's image for the entry (a URL the wizard
        // previews from; empty when the listing has none).
        QString thumbnail;
    };
    struct ListEntries
    {
        bool isEmpty() const { return videos.isEmpty(); }
        QString title;
        QList<ListVideo> videos;
        // The read's failure reason (empty = success): the collection
        // behind the source could not be fetched (a 404, a 400, no
        // network, not signed in).
        QString error;
        // True when the source names a collection even without entries
        // (an empty playlist): it is a container, not a single item.
        bool isCollection = false;
    };
    using ListCallback = std::function<void(const ListEntries &)>;
    virtual void fetchList(const QUrl &url, const ListCallback &cb)
    {
        cb(ListEntries());
    }
};

#endif // SOURCEPROVIDER_H
