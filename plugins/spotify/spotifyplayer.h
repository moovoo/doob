#ifndef SPOTIFYPLAYER_H
#define SPOTIFYPLAYER_H

#include <QJsonDocument>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>

#include <doob/audioplayer.h>
#include <doob/dspchain.h>

#include <functional>

#include "capturepump.h"
#include "privatepulse.h"
#include "spotifyipc.h"
#include "systemrelay.h"

class DspChain;
class SpotifyAuth;

// The Spotify playback driver (the AudioPlayer the provider switches in).
//
// The audio path: the Web Playback SDK (in the spotify-web helper
// process) plays into the plugin's private PulseAudio server's null
// sink (the in-process pulsestub); the server's sink hands the PCM to
// the capture's ring (the back-pressure paces the engine), a
// self-paced worker thread (CapturePump — it owns the real-time
// clock, padding gaps with silence) reads the ring the same way the
// FFmpeg driver's output pulls its ring, the core's DSP chain runs
// over the frames, they are tapped (the visualization sees what is
// heard), and relayed to the user's default sink (SystemSinkRelay —
// a blocking pa_simple write paces the capture at real time). The
// position and duration come from the SDK's own reports (the
// helper's 1 s poll), not from frame counts.
//
// Everything is lazy: the private server, the helper and the capture
// start on the first play (the provider's resolvePlayback runs
// ensureReady() before it hands the item over), and stay up afterwards
// (the next play is instant). A stop is a release, not a teardown: it
// closes the SDK's session with Spotify (the device drops out of
// Spotify Connect — while doob is idle, no other client can play
// through it, and Spotify does not see doob's queue), the helper and
// the audio path stay warm, and the next play re-registers the device
// (a connect — its device id is new on every registration, and the
// play names it). Nothing is torn down until the sign-in changes, the
// helper dies, or the driver is deleted (the plugin's shutdown).
//
// Track changes: the SDK auto-advances within its context (an album, a
// playlist) — the driver hands the advance back to the playlist when it
// sees the position jump backwards (the next track of the context), and
// doob's normal advance re-plays the row through the driver. A track
// played through to its end is handed back the same way: doob plays
// single tracks (the SDK's context is empty), so the engine has nothing
// to advance to — it pauses at the track's reset position, and the
// driver reads that as the end (finished(), not paused()).
class SpotifyPlayer : public AudioPlayer
{
    Q_OBJECT
public:
    // auth: the plugin's sign-in state (non-owning; a QPointer, so a
    // driver outliving its plugin — one the PlaybackManager created and
    // owns — degrades to "not signed in" instead of dangling).
    // dataDir: the plugin's data directory (the private server's
    // per-launch socket directories land under <dataDir>/pulse).
    SpotifyPlayer(SpotifyAuth *auth, QString dataDir,
                  QObject *parent = nullptr);
    ~SpotifyPlayer() override;

    void play(const MediaItem &item) override;
    void pause() override;
    void resume() override;
    void stop() override;
    qint64 positionMs() const override;
    qint64 durationMs() const override;
    void setPositionMs(qint64 positionMs) override;
    bool supportsVolume() const override;
    // The SDK's setVolume has no boost: a value above 1.0 is capped.
    void setVolume(float volume) override;

