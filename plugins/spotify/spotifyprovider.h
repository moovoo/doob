#ifndef SPOTIFYPROVIDER_H
#define SPOTIFYPROVIDER_H

#include <doob/sourceprovider.h>

#include "spotifyauth.h"
#include "spotifywebapi.h"

class SpotifyPlayer;

// A source provider that claims Spotify links: the spotify:track:/
// spotify:album:/spotify:playlist:/spotify:artist: URIs and the
// open.spotify.com / spotify.com track, album, playlist and artist
// pages.
//
// The stored (canonical) form of a claimed link is its
// open.spotify.com web link (canonicalUrl) — the shareable form the
// native player copies, a portable https URL — so a re-pasted link with
// a share token (si=) hits the same item. The play command is the
// link's spotify: URI (playCommand — the Web API's shape). Metadata and
// collections go through the Web API (an
// authenticated fetch — not signed in, a metadata fetch answers empty
// and the import degrades to a skipped source, the skip's reason is the
// fetchFailureHint() sign-in hint; a collection fetch answers with the
// failure in ListEntries::error, so the import wizard shows the link as
// failed instead of importing the container as a single item).
//
// Playback is the plugin's own driver (the SpotifyPlayer, plugin-owned
// and handed to the PlaybackManager by player()): resolvePlayback()
// brings the device up (the private server, the spotify-web helper, the
// capture) and hands back the stored source's spotify: URI — the driver
// reads it as its play command, there is no stream to fetch.
class SpotifyProvider : public SourceProvider
{
public:
    SpotifyProvider(SpotifyAuth *auth, SpotifyPlayer *player,
                    QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("spotify"); }
    QString title() const override { return QStringLiteral("Spotify"); }
    bool accepts(const QUrl &url) const override;
    QUrl canonicalUrl(const QUrl &url) const override;
    void fetchMetadata(const QUrl &url, MetadataCallback cb) override;
    void fetchList(const QUrl &url, const ListCallback &cb) override;

    // The plugin's driver (the PlaybackManager switches it in for
    // spotify items); nullptr in a build without the driver.
    AudioPlayer* player() const override;
    // The driver's device comes up during the resolution, so play() can
    // send its command at once.
    void resolvePlayback(const MediaItem &item,
                         const PlaybackCallback &cb) override;

    SpotifyAuth* auth() const { return m_auth; }
    SpotifyWebApi* api() const { return m_api; }

    // The import's skip reason (the core shows it when a fetch is
    // skipped): the sign-in hint while there is no account, empty
    // otherwise.
    QString fetchFailureHint() const override;
    // The hint's text (static, so the driver's error messages carry the
    // same words).
    static QString authHintText();

    // Link parsing (static, so the tests and the UI both reach them): a
    // spotify: URI or an open.spotify.com link → (kind, id); both empty
    // when the link is not recognized.
    static void parseLink(const QUrl &url, QString &kind, QString &id);
    static QString spotifyUri(const QString &kind, const QString &id);
    // The open.spotify.com web link of a (kind, id) — the stored
    // (canonical) form, the shareable form the native player copies.
    static QString spotifyWebUrl(const QString &kind, const QString &id);
    // The driver's play command for a stored source: its spotify: URI
    // (the Web API's shape) — the stored form is the web link, a legacy
    // spotify: source passes through.
    static QUrl playCommand(const MediaItem &item);
    // The claimable kinds (the importable shapes).
    static bool isClaimableKind(const QString &kind);

private:
    SpotifyAuth* m_auth;
    SpotifyPlayer* m_player;
    SpotifyWebApi* m_api;
};

#endif // SPOTIFYPROVIDER_H
