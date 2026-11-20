#include "spotifyauth.h"

#include <doob/settingsmanager.h>

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QTimeZone>
#include <QUrlQuery>
#include <QVariant>

#include <algorithm>
#include <memory>

namespace {
const QString kAuthorizeUrl =
    QStringLiteral("https://accounts.spotify.com/authorize");
// streaming is the Web Playback SDK's scope; the rest cover the Web API
// calls the plugin makes (playback state, playlists, albums).
const QString kScopes =
    QStringLiteral("streaming user-read-playback-state "
                   "user-modify-playback-state playlist-read-private "
                   "playlist-read-collaborative");
} // namespace

SpotifyAuth::SpotifyAuth(QObject *parent) : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
    // A client-id change invalidates the stored token (a token belongs to
    // the app that issued it): force a re-login.
    connect(SettingsManager::getInstance(), &SettingsManager::changed, this,
            [this](const QString &key, const QVariant &) {
                if (key != QLatin1String(kSpotifyClientIdKey))
                    return;
                qWarning("spotify: the client id changed — the stored token "
                         "is for the previous app, sign in again");
                logout();
                emit statusMessage(QStringLiteral(
                    "client id changed — sign in again"));
            });
}

void SpotifyAuth::setTokenPath(const QString &path)
{
    m_tokenPath = path;
    m_token = loadToken(tokenPath());
    scheduleRefresh();
    emit authChanged();
}

QString SpotifyAuth::tokenPath() const
{
    return m_tokenPath + QStringLiteral("/spotify-token.json");
}

SpotifyAuth::State SpotifyAuth::state() const
{
    // A refresh token is what makes a sign-in durable: an expired access
    // token counts as signed in (it is refreshed on demand).
    return m_token.hasRefresh() ? State::SignedIn : State::NotSignedIn;
}

QString SpotifyAuth::clientId() const
{
    const QString id =
        SettingsManager::getInstance()->value(kSpotifyClientIdKey).toString();
    return id.isEmpty() ? QString::fromLatin1(SPOTIFY_CLIENT_ID_DEFAULT) : id;
}

int SpotifyAuth::redirectPort() const
{
    const int port =
        SettingsManager::getInstance()->value(kSpotifyRedirectPortKey).toInt();
    return (port >= 1 && port <= 65535) ? port : kDefaultSpotifyRedirectPort;
}

QJsonObject SpotifyAuth::Token::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("access_token")] = accessToken;
    obj[QStringLiteral("refresh_token")] = refreshToken;
    obj[QStringLiteral("expires_at")] = expiresAt.toMSecsSinceEpoch();
    obj[QStringLiteral("scopes")] = scopes;
    return obj;
}

SpotifyAuth::Token SpotifyAuth::Token::fromJson(const QJsonObject &obj)
{
    Token t;
    t.accessToken = obj.value(QStringLiteral("access_token")).toString();
    t.refreshToken = obj.value(QStringLiteral("refresh_token")).toString();
    t.expiresAt = QDateTime::fromMSecsSinceEpoch(
        obj.value(QStringLiteral("expires_at")).toVariant().toLongLong(),
        QTimeZone(0));
    t.scopes = obj.value(QStringLiteral("scopes")).toString();
    return t;
}

SpotifyAuth::Token SpotifyAuth::Token::fromApi(const QJsonObject &obj)
{
    Token t;
    t.accessToken = obj.value("access_token").toString();
    t.refreshToken = obj.value("refresh_token").toString();
    t.expiresAt = QDateTime::currentDateTimeUtc().addSecs(
        obj.value("expires_in").toInt(3600));
    t.scopes = obj.value("scope").toString();
    return t;
}

SpotifyAuth::Token SpotifyAuth::Token::withStoredRefresh(
    const Token &token, const QString &storedRefresh)
{
    Token out = token;
    if (out.refreshToken.isEmpty() && !storedRefresh.isEmpty())
        out.refreshToken = storedRefresh;
    return out;
}

SpotifyAuth::Token SpotifyAuth::loadToken(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return Token::fromJson(QJsonDocument::fromJson(file.readAll()).object());
}

