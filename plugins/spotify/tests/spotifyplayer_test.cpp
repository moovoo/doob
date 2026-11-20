#include <QtTest>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <array>

// The core headers go through the <doob/...> prefix (the doob_lib
// target's include layout), so the test builds both in-tree and
// standalone against the installed API.
#include <doob/mediaitem.h>
#include <doob/settings.h>
#include <doob/settingsmanager.h>

// The driver's stack compiled straight in (the same way the other
// plugin's tests compile theirs): the unit tests reach the classes
// directly. The driver runs against a fake helper (a bash script
// speaking the JSON-lines protocol) and a fake private server (the
// seam returns a nonexistent socket address, so the server never
// runs and the capture's pump just reads silence from the ring), so
// no WebEngine and no system PulseAudio are touched.
#include "spotifyauth.h"
#include "spotifyipc.h"
#include "spotifyplayer.h"
#include "spotifyprovider.h"

#include "testhttpserver.h"

namespace {

// The test-seam driver: the fake helper (bash + the script) and the
// fake private server.
class FakeSpotifyPlayer : public SpotifyPlayer
{
public:
    FakeSpotifyPlayer(SpotifyAuth *auth, const QString &dataDir,
                      QObject *parent = nullptr)
        : SpotifyPlayer(auth, dataDir, parent) {}

    using SpotifyPlayer::feedState;
    using SpotifyPlayer::feedPosition;
    using SpotifyPlayer::feedError;
    using SpotifyPlayer::feedConnectResult;
    using SpotifyPlayer::setRecoveryWindowMs;

protected:
    QString helperPath() const override { return QStringLiteral("bash"); }
    QStringList helperArguments() const override
    {
        return { QString::fromLatin1(FAKE_SPOTIFY_WEB) };
    }
    bool startPrivateServer() override { return true; }
    QString privateServerString() const override
    {
        return QStringLiteral("unix:/nonexistent/doob-test/native");
    }
};

// The driver's real helperPath() (not the fake's override — the
// override is why the split-layout bug shipped): the test reaches the
// base implementation to check that it resolves the helper.
class RealHelperPathPlayer : public SpotifyPlayer
{
public:
    RealHelperPathPlayer(SpotifyAuth *auth, const QString &dataDir,
                         QObject *parent = nullptr)
        : SpotifyPlayer(auth, dataDir, parent) {}

    using SpotifyPlayer::helperPath;
};

// The auth's protected setToken seam (the tests drive a refresh and a
// re-login without the network).
class TestAuth : public SpotifyAuth
{
public:
    using SpotifyAuth::setToken;
};

class SpotifyPlayerTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    void ipcRoundTrip();
    void ensureReadyWithFakeHelper();
    void playPauseSeekAndTrackChange();
    void remoteTransportFollows();
    void commandedPlayStartIsNotAutoAdvance();
    void notSignedInFails();
    void tokenRefreshReachesTheHelper();
    void signOutTearsDownTheDevice();
    void reloginTearsDownTheDevice();
    void warmDeviceRefreshesExpiredToken();
    void transientAuthErrorKeepsThePlayback();
    void playbackErrorKeepsThePlayback();
    void unrecoveredErrorEscalates();
    void stopReleasesTheDeviceAndTheNextPlayReconnects();
    void aRefusedReconnectFailsThePlay();
    void sameTrackRestartIsNotAutoAdvance();
    void trackEndIsFinished();
    void aPauseIsNotATrackEnd();
    void realHelperPathResolvesInBuildLayout();

private:
    SpotifyAuth::Token fakeToken() const;
    // A stored (not expired) fake token under dir (the auth's data
    // dir), loaded into the auth.
    void writeFakeToken(SpotifyAuth *auth, const QString &dir) const;
    MediaItem fakeItem() const;
};

void SpotifyPlayerTest::initTestCase()
{
    // The SpotifyAuth ctor connects to the SettingsManager (the
    // client-id watcher) — a loaded manager is required.
    qputenv("DOOB_DATA_DIR", "/tmp/doob_spotifyplayer_test");
    Settings::reset();
    SettingsManager* settings = SettingsManager::getInstance();
    settings->setSettingsPath(
        "/tmp/doob_spotifyplayer_test/settings.json");
    settings->load();
}

void SpotifyPlayerTest::cleanupTestCase()
{
    qunsetenv("DOOB_DATA_DIR");
    Settings::reset();
    SettingsManager::reset();
}

SpotifyAuth::Token SpotifyPlayerTest::fakeToken() const
{
    SpotifyAuth::Token token;
    token.accessToken = QStringLiteral("fake-access");
    token.refreshToken = QStringLiteral("fake-refresh");
    token.expiresAt = QDateTime::currentDateTimeUtc().addSecs(3600);
    token.scopes = QStringLiteral("streaming");
    return token;
}

