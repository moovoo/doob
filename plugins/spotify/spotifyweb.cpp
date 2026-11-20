// The spotify-web helper: a separate process that hosts the Spotify Web
// Playback SDK (a QWebEngine page + a QWebChannel bridge) and relays its
// device's state to the main process over JSON lines (stdin in, stdout
// out — the same channel the driver's SpotifyIpc speaks).
//
// It is a process, not a thread: the SDK's audio must land on the
// plugin's private PulseAudio server, and PULSE_SERVER has to be in the
// environment before this process's tree (the WebEngine's children)
// forks — the main doob process must never be pointed at the private
// server. The driver spawns the helper with PULSE_SERVER and
// SPOTIFY_TOKEN in its environment.
//
// The helper is deliberately thin: it owns no sign-in (the token
// arrives in the environment, refreshes ride a "token" command), no
// device selection (the driver knows when the device is ready) and no
// playlist logic. Starting a track is a Web API call the helper makes
// with its own token and device id (PUT /me/player/play — the only way
// to name what plays); pause/resume/seek/volume are the SDK's own
// transport methods.

#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProcess>
#include <QSocketNotifier>
#include <QTimer>
#include <QVBoxLayout>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <QMainWindow>

#include <array>
#include <fcntl.h>
#include <unistd.h>

#include "spotifywebapi.h"

namespace {

// Everything currently available on stdin (a QSocketNotifier fired, so
// there is at least a byte; the fd is made non-blocking for the drain
// and restored after). An EOF (the parent closing the pipe) reads 0.
QByteArray readStdinAvailable()
{
    const int flags = ::fcntl(STDIN_FILENO, F_GETFL, 0);
    // NOLINTNEXTLINE(bugprone-signed-bitwise) the flags word is a bit field.
    ::fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    QByteArray data;
    std::array<char, 4096> buffer{};
    for (;;) {
        const qint64 count =
            ::read(STDIN_FILENO, buffer.data(), buffer.size());
        if (count <= 0)
            break;
        data.append(buffer.data(), static_cast<int>(count));
    }
    ::fcntl(STDIN_FILENO, F_SETFL, flags);
    return data;
}

// A JSON line to stdout (the driver's SpotifyIpc reads it). A full
// write: a pipe may take a line in parts.
void sendEvent(const QJsonObject &event)
{
    const QByteArray line =
        QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n';
    qsizetype offset = 0;
    while (offset < line.size()) {
        const QByteArray rest = line.mid(offset);
        const qint64 written =
            ::write(STDOUT_FILENO, rest.constData(), rest.size());
        if (written <= 0)
            return;
        offset += written;
    }
}

// This Qt declares javaScriptConsoleMessage() as a protected virtual
// (not a public signal) — re-emit it from a subclass so the page's
// console can be mirrored to the driver (as "log" lines).
class ConsolePage : public QWebEnginePage
{
    Q_OBJECT
public:
    explicit ConsolePage(QObject *parent = nullptr)
        : QWebEnginePage(parent) {}

signals:
    void consoleMessage(const QString &message, int lineNumber,
                        const QString &source);

protected:
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level,
                                  const QString &message, int lineNumber,
                                  const QString &sourceID) override
    {
        Q_UNUSED(level);
        emit consoleMessage(message, lineNumber, sourceID);
    }
};

// The C++ half of the QWebChannel link (the JS half is the page's
// window.spotifyBridge): the page's SDK events in, the driver's
// transport commands out.
class WebBridge : public QObject
{
    Q_OBJECT
public:
    WebBridge(QWebEnginePage *page, SpotifyWebApi *api, QObject *parent = nullptr)
        : QObject(parent)
        , m_page(page)
        , m_api(api)
    {
    }

    // (Re)sets the access token (the environment's at start, a refresh
    // on a "token" command) and pushes it to the page and the Web API.
    void setToken(const QString &token)
    {
        m_token = token;
        m_api->setAccessToken(token);
        deliverToken();
    }
    QString token() const { return m_token; }