bool SpotifyAuth::saveToken(const QString &path, const Token &token)
{
    QFileInfo fi(path);
    if (!fi.dir().exists() && !fi.dir().mkpath(fi.dir().absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(token.toJson()).toJson(QJsonDocument::Compact));
    return true;
}

QByteArray SpotifyAuth::b64url(const QByteArray &data)
{
    return data.toBase64(QByteArray::Base64UrlEncoding
                         | QByteArray::OmitTrailingEquals);
}

QByteArray SpotifyAuth::randomB64Url(int bytes)
{
    QByteArray data(bytes, 0);
    std::generate(data.begin(), data.end(),
                  [] {
                      return static_cast<char>(
                          QRandomGenerator::global()->bounded(256));
                  });
    return b64url(data);
}

QByteArray SpotifyAuth::pkceChallenge(const QByteArray &verifier)
{
    return b64url(QCryptographicHash::hash(verifier, QCryptographicHash::Sha256));
}

void SpotifyAuth::login(const TokenCallback &done)
{
    if (m_server != nullptr) {
        done(Token(), QStringLiteral("a sign-in is already in progress"));
        return;
    }
    auto fired = std::make_shared<bool>(false);
    TokenCallback call = [this, fired, done](const Token &t, const QString &err) {
        if (*fired)
            return;
        *fired = true;
        if (m_server) {
            m_server->close();
            m_server->deleteLater();
            m_server = nullptr;
        }
        if (m_loginTimeout)
            m_loginTimeout->stop();
        if (m_loginCall)
            m_loginCall = nullptr;
        done(t, err);
    };
    m_loginCall = call;

    m_verifier = QString::fromLatin1(randomB64Url(32));
    m_state = QString::fromLatin1(randomB64Url(16));
    const int port = redirectPort();
    const QString redirectUri =
        QStringLiteral("http://127.0.0.1:%1/callback").arg(port);

    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::LocalHost, port)) {
        const QString problem = m_server->errorString();
        m_server->deleteLater();
        m_server = nullptr;
        m_loginCall = nullptr;
        call(Token(), QStringLiteral("cannot listen on 127.0.0.1:%1: %2")
                     .arg(port)
                     .arg(problem));
        return;
    }
    connect(m_server, &QTcpServer::newConnection, this,
            [this, call] {
                QTcpSocket *socket = m_server->nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, this,
                        [this, socket, call] {
                            handleCallbackSocket(socket, call);
                        });
                connect(socket, &QTcpSocket::disconnected, socket,
                        &QTcpSocket::deleteLater);
                connect(socket, &QObject::destroyed, this,
                        [this, socket] {
                            m_socketBuffers.remove(socket);
                        });
            });

    QUrl url(kAuthorizeUrl);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), clientId());
    query.addQueryItem(QStringLiteral("response_type"),
                       QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("redirect_uri"), redirectUri);
    query.addQueryItem(QStringLiteral("scope"), kScopes);
    query.addQueryItem(QStringLiteral("code_challenge_method"),
                       QStringLiteral("S256"));
    query.addQueryItem(
        QStringLiteral("code_challenge"),
        QString::fromLatin1(pkceChallenge(m_verifier.toLatin1())));
    query.addQueryItem(QStringLiteral("state"), m_state);
    url.setQuery(query);

    m_loginTimeout = new QTimer(this);
    m_loginTimeout->setSingleShot(true);
    m_loginTimeout->setInterval(5 * 60 * 1000);
    connect(m_loginTimeout, &QTimer::timeout, this,
            [call] {
                call(Token(), QStringLiteral(
                                  "authorization timed out (no redirect within "
                                  "5 minutes)"));
            });
    m_loginTimeout->start();

    emit statusMessage(QStringLiteral(
        "waiting for the browser (the sign-in URL was opened automatically): "
        "%1")
                          .arg(url.toString()));
    QDesktopServices::openUrl(url);
}

void SpotifyAuth::logout()
{
    if (state() == State::SignedIn) {
        m_token = Token();
        QFile::remove(tokenPath());
        if (m_refreshTimer != nullptr)
            m_refreshTimer->stop();
        emit authChanged();
    }
    if (m_loginCall) {
        TokenCallback call = std::move(m_loginCall);
        m_loginCall = nullptr;
        call(Token(), QStringLiteral("sign-in cancelled"));
    }
}

