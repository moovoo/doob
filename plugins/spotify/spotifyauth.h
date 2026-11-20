#ifndef SPOTIFYAUTH_H
#define SPOTIFYAUTH_H

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QTcpServer;
class QTimer;
class QTcpSocket;

// The plugin's settings keys (the central settings.json, the keys
// prefixed with the plugin's id — a convention the SettingsManager does
// not enforce). The client id is a public identifier (the PKCE flow's
// security comes from the challenge, not from hiding it) — the default
// is the build-time id (SPOTIFY_CLIENT_ID_DEFAULT), a user's own app
// overrides it here. The loopback port must match the redirect URI the
// app registered in the Spotify Developer Dashboard
// (http://127.0.0.1:<port>/callback).
inline constexpr const char *kSpotifyClientIdKey = "spotify.client_id";
inline constexpr const char *kSpotifyRedirectPortKey = "spotify.redirect_port";
inline constexpr int kDefaultSpotifyRedirectPort = 8888;

// The build-time default client id (CMake's SPOTIFY_CLIENT_ID_DEFAULT;
// the #ifndef fallback keeps a standalone build compiling).
#ifndef SPOTIFY_CLIENT_ID_DEFAULT
#define SPOTIFY_CLIENT_ID_DEFAULT "fdffd336889e471fac16ca0a1710b662"
#endif

// Spotify OAuth for the plugin: authorization code + PKCE with a
// 127.0.0.1:<port> loopback redirect (what Spotify's desktop apps use —
// the device code flow is allowlist-only, so it is not offered), plus
// token exchange/refresh against accounts.spotify.com and a JSON token
// file in the plugin's data directory.
//
// The state is SignedIn whenever a token with a refresh token is stored
// (an expired access token counts — it is refreshed on demand); every
// token-consuming call goes through validToken(), which auto-refreshes.
class SpotifyAuth : public QObject
{
    Q_OBJECT
public:
    // The expiry margin (a token that expires within it counts as
    // expired): a request in flight must not 401 mid-request, and the
    // refresh-ahead (below) fires on it.
    static constexpr int kRefreshMarginSecs = 30;

    struct Token
    {
        QString accessToken;
        QString refreshToken;
        QDateTime expiresAt; // the access token's expiry (UTC)
        QString scopes;

        bool hasAccess() const { return !accessToken.isEmpty(); }
        bool hasRefresh() const { return !refreshToken.isEmpty(); }
        // A margin (kRefreshMarginSecs), so a token that expires while a
        // request is in flight does not 401 mid-request.
        bool expired() const
        {
            return expiresAt <
                   QDateTime::currentDateTimeUtc()
                       .addSecs(kRefreshMarginSecs);
        }
        bool valid() const { return hasAccess() && !expired(); }

        QJsonObject toJson() const;
        // The token file format (the "expires_at" epoch-ms shape).
        static Token fromJson(const QJsonObject &obj);
        // The accounts.spotify.com token-endpoint response shape
        // ("expires_in" seconds, relative to now).
        static Token fromApi(const QJsonObject &obj);
        // A refresh response carries no (new) refresh token: the stored
        // one rides on — so the saved file never loses it. A response
        // that does carry one (rotation) wins.
        static Token withStoredRefresh(const Token &token,
                                       const QString &storedRefresh);
    };

    enum class State
    {
        NotSignedIn,
        SignedIn, // a token with a refresh token is stored
    };

    explicit SpotifyAuth(QObject *parent = nullptr);

    // The token file (JSON): <path>/spotify-token.json. The plugin sets
    // its own data directory; a missing file loads as NotSignedIn.
    void setTokenPath(const QString &path);
    QString tokenPath() const;

    State state() const;
    Token token() const { return m_token; }

    // done() is invoked exactly once (an empty error string means
    // success). Listens on 127.0.0.1:<the configured port> and opens the
    // system browser at the authorize URL; a 5-minute timeout fails the
    // flow.
    using TokenCallback = std::function<void(const Token &, const QString &)>;
    void login(const TokenCallback &done);