    // Start playback on the ready device (the Web API's job — the SDK's
    // own transport cannot name what plays).
    void play(const QString &uri, qint64 positionMs)
    {
        if (m_deviceId.isEmpty()) {
            emit logLine(QStringLiteral("play: no device yet"));
            emit playResult(false, QStringLiteral("the device is not ready"));
            return;
        }
        // A track is named by its uri; an album, playlist or artist is
        // a context (the Web API plays it from its first track).
        const bool track = uri.section(QLatin1Char(':'), 1, 1)
            == QLatin1String("track");
        m_api->play(m_deviceId,
                    track ? uri : QString(),
                    track ? QString() : uri,
                    positionMs,
                    [this](const QJsonObject &, const QString &error) {
                        if (error.isEmpty())
                            emit playResult(true, QString());
                        else
                            emit playResult(false, error);
                    });
    }

public slots: // JS -> C++
    // The SDK asked for a token (connect / expiry): answer with the
    // current one.
    void requestToken()
    {
        if (m_token.isEmpty())
            emit logLine(QStringLiteral("token requested, none held"));
        deliverToken();
    }
    void reportReady(const QString &deviceId)
    {
        m_deviceId = deviceId;
        emit ready(deviceId);
    }
    void reportNotReady()
    {
        m_deviceId.clear();
        emit notReady();
    }
    void reportState(const QString &json)
    {
        emit stateChanged(
            json == QLatin1String("null")
                ? QJsonObject()
                : QJsonDocument::fromJson(json.toUtf8()).object());
    }
    void reportError(const QString &json)
    {
        emit playerError(QJsonDocument::fromJson(json.toUtf8()).object());
    }
    void reportPosition(qint64 positionMs, qint64 durationMs)
    {
        emit positionChanged(positionMs, durationMs);
    }
    // The page's connect() answered (a connect can fail — a bad token,
    // no network): the driver's pending play rides on it.
    void reportConnectResult(bool ok)
    {
        emit connectResult(ok);
    }
    void pageLog(const QString &message)
    {
        emit logLine(message);
    }

signals:
    void ready(const QString &deviceId);
    void notReady();
    void stateChanged(const QJsonObject &state);
    void playerError(const QJsonObject &error);
    void positionChanged(qint64 positionMs, qint64 durationMs);
    void logLine(const QString &line);
    void playResult(bool ok, const QString &error);
    void connectResult(bool ok);

private:
    void deliverToken()
    {
        // QJsonValue::toJson() is a valid JS string literal (quoted,
        // escaped).
        m_page->runJavaScript(QStringLiteral("window.spotifyTokenDelivered(%1);")
                                  .arg(QJsonValue(m_token).toJson()));
    }

    QWebEnginePage *m_page;
    SpotifyWebApi *m_api;
    QString m_token;
    QString m_deviceId;
};

