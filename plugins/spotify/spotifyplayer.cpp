#include "spotifyplayer.h"

#include "audiotap.h"
#include "spotifyauth.h"
#include "spotifyprovider.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>

#include <cstring>

SpotifyPlayer::SpotifyPlayer(SpotifyAuth *auth, QString dataDir,
                             QObject *parent)
    : AudioPlayer(parent)
    , m_auth(auth)
    , m_dataDir(std::move(dataDir))
{
    m_pulse.setBaseDir(m_dataDir + QStringLiteral("/pulse"));
    connect(&m_ipc, &SpotifyIpc::ready, this, &SpotifyPlayer::onHelperReady);
    connect(&m_ipc, &SpotifyIpc::notReady, this,
            &SpotifyPlayer::onHelperNotReady);
    connect(&m_ipc, &SpotifyIpc::state, this, &SpotifyPlayer::onHelperState);
    // The helper's log (the page's console, the SDK's messages): the
    // operational diagnostics of a remote process, kept in doob's log.
    connect(&m_ipc, &SpotifyIpc::logLine, this,
            [](const QString &line) {
                qWarning("spotify: %s", qPrintable(line));
            });
    connect(&m_ipc, &SpotifyIpc::position, this,
            &SpotifyPlayer::onHelperPosition);
    connect(&m_ipc, &SpotifyIpc::playerError, this,
            &SpotifyPlayer::onHelperError);
    connect(&m_ipc, &SpotifyIpc::playResult, this,
            &SpotifyPlayer::onHelperPlayResult);
    connect(&m_ipc, &SpotifyIpc::connectResult, this,
            &SpotifyPlayer::onHelperConnectResult);
    connect(&m_ipc, &SpotifyIpc::helperExited, this,
            &SpotifyPlayer::onHelperExited);
    // The sign-in's changes (a login, a logout, a client-id change) and
    // the token refreshes of the same account: the running helper's
    // token is the one from its start, so a refresh is pushed to it and
    // a changed sign-in tears the device down (it belongs to the
    // previous account).
    if (!m_auth.isNull()) {
        connect(m_auth, &SpotifyAuth::authChanged, this,
                &SpotifyPlayer::onAuthChanged);
        connect(m_auth, &SpotifyAuth::accountChanged, this,
                &SpotifyPlayer::onAccountChanged);
    }
    m_watchdog.setSingleShot(true);
    m_watchdog.setInterval(kReadyTimeoutMs);
    connect(&m_watchdog, &QTimer::timeout, this, &SpotifyPlayer::onWatchdog);
    m_recoveryWatch.setSingleShot(true);
    m_recoveryWatch.setInterval(kRecoveryWindowMs);
    connect(&m_recoveryWatch, &QTimer::timeout, this,
            &SpotifyPlayer::onRecoveryWatchdog);
    // The tap signals cross the capture thread -> GUI boundary by
    // queued delivery, which serializes the waveform payload: the type
    // must be a registered metatype for the hop to work.
    qRegisterMetaType<QVector<float>>();
}

SpotifyPlayer::~SpotifyPlayer()
{
    stopCapture();
    m_ipc.stop();
    m_pulse.stop();
}

// --- the device's lifecycle --------------------------------------------

