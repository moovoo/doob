#include "spotifywebapi.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QTimer>
#include <QUrlQuery>

SpotifyWebApi::SpotifyWebApi(QObject *parent) : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
}

void SpotifyWebApi::setAccessToken(const QString &accessToken)
{
    m_token = accessToken;
}

SpotifyWebApi::Track SpotifyWebApi::trackFromJson(const QJsonObject &t)
{
    Track track;
    track.id = t.value("id").toString();
    track.uri = t.value("uri").toString();
    track.name = t.value("name").toString();
    QStringList artists;
    for (const auto &value : t.value("artists").toArray())
        artists.append(value.toObject().value("name").toString());
    track.artist = artists.join(QStringLiteral(", "));
    track.album =
        t.value("album").toObject().value("name").toString();
    track.durationMs = t.value("duration_ms").toVariant().toLongLong();
    // The album's image (the largest first — the CDN's URLs need no
    // auth; the wizard's review previews it per row).
    const QJsonArray images =
        t.value("album").toObject().value("images").toArray();
    if (!images.isEmpty())
        track.thumbnail =
            images.first().toObject().value("url").toString();
    return track;
}

QJsonObject SpotifyWebApi::entryTrack(const QJsonObject &entry)
{
    // The playlist /items page wraps the object in "item" (the
    // deprecated "track" duplicate alongside); the album /tracks page
    // carries the (flat) track object as the entry itself.
    QJsonObject track = entry.value("item").toObject();
    if (track.isEmpty())
        track = entry.value("track").toObject();
    if (track.isEmpty() && entry.contains(QStringLiteral("uri")))
        track = entry;
    return track;
}

void SpotifyWebApi::fetchTrack(const QString &id, const Callback &cb)
{
    get(QUrl(m_base + QStringLiteral("/tracks/") + id), cb);
}

void SpotifyWebApi::fetchAlbum(const QString &id, const Callback &cb)
{
    get(QUrl(m_base + QStringLiteral("/albums/") + id), cb);
}

void SpotifyWebApi::fetchPlaylist(const QString &id, const Callback &cb)
{
    get(QUrl(m_base + QStringLiteral("/playlists/") + id), cb);
}

void SpotifyWebApi::fetchArtist(const QString &id, const Callback &cb)
{
    get(QUrl(m_base + QStringLiteral("/artists/") + id), cb);
}

void SpotifyWebApi::fetchArtistTracks(const QString &artistId,
                                      const TrackListCallback &cb)
{
    // The artist's own catalog (the top-tracks endpoint's replacement —
    // Spotify 403s it for some accounts, and the catalog is the
    // superset): the artist's name (the listing's title), then every
    // track of every album and single. include_groups limits the
    // listing to the artist's own releases — compilation and
    // appears-on are other artists' records (soundtracks, featured
    // appearances) and would flood the list with tracks the artist
    // does not own. No limit parameter: the endpoint's default page
    // (the explicit maximum is 10 — a larger value is a 400 "Invalid
    // limit"), the "next" cursor walks the rest.
    get(QUrl(m_base + QStringLiteral("/artists/") + artistId),
        [this, artistId, cb](const QJsonObject &obj, const QString &err) {
            if (!err.isEmpty()) {
                cb(TrackList(), err);
                return;
            }
            fetchArtistAlbumPage(
                QUrl(m_base + QStringLiteral("/artists/") + artistId
                     + QStringLiteral("/albums?include_groups=album,single")),
                obj.value("name").toString(),
                TrackAccumulator(new QList<Track>()), cb);
        });
}

void SpotifyWebApi::fetchArtistAlbumPage(const QUrl &page, const QString &title,
                                         const TrackAccumulator &tracks,
                                         const TrackListCallback &cb)
{
    get(page, [this, title, tracks, cb](const QJsonObject &obj,
                                        const QString &err) {
        if (!err.isEmpty()) {
            cb(TrackList(), err);
            return;
        }
        // (id, name) pairs: the name tags the album's tracks (the
        // /tracks page's reduced objects do not carry the album).
        QList<QPair<QString, QString>> albums;
        for (const auto &value : obj.value("items").toArray()) {
            const QJsonObject album = value.toObject();
            albums.append({album.value("id").toString(),
                           album.value("name").toString()});
        }
        fetchArtistNextAlbum(albums, 0, obj.value("next").toVariant().toUrl(),
                             title, tracks, cb);
    });
}