// The helper's window: the SDK's page, a status line, and the two IPC
// ends (stdin commands, stdout events).
class WebWindow : public QMainWindow
{
    Q_OBJECT
public:
    WebWindow()
        : QMainWindow(nullptr)
        , m_status(new QLabel(this))
        , m_view(new QWebEngineView(this))
    {
        setWindowTitle(QStringLiteral("Spotify (doob)"));
        resize(640, 420);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->addWidget(m_status);
        layout->addWidget(m_view, 1);

        m_api = new SpotifyWebApi(this);
        m_page = new ConsolePage(this);
        m_view->setPage(m_page);
        m_bridge = new WebBridge(m_page, m_api, this);
        auto *channel = new QWebChannel(this);
        channel->registerObject(QStringLiteral("spotifyBridge"), m_bridge);
        m_page->setWebChannel(channel);

        m_status->setText(QStringLiteral(
            "server: %1").arg(qEnvironmentVariable("PULSE_SERVER")));
        if (qEnvironmentVariable("SPOTIFY_TOKEN").isEmpty())
            sendEvent({{QStringLiteral("type"), QStringLiteral("error")},
                       {QStringLiteral("message"),
                        QStringLiteral("no Spotify token (not signed in?)"),
                       },
                      });

        // The driver's commands (stdin, JSON lines).
        auto *stdinNotifier =
            new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, this);
        connect(stdinNotifier, &QSocketNotifier::activated, this,
                &WebWindow::onStdin);
        // The page's console (to the driver's log).
        connect(m_page, &ConsolePage::consoleMessage, this,
                [this](const QString &message, int lineNumber,
                       const QString &source) {
                    const QString name =
                        source.section(QLatin1Char('/'), -1);
                    sendEvent({{QStringLiteral("type"), QStringLiteral("log")},
                               {QStringLiteral("line"),
                                QStringLiteral("[%1:%2] %3")
                                    .arg(name, QString::number(lineNumber),
                                         message),
                               },
                              });
                });
        // The bridge's events (stdout, JSON lines).
        connect(m_bridge, &WebBridge::ready, this,
                [this](const QString &deviceId) {
                    sendEvent({{QStringLiteral("type"), QStringLiteral("ready")},
                               {QStringLiteral("device"), deviceId},
                              });
                    m_status->setText(QStringLiteral("device: %1")
                                          .arg(deviceId));
                });
        connect(m_bridge, &WebBridge::notReady, this, [this] {
            sendEvent({{QStringLiteral("type"), QStringLiteral("notready")}});
            m_status->setText(QStringLiteral("device: not ready"));
        });
        connect(m_bridge, &WebBridge::stateChanged, this,
                [this](const QJsonObject &state) {
                    QJsonObject out;
                    out.insert(QStringLiteral("type"),
                               QStringLiteral("state"));
                    if (state.isEmpty()) {
                        out.insert(QStringLiteral("stopped"), true);
                        m_status->setText(QStringLiteral("stopped"));
                    } else {
                        m_status->setText(
                            (state.value("paused").toBool()
                                 ? QStringLiteral("paused — ")
                                 : QStringLiteral("playing — "))
                                + state.value("title").toString());
                        for (auto it = state.constBegin();
                             it != state.constEnd(); ++it)
                            out.insert(it.key(), it.value());
                    }
                    sendEvent(out);
                });
        connect(m_bridge, &WebBridge::positionChanged, this,
                [this](qint64 positionMs, qint64 durationMs) {
                    sendEvent({{QStringLiteral("type"), QStringLiteral("position")},
                               {QStringLiteral("position"), positionMs},
                               {QStringLiteral("duration"), durationMs},
                              });
                });
        connect(m_bridge, &WebBridge::playerError, this,
                [this](const QJsonObject &error) {
                    // The page tags each error with the SDK listener's
                    // name ("type" there) — it rides the envelope as
                    // "listener" (the envelope's own "type" is this
                    // line's kind, not the page's).
                    sendEvent({{QStringLiteral("type"), QStringLiteral("error")},
                               {QStringLiteral("listener"),
                                error.value("type").toString(),
                               },
                               {QStringLiteral("message"),
                                error.value("message").toString(),
                               },
                               {QStringLiteral("reason"),
                                error.value("reason").toString(),
                               },
                              });
                });
        connect(m_bridge, &WebBridge::logLine, this,
                [this](const QString &line) {
                    sendEvent({{QStringLiteral("type"), QStringLiteral("log")},
                               {QStringLiteral("line"), line},
                              });
                });
        connect(m_bridge, &WebBridge::playResult, this,
                [this](bool ok, const QString &error) {
                    sendEvent({{QStringLiteral("type"), QStringLiteral("play-result")},
                               {QStringLiteral("ok"), ok},
                               {QStringLiteral("error"), error},
                              });
                });
        connect(m_bridge, &WebBridge::connectResult, this, [this](bool ok) {
            sendEvent({{QStringLiteral("type"), QStringLiteral("connect-result")},
                       {QStringLiteral("ok"), ok},
                      });
        });