void SpotifyPlayer::ensureReady(const ReadyCallback &done)
{
    if (m_ready) {
        qInfo("spotify: ensureReady: the device is warm, revalidating the token");
        // The device is warm: the token may have lapsed while it idled
        // (the refresh-ahead's margin, a play before it fires) — the
        // refresh (and the push to the running helper it triggers)
        // must land before the play the caller sends next.
        if (m_auth.isNull()
            || m_auth->state() != SpotifyAuth::State::SignedIn) {
            done(false, SpotifyProvider::authHintText());
            return;
        }
        m_auth->validToken([this, done](const SpotifyAuth::Token &token,
                                        const QString &error) {
            if (!error.isEmpty()) {
                qWarning("spotify: warm start failed: %s", qPrintable(error));
                done(false, error);
                return;
            }
            // The token the helper carries (a refresh has pushed it,
            // the unrefreshed one is the start's).
            m_token = token.accessToken;
            qInfo("spotify: warm start ok (the token expires at %s)",
                  qPrintable(token.expiresAt.toString(Qt::ISODate)));
            done(true, QString());
        });
        return;
    }
    if (m_starting) {
        m_pending.append(done);
        return;
    }
    m_starting = true;
    m_pending.append(done);
    if (m_auth.isNull() || m_auth->state() != SpotifyAuth::State::SignedIn) {
        failStart(SpotifyProvider::authHintText());
        return;
    }
    qInfo("spotify: starting the device");
    // The helper needs a current access token (the SDK initializes
    // with it). A refresh of the same account is pushed to the running
    // helper by onAuthChanged; a re-login tears it down, so the next
    // start is here, with the new account's token.
    m_auth->validToken([this](const SpotifyAuth::Token &token,
                              const QString &error) {
        if (!error.isEmpty()) {
            failStart(error);
            return;
        }
        // A login may have landed while the refresh was in flight (the
        // callback answers with the previous account's token): this
        // start is stale — the next play takes the new account.
        if (!m_starting
            || token.accessToken != m_auth->token().accessToken) {
            failStart(QStringLiteral("the sign-in changed during the start"));
            return;
        }
        m_token = token.accessToken;
        if (!startPrivateServer()) {
            failStart(QStringLiteral(
                "The private PulseAudio server would not start"));
            return;
        }
        startCapture();
        startHelper();
        m_watchdog.start();
    });
}

void SpotifyPlayer::startHelper()
{
    QProcessEnvironment environment =
        QProcessEnvironment::systemEnvironment();
    // The helper's whole process tree (the WebEngine's children) must
    // hear the private server: the environment is the only way to get
    // it there before they fork.
    environment.insert(QStringLiteral("PULSE_SERVER"),
                        privateServerString());
    environment.insert(QStringLiteral("SPOTIFY_TOKEN"), m_token);
    if (!m_ipc.start(helperPath(), helperArguments(), environment))
        failStart(QStringLiteral("The Spotify helper would not start"));
}

void SpotifyPlayer::failStart(const QString &error)
{
    qWarning("spotify: the device start failed: %s", qPrintable(error));
    m_watchdog.stop();
    stopCapture();
    m_pulse.stop();
    fulfillPending(false, error);
}

void SpotifyPlayer::fulfillPending(bool ok, const QString &error)
{
    m_starting = false;
    m_ready = ok;
    auto pending = std::move(m_pending);
    m_pending.clear();
    for (const ReadyCallback &call : pending)
        call(ok, error);
}

void SpotifyPlayer::teardownDevice()
{
    qWarning("spotify: the device was torn down");
    m_ready = false;
    m_connected = false;
    m_deviceId.clear();
    m_position = 0;
    m_duration = 0;
    m_trackTitle.clear();
    m_jumpPending = false;
    m_recoveryWatch.stop();
    stopCapture();
    m_ipc.stop();
    m_pulse.stop();
    // A connect in flight for a play: its helper is gone — the play
    // fails (the manager stops).
    if (!m_pendingPlay.isEmpty()) {
        m_pendingPlay.clear();
        emit errorOccurred(QStringLiteral(
            "The Spotify device went down while starting playback"));
    }
}

void SpotifyPlayer::onWatchdog()
{
    if (m_starting && !m_ready)
        failStart(QStringLiteral("The Spotify device did not come up"));
}

void SpotifyPlayer::onHelperReady(const QString &deviceId)
{
    m_deviceId = deviceId;
    m_connected = true;
    m_watchdog.stop();
    qInfo("spotify: the device is ready (%s)", qPrintable(deviceId));
    if (m_starting) {
        fulfillPending(true, QString());
    } else {
        // The helper came up after the start was already given up on
        // (a slow WebEngine): the device is usable from the next play.
        m_ready = true;
    }
    // A fresh device starts from the user's level.
    sendCommand({{QStringLiteral("cmd"), QStringLiteral("volume")},
                 {QStringLiteral("value"), m_volume},
                });
    // The play a connect was made for: the device id is the new
    // one's, and the play names it.
    if (!m_pendingPlay.isEmpty()) {
        const QString uri = m_pendingPlay;
        m_pendingPlay.clear();
        sendPlay(uri);
    }
}

void SpotifyPlayer::onHelperNotReady()
{
    m_deviceId.clear();
    m_connected = false;
    qWarning("spotify: the device dropped out (it reconnects on its own)");
}