void SpotifyPlayerTest::writeFakeToken(SpotifyAuth *auth,
                                       const QString &dir) const
{
    // The file first, then the path: setTokenPath() loads the file
    // (an auth constructed over an empty dir is NotSignedIn).
    QVERIFY(SpotifyAuth::saveToken(dir + QStringLiteral("/spotify-token.json"),
                                   fakeToken()));
    auth->setTokenPath(dir);
}

MediaItem SpotifyPlayerTest::fakeItem() const
{
    MediaItem item;
    item.id = 1;
    item.title = QStringLiteral("Fake Track");
    item.source = QUrl(QStringLiteral("spotify:track:fake"));
    return item;
}

// The JSON-lines channel against the fake helper: framing both ways,
// the event types, the clean exit.
void SpotifyPlayerTest::ipcRoundTrip()
{
    SpotifyIpc ipc;
    QVERIFY(ipc.start(QStringLiteral("bash"),
                      { QString::fromLatin1(FAKE_SPOTIFY_WEB) },
                      QProcessEnvironment()));

    QSignalSpy readySpy(&ipc, &SpotifyIpc::ready);
    // The ready line is the helper's first output.
    QTRY_COMPARE(readySpy.count(), 1);
    QCOMPARE(readySpy.first().first().toString(),
             QStringLiteral("fake-device"));

    QSignalSpy stateSpy(&ipc, &SpotifyIpc::state);
    QSignalSpy resultSpy(&ipc, &SpotifyIpc::playResult);
    ipc.send({{QStringLiteral("cmd"), QStringLiteral("play")},
              {QStringLiteral("uri"),
               QStringLiteral("spotify:track:fake"),
              },
              {QStringLiteral("position"), 0},
             });
    QTRY_COMPARE(stateSpy.count(), 1);
    QTRY_COMPARE(resultSpy.count(), 1);
    QCOMPARE(resultSpy.first().first().toBool(), true);
    const QJsonObject state = stateSpy.first().first().value<QJsonObject>();
    QCOMPARE(state.value(QStringLiteral("paused")).toBool(), false);

    QSignalSpy posSpy(&ipc, &SpotifyIpc::position);
    ipc.send({{QStringLiteral("cmd"), QStringLiteral("seek")},
              {QStringLiteral("position"), 9999},
             });
    QTRY_COMPARE(posSpy.count(), 1);
    QCOMPARE(posSpy.first().first().toLongLong(), 9999);
    QCOMPARE(posSpy.first().last().toLongLong(), 210000);

    // The device's (re)registration: the connect's result and the
    // fresh device's ready; the disconnect is silent (the session
    // closes, the helper stays up).
    QSignalSpy connectSpy(&ipc, &SpotifyIpc::connectResult);
    ipc.send({{QStringLiteral("cmd"), QStringLiteral("connect")}});
    QTRY_COMPARE(connectSpy.count(), 1);
    QCOMPARE(connectSpy.first().first().toBool(), true);
    QTRY_COMPARE(readySpy.count(), 2);
    ipc.send({{QStringLiteral("cmd"), QStringLiteral("disconnect")}});
    QTRY_COMPARE(readySpy.count(), 2);  // the helper answers nothing

    QSignalSpy exitedSpy(&ipc, &SpotifyIpc::helperExited);
    ipc.stop();  // the quit command, then a clean exit
    QTRY_COMPARE(ipc.isRunning(), false);
    QCOMPARE(exitedSpy.count(), 0);  // a clean exit is not a crash
}

// The driver's lazy start against the fake helper: the private server
// "comes up" (the seam), the fake helper answers ready, and the
// device is usable.
void SpotifyPlayerTest::ensureReadyWithFakeHelper()
{
    QTemporaryDir dir;
    SpotifyAuth auth;
    writeFakeToken(&auth, dir.path());
    FakeSpotifyPlayer player(&auth, dir.path());

    bool done = false;
    bool ok = false;
    QString error;
    player.ensureReady([&](bool success, const QString &err) {
        done = true;
        ok = success;
        error = err;
    });
    QTRY_VERIFY(done);
    QVERIFY2(ok, qPrintable(error));
    QVERIFY(player.isReady());
}

