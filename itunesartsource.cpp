#include "itunesartsource.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include "imagefetch.h"
#include "settingsmanager.h"

namespace {

// The pattern's tokens and the item fields they resolve to. An unknown
// token (a typo among them) resolves to nothing and drops out.
QString fieldFor(const MediaItem &item, const QString &token)
{
    if (token == QLatin1String("artist"))
        return item.artist;
    if (token == QLatin1String("title"))
        return item.title;
    if (token == QLatin1String("album"))
        return item.album;
    if (token == QLatin1String("album_artist"))
        return item.albumArtist;
    return {};
}

// One pattern, resolved for the item: the tokens become their
// (trimmed) field values, an empty field and an unknown token drop
// out, and the whitespace runs the gaps leave behind are collapsed.
QString resolvePattern(const MediaItem &item, const QString &pattern)
{
    QString out;
    qsizetype i = 0;
    while (i < pattern.size()) {
        const qsizetype open = pattern.indexOf(QLatin1Char('{'), i);
        if (open < 0) {
            out += pattern.mid(i);
            break;
        }
        out += pattern.mid(i, open - i);
        const qsizetype close = pattern.indexOf(QLatin1Char('}'), open + 1);
        if (close < 0) { // a dangling "{": the rest is literal
            out += pattern.mid(open);
            break;
        }
        const QString value =
            fieldFor(item, pattern.mid(open + 1, close - open - 1)).trimmed();
        if (!value.isEmpty())
            out += value;
        i = close + 1;
    }
    return out.simplified();
}

}

iTunesArtSource::iTunesArtSource(QObject *parent)
    : ArtSource(parent)
{
    m_manager = new QNetworkAccessManager(this);
    SettingsManager::getInstance()->registerSetting(
        { PatternsKey, QStringLiteral("Art search patterns"),
          QStringLiteral("The search terms the iTunes lookup tries for an "
                         "item with no art, in order — the first result "
                         "with art wins. Tokens: {artist}, {title}, "
                         "{album}, {album_artist} (an empty field and an "
                         "unknown token drop out; a pattern that resolves "
                         "to nothing is skipped)."),
          QStringList{ QStringLiteral("{artist} {album}"),
                       QStringLiteral("{artist} {title}"), },
          QString(), });
}

QString iTunesArtSource::buildSearchUrl(const QString &base,
                                        const QString &term)
{
    return base + QStringLiteral("/search?term=")
        + QUrl::toPercentEncoding(term)
        + QStringLiteral("&media=music&limit=1");
}

QStringList iTunesArtSource::buildTerms(const MediaItem &item,
                                        const QStringList &patterns)
{
    QStringList terms;
    for (const QString &pattern : patterns) {
        const QString term = resolvePattern(item, pattern);
        if (!term.isEmpty() && !terms.contains(term))
            terms.append(term);
    }
    return terms;
}

QString iTunesArtSource::artworkUrlFromJson(const QJsonObject &response)
{
    const QJsonArray results = response.value("results").toArray();
    if (results.isEmpty())
        return {};
    QString art = results.first().toObject()
                      .value("artworkUrl100")
                      .toString();
    if (art.isEmpty())
        return {};
    // The rendition in the URL is a substitution away (the same image,
    // a larger size): 100x100bb -> 600x600bb.
    art.replace(QLatin1String("100x100bb"), QLatin1String("600x600bb"));
    return art;
}

void iTunesArtSource::setBaseUrl(const QString &base)
{
    m_base = base;
}

void iTunesArtSource::fetch(const MediaItem &item, ArtKind kind,
                            const Callback &cb)
{
    // A kind this source does not serve answers empty (Lyrics is
    // reserved), and a term set that resolves to nothing (no identity
    // fields, patterns that fit none of them) answers empty without a
    // request.
    if (kind != ArtKind::Art) {
        cb(QString(), QByteArray());
        return;
    }
    const QStringList patterns =
        SettingsManager::getInstance()->value(PatternsKey).toStringList();
    const QStringList terms = buildTerms(item, patterns);
    if (terms.isEmpty()) {
        cb(QString(), QByteArray());
        return;
    }
    searchTerm(0, terms, cb);
}

void iTunesArtSource::searchTerm(int index, const QStringList &terms,
                                 const Callback &cb)
{
    QNetworkRequest request{QUrl(buildSearchUrl(m_base, terms.at(index)))};
    QNetworkReply* reply = m_manager->get(request);
    QTimer::singleShot(ImageFetch::kTimeoutMs, reply, [reply] {
        reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [this, index, terms, cb, reply] {
        const QByteArray raw = reply->readAll();
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                .toInt();
        reply->deleteLater();
        const bool ok = reply->error() == QNetworkReply::NoError
            && status == 200 && !raw.isEmpty();
        const QString artUrl = ok
            ? artworkUrlFromJson(QJsonDocument::fromJson(raw).object())
            : QString();
        // A miss (a transport error, no results, no art in the first
        // result): the next term, the empty answer once exhausted.
        if (artUrl.isEmpty()) {
            if (index + 1 < terms.size())
                searchTerm(index + 1, terms, cb);
            else
                cb(QString(), QByteArray());
            return;
        }
        // The image goes through the shared fetcher: the byte cap, the
        // 15 s timeout and the once-per-URL-per-session RAM cache. Its
        // callback is (data, mime) — flipped back to this interface's
        // (mime, data).
        ImageFetch::getInstance()->fetch(
            QUrl(artUrl), [cb](const QByteArray &data, const QString &mime) {
            cb(mime, data);
        });
    });
}
