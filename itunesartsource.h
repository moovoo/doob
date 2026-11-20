#ifndef ITUNESARTSOURCE_H
#define ITUNESARTSOURCE_H

#include <QJsonObject>
#include <QStringList>

#include "artsource.h"

class QNetworkAccessManager;

// The core's art source: the iTunes Search API (no key, one GET per
// term — /search?term=<term>&media=music&limit=1). The search terms
// are resolved from the item through the art.itunes_search_patterns
// setting (ordered patterns — the default tries the album's identity,
// then the track's — the first term whose first result carries art
// wins); the art is that artworkUrl100, upscaled to 600x600 by URL
// substitution. The search JSON and the image (through the shared
// ImageFetch, the 2a byte cap) are read asynchronously; the callback
// lands exactly once, on the GUI thread (an empty result = none found).
class iTunesArtSource : public ArtSource
{
    Q_OBJECT
public:
    explicit iTunesArtSource(QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("itunes"); }
    QString title() const override { return QStringLiteral("iTunes"); }

    void fetch(const MediaItem &item, ArtKind kind,
               const Callback &cb) override;

    // The search-patterns setting's key (the config browser and the
    // tests name it).
    inline static const char *PatternsKey = "art.itunes_search_patterns";

    // The pure halves (network-free): the search URL against the base,
    // the search terms resolved from an item and a pattern list (the
    // empty and repeated terms dropped), and the art URL out of a
    // search response (the largest rendition of the first result's
    // artwork; "" when there is no usable one).
    static QString buildSearchUrl(const QString &base, const QString &term);
    static QStringList buildTerms(const MediaItem &item,
                                  const QStringList &patterns);
    static QString artworkUrlFromJson(const QJsonObject &response);

    // Test seam: point the search at another host (the default is
    // https://itunes.apple.com).
    void setBaseUrl(const QString &base);

private:
    // The chain: the term at index; a miss (a transport error, no
    // results, no art in the first result) continues with the next
    // term, the exhausted chain answers empty.
    void searchTerm(int index, const QStringList &terms, const Callback &cb);

    QNetworkAccessManager* m_manager = nullptr;
    QString m_base = QStringLiteral("https://itunes.apple.com");
};

#endif // ITUNESARTSOURCE_H