// The transport: play/pause/seek through the fake, the position and
// duration reports, and the backward-jump finished() detector (a
// commanded seek is exempt; the fake's volume reply plays the role of
// the SDK having started the next track).
void SpotifyPlayerTest::playPauseSeekAndTrackChange()
{
    QTemporaryDir dir;
    SpotifyAuth auth;
    writeFakeToken(&auth, dir.path());
    FakeSpotifyPlayer player(&auth, dir.path());

    QSignalSpy posSpy(&player, &AudioPlayer::positionChanged);
    QSignalSpy durSpy(&player, &AudioPlayer::durationChanged);
    QSignalSpy finishedSpy(&player, &AudioPlayer::finished);

    bool done = false;
    player.ensureReady([&](bool success, const QString &) {
        done = success;
    });
    QTRY_VERIFY(done);

    // The device coming up applies the driver's volume: the fake
    // answers it with a position event (0, 210000) before the play.
    QTRY_COMPARE(posSpy.count(), 1);
    QTRY_COMPARE(durSpy.count(), 1);

    player.play(fakeItem());
    // The fake's play answer is a state event (position 0, duration
    // 210000).
    QTRY_COMPARE(posSpy.count(), 2);
    QCOMPARE(player.positionMs(), 0);
    QCOMPARE(player.durationMs(), 210000);

    player.pause();
    QTRY_COMPARE(posSpy.count(), 3);
    QCOMPARE(player.positionMs(), 5000);

    // A commanded seek: the position's next backward jump is the
    // seek's, not a track change. The optimistic local update and the
    // fake's confirmation both land.
    player.setPositionMs(9999);
    QTRY_COMPARE(posSpy.count(), 5);
    QCOMPARE(player.positionMs(), 9999);
    QCOMPARE(finishedSpy.count(), 0);

    // The fake's volume reply is a synthetic new-track event
    // (position 0): a backward jump with no commanded seek is the SDK
    // having auto-advanced — finished() goes back to the playlist.
    player.setVolume(0.5F);
    QTRY_COMPARE(finishedSpy.count(), 1);
    QCOMPARE(player.positionMs(), 0);

    // stop() is a release: the session closes (the device leaves
    // Spotify Connect), the driver's state is cleared, the helper
    // stays warm for the next play.
    player.stop();
    QCOMPARE(player.positionMs(), 0);
    QCOMPARE(player.durationMs(), 0);
}

// A transport change outside of doob (the native Spotify app paused,
// resumed or stopped the stream): the driver mirrors it, so the
// PlaybackManager's state — and the UI — follows.
void SpotifyPlayerTest::remoteTransportFollows()
{
    QTemporaryDir dir;
    SpotifyAuth auth;
    writeFakeToken(&auth, dir.path());
    FakeSpotifyPlayer player(&auth, dir.path());

    QSignalSpy pausedSpy(&player, &AudioPlayer::paused);
    QSignalSpy resumedSpy(&player, &AudioPlayer::resumed);
    QSignalSpy stoppedSpy(&player, &AudioPlayer::stopped);

    bool done = false;
    player.ensureReady([&](bool success, const QString &) {
        done = success;
    });
    QTRY_VERIFY(done);

    player.play(fakeItem());
    // The initial state report (playing) is no transition: the driver
    // starts unpaused, like the SDK's device.
    QTRY_COMPARE(pausedSpy.count(), 0);
    QTRY_COMPARE(resumedSpy.count(), 0);

    // A pause outside of doob (the native app): the driver mirrors it.
    player.feedState({{QStringLiteral("paused"), true},
                      {QStringLiteral("position"), 5000},
                      {QStringLiteral("duration"), 210000},});
    QCOMPARE(pausedSpy.count(), 1);
    QCOMPARE(resumedSpy.count(), 0);

    // A resume outside of doob.
    player.feedState({{QStringLiteral("paused"), false},
                      {QStringLiteral("position"), 5000},
                      {QStringLiteral("duration"), 210000},});
    QCOMPARE(resumedSpy.count(), 1);

    // A stop outside of doob (the device went idle): mirrored, the
    // driver's position and duration cleared.
    player.feedState({{QStringLiteral("stopped"), true}});
    QCOMPARE(stoppedSpy.count(), 1);
    QCOMPARE(player.positionMs(), 0);
    QCOMPARE(player.durationMs(), 0);

    // The manager reacts with its own stop() (the driver's state is
    // cleared with it) — a further idle report is not a stop.
    player.stop();
    player.feedState({{QStringLiteral("stopped"), true}});
    QCOMPARE(stoppedSpy.count(), 1);
}

// The stale-report race: the outgoing track's last position reports
// land after a commanded play, before the new item's state — the
// commanded start must not read as an auto-advance (the playlist
// would skip the commanded item and a second track would start over
// the first, which the switch-back then plays in parallel).
void SpotifyPlayerTest::commandedPlayStartIsNotAutoAdvance()
{
    QTemporaryDir dir;
    SpotifyAuth auth;
    writeFakeToken(&auth, dir.path());
    FakeSpotifyPlayer player(&auth, dir.path());

    QSignalSpy posSpy(&player, &AudioPlayer::positionChanged);
    QSignalSpy finishedSpy(&player, &AudioPlayer::finished);

    bool done = false;
    player.ensureReady([&](bool success, const QString &) {
        done = success;
    });
    QTRY_VERIFY(done);

    player.play(fakeItem());
    // The first event is the fake's volume answer at start-up; the
    // play's state answer (position 0) follows — it is the commanded
    // start the suppression is consumed by.
    QTRY_COMPARE(posSpy.count(), 2);
    QCOMPARE(finishedSpy.count(), 0);

    // The track plays on to 110 s.
    player.feedPosition(110000, 210000);
    QCOMPARE(player.positionMs(), 110000);

    // A commanded new start — and, before the event loop delivers the
    // fake's state answer, the outgoing track's last, stale report
    // lands (position 112 s).
    player.play(fakeItem());
    player.feedPosition(112000, 210000);
    // The fake's state (position 0) — the fifth event: the volume
    // answer at start-up, the two play answers and the two feeds.
    QTRY_COMPARE(posSpy.count(), 5);
    QCOMPARE(finishedSpy.count(), 0);
    QCOMPARE(player.positionMs(), 0);

    // A real auto-advance (a backward jump with no command in
    // between) is still detected: the jump is seen in the position
    // report first, and the state event that follows it — the new
    // track's, with a new title — decides.
    player.feedPosition(5000, 210000);
    player.feedPosition(0, 210000);
    QCOMPARE(finishedSpy.count(), 0);
    player.feedState({{QStringLiteral("paused"), false},
                      {QStringLiteral("position"), 0},
                      {QStringLiteral("duration"), 210000},
                      {QStringLiteral("title"),
                       QStringLiteral("Next Track"),
                     },
                     });
    QCOMPARE(finishedSpy.count(), 1);
}

