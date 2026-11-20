#include <QtTest>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxyFactory>
#include <QStandardPaths>
#include <QTemporaryDir>

// The core headers go through the <doob/...> prefix (the doob_lib
// target's include layout), so the test builds both in-tree and
// standalone against the installed API.
#include <doob/mediaitem.h>
#include <doob/pluginmanager.h>
#include <doob/settings.h>
#include <doob/settingsmanager.h>
#include <doob/settingsregistry.h>
#include <doob/sourcemanager.h>
#include <doob/trackmetadata.h>

// The plugin's pure sources are compiled straight in (the same way
// tests/plugins/ compiles the fake plugin and the ffmpeg plugin's tests
// compile theirs) — the unit tests reach the classes directly, while
// the plugin-level tests load the built .so (its own copies of the
// classes) through the PluginManager and use it only through the core
// interfaces.
#include "spotifyauth.h"
#include "spotifyprovider.h"
#include "spotifywebapi.h"

#include "testhttpserver.h"

namespace {

// Reaches the token store (the login/refresh flows' common path) to
// check where the persistence lands.
class TestAuth : public SpotifyAuth
{
public:
    using SpotifyAuth::setToken;
};

class SpotifyTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Unit tests (the directly compiled sources).
    void claimsSpotifyUrls();
    void canonicalizesLinks();
    void pkceBuildingBlocks();
    void tokenRoundTrip();
    void tokenFileRoundTrip();
    void playBodyShapes();
    void playlistEntryTrackField();
    void artistCatalogWalk();
    void artistCatalogFetchError();
    void albumFetchListCarriesAlbum();
    void trackFetchMetadataCarriesArt();
    void albumFetchMetadataCarriesArt();
    void artistFetchMetadataCarriesArt();
    void fetchMetadataWithoutImageAnswersWithoutArt();
    void authStateTransitions();
    void setTokenPersistsToTokenFile();
    void validTokenBehaviors();
    void refreshAheadKeepsTheTokenCurrent();

    // Plugin-level tests (the built .so, through the core interfaces).
    void settingsRegistered();
    void notSignedInFetchIsSkipped();
    void storedTokenDefersFetch();
    void clientIdChangeSignsOut();

private:
    // A stored (not expired) fake token in the plugin's token file.
    void writeFakeTokenFile();
    void removeTokenFile();
    QString tokenFile() const;
    SpotifyAuth::Token fakeToken() const;

    // The dev build drops the plugins/ .so files in <app dir>/plugins
    // (one of the search paths), next to the test executable.
    QString pluginDir() const
    {
        return QCoreApplication::applicationDirPath() + "/plugins";
    }
    bool pluginBuilt() const
    {
        return !QDir(pluginDir())
                    .entryList({"libspotify.so"}, QDir::Files)
                    .isEmpty();
    }
    // Loads the plugin from the dev build's directory and looks up its
    // provider (nullptr when registration did not happen — the caller
    // QFAILs, the QSKIP-able preconditions stay with the caller).
    SourceProvider* spotifyProvider();
};

void SpotifyTest::initTestCase()
{
    // The API client's tests reach a local fake server: no system proxy
    // in between (a dead proxy would break the loopback requests).
    QNetworkProxyFactory::setUseSystemConfiguration(false);
    // The plugin's initialize() registers its settings (needs a data
    // dir for the central settings.json) and the SpotifyAuth ctor
    // connects to the SettingsManager.
    qputenv("DOOB_DATA_DIR", "/tmp/doob_spotify_test");
    Settings::reset();
    SettingsManager* settings = SettingsManager::getInstance();
    settings->setSettingsPath("/tmp/doob_spotify_test/settings.json");
    settings->load();
}

void SpotifyTest::cleanupTestCase()
{
    qunsetenv("DOOB_DATA_DIR");
    Settings::reset();
    SettingsManager::reset();
}

void SpotifyTest::init()
{
    PluginManager::getInstance()->shutdownPlugins();
    removeTokenFile();
}

void SpotifyTest::cleanup()
{
    PluginManager::getInstance()->shutdownPlugins();
}

SourceProvider* SpotifyTest::spotifyProvider()
{
    PluginManager::getInstance()->loadFromDir(pluginDir());
    return SourceManager::getInstance()->providerById("spotify");
}

QString SpotifyTest::tokenFile() const
{
    return Settings::instance()->pluginDataPath("spotify")
        + "/spotify-token.json";
}

SpotifyAuth::Token SpotifyTest::fakeToken() const
{
    SpotifyAuth::Token token;
    token.accessToken = QStringLiteral("fake-access");
    token.refreshToken = QStringLiteral("fake-refresh");
    token.expiresAt = QDateTime::currentDateTimeUtc().addSecs(3600);
    token.scopes = QStringLiteral("streaming");
    return token;
}

