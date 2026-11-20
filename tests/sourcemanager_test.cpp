#include <QtTest>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <utility>

#include "sourcemanager.h"
#include "testproviders.h"

namespace {

class SourceManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void registerAndLookUpById();
    void duplicateIdIsRejected();
    void unregisterRemovesTheProvider();
    void providerForReturnsTheFirstClaimant();
    void providerForYieldsNoneWithoutAClaimant();
    void classifyKinds();
    void classifyNormalizesLocalPaths();
    void fetchMetadataDeliversTheConfiguredMeta();
    void optionalPlaybackDefaultsAreEmpty();
    void clearDropsTheRegistrations();

private:
    SourceManager* m_sm = nullptr;
    FakeProvider m_a{"a", "Provider A", this};
    FakeProvider m_b{"b", "Provider B", this};
};

void SourceManagerTest::init()
{
    m_sm = SourceManager::getInstance();
    m_sm->clear();
    m_a.setAccepts({});
    m_b.setAccepts({});
}

void SourceManagerTest::cleanup()
{
    m_sm->clear();
}

void SourceManagerTest::registerAndLookUpById()
{
    QVERIFY(m_sm->registerProvider(&m_a));
    QVERIFY(m_sm->registerProvider(&m_b));

    QCOMPARE(m_sm->providers().size(), 2);
    QCOMPARE(m_sm->providerById("a"), &m_a);
    QCOMPARE(m_sm->providerById("b"), &m_b);
    QVERIFY(m_sm->providerById("nope") == nullptr);
}

void SourceManagerTest::duplicateIdIsRejected()
{
    QVERIFY(m_sm->registerProvider(&m_a));

    FakeProvider twin("a", "Twin A", this);
    QVERIFY(!m_sm->registerProvider(&twin));
    QCOMPARE(m_sm->providers().size(), 1);
    QCOMPARE(m_sm->providerById("a"), &m_a);
}

void SourceManagerTest::unregisterRemovesTheProvider()
{
    QVERIFY(m_sm->registerProvider(&m_a));
    m_sm->unregisterProvider(&m_a);

    QVERIFY(m_sm->providerById("a") == nullptr);
    QVERIFY(m_sm->providers().isEmpty());
}

void SourceManagerTest::providerForReturnsTheFirstClaimant()
{
    m_a.setAccepts({"https://example.com/both"});
    m_b.setAccepts({"https://example.com/both", "https://example.com/only-b"});
    QVERIFY(m_sm->registerProvider(&m_a));
    QVERIFY(m_sm->registerProvider(&m_b));

    QCOMPARE(m_sm->providerFor(QUrl("https://example.com/both")), &m_a);
    QCOMPARE(m_sm->providerFor(QUrl("https://example.com/only-b")), &m_b);
}

void SourceManagerTest::providerForYieldsNoneWithoutAClaimant()
{
    m_a.setAccepts({"https://example.com/a"});
    QVERIFY(m_sm->registerProvider(&m_a));

    QVERIFY(m_sm->providerFor(QUrl("https://example.com/unclaimed")) == nullptr);
}

void SourceManagerTest::classifyKinds()
{
    m_a.setAccepts({"https://example.com/a"});
    QVERIFY(m_sm->registerProvider(&m_a));

    // A provider-claimed source: the claimant is named, the url as pasted.
    const auto claimed = m_sm->classify(QUrl("https://example.com/a"));
    QCOMPARE(claimed.kind, SourceManager::SourceKind::Provider);
    QCOMPARE(claimed.provider, &m_a);
    QCOMPARE(claimed.url.toString(), QString("https://example.com/a"));

    // A plain stream URL (no claimant) is Remote, as pasted.
    const auto remote = m_sm->classify(QUrl("https://example.com/stream.m3u8"));
    QCOMPARE(remote.kind, SourceManager::SourceKind::Remote);
    QCOMPARE(remote.provider, static_cast<SourceProvider*>(nullptr));
    QCOMPARE(remote.url.toString(), QString("https://example.com/stream.m3u8"));

    // An unknown scheme no provider claims, and an empty url, are rejected.
    QCOMPARE(m_sm->classify(QUrl("ftp://example.com/x"))
                 .kind, SourceManager::SourceKind::Rejected);
    QCOMPARE(m_sm->classify(QUrl()).kind, SourceManager::SourceKind::Rejected);
}

void SourceManagerTest::classifyNormalizesLocalPaths()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/track.flac";
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();

    // An existing local file is Local, normalized to its absolute path
    // (a relative input comes back absolute).
    const QDir previous = QDir::current();
    QVERIFY(QDir::setCurrent(dir.path()));
    const auto local = m_sm->classify(QUrl::fromLocalFile("track.flac"));
    QVERIFY(QDir::setCurrent(previous.path()));
    QCOMPARE(local.kind, SourceManager::SourceKind::Local);
    QCOMPARE(local.url.toLocalFile(), path);

    // A missing local file is rejected (nothing to import).
    QCOMPARE(m_sm->classify(QUrl::fromLocalFile(dir.path() + "/gone.flac"))
                 .kind, SourceManager::SourceKind::Rejected);
}

void SourceManagerTest::fetchMetadataDeliversTheConfiguredMeta()
{
    TrackMeta meta;
    meta.title = "Fetched";
    m_a.setMeta(meta);
    m_a.setAccepts({"https://example.com/a"});

    TrackMeta got;
    m_a.fetchMetadata(QUrl("https://example.com/a"),
                      [&got](const TrackMeta &m) { got = m; });

    QCOMPARE(got.title, QString("Fetched"));
    QCOMPARE(m_a.m_fetched, QStringList{"https://example.com/a"});
}

void SourceManagerTest::optionalPlaybackDefaultsAreEmpty()
{
    MediaItem item;
    item.source = QUrl("https://example.com/track");

    QVERIFY(m_a.playableUrl(item).isEmpty());
    QVERIFY(m_a.player() == nullptr);
    QCOMPARE(m_a.shareableUrl(item), QString("https://example.com/track"));
}

void SourceManagerTest::clearDropsTheRegistrations()
{
    QVERIFY(m_sm->registerProvider(&m_a));
    m_sm->clear();

    QVERIFY(m_sm->providerById("a") == nullptr);
    QVERIFY(m_sm->providers().isEmpty());
}

}

QTEST_MAIN(SourceManagerTest)
#include "sourcemanager_test.moc"
