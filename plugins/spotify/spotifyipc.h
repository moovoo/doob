#ifndef SPOTIFYIPC_H
#define SPOTIFYIPC_H

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QProcessEnvironment>
#include <QStringList>

class QProcess;

// The JSON-lines channel to the spotify-web helper (one JSON object per
// line over the process's stdin and stdout).
//
// Main -> helper are commands: {"cmd": "play" | "pause" | "resume" |
// "seek" | "volume" | "token" | "connect" | "disconnect" | "quit",
// ...}. Helper -> main are events: {"type": "ready" | "notready" |
// "state" | "position" | "error" | "log" | "play-result" |
// "connect-result", ...} — "type" is the line's kind; the page's own
// tag on an "error" line (which SDK listener fired) rides along as
// "listener".
//
// Everything runs on the GUI thread (the process's events do); the
// helper's stderr is forwarded to the parent's stderr (the WebEngine
// and the page's own console are visible there for debugging).
class SpotifyIpc : public QObject
{
    Q_OBJECT
public:
    explicit SpotifyIpc(QObject *parent = nullptr);
    ~SpotifyIpc() override;

    // Starts the helper (the environment carries PULSE_SERVER and
    // SPOTIFY_TOKEN — they must be in place before the helper's
    // WebEngine process tree comes up). False when the process does
    // not start.
    bool start(const QString &program, const QStringList &arguments,
               const QProcessEnvironment &environment);
    // Asks for a clean exit (the "quit" command), then terminates and
    // kills whatever is left. Safe to call twice.
    void stop();
    bool isRunning() const;

    // A command line (a no-op when the helper is not running).
    void send(const QJsonObject &command);

signals:
    // The SDK's device is up (the helper can take play commands).
    void ready(const QString &deviceId);
    void notReady();
    // The SDK's player_state_changed (empty object = playback stopped).
    void state(const QJsonObject &state);
    // The SDK's periodic position (the 1 s poll, while playing).
    void position(qint64 positionMs, qint64 durationMs);
    // The SDK's playback/initialization/autoplay failures.
    void playerError(const QJsonObject &error);
    // Anything the helper logs (the page's console, token events).
    void logLine(const QString &line);
    // The Web API's play command answered.
    void playResult(bool ok, const QString &error);
    // The SDK's connect() answered (a connect can fail — a bad token,
    // no network).
    void connectResult(bool ok);
    // The helper process exited on its own (a quit command is a clean
    // exit, not this signal's concern — stop() swallows it).
    void helperExited(int exitCode);

private slots:
    void onStdout();
    void onExited(int exitCode, QProcess::ExitStatus status);

private:
    void handleEvent(const QJsonObject &event);

    QProcess *m_process = nullptr;
    QByteArray m_buffer;
    // A stop() in flight: its exit is the clean exit, not a crash.
    bool m_stopping = false;
};

#endif // SPOTIFYIPC_H
