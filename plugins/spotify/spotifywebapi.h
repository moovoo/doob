#ifndef SPOTIFYWEBAPI_H
#define SPOTIFYWEBAPI_H

#include <QList>
#include <QJsonObject>
#include <QObject>
#include <QPair>
#include <QSharedPointer>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;

// A minimal Spotify Web API client (api.spotify.com/v1) — the metadata
// the provider serves (tracks, albums, playlists), the authenticated
// user, and the playback start command (PUT /me/player/play). Every call
// invokes its callback exactly once on the GUI thread (an empty error
// string means success).
class SpotifyWebApi : public QObject
{
    Q_OBJECT
public:
    struct Track
    {
        QString id;
        QString uri; // spotify:track:<id>
        QString name;
        QString artist;
        QString album;
        qint64 durationMs = 0;
        // The album's image (the largest, the CDN's URL — no auth).
        QString thumbnail;
    };

    // A collection listing (a playlist's, an album's, or an artist's
    // tracks, in order) with its title.
    struct TrackList
    {
        QString title;
        QList<Track> tracks;
    };

    explicit SpotifyWebApi(QObject *parent = nullptr);
    void setAccessToken(const QString &accessToken);
    QString accessToken() const { return m_token; }
    // The test seam: the API's base URL (the tests point it at a local
    // fake server).
    void setBaseUrl(const QString &base) { m_base = base; }

    // The raw endpoint object, plus an error.
    using Callback = std::function<void(const QJsonObject &payload,
                                        const QString &error)>;
    using TrackListCallback =
        std::function<void(const TrackList &tracks, const QString &error)>;

    void fetchTrack(const QString &id, const Callback &cb);
    void fetchAlbum(const QString &id, const Callback &cb);
    void fetchPlaylist(const QString &id, const Callback &cb);
    void fetchArtist(const QString &id, const Callback &cb);
    // The collection's tracks (paginated, in order).
    void fetchPlaylistTracks(const QString &playlistId,
                             const TrackListCallback &cb);
    void fetchAlbumTracks(const QString &albumId, const TrackListCallback &cb);
    // The artist's own catalog: every track of every album and single,
    // in release order (the top-tracks endpoint's replacement — Spotify
    // 403s it for some accounts, and the catalog is the superset).
    void fetchArtistTracks(const QString &artistId,
                           const TrackListCallback &cb);
    // The authenticated user (GET /v1/me).
    void fetchCurrentUser(const Callback &cb);
    // Start playback on a device (PUT /v1/me/player/play): exactly one of
    // trackUri / contextUri (a track wins when both are given);
    // positionMs is the start offset (0 = the beginning). A 2xx with no
    // body (a 204) is success.
    void play(const QString &deviceId, const QString &trackUri,
              const QString &contextUri, qint64 positionMs,
              const Callback &cb);
    // The play request body (static so the tests can check it).
    static QJsonObject playBody(const QString &trackUri,
                                const QString &contextUri,
                                qint64 positionMs);
    // The playlist /items page's entry object: the "item" field, the
    // deprecated "track" duplicate as the fallback (static so the tests
    // can check it).
    static QJsonObject entryTrack(const QJsonObject &entry);

private:
    void get(const QUrl &url, const Callback &cb);
    // The rate-limit retry: a 429 (a collection listing is a burst of
    // requests, and a dev app's limit is low) is backed off — the
    // Retry-After header's seconds, clamped — and replayed at most
    // twice before the failure is reported.
    void get(const QUrl &url, const Callback &cb, int attempt);
    // The shared accumulator the paged walks append to (the list outlives
    // the request chain — a by-value parameter would be copied per hop).
    using TrackAccumulator = QSharedPointer<QList<Track>>;
    // The paginated collection pages (the album's /tracks, the
    // playlist's /items — limit=50, the documented maximum of both)
    // until "next" is gone.
    void getTracksPage(const QUrl &next, const QString &title,
                       const TrackAccumulator &tracks,
                       const TrackListCallback &cb);
    // The album's track pages, straight from the first page.
    void fetchAlbumTrackPages(const QString &albumId, const QString &title,
                              const TrackAccumulator &tracks,
                              const TrackListCallback &cb);
    // The artist's catalog walk: the album pages (the endpoint's
    // default page size), then each album's tracks — one album at a
    // time (a parallel burst would trip the rate limit). The albums ride
    // as (id, name) pairs: the name tags the album's tracks (below).
    void fetchArtistAlbumPage(const QUrl &page, const QString &title,
                              const TrackAccumulator &tracks,
                              const TrackListCallback &cb);
    void fetchArtistNextAlbum(const QList<QPair<QString, QString>> &albums,
                              int index, const QUrl &nextPage,
                              const QString &title,
                              const TrackAccumulator &tracks,
                              const TrackListCallback &cb);
    static Track trackFromJson(const QJsonObject &t);
    // The album name onto the tracks that lack one: the album's own
    // /tracks page carries reduced track objects (no "album" field —
    // only the album object itself names the album), while the
    // playlist's /items entries already carry theirs.
    static void tagAlbum(QList<Track> &tracks, const QString &album);

    QNetworkAccessManager *m_nam = nullptr;
    QString m_token;
    QString m_base = QStringLiteral("https://api.spotify.com/v1");
};

#endif // SPOTIFYWEBAPI_H