void SpotifyTest::writeFakeTokenFile()
{
    QDir().mkpath(QFileInfo(tokenFile()).path());
    QVERIFY(SpotifyAuth::saveToken(tokenFile(), fakeToken()));
}

void SpotifyTest::removeTokenFile()
{
    QFile::remove(tokenFile());
}

void SpotifyTest::claimsSpotifyUrls()
{
    SpotifyAuth auth;
    SpotifyProvider provider(&auth, nullptr);

    // The claimable shapes: track, album, playlist, artist (the
    // spotify: URIs and the web pages, a share token along).
    QVERIFY(provider.accepts(QUrl("spotify:track:3n3Ppam7vgaVa1iaRUc9Lp")));
    QVERIFY(provider.accepts(QUrl("spotify:album:5xiM9LfGaNPItwFkCDGbfd")));
    QVERIFY(provider.accepts(QUrl("spotify:playlist:37i9dQZF1DXcBWIGoYBM5M")));
    QVERIFY(provider.accepts(QUrl("spotify:artist:0GH357WTRe3Qg7CDBDsp7d")));
    QVERIFY(provider.accepts(
        QUrl("https://open.spotify.com/track/3n3Ppam7vgaVa1iaRUc9Lp?si=abc")));
    QVERIFY(provider.accepts(QUrl("https://spotify.com/album/5xM9LfGaNPItwFkCDGbfd")));
    QVERIFY(provider.accepts(
        QUrl("https://open.spotify.com/artist/0GH357WTRe3Qg7CDBDsp7d?si=xyz")));
    // Search pages and foreign hosts are not importable.
    QVERIFY(!provider.accepts(
        QUrl("https://open.spotify.com/search/never%20gonna")));
    QVERIFY(!provider.accepts(QUrl("https://example.com/track/abc")));
}

void SpotifyTest::canonicalizesLinks()
{
    SpotifyAuth auth;
    SpotifyProvider provider(&auth, nullptr);

    // The stored form is the open.spotify.com web link (the shareable
    // form the native player copies): the incidental arguments (a share
    // token, a start offset) are dropped, and a pasted spotify: URI
    // lands on the same link — a re-paste hits the same item.
    QCOMPARE(provider.canonicalUrl(
                 QUrl("https://open.spotify.com/track/3n3Ppam7vgaVa1iaRUc9Lp?si=abc&t=42")),
             QUrl("https://open.spotify.com/track/3n3Ppam7vgaVa1iaRUc9Lp"));
    QCOMPARE(provider.canonicalUrl(
                 QUrl("https://spotify.com/album/5xM9LfGaNPItwFkCDGbfd")),
             QUrl("https://open.spotify.com/album/5xM9LfGaNPItwFkCDGbfd"));
    QCOMPARE(provider.canonicalUrl(QUrl("spotify:playlist:37i9dQZF1DXcBWIGoYBM5M")),
             QUrl("https://open.spotify.com/playlist/37i9dQZF1DXcBWIGoYBM5M"));
    QCOMPARE(provider.canonicalUrl(
                 QUrl("https://open.spotify.com/artist/0GH357WTRe3Qg7CDBDsp7d?si=xyz")),
             QUrl("https://open.spotify.com/artist/0GH357WTRe3Qg7CDBDsp7d"));
    QCOMPARE(provider.canonicalUrl(QUrl("spotify:track:3n3Ppam7vgaVa1iaRUc9Lp")),
             QUrl("https://open.spotify.com/track/3n3Ppam7vgaVa1iaRUc9Lp"));
    // A link the provider does not claim is canonicalized as pasted.
    QCOMPARE(provider.canonicalUrl(
                 QUrl("https://open.spotify.com/search/never%20gonna")),
             QUrl("https://open.spotify.com/search/never%20gonna"));

    // The play command: the stored web link's spotify: URI (the Web
    // API's shape) — a legacy spotify: source passes through.
    MediaItem item;
    item.source =
        QUrl("https://open.spotify.com/track/3n3Ppam7vgaVa1iaRUc9Lp");
    QCOMPARE(SpotifyProvider::playCommand(item),
             QUrl("spotify:track:3n3Ppam7vgaVa1iaRUc9Lp"));
    MediaItem legacy;
    legacy.source = QUrl("spotify:album:5xM9LfGaNPItwFkCDGbfd");
    QCOMPARE(SpotifyProvider::playCommand(legacy),
             QUrl("spotify:album:5xM9LfGaNPItwFkCDGbfd"));
}