void SpotifyWebApi::fetchArtistNextAlbum(
    const QList<QPair<QString, QString>> &albums, int index,
    const QUrl &nextPage, const QString &title,
    const TrackAccumulator &tracks, const TrackListCallback &cb)
{
    if (index >= albums.size()) {
        if (!nextPage.isEmpty()) {
            fetchArtistAlbumPage(nextPage, title, tracks, cb);
            return;
        }
        // A track can sit in several of the artist's releases (a single
        // re-issued on an album, a deluxe edition): one row per track.
        QList<Track> unique;
        QSet<QString> seen;
        for (const Track &track : *tracks)
            if (!seen.contains(track.id)) {
                seen.insert(track.id);
                unique.append(track);
            }
        cb(TrackList{title, unique}, QString());
        return;
    }
    const QString albumId = albums.at(index).first;
    const QString albumName = albums.at(index).second;
    // The pages land in the shared accumulator (the callback's list IS
    // it): tag the album's own tracks in place (the earlier albums'
    // tracks already carry their names) and move on — no copy, no
    // re-append (the final dedup drops the release duplicates).
    fetchAlbumTrackPages(albumId, title, tracks,
                         [this, albums, index, nextPage, title, tracks, cb,
                          albumName](const TrackList &,
                                     const QString &err) {
                             if (!err.isEmpty()) {
                                 cb(TrackList(), err);
                                 return;
                             }
                             tagAlbum(*tracks, albumName);
                             fetchArtistNextAlbum(albums, index + 1, nextPage,
                                                  title, tracks, cb);
                         });
}

void SpotifyWebApi::fetchPlaylistTracks(const QString &playlistId,
                                        const TrackListCallback &cb)
{
    // The playlist's items: GET /playlists/{id}/items (the /tracks
    // listing is deprecated), paged at limit=50.
    get(QUrl(m_base + QStringLiteral("/playlists/") + playlistId),
        [this, playlistId, cb](const QJsonObject &obj, const QString &err) {
            if (!err.isEmpty()) {
                cb(TrackList(), err);
                return;
            }
            getTracksPage(QUrl(m_base + QStringLiteral("/playlists/")
                               + playlistId
                               + QStringLiteral("/items?limit=50")),
                          obj.value("name").toString(),
                          TrackAccumulator(new QList<Track>()), cb);
        });
}

void SpotifyWebApi::fetchAlbumTrackPages(const QString &albumId,
                                         const QString &title,
                                         const TrackAccumulator &tracks,
                                         const TrackListCallback &cb)
{
    // The album's tracks: GET /albums/{id}/tracks, paged at limit=50
    // (the documented maximum — a larger value is a 400 "Invalid
    // limit").
    getTracksPage(QUrl(m_base + QStringLiteral("/albums/")
                       + albumId + QStringLiteral("/tracks?limit=50")),
                  title, tracks, cb);
}

void SpotifyWebApi::fetchAlbumTracks(const QString &albumId,
                                     const TrackListCallback &cb)
{
    // The album's name (the listing's title), then the track pages —
    // tagged with it: the /tracks page's reduced track objects do not
    // carry the album themselves (only the album object names it).
    get(QUrl(m_base + QStringLiteral("/albums/") + albumId),
        [this, albumId, cb](const QJsonObject &obj, const QString &err) {
            if (!err.isEmpty()) {
                cb(TrackList(), err);
                return;
            }
            const QString name = obj.value("name").toString();
            fetchAlbumTrackPages(
                albumId, name, TrackAccumulator(new QList<Track>()),
                [name, cb](const TrackList &list, const QString &err) {
                    if (!err.isEmpty()) {
                        cb(TrackList(), err);
                        return;
                    }
                    QList<Track> tracks = list.tracks;
                    tagAlbum(tracks, name);
                    cb(TrackList{list.title, tracks}, QString());
                });
        });
}

void SpotifyWebApi::tagAlbum(QList<Track> &tracks, const QString &album)
{
    if (album.isEmpty())
        return;
    for (Track &track : tracks)
        if (track.album.isEmpty())
            track.album = album;
}