void SpotifyPlayer::onHelperExited(int exitCode)
{
    Q_UNUSED(exitCode);
    qWarning("spotify: the helper process exited on its own");
    teardownDevice();
    if (m_starting)
        failStart(QStringLiteral("The Spotify helper exited during start"));
    if (!m_requested.isEmpty())
        emit errorOccurred(QStringLiteral(
            "The Spotify helper process exited"));
}

// --- the sign-in's changes ----------------------------------------------

void SpotifyPlayer::onAuthChanged()
{
    if (m_auth.isNull())
        return;
    if (m_auth->state() != SpotifyAuth::State::SignedIn) {
        // Signed out (a logout, a failed refresh, a client-id change):
        // the device's token is gone — the device is torn down, and
        // the next play is refused with the sign-in hint while there
        // is no account.
        if (m_ready || m_ipc.isRunning() || m_starting)
            teardownDevice();
        if (m_starting)
            failStart(SpotifyProvider::authHintText());
        if (!m_requested.isEmpty())
            // A sign-out mid-playback: the audio just stopped — the
            // manager follows (a silent stop would read as a track
            // end).
            emit errorOccurred(QStringLiteral(
                "Signed out of Spotify — playback stopped"));
        return;
    }
    // A token refresh of the same account: the running helper's copy
    // (the one from its start) is the token that just expired — hand
    // it the current one.
    if (m_ipc.isRunning())
        pushCurrentToken();
}

void SpotifyPlayer::onAccountChanged()
{
    // A new sign-in: the running device is registered on the previous
    // account's token — tear it down, the next play brings it up
    // fresh.
    if (m_ready || m_ipc.isRunning())
        teardownDevice();
    if (m_starting)
        failStart(QStringLiteral("the sign-in changed during the start"));
    if (!m_requested.isEmpty())
        emit errorOccurred(QStringLiteral(
            "The Spotify account changed — playback stopped"));
}

void SpotifyPlayer::pushCurrentToken()
{
    m_auth->validToken([this](const SpotifyAuth::Token &token,
                              const QString &error) {
        // A sign-out may have landed in the meantime (the callback's
        // token is then the old account's, and the device is torn
        // down by onAuthChanged).
        if (!error.isEmpty()) {
            qInfo("spotify: token push skipped (%s)", qPrintable(error));
            return;
        }
        if (m_auth.isNull()
            || m_auth->state() != SpotifyAuth::State::SignedIn) {
            qInfo("spotify: token push skipped (no longer signed in)");
            return;
        }
        if (token.accessToken == m_token) {
            qInfo("spotify: token push skipped (the helper already carries it)");
            return;
        }
        m_token = token.accessToken;
        qInfo("spotify: pushing a refreshed token to the running helper");
        sendCommand({{QStringLiteral("cmd"), QStringLiteral("token")},
                     {QStringLiteral("token"), m_token},
                    });
    });
}

// --- the transport ------------------------------------------------------

void SpotifyPlayer::play(const MediaItem &item)
{
    if (m_auth.isNull() || m_auth->state() != SpotifyAuth::State::SignedIn) {
        emit errorOccurred(SpotifyProvider::authHintText());
        return;
    }
    if (!m_ready) {
        // The provider's resolvePlayback runs ensureReady() first, so
        // this is a direct driver use without the device (or a start
        // that already failed with a message the user saw).
        emit errorOccurred(QStringLiteral(
            "The Spotify device is not ready (is the helper installed "
            "next to doob?)"));
        return;
    }
    if (m_connected) {
        sendPlay(item.source.toString());
        return;
    }
    // The device is not registered (it was stopped, or it dropped
    // out): register it first — the play names the device id, and
    // that is new on every connect. The play is flushed by the
    // connect's ready (onHelperReady). A connect already in flight
    // for a play only gets its item replaced.
    if (m_pendingPlay.isEmpty()) {
        m_pendingPlay = item.source.toString();
        qInfo("spotify: play %s (the device is not registered — connecting first)",
              qPrintable(m_pendingPlay));
        sendCommand({{QStringLiteral("cmd"), QStringLiteral("connect")}});
    } else {
        m_pendingPlay = item.source.toString();
    }
}