void SpotifyTest::pkceBuildingBlocks()
{
    // base64url without padding (the RFC 7636 encoding — a padded or
    // standard-alphabet challenge breaks the token exchange).
    QCOMPARE(SpotifyAuth::b64url(QByteArrayLiteral("hello")),
             QByteArrayLiteral("aGVsbG8"));
    // The S256 challenge of the RFC 7636 appendix verifier — the value
    // independently computed (base64url(SHA256(verifier)), unpadded).
    QCOMPARE(SpotifyAuth::pkceChallenge(
                 QByteArrayLiteral("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXw")),
             QByteArrayLiteral("LjpMKJRsJ-95DtvIeU9GCxhTWmmqrdNTCHRavuE4Z7U"));
    // The random verifier is base64url (no padding characters) and the
    // requested length pre-encoding.
    const QByteArray verifier = SpotifyAuth::randomB64Url(32);
    QCOMPARE(verifier.size(), 43); // ceil(32 * 4 / 3), unpadded
    QVERIFY(!verifier.contains('=') && !verifier.contains('+')
            && !verifier.contains('/'));
}

void SpotifyTest::tokenRoundTrip()
{
    // The accounts.spotify.com response shape ("expires_in" seconds,
    // relative to now).
    QJsonObject api;
    api[QStringLiteral("access_token")] = QStringLiteral("a1");
    api[QStringLiteral("refresh_token")] = QStringLiteral("r1");
    api[QStringLiteral("expires_in")] = 3600;
    const SpotifyAuth::Token fromApi = SpotifyAuth::Token::fromApi(api);
    QVERIFY(fromApi.valid());
    QVERIFY(fromApi.expiresAt >
            QDateTime::currentDateTimeUtc().addSecs(3500));

    // A refresh response carries no refresh token: the stored one rides
    // on (the saved file never loses it), a rotated one wins.
    QJsonObject refresh;
    refresh[QStringLiteral("access_token")] = QStringLiteral("a2");
    refresh[QStringLiteral("expires_in")] = 3600;
    const SpotifyAuth::Token refreshed = SpotifyAuth::Token::withStoredRefresh(
        SpotifyAuth::Token::fromApi(refresh), QStringLiteral("r1"));
    QCOMPARE(refreshed.accessToken, QStringLiteral("a2"));
    QCOMPARE(refreshed.refreshToken, QStringLiteral("r1"));
    SpotifyAuth::Token rotatedToken;
    rotatedToken.accessToken = QStringLiteral("a3");
    rotatedToken.refreshToken = QStringLiteral("r2");
    rotatedToken.expiresAt = QDateTime::currentDateTimeUtc().addSecs(3600);
    const SpotifyAuth::Token rotated =
        SpotifyAuth::Token::withStoredRefresh(rotatedToken,
                                              QStringLiteral("r1"));
    QCOMPARE(rotated.refreshToken, QStringLiteral("r2"));
}

void SpotifyTest::tokenFileRoundTrip()
{
    // The token file (the "expires_at" epoch-ms shape).
    QJsonObject api;
    api[QStringLiteral("access_token")] = QStringLiteral("a1");
    api[QStringLiteral("refresh_token")] = QStringLiteral("r1");
    api[QStringLiteral("expires_in")] = 3600;
    const SpotifyAuth::Token fromApi = SpotifyAuth::Token::fromApi(api);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/token.json";
    QVERIFY(SpotifyAuth::saveToken(path, fromApi));
    const SpotifyAuth::Token loaded = SpotifyAuth::loadToken(path);
    QCOMPARE(loaded.accessToken, QStringLiteral("a1"));
    QCOMPARE(loaded.refreshToken, QStringLiteral("r1"));
    QCOMPARE(loaded.expiresAt, fromApi.expiresAt);
    // A missing file loads as an empty token.
    QVERIFY(!SpotifyAuth::loadToken(dir.path() + "/absent.json").hasAccess());
}

void SpotifyTest::playBodyShapes()
{
    // A track (exactly what plays) wins over a context.
    const QJsonObject track =
        SpotifyWebApi::playBody(QStringLiteral("spotify:track:abc"),
                                QStringLiteral("spotify:playlist:xyz"), 0);
    QCOMPARE(track.value("uris").toArray().first().toString(),
             QStringLiteral("spotify:track:abc"));
    QVERIFY(!track.contains("context_uri"));
    QVERIFY(!track.contains("position_ms"));
    // A context (a playlist/album, played from the start).
    const QJsonObject context =
        SpotifyWebApi::playBody(QString(), QStringLiteral("spotify:playlist:xyz"),
                                0);
    QCOMPARE(context.value("context_uri").toString(),
             QStringLiteral("spotify:playlist:xyz"));
    // A start offset.
    const QJsonObject positioned =
        SpotifyWebApi::playBody(QStringLiteral("spotify:track:abc"), QString(),
                                30000);
    QCOMPARE(positioned.value("position_ms").toVariant().toLongLong(),
             static_cast<qint64>(30000));
}