void SpotifyAuth::validToken(const TokenCallback &done)
{
    const qint64 msLeft =
        m_token.expiresAt.toMSecsSinceEpoch()
        - QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    if (m_token.valid()) {
        qInfo("spotify: the token is valid (expires in %llds)",
              static_cast<long long>(msLeft / 1000));
        done(m_token, QString());
        return;
    }
    if (m_refreshing) {
        qInfo("spotify: a token refresh is already in flight");
        m_pendingValid.append(done);
        return;
    }
    if (!m_token.hasRefresh()) {
        done(Token(), QStringLiteral("not signed in"));
        return;
    }
    qInfo("spotify: the token is %llds past its expiry — refreshing it",
          static_cast<long long>(-msLeft / 1000));
    m_refreshing = true;
    emit statusMessage(QStringLiteral("refreshing the token…"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("grant_type"),
                       QStringLiteral("refresh_token"));
    query.addQueryItem(QStringLiteral("refresh_token"), m_token.refreshToken);
    query.addQueryItem(QStringLiteral("client_id"), clientId());
    postToken(query,
              [this, done](const Token &t, const QString &err) {
                  m_refreshing = false;
                  auto pending = std::move(m_pendingValid);
                  m_pendingValid.clear();
                  if (err.isEmpty() && m_token.hasRefresh()) {
                      // A logout during the refresh clears the token: do
                      // not resurrect it with the stale response.
                      setToken(t);
                      qInfo("spotify: the token was refreshed (expires in %llds)",
                            static_cast<long long>(
                                (m_token.expiresAt.toMSecsSinceEpoch()
                                 - QDateTime::currentDateTimeUtc().toMSecsSinceEpoch())
                                    / 1000));
                      for (const auto &cb : pending)
                          cb(m_token, QString());
                      done(m_token, QString());
                      return;
                  }
                  const QString message =
                      err.isEmpty() ? QStringLiteral("signed out") : err;
                  qWarning("spotify: token refresh failed: %s",
                           qPrintable(message));
                  if (m_token.hasRefresh())
                      logout();
                  for (const auto &cb : pending)
                      cb(Token(), message);
                  done(Token(), message);
              },
              m_token.refreshToken);
}

void SpotifyAuth::setToken(const Token &token)
{
    m_token = token;
    saveToken(tokenPath(), token);
    scheduleRefresh();
    emit authChanged();
}

void SpotifyAuth::setTokenEndpoint(const QUrl &url)
{
    m_tokenUrl = url;
}

// The refresh-ahead (the comment on the declaration says why): the
// single-shot timer fires a margin before the expiry, so a token that
// has lapsed within the margin refreshes at once. The success re-arms
// through setToken (the new expiry), the failure has logged out (the
// timer is stopped there).
void SpotifyAuth::scheduleRefresh()
{
    if (m_refreshTimer == nullptr) {
        m_refreshTimer = new QTimer(this);
        m_refreshTimer->setSingleShot(true);
        connect(m_refreshTimer, &QTimer::timeout, this, [this] {
            validToken([](const SpotifyAuth::Token &, const QString &) {
                // The success re-arms through setToken; the failure
                // has logged out.
            });
        });
    }
    if (!m_token.hasAccess() || !m_token.hasRefresh()) {
        m_refreshTimer->stop();
        return;
    }
    const qint64 marginMs =
        static_cast<qint64>(kRefreshMarginSecs) * 1000;
    const qint64 msUntilExpiry =
        m_token.expiresAt.toMSecsSinceEpoch()
        - QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    const qint64 delayMs =
        qMax(static_cast<qint64>(0), msUntilExpiry - marginMs);
    // The delay is a minute-scale value (the token's remaining
    // lifetime): int milliseconds hold it.
    m_refreshTimer->start(static_cast<int>(delayMs));
}

void SpotifyAuth::handleCallbackSocket(QTcpSocket *socket,
                                       const TokenCallback &call)
{
    // A request may arrive in several TCP chunks; a truncated first line
    // would look like a state mismatch, so buffer until the headers are
    // complete.
    QByteArray &buffer = m_socketBuffers[socket];
    buffer.append(socket->readAll());
    if (!buffer.contains("\r\n\r\n"))
        return;

    // The request line: GET /callback?code=...&state=... HTTP/1.1
    const qsizetype lineEnd = buffer.indexOf("\r\n");
    const QByteArray line = buffer.left(lineEnd);
    const QList<QByteArray> parts = line.split(' ');
    m_socketBuffers.remove(socket);
    if (parts.size() < 2) {
        replyAndClose(socket, QStringLiteral("Bad request"),
                      QStringLiteral("expected a GET request"));
        return;
    }
    const QUrl url(QString::fromLatin1(parts.at(1)));
    // Anything that is not the callback (a favicon fetch for the result
    // page, ...) is not an authorization event: answer and stay quiet.
    if (url.path() != QLatin1String("/callback")) {
        replyAndClose(socket, QStringLiteral("Not found"),
                      QStringLiteral("nothing here"));
        return;
    }
    const QUrlQuery query(url.query());
    const QString state = query.queryItemValue(QStringLiteral("state"));
    if (state != m_state) {
        replyAndClose(socket, QStringLiteral("State mismatch"),
                      QStringLiteral("the state parameter did not match; "
                                     "nothing was authorized"));
        call(Token(), QStringLiteral("callback state mismatch"));
        return;
    }
    const QString error = query.queryItemValue(QStringLiteral("error"));
    if (!error.isEmpty()) {
        replyAndClose(socket, QStringLiteral("Authorization failed"),
                      QStringLiteral("%1: %2")
                          .arg(error,
                               query.queryItemValue(
                                   QStringLiteral("error_description"))));
        call(Token(), QStringLiteral("authorization failed: %1 %2")
                     .arg(error,
                          query.queryItemValue(
                              QStringLiteral("error_description"))));
        return;
    }
    const QString code = query.queryItemValue(QStringLiteral("code"));
    if (code.isEmpty()) {
        replyAndClose(socket, QStringLiteral("No code"),
                      QStringLiteral("the redirect carried no code"));
        call(Token(), QStringLiteral("the redirect carried no code"));
        return;
    }
    replyAndClose(socket, QStringLiteral("Success"),
                  QStringLiteral("You can close this window and return to "
                                 "the app."));
    exchangeCode(code, call);
}

void SpotifyAuth::replyAndClose(QTcpSocket *socket, const QString &title,
                                const QString &body)
{
    const QByteArray html =
        QByteArrayLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\">")
            .append(title.toUtf8())
            .append(QByteArrayLiteral("</title></head><body><h3>"))
            .append(body.toUtf8())
            .append(QByteArrayLiteral("</h3></body></html>"));
    const QByteArray out =
        QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: text/html; "
                          "charset=utf-8\r\nContent-Length: ")
            .append(QByteArray::number(html.size()))
            .append(QByteArrayLiteral("\r\nConnection: close\r\n\r\n"))
            .append(html);
    socket->write(out);
    socket->disconnectFromHost();
}