void SpotifyPlayer::sendPlay(const QString &uri)
{
    qInfo("spotify: play %s (the requested was %s)",
          qPrintable(uri), qPrintable(m_requested));
    m_requested = uri;
    m_seekPending = false;
    m_playStartPending = true;
    m_jumpPending = false;
    m_position = 0;
    sendCommand({{QStringLiteral("cmd"), QStringLiteral("play")},
                 {QStringLiteral("uri"), uri},
                 {QStringLiteral("position"), 0},
                });
}

void SpotifyPlayer::pause()
{
    qInfo("spotify: pause() was called");
    sendCommand({{QStringLiteral("cmd"), QStringLiteral("pause")}});
}

void SpotifyPlayer::resume()
{
    sendCommand({{QStringLiteral("cmd"), QStringLiteral("resume")}});
}

void SpotifyPlayer::stop()
{
    qInfo("spotify: stop() was called (the requested was %s)",
          qPrintable(m_requested));
    // A stop is a release (the AudioPlayer's contract): the SDK's
    // session closes — the device drops out of Spotify Connect, so
    // while doob is idle no other client (the native app, a phone) can
    // play through it, and Spotify does not see doob's queue. The
    // helper, the private server and the capture stay warm; the next
    // play re-registers the device (a connect, a new device id).
    sendCommand({{QStringLiteral("cmd"), QStringLiteral("disconnect")}});
    m_connected = false;
    m_requested.clear();
    m_playStartPending = false;
    m_jumpPending = false;
    m_recoveryWatch.stop();
    m_position = 0;
    m_duration = 0;
    m_seekPending = false;
}

qint64 SpotifyPlayer::positionMs() const
{
    return m_position;
}

qint64 SpotifyPlayer::durationMs() const
{
    return m_duration;
}

void SpotifyPlayer::setPositionMs(qint64 positionMs)
{
    if (m_duration > 0)
        positionMs = qBound<qint64>(0, positionMs, m_duration - 1);
    m_seekPending = true;
    m_jumpPending = false;
    m_position = positionMs;
    emit positionChanged(positionMs);
    sendCommand({{QStringLiteral("cmd"), QStringLiteral("seek")},
                 {QStringLiteral("position"), positionMs},
                });
}

bool SpotifyPlayer::supportsVolume() const
{
    return true;
}

void SpotifyPlayer::setVolume(float volume)
{
    m_volume = qBound(0.0F, volume, 1.0F);
    sendCommand({{QStringLiteral("cmd"), QStringLiteral("volume")},
                 {QStringLiteral("value"), m_volume},
                });
}

void SpotifyPlayer::sendCommand(const QJsonObject &command)
{
    qInfo("spotify: cmd %s",
          qPrintable(QJsonDocument(command).toJson(QJsonDocument::Compact)));
    if (m_ready && m_ipc.isRunning()) {
        m_ipc.send(command);
        return;
    }
    qWarning("spotify: cmd dropped (ready=%d, the helper is not running): %s",
             m_ready ? 1 : 0,
             qPrintable(QJsonDocument(command).toJson(QJsonDocument::Compact)));
}

// --- the SDK's reports --------------------------------------------------