void SpotifyTest::playlistEntryTrackField()
{
    // The playlist /items page's entry: the object sits in "item" (the
    // deprecated "track" duplicate alongside).
    QJsonObject track;
    track[QStringLiteral("id")] = QStringLiteral("t1");
    track[QStringLiteral("name")] = QStringLiteral("A Track");
    QJsonObject entry;
    entry[QStringLiteral("item")] = track;
    QCOMPARE(SpotifyWebApi::entryTrack(entry).value("id").toString(),
             QStringLiteral("t1"));
    // The deprecated duplicate (an older response shape): the fallback.
    QJsonObject legacy;
    legacy[QStringLiteral("track")] = track;
    QCOMPARE(SpotifyWebApi::entryTrack(legacy).value("id").toString(),
             QStringLiteral("t1"));
    // The album /tracks page's entry: the (flat) track object itself
    // (no wrapper).
    QJsonObject flat;
    flat[QStringLiteral("id")] = QStringLiteral("t2");
    flat[QStringLiteral("name")] = QStringLiteral("B Track");
    flat[QStringLiteral("uri")] = QStringLiteral("spotify:track:t2");
    QCOMPARE(SpotifyWebApi::entryTrack(flat).value("id").toString(),
             QStringLiteral("t2"));
    // An entry with no wrapper and no track shape: an empty object.
    QVERIFY(SpotifyWebApi::entryTrack(QJsonObject()).isEmpty());
}

void SpotifyTest::artistCatalogWalk()
{
    MiniHttpServer server;
    const QString base = server.baseUrl();
    // A flat track object (the album /tracks page's entry shape — the
    // API's reduced object: no "album" field, the name rides on the
    // album page's entry).
    const auto track = [](const char *id, const char *name) {
        return QStringLiteral(
                       R"({"id":"%1","uri":"spotify:track:%1","name":"%2","artists":[{"name":"A"}],"duration_ms":1000})")
            .arg(id, name);
    };
    server.route(QStringLiteral("/artists/a1"), 200,
                 QByteArrayLiteral(R"({"name":"An Artist"})"));
    // Two album pages (the artist's own releases only).
    server.route(
        QStringLiteral(
            "/artists/a1/albums?include_groups=album,single"), 200,
        QStringLiteral(
            R"({"items":[{"id":"al1","name":"Release One"},{"id":"al2","name":"Release Two"}],"next":"%1/albumpage2"})")
            .arg(base)
            .toUtf8());
    server.route(
        QStringLiteral("/albumpage2"), 200,
        QByteArrayLiteral(
            R"({"items":[{"id":"al3","name":"Release Three"}]})"));
    // The albums' track pages — t2 sits in two releases and must come
    // back once.
    server.route(
        QStringLiteral("/albums/al1/tracks?limit=50"), 200,
        QStringLiteral(R"({"items":[%1,%2],"next":"%3/al1page2"})")
            .arg(track("t1", "One"), track("t2", "Two"), base)
            .toUtf8());
    server.route(QStringLiteral("/al1page2"), 200,
                 QStringLiteral(R"({"items":[%1]})")
                     .arg(track("t3", "Three"))
                     .toUtf8());
    server.route(QStringLiteral("/albums/al2/tracks?limit=50"), 200,
                 QStringLiteral(R"({"items":[%1,%2]})")
                     .arg(track("t2", "Two"), track("t4", "Four"))
                     .toUtf8());
    server.route(QStringLiteral("/albums/al3/tracks?limit=50"), 200,
                 QStringLiteral(R"({"items":[%1]})")
                     .arg(track("t5", "Five"))
                     .toUtf8());

    SpotifyWebApi api;
    api.setBaseUrl(base);
    SpotifyWebApi::TrackList list;
    QString error;
    bool done = false;
    api.fetchArtistTracks(
        QStringLiteral("a1"),
        [&](const SpotifyWebApi::TrackList &l, const QString &err) {
            list = l;
            error = err;
            done = true;
        });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QVERIFY(error.isEmpty());
    QCOMPARE(list.title, QStringLiteral("An Artist"));
    QCOMPARE(list.tracks.size(), 5);
    // Release order (the album pages' order, each album's tracks in
    // order), the duplicate dropped.
    QStringList ids;
    for (const SpotifyWebApi::Track &t : list.tracks)
        ids.append(t.id);
    QCOMPARE(ids, QStringList({"t1", "t2", "t3", "t4", "t5"}));
    // The album name (the album page's entry) tags each track (the
    // wizard's import uses it as the item's album — no per-track
    // re-fetch).
    QCOMPARE(list.tracks.at(0).album, QStringLiteral("Release One"));
    QCOMPARE(list.tracks.at(2).album, QStringLiteral("Release One"));
    QCOMPARE(list.tracks.at(3).album, QStringLiteral("Release Two"));
    QCOMPARE(list.tracks.at(4).album, QStringLiteral("Release Three"));
    // The catalog is the artist's own releases only (no compilations,
    // no appears-on).
    QVERIFY(server.requestPaths().contains(
        QStringLiteral("/artists/a1/albums?include_groups=album,single")));
}

