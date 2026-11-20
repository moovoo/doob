#ifndef IMAGEFETCH_H
#define IMAGEFETCH_H

#include <QByteArray>
#include <QHash>
#include <QPair>
#include <QQueue>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;

// The shared plumbing of the image reads (the art fetches): download the
// image at url as capped raw bytes and invoke cb exactly once (on the
// GUI thread, empty data = failure). One shared QNetworkAccessManager
// (the GUI thread), a 15 s timeout, a 10 MB byte cap enforced while the
// download runs, and a RAM-only, URL-keyed, size-capped (~32 MB) cache:
// the same image URL behind many items (an album's cover, one per track)
// is downloaded once per session, not once per item. The per-item
// item_artwork row is the persistent cache (a later play is offline), so
// v1 has no disk cache — the escape hatch, if it ever is needed, is a
// QNetworkDiskCache on the same manager.
class ImageFetch
{
public:
    // The download's result: the image bytes and their mime type (the
    // reply's Content-Type, falling back to the URL's suffix).
    using Callback =
        std::function<void(const QByteArray &data, const QString &mime)>;

    static ImageFetch* getInstance();
    static void reset(); // for tests

    // Downloads the image at url (http/https only); cb is invoked
    // exactly once, on the GUI thread — empty data on a timeout, an
    // HTTP error, a reply over the byte cap or an invalid url.
    void fetch(const QUrl &url, const Callback &cb);

    // Test seam: drops the RAM cache.
    void clearCache();

    // The bounds (the oversized-reply and the eviction tests size their
    // traffic against them).
    inline static const int kTimeoutMs = 15000;
    inline static const int kMaxBytes = 10 * 1024 * 1024;
    inline static const int kCacheBytes = 32 * 1024 * 1024;

private:
    ImageFetch();

    struct Entry
    {
        QByteArray data;
        QString mime;
    };

    QNetworkAccessManager* m_manager = nullptr;
    QHash<QString, Entry> m_cache;
    QQueue<QString> m_order; // the cache's insertion order (FIFO eviction)
    qint64 m_cacheSize = 0;

    inline static ImageFetch* m_instance = nullptr;
};

inline ImageFetch* ImageFetch::getInstance()
{
    if (m_instance == nullptr)
        m_instance = new ImageFetch;
    return m_instance;
}

#endif // IMAGEFETCH_H
