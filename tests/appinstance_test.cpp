#include <QtTest>
#include <QLocalSocket>
#include <QTemporaryDir>

#include <memory>

#include "appinstance.h"
#include "settings.h"

namespace {

class AppInstanceTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void serverNameIsPerDataDir();
    void serverNameDiffersPerDataDir();
    void acquireWithoutRunningInstanceListens();
    void secondInstanceForwardsToPrimary();
    void secondInstanceExitsWithoutPayloadSources();
    void encodeDecodeRoundtrip();
    void decodeGarbageFails();

private:
    std::unique_ptr<QTemporaryDir> m_dir;
};

void AppInstanceTest::init()
{
    // A private data directory per test: the socket name (and therefore
    // the gate) is derived from it, so the tests never collide with a
    // running doob.
    m_dir = std::make_unique<QTemporaryDir>();
    qputenv("DOOB_DATA_DIR", m_dir->path().toUtf8());
    Settings::reset();
}

void AppInstanceTest::cleanup()
{
    qunsetenv("DOOB_DATA_DIR");
    Settings::reset();
}

void AppInstanceTest::serverNameIsPerDataDir()
{
    // The name is the data directory flattened to a plain name under a
    // "doob-" prefix (it keys a socket file on Unix: no separators).
    const QString name = AppInstance::serverName();
    QVERIFY(name.startsWith(QLatin1String("doob-")));
    QVERIFY(name.size() > QStringLiteral("doob-").size());
    QVERIFY(!name.contains(QLatin1Char('/')));
}

void AppInstanceTest::serverNameDiffersPerDataDir()
{
    qputenv("DOOB_DATA_DIR", "/tmp/doob-a");
    Settings::reset();
    const QString a = AppInstance::serverName();
    qputenv("DOOB_DATA_DIR", "/tmp/doob-b");
    Settings::reset();
    const QString b = AppInstance::serverName();
    QVERIFY(a != b);
    QVERIFY(a.startsWith(QLatin1String("doob-")));
    // No path separators in the name (it keys a socket file on Unix).
    QVERIFY(!a.contains(QLatin1Char('/')));
}

void AppInstanceTest::acquireWithoutRunningInstanceListens()
{
    AppInstance instance;
    QVERIFY(instance.acquire(CliRequest()));

    // The gate is live: a socket to the server name connects.
    QLocalSocket probe;
    probe.connectToServer(AppInstance::serverName());
    QVERIFY(probe.waitForConnected(1000));
    probe.disconnectFromServer();
}

void AppInstanceTest::secondInstanceForwardsToPrimary()
{
    AppInstance primary;
    QVERIFY(primary.acquire(CliRequest()));

    QSignalSpy spy(&primary, &AppInstance::payloadReceived);

    CliRequest forwarded;
    forwarded.sources = { "/tmp/a.mp3", "https://example.com/stream.mp3" };
    forwarded.play = true;
    forwarded.newPlaylist = true;
    forwarded.newPlaylistName = "Mix";

    AppInstance secondary;
    QVERIFY(!secondary.acquire(forwarded));  // the primary took the sources

    QCOMPARE(spy.wait(2000), true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<CliRequest>(), forwarded);
}

void AppInstanceTest::secondInstanceExitsWithoutPayloadSources()
{
    // The forwarded request carries no sources: the primary still gets
    // the payload (the flags ride with the sources).
    AppInstance primary;
    QVERIFY(primary.acquire(CliRequest()));

    QSignalSpy spy(&primary, &AppInstance::payloadReceived);
    AppInstance secondary;
    CliRequest empty;
    empty.play = true;
    QVERIFY(!secondary.acquire(empty));

    QCOMPARE(spy.wait(2000), true);
    QCOMPARE(spy.at(0).at(0).value<CliRequest>(), empty);
}

void AppInstanceTest::encodeDecodeRoundtrip()
{
    CliRequest request;
    request.sources = { "a.mp3", "https://x.example/stream" };
    request.play = true;
    request.newPlaylist = true;
    request.newPlaylistName = "Mix";

    CliRequest decoded;
    QVERIFY(AppInstance::decode(AppInstance::encode(request), decoded));
    QCOMPARE(decoded, request);
}

void AppInstanceTest::decodeGarbageFails()
{
    CliRequest decoded;
    QVERIFY(!AppInstance::decode(QByteArrayLiteral("not json"), decoded));
    QVERIFY(!AppInstance::decode(QByteArray(), decoded));
}

} // namespace

QTEST_MAIN(AppInstanceTest)
#include "appinstance_test.moc"