void SpotifyTest::artistCatalogFetchError()
{
    MiniHttpServer server;
    const QString base = server.baseUrl();
    server.route(QStringLiteral("/artists/a2"), 200,
                 QByteArrayLiteral(R"({"name":"Sad Artist"})"));
    server.route(
        QStringLiteral("/artists/a2/albums?include_groups=album,single"), 200,
        QByteArrayLiteral(R"({"items":[{"id":"al9"}]})"));
    server.route(QStringLiteral("/albums/al9/tracks?limit=50"), 403,
                 QByteArrayLiteral(
                     R"({"error":{"status":403,"message":"Forbidden"}})"));

    SpotifyWebApi api;
    api.setBaseUrl(base);
    SpotifyWebApi::TrackList list;
    QString error;
    bool done = false;
    api.fetchArtistTracks(
        QStringLiteral("a2"),
        [&](const SpotifyWebApi::TrackList &l, const QString &err) {
            list = l;
            error = err;
            done = true;
        });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QCOMPARE(error, QStringLiteral("HTTP 403: Forbidden"));
    QVERIFY(list.tracks.isEmpty());
}

void SpotifyTest::albumFetchListCarriesAlbum()
{
    // The album expansion (the wizard's review rows): the album name
    // must ride with each row — the import uses the provided metadata
    // as-is (no per-track re-fetch), so a name dropped here never
    // reaches the item. The /tracks page's reduced objects do not
    // carry the album (only the album object does): the name is tagged
    // from the collection.
    MiniHttpServer server;
    const QString base = server.baseUrl();
    server.route(QStringLiteral("/albums/al1"), 200,
                 QByteArrayLiteral(R"({"name":"An Album"})"));
    server.route(
        QStringLiteral("/albums/al1/tracks?limit=50"), 200,
        QStringLiteral(
            R"({"items":[{"id":"t1","uri":"spotify:track:t1","name":"One","artists":[{"name":"A"}],"duration_ms":2000},{"id":"t2","uri":"spotify:track:t2","name":"Two","artists":[{"name":"B"}],"duration_ms":3000}]})")
            .toUtf8());

    TestAuth auth;
    auth.setToken(fakeToken());
    SpotifyProvider provider(&auth, nullptr, this);
    provider.api()->setBaseUrl(base);

    SourceProvider::ListEntries entries;
    bool done = false;
    provider.fetchList(
        QUrl("spotify:album:al1"),
        [&entries, &done](const SourceProvider::ListEntries &e) {
            entries = e;
            done = true;
        });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QVERIFY(entries.error.isEmpty());
    QVERIFY(entries.isCollection);
    QCOMPARE(entries.title, QStringLiteral("An Album"));
    QCOMPARE(entries.videos.size(), 2);
    QCOMPARE(entries.videos.at(0).title, QStringLiteral("One"));
    QCOMPARE(entries.videos.at(0).artist, QStringLiteral("A"));
    QCOMPARE(entries.videos.at(0).album, QStringLiteral("An Album"));
    QCOMPARE(entries.videos.at(0).durationMs, 2000);
    QCOMPARE(entries.videos.at(1).title, QStringLiteral("Two"));
    QCOMPARE(entries.videos.at(1).album, QStringLiteral("An Album"));
    QCOMPARE(entries.videos.at(1).durationMs, 3000);
}