// Not signed in: play() fails with the sign-in hint (the Playback
// Manager's playbackError shows it in the status bar) and the device
// never starts.
void SpotifyPlayerTest::notSignedInFails()
{
    QTemporaryDir dir;
    SpotifyAuth auth;
    auth.setTokenPath(dir.path());  // an empty dir: no token file
    FakeSpotifyPlayer player(&auth, dir.path());

    bool done = false;
    bool ok = true;
    player.ensureReady([&](bool success, const QString &) {
        done = true;
        ok = success;
    });
    QTRY_VERIFY(done);
    QVERIFY(!ok);
    QVERIFY(!player.isReady());

    QSignalSpy errorSpy(&player, &AudioPlayer::errorOccurred);
    player.play(fakeItem());
    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(errorSpy.first().first().toString(),
             SpotifyProvider::authHintText());
}

// A token refresh of the same account (the access token's hourly
// expiry): the running helper is handed the new token (the token
// command — the SDK re-asks for a token on expiry, and the helper's
// own copy is the one from its start). The fake's token answer is a
// position event (the ack); the device stays up (no teardown on a
// refresh).
void SpotifyPlayerTest::tokenRefreshReachesTheHelper()
{
    QTemporaryDir dir;
    TestAuth auth;
    writeFakeToken(&auth, dir.path());
    FakeSpotifyPlayer player(&auth, dir.path());

    QSignalSpy posSpy(&player, &AudioPlayer::positionChanged);
    bool done = false;
    player.ensureReady([&](bool success, const QString &) {
        done = success;
    });
    QTRY_VERIFY(done);
    QVERIFY(player.isReady());
    // The start-up volume answer's position (0).
    QTRY_COMPARE(posSpy.count(), 1);

    // The refresh (the same account, a new access token): authChanged
    // only — no account change, no teardown.
    SpotifyAuth::Token token = auth.token();
    token.accessToken = QStringLiteral("fake-access-2");
    token.expiresAt = QDateTime::currentDateTimeUtc().addSecs(3600);
    auth.setToken(token);
    // The helper's token ack (the position event).
    QTRY_COMPARE(posSpy.count(), 2);
    // The device stayed up (a refresh does not restart the helper).
    QVERIFY(player.isReady());
}

// A sign-out while the device is up: the device is torn down (the
// token is gone), a mid-playback stop is surfaced (not a silent
// one), and the next play is refused with the sign-in hint.
void SpotifyPlayerTest::signOutTearsDownTheDevice()
{
    QTemporaryDir dir;
    TestAuth auth;
    writeFakeToken(&auth, dir.path());
    FakeSpotifyPlayer player(&auth, dir.path());

    QSignalSpy posSpy(&player, &AudioPlayer::positionChanged);
    bool done = false;
    player.ensureReady([&](bool success, const QString &) {
        done = success;
    });
    QTRY_VERIFY(done);
    QVERIFY(player.isReady());

    player.play(fakeItem());
    // The volume answer at start-up and the play's state answer.
    QTRY_COMPARE(posSpy.count(), 2);

    QSignalSpy errorSpy(&player, &AudioPlayer::errorOccurred);
    auth.logout();
    QTRY_VERIFY(!player.isReady());
    // The mid-playback stop, surfaced (the manager follows).
    QCOMPARE(errorSpy.count(), 1);

    // The next play is refused with the sign-in hint (the device is
    // down and there is no account).
    player.play(fakeItem());
    QCOMPARE(errorSpy.count(), 2);
    QCOMPARE(errorSpy.last().first().toString(),
             SpotifyProvider::authHintText());
}

