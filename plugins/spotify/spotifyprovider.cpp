#include "spotifyprovider.h"

#include "spotifyplayer.h"

#include <doob/imagefetch.h>

#include <QJsonArray>
#include <QUrlQuery>

namespace {
// The track/album JSON's "artists" array onto a comma-joined string.
QString artistsFromJson(const QJsonObject &obj)
{
    QStringList artists;
    for (const auto &value : obj.value("artists").toArray())
        artists.append(value.toObject().value("name").toString());
    return artists.join(QStringLiteral(", "));
}

// A track object (the /tracks/{id} response, the /tracks pages' entries)
// onto a TrackMeta.
TrackMeta trackMetaFromJson(const QJsonObject &obj)
{
    TrackMeta meta;
    meta.title = obj.value("name").toString();
    meta.artist = artistsFromJson(obj);
    meta.album =
        obj.value("album").toObject().value("name").toString();
    meta.durationMs = obj.value("duration_ms").toVariant().toLongLong();
    meta.trackNumber = obj.value("track_number").toInt();
    return meta;
}

// An album object onto a TrackMeta: the album name and its artist(s)
// (no duration — it is a context, played from the start).
TrackMeta albumMetaFromJson(const QJsonObject &obj)
{
    TrackMeta meta;
    meta.title = obj.value("name").toString();
    meta.albumArtist = artistsFromJson(obj);
    meta.year = obj.value("release_date")
                    .toString()
                    .section(QLatin1Char('-'), 0, 0)
                    .toInt();
    return meta;
}

// The image the Web API object carries: the track's album.images[0], the
// album/artist's top-level images[0] (the largest first). The CDN image
// URLs need no auth. Empty when the object has no image.
QString artUrlFromJson(const QJsonObject &obj, const QString &kind)
{
    const QJsonObject base = kind == QLatin1String("track")
        ? obj.value("album").toObject()
        : obj;
    const QJsonArray images = base.value("images").toArray();
    if (images.isEmpty())
        return {};
    return images.first().toObject().value("url").toString();
}

// The read's meta with the image it references: the image is downloaded
// on the shared image path (empty art on a failed download) and the read
// lands exactly once, on the GUI thread.
void deliverWithArt(const TrackMeta &meta, const QString &artUrl,
                    const SourceProvider::MetadataCallback &cb)
{
    if (artUrl.isEmpty()) {
        cb(meta);
        return;
    }
    ImageFetch::getInstance()->fetch(QUrl(artUrl),
        [meta, cb](const QByteArray &data, const QString &mime) {
            TrackMeta out = meta;
            if (!data.isEmpty()) {
                out.artData = data;
                out.artMime = mime;
            }
            cb(out);
        });
}
} // namespace

SpotifyProvider::SpotifyProvider(SpotifyAuth *auth, SpotifyPlayer *player,
                                 QObject *parent)
    : SourceProvider(parent)
    , m_auth(auth)
    , m_player(player)
    , m_api(new SpotifyWebApi(this))
{
}

AudioPlayer* SpotifyProvider::player() const
{
    return m_player;
}

// A claimed link's (kind, id): the spotify: scheme (spotify:track:<id>)
// and the web pages (open.spotify.com/<kind>/<id>). Anything else —
// artists, channels, search — is not importable.
void SpotifyProvider::parseLink(const QUrl &url, QString &kind, QString &id)
{
    kind.clear();
    id.clear();
    if (url.scheme() == QLatin1String("spotify")) {
        // spotify:track:3n3Ppam7vgaVa1iaRUc9Lp
        const QStringList parts =
            url.toString().mid(QStringLiteral("spotify:").size()).split(':');
        if (parts.size() == 2 && !parts.first().isEmpty()) {
            kind = parts.first();
            id = parts.last();
        }
        return;
    }
    if (url.host() == QLatin1String("open.spotify.com")
        || url.host() == QLatin1String("spotify.com")) {
        // https://open.spotify.com/track/<id>?si=...
        const QStringList segments = url.path().split('/', Qt::SkipEmptyParts);
        if (segments.size() >= 2) {
            kind = segments.first();
            id = segments.at(1);
        }
    }
}