void SpotifyTest::trackFetchMetadataCarriesArt()
{
    // The track read's art: the album's images[0] (the largest first),
    // downloaded on the shared image path (a CDN URL needs no auth).
    MiniHttpServer server;
    const QString base = server.baseUrl();
    server.route(
        "/tracks/t1", 200,
        QStringLiteral(
            R"({"id":"t1","name":"One","artists":[{"name":"A"}],"duration_ms":2000,"album":{"name":"An Album","images":[{"url":"%1/img.jpg","width":640},{"url":"%1/img-small.jpg","width":64}]}})")
            .arg(base)
            .toUtf8());
    server.route("/img.jpg", 200, QByteArray("cover-bytes"),
                 QStringLiteral("image/jpeg"));

    TestAuth auth;
    auth.setToken(fakeToken());
    SpotifyProvider provider(&auth, nullptr, this);
    provider.api()->setBaseUrl(base);

    TrackMeta meta;
    bool done = false;
    provider.fetchMetadata(QUrl("spotify:track:t1"),
                           [&meta, &done](const TrackMeta &m) {
            meta = m;
            done = true;
        });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);

    QCOMPARE(meta.title, QStringLiteral("One"));
    QCOMPARE(meta.artMime, QStringLiteral("image/jpeg"));
    QCOMPARE(meta.artData, QByteArray("cover-bytes"));
    // The largest image won, not the small one.
    QCOMPARE(server.requestPaths().count("/img.jpg"), 1);
    QCOMPARE(server.requestPaths().count("/img-small.jpg"), 0);
}

void SpotifyTest::albumFetchMetadataCarriesArt()
{
    // The album read's art: the object's own images[0].
    MiniHttpServer server;
    const QString base = server.baseUrl();
    server.route(
        "/albums/al1", 200,
        QStringLiteral(
            R"({"name":"An Album","release_date":"2020-05-01","images":[{"url":"%1/album.jpg","width":640}]})")
            .arg(base)
            .toUtf8());
    server.route("/album.jpg", 200, QByteArray("album-bytes"),
                 QStringLiteral("image/jpeg"));

    TestAuth auth;
    auth.setToken(fakeToken());
    SpotifyProvider provider(&auth, nullptr, this);
    provider.api()->setBaseUrl(base);

    TrackMeta meta;
    bool done = false;
    provider.fetchMetadata(QUrl("spotify:album:al1"),
                           [&meta, &done](const TrackMeta &m) {
            meta = m;
            done = true;
        });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);

    QCOMPARE(meta.title, QStringLiteral("An Album"));
    QCOMPARE(meta.artMime, QStringLiteral("image/jpeg"));
    QCOMPARE(meta.artData, QByteArray("album-bytes"));
}

void SpotifyTest::artistFetchMetadataCarriesArt()
{
    // The artist read's art: the object's own images[0].
    MiniHttpServer server;
    const QString base = server.baseUrl();
    server.route(
        "/artists/ar1", 200,
        QStringLiteral(
            R"({"name":"An Artist","images":[{"url":"%1/artist.jpg","width":640}]})")
            .arg(base)
            .toUtf8());
    server.route("/artist.jpg", 200, QByteArray("artist-bytes"),
                 QStringLiteral("image/jpeg"));

    TestAuth auth;
    auth.setToken(fakeToken());
    SpotifyProvider provider(&auth, nullptr, this);
    provider.api()->setBaseUrl(base);

    TrackMeta meta;
    bool done = false;
    provider.fetchMetadata(QUrl("spotify:artist:ar1"),
                           [&meta, &done](const TrackMeta &m) {
            meta = m;
            done = true;
        });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);

    QCOMPARE(meta.title, QStringLiteral("An Artist"));
    QCOMPARE(meta.artMime, QStringLiteral("image/jpeg"));
    QCOMPARE(meta.artData, QByteArray("artist-bytes"));
}

void SpotifyTest::fetchMetadataWithoutImageAnswersWithoutArt()
{
    // An object without images: the read lands without art (the import
    // stores the item, no art row).
    MiniHttpServer server;
    const QString base = server.baseUrl();
    server.route(
        "/tracks/t1", 200,
        QByteArrayLiteral(
            R"({"id":"t1","name":"One","artists":[{"name":"A"}],"duration_ms":2000,"album":{"name":"An Album"}})"));

    TestAuth auth;
    auth.setToken(fakeToken());
    SpotifyProvider provider(&auth, nullptr, this);
    provider.api()->setBaseUrl(base);

    TrackMeta meta;
    bool done = false;
    provider.fetchMetadata(QUrl("spotify:track:t1"),
                           [&meta, &done](const TrackMeta &m) {
            meta = m;
            done = true;
        });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);

    QCOMPARE(meta.title, QStringLiteral("One"));
    QVERIFY(meta.artData.isEmpty());
    QVERIFY(meta.artMime.isEmpty());
}