void SpotifyPlayer::onHelperState(const QJsonObject &state)
{
    if (state.value(QStringLiteral("stopped")).toBool()) {
        qInfo("spotify: state: stopped (the requested was %s)",
              qPrintable(m_requested));
        m_sdkPaused = false;
        m_playStartPending = false;
        m_jumpPending = false;
        m_position = 0;
        m_duration = 0;
        emit durationChanged(0);
        if (!m_requested.isEmpty()) {
            // A stop outside of doob (another client of the backend —
            // the native app stopped the queue or took the device over):
            // the manager follows. doob's own stop() clears
            // m_requested first, so it is not echoed back to itself.
            qInfo("spotify: emitting stopped() (a remote stop)");
            emit stopped();
        }
        return;
    }
    const bool sdkPaused = state.value(QStringLiteral("paused")).toBool();
    const qint64 position =
        state.value(QStringLiteral("position")).toVariant().toLongLong();
    const qint64 duration =
        state.value(QStringLiteral("duration")).toVariant().toLongLong();
    // The engine repeats the end's state event a few times (and reports
    // the idle device at zero before the first play): the driver has no
    // active track and no position — a transition would emit a stale
    // paused(), the reports would re-anchor the old track's duration.
    if (m_requested.isEmpty() && sdkPaused && m_position == 0
        && position < kEndResetToleranceMs) {
        qInfo("spotify: state: ignored (no active track, the device is idle at zero)");
        return;
    }
    // The state event that follows a commanded play is the new item's
    // start: its position reset is not an auto-advance (and the
    // outgoing track's last, stale reports must not have re-armed the
    // jump detector in the meantime).
    const bool suppress = m_playStartPending;
    m_playStartPending = false;
    qInfo("spotify: state: paused=%d pos=%lld dur=%lld title=%s (suppress=%d)",
          sdkPaused ? 1 : 0,
          static_cast<long long>(position),
          static_cast<long long>(duration),
          qPrintable(state.value(QStringLiteral("title")).toString()),
          suppress ? 1 : 0);
    // The track's end: doob plays single tracks (the SDK's context is
    // empty — the engine has nothing to advance to), so it plays the
    // track through and pauses at the track's reset position. The
    // reset (the position at zero, the last report near the end) tells
    // an end from a pause (which keeps its position) and from the
    // same-title restart after an auth hiccup (which keeps playing):
    // the advance goes back to the playlist as finished(), which
    // re-plays the next row through the driver.
    const bool trackEnd = sdkPaused && !m_sdkPaused && !suppress
        && m_duration > 0
        && m_position >= m_duration - kEndOfTrackToleranceMs
        && (position < kEndResetToleranceMs || position >= m_duration);
    if (trackEnd) {
        qInfo("spotify: the track reached its end (pos %lld of %lld) — emitting finished()",
              static_cast<long long>(m_position),
              static_cast<long long>(m_duration));
        // The driver's state is cleared like a stop (the engine is
        // already paused at the end — no command is sent); the manager
        // advances (or stops at the playlist's end) and the next play
        // re-anchors the device.
        m_sdkPaused = true;
        m_requested.clear();
        m_jumpPending = false;
        m_recoveryWatch.stop();
        m_position = 0;
        m_duration = 0;
        m_seekPending = false;
        m_trackTitle.clear();
        emit finished();
        return;
    }
    if (sdkPaused != m_sdkPaused) {
        m_sdkPaused = sdkPaused;
        // A remote transport change (the native Spotify app): the
        // manager's state follows. doob's own pause/resume land here
        // too, as a no-op (the manager is already in that state).
        if (sdkPaused) {
            qInfo("spotify: emitting paused()");
            emit paused();
        } else {
            qInfo("spotify: emitting resumed()");
            emit resumed();
        }
    }
    updatePosition(position, duration, suppress,
                   state.value(QStringLiteral("title")).toString());
}

void SpotifyPlayer::onHelperPosition(qint64 positionMs, qint64 durationMs)
{
    // A stale report of the outgoing track can land after a commanded
    // play, before the new item's state: it must not count as a
    // backward jump.
    updatePosition(positionMs, durationMs, m_playStartPending);
}

void SpotifyPlayer::updatePosition(qint64 positionMs, qint64 durationMs,
                                   bool suppressFinish, const QString &title)
{
    // A position report means the engine is alive (and its token is
    // working): a pending transient error has recovered — cancel the
    // stop its recovery window was arming.
    if (m_recoveryWatch.isActive()) {
        m_recoveryWatch.stop();
        qInfo("spotify: a position report cancelled the recovery window");
    }
    // A backward jump (that no commanded seek or play start accounts
    // for) is the SDK having auto-advanced to the next track of its
    // context: hand the advance back to the playlist (doob re-plays the
    // next row through the driver, which re-anchors the context on that
    // track). A jump to the same track's title is the engine restarting
    // it (a reconnect after an auth hiccup) — not an advance.
    const bool backwardJump = !m_seekPending && !suppressFinish
        && m_position > 0 && positionMs < m_position - 1000;
    if (backwardJump) {
        if (title.isEmpty()) {
            // A position report carries no title: the state event that
            // follows a real track change decides (by its title).
            m_jumpPending = true;
            qInfo("spotify: a backward jump without a title (pos %lld -> %lld) — the next state's title decides",
                  static_cast<long long>(m_position),
                  static_cast<long long>(positionMs));
        } else {
            m_jumpPending = false;
            if (m_trackTitle.isEmpty() || title != m_trackTitle) {
                qInfo("spotify: auto-advance (pos %lld -> %lld): %s -> %s",
                      static_cast<long long>(m_position),
                      static_cast<long long>(positionMs),
                      qPrintable(m_trackTitle), qPrintable(title));
                emit finished();
            }
            m_trackTitle = title;
        }
    } else if (m_jumpPending && !title.isEmpty()) {
        // The jump a position report saw first lands in this state
        // event: its title decides (a same-track restart is not an
        // auto-advance).
        m_jumpPending = false;
        if (m_trackTitle.isEmpty() || title != m_trackTitle) {
            qInfo("spotify: auto-advance (pos %lld -> %lld, the title-less jump): %s -> %s",
                  static_cast<long long>(m_position),
                  static_cast<long long>(positionMs),
                  qPrintable(m_trackTitle), qPrintable(title));
            emit finished();
        }
        m_trackTitle = title;
    } else if (!title.isEmpty()) {
        m_trackTitle = title;
    }
    m_seekPending = false;
    m_position = positionMs;
    emit positionChanged(positionMs);
    if (durationMs > 0 && durationMs != m_duration) {
        m_duration = durationMs;
        emit durationChanged(durationMs);
    }
}

