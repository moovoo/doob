#ifndef ARTFALLBACK_H
#define ARTFALLBACK_H

#include <QHash>
#include <QObject>
#include <QVariant>

#include "mediaitem.h"

// The art-fallback resolver: when a play starts on an item that has no
// stored art, it looks the art up through the source the art.fallback
// setting names (registered, default "itunes"; "off" disables it). A
// hit goes through MediaItemStore::setArtwork (the
// itemArtworkChanged signal updates the displays); a miss lands in a
// RAM negative cache (id -> time of the miss, a TTL), so a miss is not
// re-fetched on every play. One lookup at a time (the
// async+serialized convention): a play start while a lookup is in
// flight is skipped (it is retried on the item's next play start).
class ArtFallback : public QObject
{
    Q_OBJECT
public:
    static ArtFallback* getInstance(QObject *parent = nullptr);
    // Test seam: drop the instance so the next getInstance() re-runs
    // the ctor (re-connects the play watch, re-registers the setting).
    static void reset();

    // The art.fallback setting's key (the config browser and the tests
    // name it).
    inline static const char *FallbackKey = "art.fallback";

    // Test seam: the negative cache's TTL (the default is a day).
    void setMissTtlMs(qint64 ttlMs);

private:
    explicit ArtFallback(QObject *parent = nullptr);
    void onPlayingItemChanged(const MediaItem &item);
    bool isNegativeCached(const MediaItem &item);
    void recordMiss(quint64 id);
    void pruneExpiredMisses();

    // The ids of known misses -> the time of the miss (ms since
    // epoch). RAM-only: a restart re-tries, and the per-item art row
    // is the persistent memory of the hits.
    QHash<quint64, qint64> m_misses;
    qint64 m_missTtlMs = 24 * 3600 * 1000;
    bool m_lookupRunning = false;
    inline static ArtFallback* m_instance;
};

#endif // ARTFALLBACK_H