// A re-login (a new account's sign-in, the accountChanged emission):
// the running device is torn down (it is registered on the previous
// account's token), and the next start is fresh.
void SpotifyPlayerTest::reloginTearsDownTheDevice()
{
    QTemporaryDir dir;
    TestAuth auth;
    writeFakeToken(&auth, dir.path());
    FakeSpotifyPlayer player(&auth, dir.path());

    bool done = false;
    player.ensureReady([&](bool success, const QString &) {
        done = success;
    });
    QTRY_VERIFY(done);
    QVERIFY(player.isReady());

    // The login's token (the login's setToken), then the account
    // change itself (the login's postToken emits it — the network is
    // not testable, the signal is emitted directly).
    SpotifyAuth::Token token = auth.token();
    token.accessToken = QStringLiteral("fake-access-2");
    token.expiresAt = QDateTime::currentDateTimeUtc().addSecs(3600);
    auth.setToken(token);
    // The signature is parenthesized (this Qt's indexOfSignal form).
    const int signalIndex =
        auth.metaObject()->indexOfSignal("accountChanged()");
    QVERIFY(signalIndex >= 0);
    std::array<void *, 1> args{nullptr};
    QMetaObject::activate(&auth, signalIndex, args.data());
    QTRY_VERIFY(!player.isReady());

    // The next play starts the device fresh (with the new token).
    done = false;
    player.ensureReady([&](bool success, const QString &) {
        done = success;
    });
    QTRY_VERIFY(done);
    QVERIFY(player.isReady());
}