void SpotifyPlayer::onHelperError(const QJsonObject &error)
{
    const QString message =
        error.value(QStringLiteral("message")).toString();
    qWarning("spotify: player error: %s", qPrintable(message));
    // A broken device brings no new start: the flag must not outlive
    // it (a later, real auto-advance would be swallowed).
    m_playStartPending = false;
    // The page's listener tag, carried by the transport as "listener"
    // (the envelope's "type" is the line's kind — always "error").
    const QString type =
        error.value(QStringLiteral("listener")).toString();
    // The transient errors (an auth hiccup, a startup playback race —
    // the log's "Invalid token scopes." / "no list was loaded."): the
    // engine retries on its own (re-asking for a token, re-loading the
    // list) and recovers while the audio starts — a stop would kill a
    // playback that is working. The token is re-pushed (the engine's
    // next GET_TOKEN picks it up) and the error gets a recovery window
    // (a position report in it cancels the stop). The rest (an
    // initialization, an account, an autoplay block) is fatal at once.
    if (type == QLatin1String("authentication")
        || type == QLatin1String("playback")) {
        qInfo("spotify: player error of type %s is transient — re-pushing the token, arming the recovery window",
              qPrintable(type));
        if (!m_auth.isNull())
            pushCurrentToken();
        m_lastError = message;
        m_recoveryWatch.start();
        return;
    }
    qWarning("spotify: player error of type %s is fatal — emitting errorOccurred",
             qPrintable(type));
    emit errorOccurred(message);
}

void SpotifyPlayer::onRecoveryWatchdog()
{
    // The window expired with no position report: the engine did not
    // recover from the last transient error — the playback is broken
    // (the manager stops, the status bar shows the reason).
    if (!m_requested.isEmpty()) {
        qWarning("spotify: the recovery window expired without a position report — surfacing %s",
                 qPrintable(m_lastError));
        emit errorOccurred(m_lastError);
    }
}

void SpotifyPlayer::onHelperPlayResult(bool ok, const QString &error)
{
    if (!ok) {
        qWarning("spotify: the Web API refused the play: %s",
                 qPrintable(error));
        // No new start is coming: the current track's end must stay
        // detectable as an auto-advance.
        m_playStartPending = false;
        emit errorOccurred(QStringLiteral("Spotify refused playback: %1")
                               .arg(error));
    } else {
        qInfo("spotify: the Web API accepted the play");
    }
}

void SpotifyPlayer::onHelperConnectResult(bool ok)
{
    if (ok) {
        qInfo("spotify: the connect was accepted");
        return;
    }
    qWarning("spotify: the connect was refused");
    if (m_pendingPlay.isEmpty())
        return;
    m_pendingPlay.clear();
    emit errorOccurred(QStringLiteral(
        "The Spotify device would not (re)connect"));
}

// --- the audio path (the capture thread) --------------------------------

void SpotifyPlayer::startCapture()
{
    if (m_capture != nullptr)
        return;
    m_relayOpen = false;
    // The ring the private server's sink writes into (the server's
    // pinned spec — the ring always carries it, so the capture's
    // chunk size and pace are built from it).
    m_capture = new CapturePump(&m_pulse.ring(),
                                PrivatePulse::kSampleRate,
                                PrivatePulse::kChannels, this);
    // Direct: the relay's blocking write must run on the capture
    // thread (a queued hop would let the capture outrun the relay and
    // queue unbounded audio on the GUI thread).
    connect(m_capture, &CapturePump::chunk, this,
            &SpotifyPlayer::onChunk, Qt::DirectConnection);
    m_capture->start();
}