void SpotifyTest::authStateTransitions()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SpotifyAuth auth;
    auth.setTokenPath(dir.path());
    QCOMPARE(auth.state(), SpotifyAuth::State::NotSignedIn);
    QCOMPARE(auth.tokenPath(), dir.path() + "/spotify-token.json");

    // A stored token with a refresh token is signed in (an expired
    // access token counts — it is refreshed on demand).
    auth.setTokenPath(dir.path()); // a fresh load reads the (empty) file
    QVERIFY(SpotifyAuth::saveToken(auth.tokenPath(), fakeToken()));
    auth.setTokenPath(dir.path()); // reload
    QCOMPARE(auth.state(), SpotifyAuth::State::SignedIn);
    QCOMPARE(auth.token().refreshToken, QStringLiteral("fake-refresh"));

    // A logout drops the token and the file.
    auth.logout();
    QCOMPARE(auth.state(), SpotifyAuth::State::NotSignedIn);
    QVERIFY(!QFile::exists(auth.tokenPath()));
}

void SpotifyTest::setTokenPersistsToTokenFile()
{
    // The store writes the token file <data dir>/spotify-token.json —
    // never the data directory itself (a file at the directory path
    // would break everything built under it, e.g. the driver's pulse
    // runtime directory).
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    TestAuth auth;
    auth.setTokenPath(dir.path());
    auth.setToken(fakeToken());

    QCOMPARE(auth.state(), SpotifyAuth::State::SignedIn);
    const QFileInfo file(auth.tokenPath());
    QVERIFY(file.exists() && file.isFile());
    QCOMPARE(SpotifyAuth::loadToken(auth.tokenPath()).refreshToken,
             QStringLiteral("fake-refresh"));
    // The data directory is still a directory.
    QVERIFY(QFileInfo(dir.path()).isDir());
}

void SpotifyTest::validTokenBehaviors()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Not signed in: the answer is synchronous, with the error.
    SpotifyAuth auth;
    auth.setTokenPath(dir.path());
    QString error;
    bool done = false;
    auth.validToken([&](const SpotifyAuth::Token &, const QString &err) {
        error = err;
        done = true;
    });
    QVERIFY(done);
    QCOMPARE(error, QStringLiteral("not signed in"));

    // A stored valid token: synchronous, the same token.
    QVERIFY(SpotifyAuth::saveToken(auth.tokenPath(), fakeToken()));
    auth.setTokenPath(dir.path());
    SpotifyAuth::Token got;
    done = false;
    auth.validToken([&](const SpotifyAuth::Token &token, const QString &) {
        got = token;
        done = true;
    });
    QVERIFY(done);
    QCOMPARE(got.accessToken, QStringLiteral("fake-access"));
}

// The refresh-ahead: a token that has lapsed within the refresh margin
// is refreshed at once (the timer fires at once), and the refresh
// lands on the stored token (the refresh token rides on — the
// response carries none). A token with a lifetime left is not
// refreshed (the timer sits out of the test's window).
void SpotifyTest::refreshAheadKeepsTheTokenCurrent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // The refresh's endpoint: a local fake (no network in a test).
    MiniHttpServer server;
    server.route(QStringLiteral("/api/token"), 200,
                 QByteArrayLiteral(
                     R"({"access_token":"fresh","expires_in":3600})"));

    TestAuth auth;
    auth.setTokenEndpoint(server.baseUrl() + QStringLiteral("/api/token"));
    auth.setTokenPath(dir.path());

    // A token within the refresh margin: the refresh-ahead fires at
    // once and the stored token is the fresh one.
    SpotifyAuth::Token token = fakeToken();
    token.expiresAt = QDateTime::currentDateTimeUtc().addSecs(5);
    auth.setToken(token);
    QTRY_COMPARE_WITH_TIMEOUT(auth.token().accessToken,
                              QStringLiteral("fresh"), 5000);
    QCOMPARE(auth.state(), SpotifyAuth::State::SignedIn);
    // The refresh response carries no refresh token: the stored one
    // rides on.
    QCOMPARE(auth.token().refreshToken, QStringLiteral("fake-refresh"));
    QCOMPARE(server.requestPaths().size(), 1);
    QCOMPARE(server.requestPaths().first(),
             QStringLiteral("/api/token"));

    // The success re-arms: the fresh token (an hour out) is not
    // refreshed again (no second request in the window).
    QTest::qWait(200);
    QCOMPARE(server.requestPaths().size(), 1);

    // A token with a lifetime left: no refresh at all.
    MiniHttpServer quiet;
    quiet.route(QStringLiteral("/api/token"), 200,
                QByteArrayLiteral(
                    R"({"access_token":"fresh2","expires_in":3600})"));
    TestAuth idle;
    idle.setTokenEndpoint(quiet.baseUrl() + QStringLiteral("/api/token"));
    idle.setTokenPath(dir.path());
    idle.setToken(fakeToken()); // an hour out
    QTest::qWait(200);
    QCOMPARE(quiet.requestPaths().size(), 0);
    QCOMPARE(idle.token().accessToken, QStringLiteral("fake-access"));
}