        m_view->load(QUrl(QStringLiteral("qrc:/spotify/page.html")));
        connect(m_view, &QWebEngineView::loadFinished, this,
                [this](bool ok) {
                    if (!ok) {
                        sendEvent(
                            {{QStringLiteral("type"), QStringLiteral("error")},
                             {QStringLiteral("message"),
                              QStringLiteral("the SDK page failed to load"),
                             },
                            });
                        return;
                    }
                    // The page's SDK asks for a token on connect (and on
                    // expiry) through the bridge; prime it with the
                    // environment's token, so the first request is
                    // answered without waiting for a push.
                    m_bridge->setToken(qEnvironmentVariable("SPOTIFY_TOKEN"));
                });
    }

private slots:
    void onStdin()
    {
        m_ipcBuffer.append(readStdinAvailable());
        qsizetype newline = m_ipcBuffer.indexOf('\n');
        while (newline >= 0) {
            const QByteArray line = m_ipcBuffer.left(newline).trimmed();
            m_ipcBuffer.remove(0, newline + 1);
            if (!line.isEmpty())
                handleCommand(QJsonDocument::fromJson(line).object());
            newline = m_ipcBuffer.indexOf('\n');
        }
    }

private:
    void handleCommand(const QJsonObject &command)
    {
        const QString what = command.value("cmd").toString();
        if (what == QLatin1String("play")) {
            m_bridge->play(command.value("uri").toString(),
                           command.value("position")
                               .toVariant()
                               .toLongLong());
        } else if (what == QLatin1String("pause")) {
            runJs(QStringLiteral("window.spotifyPause()"));
        } else if (what == QLatin1String("resume")) {
            runJs(QStringLiteral("window.spotifyResume()"));
        } else if (what == QLatin1String("seek")) {
            runJs(QStringLiteral("window.spotifySeek(%1)")
                      .arg(command.value("position")
                               .toVariant()
                               .toLongLong()));
        } else if (what == QLatin1String("volume")) {
            runJs(QStringLiteral("window.spotifySetVolume(%1)")
                      .arg(command.value("value").toDouble(), 0, 'f', 4));
        } else if (what == QLatin1String("connect")) {
            runJs(QStringLiteral("window.spotifyConnect()"));
        } else if (what == QLatin1String("disconnect")) {
            runJs(QStringLiteral("window.spotifyDisconnect()"));
        } else if (what == QLatin1String("token")) {
            m_bridge->setToken(command.value("token").toString());
        } else if (what == QLatin1String("quit")) {
            close();
        }
    }
    void runJs(const QString &code)
    {
        m_page->runJavaScript(code);
    }

    QLabel *m_status;
    QWebEngineView *m_view;
    ConsolePage *m_page;
    SpotifyWebApi *m_api;
    WebBridge *m_bridge;
    QByteArray m_ipcBuffer;
};
} // namespace

int main(int argc, char **argv)
{
    // The SDK's AudioContext is created with no user gesture (every
    // control is in the main process), so the browser's default
    // autoplay policy would keep it suspended and the device would stay
    // silent. HardwareMediaKeyHandling is off because its listener would
    // make Chromium register a second MPRIS player ("doob-spotify-web",
    // org.mpris.MediaPlayer2.chromium.instance<Pid>) on top of doob's
    // own — doob is the player and owns the media keys (via its MPRIS
    // registration), the helper must stay off the bus. The env var is
    // read before QApplication (Chromium is not started yet).
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
            "--autoplay-policy=no-user-gesture-required "
            "--disable-features=HardwareMediaKeyHandling");
    QCoreApplication::setOrganizationName("doob");
    QCoreApplication::setApplicationName("doob-spotify-web");
    QApplication app(argc, argv);
    WebWindow window;
    // The window exists only to host the SDK's page: the WebEngine
    // starts its render process only once the window has been shown
    // for real, so the hide waits for the first event-loop pass (a
    // synchronous show()+hide() would never map the window and the
    // page would stay suspended) — the device is never meant to be
    // seen.
    window.show();
    QTimer::singleShot(0, &window, &QWidget::hide);
    return QApplication::exec();
}

#include "spotifyweb.moc"