    // Brings the device up (the private server, the helper, the
    // capture), done() exactly once on the GUI thread (an empty error
    // on success). Concurrent callers park on the same start.
    using ReadyCallback =
        std::function<void(bool ok, const QString &error)>;
    void ensureReady(const ReadyCallback &done);
    bool isReady() const { return m_ready; }

protected:
    // Test seams (a test subclass swaps in a fake helper process and a
    // fake private server — no daemon, no WebEngine, no system
    // interference).
    virtual QString helperPath() const;
    virtual QStringList helperArguments() const;
    virtual bool startPrivateServer();
    virtual QString privateServerString() const;
    // Feeds the driver a helper state event (the same path the IPC's
    // state signal takes) — a remote transport change without a
    // command round-trip.
    void feedState(const QJsonObject &state) { onHelperState(state); }
    // Feeds the driver a helper position event (the same path the IPC's
    // position signal takes) — a stale report of a superseded track.
    void feedPosition(qint64 positionMs, qint64 durationMs)
    {
        onHelperPosition(positionMs, durationMs);
    }
    // Feeds the driver a helper error event (the same path the IPC's
    // playerError signal takes) — an engine-reported failure.
    void feedError(const QJsonObject &error) { onHelperError(error); }
    // Feeds the driver the connect's result (the same path the IPC's
    // connectResult signal takes) — a refused (re)connect.
    void feedConnectResult(bool ok) { onHelperConnectResult(ok); }
    // The recovery window (the error's grace period) in milliseconds.
    void setRecoveryWindowMs(int ms) { m_recoveryWatch.setInterval(ms); }

private:
    void startHelper();
    void failStart(const QString &error);
    void fulfillPending(bool ok, const QString &error);
    void startCapture();
    void stopCapture();
    void sendCommand(const QJsonObject &command);
    // Sends the play command and arms the driver's playback state for
    // the item (the manager's play() and the connect's ready both
    // land here — the state setup lives in one place).
    void sendPlay(const QString &uri);
    void updatePosition(qint64 positionMs, qint64 durationMs,
                        bool suppressFinish = false,
                        const QString &title = QString());
    // The device's teardown (a sign-out, a re-login, a dead capture, a
    // dead helper): the next play rebuilds the daemon, the capture and
    // the helper together.
    void teardownDevice();
    // The running helper's token is the one from its start: a refresh
    // of the same account is pushed to it (the token command), so the
    // SDK's re-ask on expiry and the helper's Web API calls see the
    // current token.
    void pushCurrentToken();

private slots:
    void onAuthChanged();
    void onAccountChanged();
    void onHelperReady(const QString &deviceId);
    void onHelperNotReady();
    void onHelperState(const QJsonObject &state);
    void onHelperPosition(qint64 positionMs, qint64 durationMs);
    void onHelperError(const QJsonObject &error);
    void onHelperPlayResult(bool ok, const QString &error);
    void onHelperConnectResult(bool ok);
    void onHelperExited(int exitCode);
    void onWatchdog();
    // The recovery window's expiry: the engine did not recover from the
    // last (transient) error — the playback is surfaced as a failure.
    void onRecoveryWatchdog();
    // The capture thread (a direct connection): the relay's blocking
    // write must pace the capture loop there.
    void onChunk(const QByteArray &frames, int frameCount);

private:
    // The device's start is refused while this long: a slow helper
    // (a cold WebEngine) is a failure with a message, not a hang.
    static constexpr int kReadyTimeoutMs = 30000;
    // The window a transient engine error (an auth hiccup, a startup
    // playback race) gets to recover: a position report in the window
    // cancels the stop, its expiry surfaces the playback as a failure.
    static constexpr int kRecoveryWindowMs = 5000;
    // The track's end, as the engine reports it: the track played
    // through (the last position report within this of the duration —
    // the PROGRESS cadence is well inside it) and the engine paused at
    // the track's reset position (this close to zero — a pause keeps
    // its position, which is what tells an end from a pause).
    static constexpr int kEndOfTrackToleranceMs = 5000;
    static constexpr int kEndResetToleranceMs = 1000;

    QPointer<SpotifyAuth> m_auth;
    QString m_dataDir;
    PrivatePulse m_pulse{this};
    SpotifyIpc m_ipc{this};
    CapturePump *m_capture = nullptr;
    SystemSinkRelay m_relay;
    DspChain m_chain{this};

    // The helper's start (the GUI thread).
    bool m_ready = false;
    bool m_starting = false;
    // The SDK's session with Spotify is open (the device is
    // registered in Connect): a stop closes it (a disconnect), the
    // next play reopens it (a connect — the device id is new on every
    // one).
    bool m_connected = false;
    QString m_deviceId;
    QString m_token;
    QList<ReadyCallback> m_pending;
    QTimer m_watchdog;
    QTimer m_recoveryWatch;

    // The playback state (the GUI thread).
    QString m_requested;
    // The play a connect is in flight for (its source's uri): the
    // device is not registered (stopped, dropped out) and the play
    // must wait for the connect's ready (which carries the new device
    // id the play names). Non-empty = a connect is in flight for a
    // play.
    QString m_pendingPlay;
    qint64 m_position = 0;
    qint64 m_duration = 0;
    float m_volume = 1.0F;
    // A commanded seek is in flight: the position's next backward jump
    // is the seek's, not a track change.
    bool m_seekPending = false;
    // A commanded play's start is expected: until the new item's state
    // lands, position jumps are the outgoing track's last (stale)
    // reports plus the new track's reset — not an auto-advance.
    bool m_playStartPending = false;
    // The SDK's transport state as last reported (a remote pause/resume
    // is echoed to the manager as the transition out of it).
    bool m_sdkPaused = false;
    // The last state event's track title (the SDK's report): a backward
    // jump to the same title is the engine restarting the track (a
    // reconnect after an auth hiccup) — not an auto-advance.
    QString m_trackTitle;
    // A backward jump seen in a title-less position report: the state
    // event that follows a real track change (which carries the title)
    // decides whether it was a new track.
    bool m_jumpPending = false;
    // The last transient engine error's message (the recovery window's
    // expiry surfaces it when the engine did not recover).
    QString m_lastError;

    // The capture/relay (the capture thread; the GUI only after the
    // capture thread has been joined by stopCapture).
    bool m_relayOpen = false;
    QVector<float> m_floatBuf;
};

#endif // SPOTIFYPLAYER_H