void SpotifyAuth::exchangeCode(const QString &code, const TokenCallback &call)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("grant_type"),
                       QStringLiteral("authorization_code"));
    query.addQueryItem(QStringLiteral("code"), code);
    query.addQueryItem(
        QStringLiteral("redirect_uri"),
        QStringLiteral("http://127.0.0.1:%1/callback").arg(redirectPort()));
    query.addQueryItem(QStringLiteral("client_id"), clientId());
    query.addQueryItem(QStringLiteral("code_verifier"), m_verifier);
    // An authorization-code exchange (a login): the sign-in itself
    // changes, not just the token.
    postToken(query, call, QString(), true);
}

void SpotifyAuth::postToken(const QUrlQuery &form, const TokenCallback &call,
                            const QString &fallbackRefresh, bool newAccount)
{
    QNetworkRequest request{m_tokenUrl};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      "application/x-www-form-urlencoded");
    QNetworkReply *reply =
        m_nam->post(request, form.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, call, fallbackRefresh, newAccount] {
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                        .toInt();
                const QJsonObject obj =
                    QJsonDocument::fromJson(reply->readAll()).object();
                reply->deleteLater();

                if (status == 0) {
                    call(Token(), QStringLiteral("token request failed: %1")
                                 .arg(reply->errorString()));
                    return;
                }
                if (status == 200) {
                    const Token token = Token::withStoredRefresh(
                        Token::fromApi(obj), fallbackRefresh);
                    setToken(token);
                    // A login (an authorization-code exchange): the
                    // sign-in itself changed, not just the token —
                    // after the authChanged the handlers need to tell
                    // apart.
                    if (newAccount)
                        emit accountChanged();
                    emit statusMessage(
                        QStringLiteral("token acquired (expires in %1s)")
                            .arg(obj.value("expires_in").toInt()));
                    call(token, QString());
                    return;
                }
                call(Token(),
                     QStringLiteral("token request failed: %1 %2")
                         .arg(obj.value("error").toString(),
                              obj.value("error_description").toString()));
            });
}