void SpotifyTest::settingsRegistered()
{
    if (!pluginBuilt())
        QSKIP("the spotify plugin is not built (DOOB_BUILD_PLUGINS=OFF)");

    SourceProvider* provider = spotifyProvider();
    if (provider == nullptr)
        QFAIL("the spotify provider is not registered");

    SettingsManager* settings = SettingsManager::getInstance();
    // The client id defaults to the build-time id (a public identifier).
    QVERIFY(!settings->value("spotify.client_id").toString().isEmpty());
    QCOMPARE(settings->value("spotify.redirect_port").toInt(), 8888);
    // The settings topic the preferences dialog lists under "Plugins".
    QVERIFY(SettingsRegistry::getInstance()->contains("spotify"));
}

void SpotifyTest::notSignedInFetchIsSkipped()
{
    if (!pluginBuilt())
        QSKIP("the spotify plugin is not built (DOOB_BUILD_PLUGINS=OFF)");

    SourceProvider* provider = spotifyProvider();
    if (provider == nullptr)
        QFAIL("the spotify provider is not registered");

    // Not signed in: the fetches answer synchronously (no network).
    // The metadata fetch answers empty (the import degrades to a
    // skipped source).
    TrackMeta meta;
    bool done = false;
    provider->fetchMetadata(
        QUrl("spotify:track:3n3Ppam7vgaVa1iaRUc9Lp"),
        [&meta, &done](const TrackMeta &m) {
            meta = m;
            done = true;
        });
    QVERIFY(done);
    QVERIFY(meta.isEmpty());

    // A collection's fetch answers with the failure: the wizard shows
    // the link as failed (never imports the container as a single
    // item), the reason is the sign-in hint.
    SourceProvider::ListEntries entries;
    done = false;
    provider->fetchList(QUrl("spotify:playlist:37i9dQZF1DXcBWIGoYBM5M"),
                        [&entries, &done](const SourceProvider::ListEntries &e) {
                            entries = e;
                            done = true;
                        });
    QVERIFY(done);
    QVERIFY(entries.isEmpty());
    QVERIFY(entries.isCollection);
    QCOMPARE(entries.error, SpotifyProvider::authHintText());

    // A track references no collection: the plain answer (the wizard
    // imports it as a plain source).
    SourceProvider::ListEntries trackEntries;
    done = false;
    provider->fetchList(QUrl("spotify:track:3n3Ppam7vgaVa1iaRUc9Lp"),
                        [&trackEntries, &done](const SourceProvider::ListEntries &e) {
                            trackEntries = e;
                            done = true;
                        });
    QVERIFY(done);
    QVERIFY(trackEntries.isEmpty());
    QVERIFY(!trackEntries.isCollection);
    QVERIFY(trackEntries.error.isEmpty());

    // The skip's reason (the import shows it): the sign-in hint while
    // there is no account.
    QCOMPARE(provider->fetchFailureHint(),
             SpotifyProvider::authHintText());
}

void SpotifyTest::storedTokenDefersFetch()
{
    if (!pluginBuilt())
        QSKIP("the spotify plugin is not built (DOOB_BUILD_PLUGINS=OFF)");

    // A stored (not expired) token: the provider must load it and go to
    // the network (the fetch is no longer answered synchronously — the
    // fake token 401s later, the async completion is not waited for).
    writeFakeTokenFile();
    SourceProvider* provider = spotifyProvider();
    if (provider == nullptr)
        QFAIL("the spotify provider is not registered");

    bool done = false;
    provider->fetchMetadata(
        QUrl("spotify:track:3n3Ppam7vgaVa1iaRUc9Lp"),
        [&done](const TrackMeta &) { done = true; });
    QVERIFY(!done);

    // Signed in: the skip reason is empty (a failed fetch now has no
    // sign-in explanation).
    QVERIFY(provider->fetchFailureHint().isEmpty());
}

void SpotifyTest::clientIdChangeSignsOut()
{
    if (!pluginBuilt())
        QSKIP("the spotify plugin is not built (DOOB_BUILD_PLUGINS=OFF)");

    // A token belongs to the app that issued it: a client-id change
    // drops the stored token (a re-login is required).
    writeFakeTokenFile();
    SourceProvider* provider = spotifyProvider();
    if (provider == nullptr)
        QFAIL("the spotify provider is not registered");
    QVERIFY(QFile::exists(tokenFile()));

    SettingsManager* settings = SettingsManager::getInstance();
    settings->setValue("spotify.client_id", QStringLiteral("some-other-app-id"));
    QVERIFY(!QFile::exists(tokenFile()));

    // Back to the default id for the other tests.
    settings->resetToDefault("spotify.client_id");
}

} // namespace

QTEST_MAIN(SpotifyTest)
#include "spotify_test.moc"