bool SpotifyProvider::isClaimableKind(const QString &kind)
{
    // The importable shapes: a single track, an album (a collection, and
    // a playable context), a playlist (a collection), an artist (a
    // collection — the catalog of its albums and singles).
    return kind == QLatin1String("track")
        || kind == QLatin1String("album")
        || kind == QLatin1String("playlist")
        || kind == QLatin1String("artist");
}

QString SpotifyProvider::spotifyUri(const QString &kind, const QString &id)
{
    return QStringLiteral("spotify:%1:%2").arg(kind, id);
}

QString SpotifyProvider::spotifyWebUrl(const QString &kind,
                                       const QString &id)
{
    return QStringLiteral("https://open.spotify.com/%1/%2").arg(kind, id);
}

QUrl SpotifyProvider::playCommand(const MediaItem &item)
{
    QString kind;
    QString id;
    parseLink(item.source, kind, id);
    if (id.isEmpty() || !isClaimableKind(kind))
        return item.source;
    return {spotifyUri(kind, id)};
}

bool SpotifyProvider::accepts(const QUrl &url) const
{
    QString kind;
    QString id;
    parseLink(url, kind, id);
    return !id.isEmpty() && isClaimableKind(kind);
}

// The stored form of a claimed link: the open.spotify.com web link (the
// shareable form the native player copies — a portable https URL, the
// spotify: scheme would need a local Spotify app to open). The web
// link's incidental arguments (a share token (si=), a start offset) are
// dropped, and a pasted spotify: URI lands on the same link, so a
// re-paste hits the same item.
QUrl SpotifyProvider::canonicalUrl(const QUrl &url) const
{
    QString kind;
    QString id;
    parseLink(url, kind, id);
    if (id.isEmpty() || !isClaimableKind(kind))
        return url;
    return {spotifyWebUrl(kind, id)};
}

QString SpotifyProvider::fetchFailureHint() const
{
    return m_auth->state() == SpotifyAuth::State::NotSignedIn
        ? authHintText()
        : QString();
}

QString SpotifyProvider::authHintText()
{
    return QStringLiteral(
        "Sign in to Spotify first (Preferences → Plugins → Spotify).");
}

void SpotifyProvider::resolvePlayback(const MediaItem &item,
                                      const PlaybackCallback &cb)
{
    if (m_player == nullptr) {
        // A build without the driver: the item is not playable (the
        // active player answers with its own "unsupported source").
        cb(QUrl());
        return;
    }
    // The device comes up here (the private server, the helper, the
    // capture — a slow start is hidden in the resolution, so play()
    // finds a ready device). The source's spotify: URI (the Web API's
    // play-command shape) is handed back as the "stream": the driver
    // reads it as its play command. A start that fails is reported by
    // play() (its errorOccurred carries the reason — the sign-in hint
    // when there is no account).
    const QUrl command = playCommand(item);
    m_player->ensureReady([command, cb](bool ok, const QString &error) {
        Q_UNUSED(ok);
        Q_UNUSED(error);
        cb(command);
    });
}