void SpotifyWebApi::getTracksPage(const QUrl &next, const QString &title,
                                  const TrackAccumulator &tracks,
                                  const TrackListCallback &cb)
{
    get(next, [this, title, tracks, cb](const QJsonObject &obj,
                                        const QString &err) {
        if (!err.isEmpty()) {
            cb(TrackList(), err);
            return;
        }
        for (const auto &value : obj.value("items").toArray()) {
            const QJsonObject track = entryTrack(value.toObject());
            if (!track.isEmpty())
                tracks->append(trackFromJson(track));
        }
        const QUrl nextUrl = obj.value("next").toVariant().toUrl();
        if (!nextUrl.isEmpty()) {
            getTracksPage(nextUrl, title, tracks, cb);
            return;
        }
        cb(TrackList{title, *tracks}, QString());
    });
}

void SpotifyWebApi::fetchCurrentUser(const Callback &cb)
{
    get(QUrl(m_base + QStringLiteral("/me")), cb);
}

QJsonObject SpotifyWebApi::playBody(const QString &trackUri,
                                    const QString &contextUri,
                                    qint64 positionMs)
{
    QJsonObject body;
    if (!trackUri.isEmpty()) {
        QJsonArray uris;
        uris.append(trackUri);
        body[QStringLiteral("uris")] = uris;
    } else if (!contextUri.isEmpty()) {
        body[QStringLiteral("context_uri")] = contextUri;
    }
    if (positionMs > 0)
        body[QStringLiteral("position_ms")] = positionMs;
    return body;
}

void SpotifyWebApi::play(const QString &deviceId, const QString &trackUri,
                         const QString &contextUri, qint64 positionMs,
                         const Callback &cb)
{
    QUrl url(m_base + QStringLiteral("/me/player/play"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("device_id"), deviceId);
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      "application/json");
    request.setRawHeader("Authorization",
                         QByteArrayLiteral("Bearer ") + m_token.toUtf8());
    const QByteArray payload = QJsonDocument(
                                   playBody(trackUri, contextUri, positionMs))
                                   .toJson(QJsonDocument::Compact);
    QNetworkReply *reply = m_nam->put(request, payload);
    connect(reply, &QNetworkReply::finished, this, [cb, reply] {
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray raw = reply->readAll();
        const QJsonObject obj = QJsonDocument::fromJson(raw).object();
        reply->deleteLater();

        if (status >= 200 && status < 300) {
            cb(obj, QString());
            return;
        }
        if (status == 0) {
            cb(QJsonObject(), QStringLiteral("request failed: %1")
                       .arg(reply->errorString()));
            return;
        }
        QString message = QStringLiteral("HTTP %1").arg(status);
        if (obj.contains(QStringLiteral("error")))
            message += QStringLiteral(": ")
                           + obj.value("error").toObject()
                                 .value("message").toString();
        cb(QJsonObject(), message);
    });
}

void SpotifyWebApi::get(const QUrl &url, const Callback &cb)
{
    get(url, cb, 0);
}

void SpotifyWebApi::get(const QUrl &url, const Callback &cb, int attempt)
{
    QNetworkRequest request(url);
    request.setRawHeader("Authorization",
                         QByteArrayLiteral("Bearer ") + m_token.toUtf8());
    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, url, cb, reply, attempt] {
                const int status =
                    reply->attribute(
                        QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QByteArray raw = reply->readAll();
                const QJsonObject obj = QJsonDocument::fromJson(raw).object();
                // Rate limited (the burst of a collection listing):
                // back off — the header's seconds, clamped — and
                // replay, a couple of times at most.
                if (status == 429 && attempt < 2) {
                    const int headerMs =
                        reply->rawHeader("Retry-After").toInt() * 1000;
                    const int waitMs = qBound(
                        1000, headerMs > 0 ? headerMs : 2000 * (attempt + 1),
                        15000);
                    reply->deleteLater();
                    QTimer::singleShot(waitMs, this,
                                       [this, url, cb, attempt] {
                                           get(url, cb, attempt + 1);
                                       });
                    return;
                }
                reply->deleteLater();

                if (status >= 200 && status < 300 && !obj.isEmpty()) {
                    cb(obj, QString());
                    return;
                }
                if (status == 0) {
                    cb(QJsonObject(),
                       QStringLiteral("request failed: %1")
                           .arg(reply->errorString()));
                    return;
                }
                QString message = QStringLiteral("HTTP %1").arg(status);
                if (obj.contains(QStringLiteral("error")))
                    message += QStringLiteral(": ")
                                   + obj.value("error").toObject()
                                         .value("message").toString();
                cb(QJsonObject(), message);
            });
}