void SpotifyPlayer::stopCapture()
{
    if (m_capture != nullptr) {
        m_capture->stop();  // joins the capture thread
        delete m_capture;
        m_capture = nullptr;
    }
    // The capture thread is joined: nothing touches the relay's
    // pa_simple any more, it can be freed from here.
    if (m_relayOpen) {
        m_relay.close();
        m_relayOpen = false;
    }
    m_chain.stop();
    m_floatBuf.clear();
}

void SpotifyPlayer::onChunk(const QByteArray &frames, int frameCount)
{
    if (frameCount <= 0 || m_capture == nullptr)
        return;
    const int rate = m_capture->sampleRate();
    const int channels = m_capture->channels();
    if (rate <= 0 || channels <= 0)
        return;
    if (!m_relayOpen) {
        if (!m_relay.open(rate, channels)) {
            qWarning("spotify: the system sink relay would not open");
            return;
        }
        m_relayOpen = true;
        m_chain.start(rate, channels);
        // What the path actually carries (the SDK's output format as
        // the private server's null sink produced it).
        QVariantMap info;
        info.insert(QStringLiteral("format"),
                    QStringLiteral("Spotify (Web Playback SDK)"));
        info.insert(QStringLiteral("sampleRate"), rate);
        info.insert(QStringLiteral("channels"), channels);
        info.insert(QStringLiteral("channelLayout"),
                    QStringLiteral("stereo"));
        emit streamInfo(info);
    }

    // s16le -> float (the chain and the tap work in float).
    const qsizetype sampleCount = static_cast<qsizetype>(frameCount) * channels;
    QVector<qint16> samples(sampleCount);
    std::memcpy(samples.data(), frames.constData(),
                static_cast<size_t>(2 * sampleCount));
    m_floatBuf.resize(sampleCount);
    for (qsizetype i = 0; i < sampleCount; ++i)
        m_floatBuf[i] = static_cast<float>(samples[i]) / 32768.0F;

    // The DSP chain shapes — and may shorten — the audio before the
    // tap and the output (the meter and the position count what is
    // heard).
    const size_t kept = m_chain.process(m_floatBuf.data(),
                                        static_cast<size_t>(frameCount),
                                        channels);
    if (kept == 0)
        return;
    const int buckets = qBound(1, static_cast<int>(kept / 8), 256);
    const AudioTap tap = tapFrames(m_floatBuf.data(), kept, buckets);

    // float -> s16le (the relay's format).
    const qsizetype outCount = static_cast<qsizetype>(kept) * channels;
    QVector<qint16> out(outCount);
    for (qsizetype i = 0; i < outCount; ++i)
        out[i] = static_cast<qint16>(
            qBound(-32768, static_cast<int>(m_floatBuf[i] * 32768.0F),
                   32767));
    m_relay.write(out.constData(), static_cast<size_t>(2 * outCount));
    emit audioLevels(tap.leftPeak, tap.rightPeak);
    emit audioWaveform(tap.waveform, static_cast<quint32>(rate));
    emit audioFrames(tap.frames, static_cast<quint32>(rate));
}

// --- the test seams' real implementations -------------------------------

QString SpotifyPlayer::helperPath() const
{
    // Next to the app (the in-tree build's layout: the helper lands in
    // <app dir>/plugins alongside the plugin's .so files), beside it
    // (an installed layout), or one level up (the split layout — FHS
    // install and AppImage: the binary in usr/bin, the helper beside
    // the plugins in usr/plugins).
    const QString appDir = QCoreApplication::applicationDirPath();
    for (const QString &candidate :
         { appDir + QStringLiteral("/plugins/spotify-web"),
           appDir + QStringLiteral("/spotify-web"),
           appDir + QStringLiteral("/../plugins/spotify-web"),
         }) {
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return appDir + QStringLiteral("/plugins/spotify-web");
}

QStringList SpotifyPlayer::helperArguments() const
{
    return {};
}

bool SpotifyPlayer::startPrivateServer()
{
    return m_pulse.start();
}

QString SpotifyPlayer::privateServerString() const
{
    return m_pulse.serverString();
}