void SpotifyProvider::fetchMetadata(const QUrl &url, MetadataCallback cb)
{
    QString kind;
    QString id;
    parseLink(url, kind, id);
    if (id.isEmpty() || !isClaimableKind(kind)
        || kind == QLatin1String("playlist")) {
        // A playlist is a collection, not a single item: it is expanded
        // by fetchList, never fetched as metadata.
        cb(TrackMeta());
        return;
    }
    m_auth->validToken([this, kind, id, cb](const SpotifyAuth::Token &token,
                                            const QString &err) {
        if (!err.isEmpty()) {
            qWarning("spotify: metadata fetch skipped: %s",
                     qPrintable(err));
            cb(TrackMeta());
            return;
        }
        m_api->setAccessToken(token.accessToken);
        if (kind == QLatin1String("track")) {
            m_api->fetchTrack(id, [cb, kind](const QJsonObject &obj,
                                       const QString &fetchError) {
                if (!fetchError.isEmpty()) {
                    qWarning("spotify: track fetch failed: %s",
                             qPrintable(fetchError));
                    cb(TrackMeta());
                    return;
                }
                deliverWithArt(trackMetaFromJson(obj),
                               artUrlFromJson(obj, kind), cb);
            });
            return;
        }
        if (kind == QLatin1String("artist")) {
            m_api->fetchArtist(id, [cb, kind](const QJsonObject &obj,
                                        const QString &fetchError) {
                if (!fetchError.isEmpty()) {
                    qWarning("spotify: artist fetch failed: %s",
                             qPrintable(fetchError));
                    cb(TrackMeta());
                    return;
                }
                TrackMeta meta;
                meta.title = obj.value("name").toString();
                deliverWithArt(meta, artUrlFromJson(obj, kind), cb);
            });
            return;
        }
        m_api->fetchAlbum(id, [cb, kind](const QJsonObject &obj,
                                   const QString &fetchError) {
            if (!fetchError.isEmpty()) {
                qWarning("spotify: album fetch failed: %s",
                         qPrintable(fetchError));
                cb(TrackMeta());
                return;
            }
            deliverWithArt(albumMetaFromJson(obj),
                           artUrlFromJson(obj, kind), cb);
        });
    });
}

void SpotifyProvider::fetchList(const QUrl &url, const ListCallback &cb)
{
    QString kind;
    QString id;
    parseLink(url, kind, id);
    if (id.isEmpty() || !isClaimableKind(kind)
        || kind == QLatin1String("track")) {
        // A track (or a link we do not claim) references no collection:
        // the wizard imports it as a plain source.
        cb(ListEntries());
        return;
    }
    // The album/playlist/artist links name a collection: a read that
    // fails answers with the reason (and isCollection), so the wizard
    // shows the link as failed instead of importing the container as a
    // single item.
    m_auth->validToken([this, kind, id, cb](const SpotifyAuth::Token &token,
                                            const QString &err) {
        if (!err.isEmpty()) {
            qWarning("spotify: list fetch skipped: %s", qPrintable(err));
            ListEntries entries;
            entries.isCollection = true;
            // The not-signed-in case carries the sign-in hint (the
            // read's own error is not actionable for the user).
            entries.error =
                m_auth->state() == SpotifyAuth::State::NotSignedIn
                ? authHintText() : err;
            cb(entries);
            return;
        }
        m_api->setAccessToken(token.accessToken);
        auto collect = [cb](const SpotifyWebApi::TrackList &list,
                            const QString &fetchError) {
            if (!fetchError.isEmpty()) {
                qWarning("spotify: list fetch failed: %s",
                         qPrintable(fetchError));
                ListEntries entries;
                entries.isCollection = true;
                entries.error = fetchError;
                cb(entries);
                return;
            }
            ListEntries entries;
            entries.title = list.title;
            // A collection even without entries (an empty playlist):
            // shown as an empty section, never imported as an item.
            entries.isCollection = true;
            for (const SpotifyWebApi::Track &track : list.tracks) {
                ListVideo video(QUrl(track.uri));
                video.title = track.name;
                video.artist = track.artist;
                // The listing carries the album name (an album's own
                // tracks, the artist's releases): it rides to the import
                // with the row, so the item's album is filled without a
                // per-track re-fetch.
                video.album = track.album;
                video.durationMs = track.durationMs;
                // The album's image (the largest): the wizard's review
                // previews it per row.
                video.thumbnail = track.thumbnail;
                entries.videos.append(video);
            }
            cb(entries);
        };
        if (kind == QLatin1String("playlist"))
            m_api->fetchPlaylistTracks(id, collect);
        else if (kind == QLatin1String("artist"))
            m_api->fetchArtistTracks(id, collect);
        else
            m_api->fetchAlbumTracks(id, collect);
    });
}