    // Drops the stored token (and fails an in-flight login).
    void logout();

    // A valid (not expired) token, exactly once on the GUI thread: a
    // stored valid token is returned as-is, an expired one with a
    // refresh token is refreshed first, and the absence of a usable
    // token answers with an empty token and the error message.
    void validToken(const TokenCallback &done);

    // The token endpoint (accounts.spotify.com/api/token; the tests
    // point a local fake at it — no network in a test).
    void setTokenEndpoint(const QUrl &url);

    // The live setting values (a settings-page edit applies to the next
    // login / fetch, no restart).
    QString clientId() const;
    int redirectPort() const;

    // The token file (JSON). loadToken() returns an empty Token when the
    // file is missing or unreadable.
    static Token loadToken(const QString &path);
    static bool saveToken(const QString &path, const Token &token);

    // PKCE building blocks (static so the tests reach them): base64url
    // without padding, random base64url of the given length, and the
    // S256 challenge (base64url(SHA256(verifier))).
    static QByteArray b64url(const QByteArray &data);
    static QByteArray randomB64Url(int bytes);
    static QByteArray pkceChallenge(const QByteArray &verifier);

signals:
    // The state changed (a login, a refresh, a logout, a client-id
    // change that forces a re-login).
    void authChanged();
    // The sign-in itself changed (a completed login — as opposed to a
    // refresh of the same account, which is authChanged only): a
    // device built on the previous sign-in's token belongs to the
    // previous account and must be torn down.
    void accountChanged();
    // Human-readable progress (the settings page's status line).
    void statusMessage(const QString &message);

protected:
    // Stores (and persists) the token and re-emits the state (the
    // login/refresh flows' common store — protected so the tests reach
    // it).
    void setToken(const Token &token);

private:
    // newAccount marks an authorization-code exchange (a login — the
    // sign-in itself changes), as opposed to a refresh of the same
    // account: on a 200 the accountChanged emission follows the
    // authChanged one.
    void postToken(const QUrlQuery &form, const TokenCallback &call,
                   const QString &fallbackRefresh = QString(),
                   bool newAccount = false);
    void exchangeCode(const QString &code, const TokenCallback &call);
    void handleCallbackSocket(QTcpSocket *socket, const TokenCallback &call);
    // The refresh-ahead: (re)arms the single-shot timer to refresh a
    // margin (kRefreshMarginSecs) before the stored token's expiry —
    // the running helper keeps presenting a token to Spotify the whole
    // time, and an on-demand refresh alone leaves a long idle with an
    // expired one. Stops the timer when there is nothing to refresh.
    void scheduleRefresh();

    static void replyAndClose(QTcpSocket *socket, const QString &title,
                              const QString &body);

    QString m_tokenPath;
    Token m_token;
    // The token endpoint (accounts.spotify.com; setTokenEndpoint()
    // points the tests' local fake at it).
    QUrl m_tokenUrl{QStringLiteral("https://accounts.spotify.com/api/token")};
    QNetworkAccessManager *m_nam = nullptr;
    // The in-flight login (null when no login is running).
    QTcpServer *m_server = nullptr;
    QTimer *m_loginTimeout = nullptr;
    QString m_verifier;
    QString m_state;
    // The request data each callback socket has delivered so far (a
    // request may arrive in several TCP chunks).
    QHash<QTcpSocket *, QByteArray> m_socketBuffers;
    // validToken() callers parked on the in-flight refresh.
    bool m_refreshing = false;
    QList<TokenCallback> m_pendingValid;
    // The in-flight login's once-guarded callback (logout() fails it).
    TokenCallback m_loginCall;
    // The refresh-ahead timer (single-shot; (re)armed by
    // scheduleRefresh() on every token change, stopped on a logout).
    QTimer *m_refreshTimer = nullptr;
};

#endif // SPOTIFYAUTH_H