// The idle expiry: the device is warm and the token lapses within the
// refresh margin (the refresh-ahead's window, a play before it fires)
// — the refresh fires and the running helper gets the fresh token
// (the token command) before the next play; the play's ensureReady
// (the warm path) then lands on the valid token, no restart.
void SpotifyPlayerTest::warmDeviceRefreshesExpiredToken()
{
    QTemporaryDir dir;
    MiniHttpServer server;
    server.route(QStringLiteral("/api/token"), 200,
                 QByteArrayLiteral(
                     R"({"access_token":"fresh","expires_in":3600})"));
    TestAuth auth;
    auth.setTokenEndpoint(server.baseUrl() + QStringLiteral("/api/token"));
    writeFakeToken(&auth, dir.path());
    FakeSpotifyPlayer player(&auth, dir.path());

    QSignalSpy posSpy(&player, &AudioPlayer::positionChanged);
    bool done = false;
    player.ensureReady([&](bool success, const QString &) {
        done = success;
    });
    QTRY_VERIFY(done);
    QVERIFY(player.isReady());
    // The start-up volume answer's position (0).
    QTRY_COMPARE(posSpy.count(), 1);

    // The token lapses while the device idles: the refresh-ahead fires
    // and the driver pushes the fresh token to the running helper.
    SpotifyAuth::Token token = auth.token();
    token.expiresAt = QDateTime::currentDateTimeUtc().addSecs(-10);
    auth.setToken(token);
    // The helper's token ack (the position event).
    QTRY_COMPARE_WITH_TIMEOUT(posSpy.count(), 2, 5000);
    QCOMPARE(auth.token().accessToken, QStringLiteral("fresh"));
    // The one refresh (the timer's and the push's callers shared it).
    QCOMPARE(server.requestPaths().size(), 1);
    // The device stayed up (a refresh does not restart the helper).
    QVERIFY(player.isReady());

    // The play's ensureReady (the device is warm) lands on the valid
    // token — no failure, no restart.
    done = false;
    QString error;
    player.ensureReady([&](bool success, const QString &err) {
        done = true;
        error = err;
        done = success;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QVERIFY2(done, qPrintable(error));
    QVERIFY(player.isReady());
}

// The engine's transient auth hiccup (a stream handshake the backend
// rejected once — the log's "Invalid token scopes."): the engine
// re-asks for a token on every retry and recovers on its own, so the
// driver must not stop the playback (the manager would mark the item
// failed while the audio is playing). The engine's next report
// cancels the pending stop — no error is surfaced even after the
// (shortened) recovery window would have expired.
void SpotifyPlayerTest::transientAuthErrorKeepsThePlayback()
{
    QTemporaryDir dir;
    TestAuth auth;
    writeFakeToken(&auth, dir.path());
    FakeSpotifyPlayer player(&auth, dir.path());
    // A short window: the test is about the recovery cancelling the
    // stop, not about the five-second default.
    player.setRecoveryWindowMs(200);

    QSignalSpy posSpy(&player, &AudioPlayer::positionChanged);
    QSignalSpy errorSpy(&player, &AudioPlayer::errorOccurred);

    bool done = false;
    player.ensureReady([&](bool success, const QString &) {
        done = success;
    });
    QTRY_VERIFY(done);
    QVERIFY(player.isReady());

    player.play(fakeItem());
    // The start-up volume answer and the play's state answer.
    QTRY_COMPARE(posSpy.count(), 2);

    // The track plays on to the 6 s mark when the engine reports the
    // hiccup.
    player.feedPosition(6000, 210000);
    // The envelope the transport delivers: the page's listener tag
    // under "listener" (its own "type" is the line's kind).
    player.feedError({{QStringLiteral("listener"),
                       QStringLiteral("authentication"),
                     },
                      {QStringLiteral("message"),
                       QStringLiteral("Invalid token scopes."),
                     },
                     });
    // No immediate failure — and the engine's next report (its
    // recovered playback) cancels the pending stop.
    QCOMPARE(errorSpy.count(), 0);
    player.feedPosition(6500, 210000);
    QCOMPARE(player.positionMs(), 6500);
    // Past the (shortened) window: the recovery held, no failure.
    QTest::qWait(400);
    QCOMPARE(errorSpy.count(), 0);
}

// The engine's startup playback race (the log's "Cannot perform
// operation; no list was loaded."): transient, the engine re-loads its
// list and starts the stream. The driver must not stop the playback
// (the manager would mark the item failed while the audio is playing)
// — the recovery cancels the pending stop.
void SpotifyPlayerTest::playbackErrorKeepsThePlayback()
{
    QTemporaryDir dir;
    TestAuth auth;
    writeFakeToken(&auth, dir.path());
    FakeSpotifyPlayer player(&auth, dir.path());
    player.setRecoveryWindowMs(200);

    QSignalSpy posSpy(&player, &AudioPlayer::positionChanged);
    QSignalSpy errorSpy(&player, &AudioPlayer::errorOccurred);

    bool done = false;
    player.ensureReady([&](bool success, const QString &) {
        done = success;
    });
    QTRY_VERIFY(done);
    QVERIFY(player.isReady());

    player.play(fakeItem());
    // The start-up volume answer and the play's state answer.
    QTRY_COMPARE(posSpy.count(), 2);

    // The startup race, then the engine's recovered playback.
    player.feedError({{QStringLiteral("listener"),
                       QStringLiteral("playback"),
                     },
                      {QStringLiteral("message"),
                       QStringLiteral(
                           "Cannot perform operation; no list was loaded."),
                     },
                     });
    QCOMPARE(errorSpy.count(), 0);
    player.feedPosition(1000, 210000);
    QCOMPARE(player.positionMs(), 1000);
    // Past the (shortened) window: the recovery held, no failure.
    QTest::qWait(400);
    QCOMPARE(errorSpy.count(), 0);
}

// A broken engine (the transient error the recovery window was arming
// is not followed by any position report — the engine does not
// recover): the window's expiry surfaces the failure (the manager
// stops, the status bar shows the reason).
void SpotifyPlayerTest::unrecoveredErrorEscalates()
{
    QTemporaryDir dir;
    TestAuth auth;
    writeFakeToken(&auth, dir.path());
    FakeSpotifyPlayer player(&auth, dir.path());
    player.setRecoveryWindowMs(200);

    QSignalSpy posSpy(&player, &AudioPlayer::positionChanged);
    bool done = false;
    player.ensureReady([&](bool success, const QString &) {
        done = success;
    });
    QTRY_VERIFY(done);
    QVERIFY(player.isReady());

    // A play is in flight (the manager expects the audio).
    player.play(fakeItem());
    QTRY_COMPARE(posSpy.count(), 2);

    QSignalSpy errorSpy(&player, &AudioPlayer::errorOccurred);
    // The engine's error, and no recovery: the window's expiry
    // surfaces the failure.
    player.feedError({{QStringLiteral("listener"),
                       QStringLiteral("playback"),
                     },
                      {QStringLiteral("message"),
                       QStringLiteral(
                           "Cannot perform operation; no list was loaded."),
                     },
                     });
    QTRY_COMPARE_WITH_TIMEOUT(errorSpy.count(), 1, 3000);
    QCOMPARE(errorSpy.first().first().toString(),
             QStringLiteral(
                 "Cannot perform operation; no list was loaded."));
}

// A stop is a release (the AudioPlayer's contract): the SDK's session
// closes (the device leaves Spotify Connect — while doob is idle,
// nothing else can play through it), the driver's state is cleared,
// and the helper stays warm (no teardown, no new start). The next play
// re-registers the device (a connect — the fake answers with a fresh
// ready) and plays on the new device (the re-ready's volume answer and
// the play's state answer are the two position events).
void SpotifyPlayerTest::stopReleasesTheDeviceAndTheNextPlayReconnects()
{
    QTemporaryDir dir;
    SpotifyAuth auth;
    writeFakeToken(&auth, dir.path());
    FakeSpotifyPlayer player(&auth, dir.path());

    QSignalSpy posSpy(&player, &AudioPlayer::positionChanged);
    bool done = false;
    player.ensureReady([&](bool success, const QString &) {
        done = success;
    });
    QTRY_VERIFY(done);
    // The start-up volume answer's position (0).
    QTRY_COMPARE(posSpy.count(), 1);

    player.play(fakeItem());
    QTRY_COMPARE(posSpy.count(), 2);
    QCOMPARE(player.durationMs(), 210000);

    player.stop();
    QCOMPARE(player.positionMs(), 0);
    QCOMPARE(player.durationMs(), 0);
    // The helper is warm: no teardown, no new start.
    QVERIFY(player.isReady());

    // The next play re-registers the device (the connect's ready
    // carries the new device id) and plays on it.
    player.play(fakeItem());
    QTRY_COMPARE(posSpy.count(), 4);
    QCOMPARE(player.positionMs(), 0);
    QCOMPARE(player.durationMs(), 210000);
}

// The connect a stopped device's play rides on is refused (a bad
// token, no network): the pending play fails (the manager stops, the
// status bar shows the reason), and the device stays warm for the next
// try. The refusal is fed directly (the fake always succeeds), before
// the fake's belated ready lands.
void SpotifyPlayerTest::aRefusedReconnectFailsThePlay()
{
    QTemporaryDir dir;
    SpotifyAuth auth;
    writeFakeToken(&auth, dir.path());
    FakeSpotifyPlayer player(&auth, dir.path());

    bool done = false;
    player.ensureReady([&](bool success, const QString &) {
        done = success;
    });
    QTRY_VERIFY(done);
    QVERIFY(player.isReady());

    // A stop (the session closes), then a play (the connect in
    // flight).
    player.stop();
    player.play(fakeItem());

    QSignalSpy errorSpy(&player, &AudioPlayer::errorOccurred);
    player.feedConnectResult(false);
    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(errorSpy.first().first().toString(),
             QStringLiteral("The Spotify device would not (re)connect"));

    // The fake's belated ready still lands: the device is registered
    // again, no play rides on it (the pending was failed), no second
    // error.
    QTest::qWait(100);
    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(player.isReady());
}

// The engine's same-track restart (a reconnect after an auth hiccup —
// the log's 6 s position reset to 0 on the running track): a backward
// jump to the same title is not an auto-advance, so no finished()
// (the playlist would replay the row from the top). A jump to a new
// title is the advance the detector exists for.
void SpotifyPlayerTest::sameTrackRestartIsNotAutoAdvance()
{
    QTemporaryDir dir;
    SpotifyAuth auth;
    writeFakeToken(&auth, dir.path());
    FakeSpotifyPlayer player(&auth, dir.path());

    QSignalSpy posSpy(&player, &AudioPlayer::positionChanged);
    QSignalSpy finishedSpy(&player, &AudioPlayer::finished);

    bool done = false;
    player.ensureReady([&](bool success, const QString &) {
        done = success;
    });
    QTRY_VERIFY(done);
    QVERIFY(player.isReady());

    player.play(fakeItem());
    // The start-up volume answer and the play's state answer ("Fake
    // Track").
    QTRY_COMPARE(posSpy.count(), 2);

    // The track plays on to the 6 s mark, then the engine restarts it
    // (the position report sees the reset first, the state event — the
    // same track's title — decides).
    player.feedPosition(6000, 210000);
    player.feedPosition(0, 210000);
    player.feedState({{QStringLiteral("paused"), false},
                      {QStringLiteral("position"), 0},
                      {QStringLiteral("duration"), 210000},
                      {QStringLiteral("title"),
                       QStringLiteral("Fake Track"),
                     },
                     });
    QCOMPARE(finishedSpy.count(), 0);
    QCOMPARE(player.positionMs(), 0);

    // A new track (a different title) is the auto-advance.
    player.feedPosition(5000, 180000);
    player.feedState({{QStringLiteral("paused"), false},
                      {QStringLiteral("position"), 0},
                      {QStringLiteral("duration"), 180000},
                      {QStringLiteral("title"),
                       QStringLiteral("Next Track"),
                     },
                     });
    QCOMPARE(finishedSpy.count(), 1);
}

// The track's end: doob plays single tracks (the SDK's context is
// empty), so the engine has nothing to advance to — it plays the track
// through and pauses at the track's reset position (position 0). The
// driver hands the advance back to the playlist as finished() (not
// paused() — the manager would sit in the paused state and the playlist
// would not move on). The engine repeats the end's state event a few
// times: the repeats are not re-emitted (no stale paused(), no
// re-anchoring of the old track's duration), and the device stays warm
// for the advance's play.
void SpotifyPlayerTest::trackEndIsFinished()
{
    QTemporaryDir dir;
    SpotifyAuth auth;
    writeFakeToken(&auth, dir.path());
    FakeSpotifyPlayer player(&auth, dir.path());

    QSignalSpy posSpy(&player, &AudioPlayer::positionChanged);
    QSignalSpy durSpy(&player, &AudioPlayer::durationChanged);
    QSignalSpy pausedSpy(&player, &AudioPlayer::paused);
    QSignalSpy finishedSpy(&player, &AudioPlayer::finished);

    bool done = false;
    player.ensureReady([&](bool success, const QString &) {
        done = success;
    });
    QTRY_VERIFY(done);
    QVERIFY(player.isReady());

    player.play(fakeItem());
    // The start-up volume answer and the play's state answer.
    QTRY_COMPARE(posSpy.count(), 2);
    QCOMPARE(player.durationMs(), 210000);

    // The track plays on to its end (the last report within the end's
    // tolerance), and the engine pauses at the track's reset position.
    player.feedPosition(209500, 210000);
    player.feedState({{QStringLiteral("paused"), true},
                      {QStringLiteral("position"), 0},
                      {QStringLiteral("duration"), 210000},
                      {QStringLiteral("title"),
                       QStringLiteral("Fake Track"),
                     },
                     });
    // The advance goes back to the playlist — not a pause.
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(pausedSpy.count(), 0);
    // The driver's state is cleared (the next play re-anchors it).
    QCOMPARE(player.positionMs(), 0);
    QCOMPARE(player.durationMs(), 0);

    // The engine repeats the end's state event: not re-emitted.
    const qsizetype positions = posSpy.count();
    const qsizetype durations = durSpy.count();
    player.feedState({{QStringLiteral("paused"), true},
                      {QStringLiteral("position"), 0},
                      {QStringLiteral("duration"), 210000},
                      {QStringLiteral("title"),
                       QStringLiteral("Fake Track"),
                     },
                     });
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(pausedSpy.count(), 0);
    QCOMPARE(posSpy.count(), positions);
    QCOMPARE(durSpy.count(), durations);
    QCOMPARE(player.durationMs(), 0);

    // The device stays warm: the advance's play re-anchors it.
    player.play(fakeItem());
    QTRY_COMPARE(posSpy.count(), positions + 1);
    QCOMPARE(player.positionMs(), 0);
    QCOMPARE(player.durationMs(), 210000);
}

// A pause is not the track's end: a user pause keeps the position (the
// end's reset to zero, with the track near its end, is the
// discriminator) — paused() goes to the manager, no finished() (the
// playlist would skip the row).
void SpotifyPlayerTest::aPauseIsNotATrackEnd()
{
    QTemporaryDir dir;
    SpotifyAuth auth;
    writeFakeToken(&auth, dir.path());
    FakeSpotifyPlayer player(&auth, dir.path());

    QSignalSpy posSpy(&player, &AudioPlayer::positionChanged);
    QSignalSpy pausedSpy(&player, &AudioPlayer::paused);
    QSignalSpy resumedSpy(&player, &AudioPlayer::resumed);
    QSignalSpy finishedSpy(&player, &AudioPlayer::finished);

    bool done = false;
    player.ensureReady([&](bool success, const QString &) {
        done = success;
    });
    QTRY_VERIFY(done);
    QVERIFY(player.isReady());

    player.play(fakeItem());
    // The start-up volume answer and the play's state answer.
    QTRY_COMPARE(posSpy.count(), 2);

    // A pause near the start (the position at zero, the track not near
    // its end): a pause, not an end.
    player.feedState({{QStringLiteral("paused"), true},
                      {QStringLiteral("position"), 500},
                      {QStringLiteral("duration"), 210000},
                      {QStringLiteral("title"),
                       QStringLiteral("Fake Track"),
                     },
                     });
    QCOMPARE(pausedSpy.count(), 1);
    QCOMPARE(finishedSpy.count(), 0);
    player.feedState({{QStringLiteral("paused"), false},
                      {QStringLiteral("position"), 500},
                      {QStringLiteral("duration"), 210000},
                      {QStringLiteral("title"),
                       QStringLiteral("Fake Track"),
                     },
                     });
    QCOMPARE(resumedSpy.count(), 1);

    // A pause near the end (the track within the end's tolerance) that
    // keeps its position: a pause, not an end.
    player.feedPosition(209500, 210000);
    player.feedState({{QStringLiteral("paused"), true},
                      {QStringLiteral("position"), 209500},
                      {QStringLiteral("duration"), 210000},
                      {QStringLiteral("title"),
                       QStringLiteral("Fake Track"),
                     },
                     });
    QCOMPARE(pausedSpy.count(), 2);
    QCOMPARE(finishedSpy.count(), 0);
}

// The real helperPath() (not the fake's override — the override is why
// the split-layout bug shipped): in the build's layout (the test
// executable in the top-level build directory, the helper in
// <build>/plugins) the first candidate must win and it must exist.
void SpotifyPlayerTest::realHelperPathResolvesInBuildLayout()
{
    QTemporaryDir dir;
    SpotifyAuth auth;
    RealHelperPathPlayer player(&auth, dir.path());

    const QString candidate = QCoreApplication::applicationDirPath()
        + QStringLiteral("/plugins/spotify-web");
    if (!QFileInfo::exists(candidate))
        QSKIP("the spotify-web helper is not built in this layout");

    // The in-tree candidate wins (the split-layout candidates are only
    // tried after it) and resolves to the built helper.
    QCOMPARE(player.helperPath(), candidate);
}

} // namespace

QTEST_MAIN(SpotifyPlayerTest)
#include "spotifyplayer_test.moc"
