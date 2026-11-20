#include <QtTest>

#include <QJsonDocument>
#include <QNetworkProxyFactory>

#include <memory>

#include "imagefetch.h"
#include "itunesartsource.h"
#include "settingsmanager.h"
#include "testhttpserver.h"

namespace {

class iTunesArtSourceTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();

    void buildSearchUrlPutsTheTermInTheQuery();
    void buildSearchUrlEncodesTheTerm();
    void buildTermsResolvesTheTokens();
    void buildTermsDropsTheEmptyAndUnknownParts();
    void buildTermsKeepsTheOrderAndSkipsDuplicates();
    void artworkUrlMapsTheFirstResultUpscased();
    void artworkUrlIsEmptyWithoutResults();
    void artworkUrlIsEmptyWithoutArtwork();
    void fetchSearchesAndDownloadsTheArt();
    void fetchFallsThroughOnAnEmptyPrimary();
    void fetchFallsThroughOnAnHttpError();
    void fetchAnswersEmptyWhenEveryTermMisses();
    void fetchUsesTheSettingsPatterns();
    void fetchWithoutIdentityAnswersEmptyAtOnce();
    void fetchOfAKindNotServedAnswersEmptyAtOnce();

private:
    MediaItem makeItem(const QString &artist, const QString &title,
                       const QString &album = QString()) const;
    // One server route table per test (init); the source is the test's
    // (its ctor registers the patterns setting once for the process).
    std::unique_ptr<MiniHttpServer> m_server;
    iTunesArtSource m_source{this};
};

void iTunesArtSourceTest::initTestCase()
{
    // The fetches reach a local fake server: no system proxy in between
    // (a dead proxy would break the loopback requests).
    QNetworkProxyFactory::setUseSystemConfiguration(false);
}

void iTunesArtSourceTest::cleanupTestCase()
{
    // The fetch's image half goes through the shared fetcher: drop its
    // (parent-less) state with the test process.
    ImageFetch::reset();
}

void iTunesArtSourceTest::init()
{
    m_server = std::make_unique<MiniHttpServer>();
    QVERIFY(m_server->baseUrl().startsWith(QStringLiteral("http://127.0.0.1:")));
}

MediaItem iTunesArtSourceTest::makeItem(const QString &artist,
                                        const QString &title,
                                        const QString &album) const
{
    MediaItem item;
    item.id = 1;
    item.artist = artist;
    item.title = title;
    item.album = album;
    return item;
}

void iTunesArtSourceTest::buildSearchUrlPutsTheTermInTheQuery()
{
    QCOMPARE(iTunesArtSource::buildSearchUrl(QStringLiteral("http://x"),
                                             QStringLiteral("A T")),
             QString("http://x/search?term=A%20T&media=music&limit=1"));
}

void iTunesArtSourceTest::buildSearchUrlEncodesTheTerm()
{
    // The term is percent-encoded (spaces, the & the API would parse).
    QCOMPARE(iTunesArtSource::buildSearchUrl(QStringLiteral("http://x"),
                                             QStringLiteral("A B C & D")),
             QString("http://x/search?term=A%20B%20C%20%26%20D"
                     "&media=music&limit=1"));
}

void iTunesArtSourceTest::buildTermsResolvesTheTokens()
{
    MediaItem item = makeItem(QStringLiteral("Artist"),
                              QStringLiteral("Title"),
                              QStringLiteral("Album"));
    item.albumArtist = QStringLiteral("Band");
    QCOMPARE(
        iTunesArtSource::buildTerms(
            item, QStringList{QStringLiteral("{artist} {album}")}),
        QStringList{QStringLiteral("Artist Album")});
    QCOMPARE(
        iTunesArtSource::buildTerms(
            item, QStringList{QStringLiteral("{album_artist} {album} {title}")}),
        QStringList{QStringLiteral("Band Album Title")});
}

void iTunesArtSourceTest::buildTermsDropsTheEmptyAndUnknownParts()
{
    // An empty field and an unknown token drop out (the whitespace run
    // they leave is collapsed); a pattern that resolves to nothing is
    // skipped whole.
    const MediaItem item =
        makeItem(QStringLiteral("Artist"), QStringLiteral("Title"),
                 QString());
    QCOMPARE(
        iTunesArtSource::buildTerms(
            item, QStringList{QStringLiteral("{album} {nope} {title}")}),
        QStringList{QStringLiteral("Title")});
    QCOMPARE(
        iTunesArtSource::buildTerms(
            item, QStringList{QStringLiteral("{album}")}),
        QStringList());
    // Literal text beside the tokens stays.
    QCOMPARE(
        iTunesArtSource::buildTerms(
            item, QStringList{QStringLiteral("by {artist} feat")}),
        QStringList{QStringLiteral("by Artist feat")});
}

void iTunesArtSourceTest::buildTermsKeepsTheOrderAndSkipsDuplicates()
{
    const MediaItem item =
        makeItem(QStringLiteral("A"), QStringLiteral("T"),
                 QStringLiteral("ALB"));
    QCOMPARE(
        iTunesArtSource::buildTerms(
            item, QStringList{QStringLiteral("{artist}"),
                              QStringLiteral("{artist}"),
                              QStringLiteral("{title}")}),
        (QStringList{QStringLiteral("A"), QStringLiteral("T")}));
}

void iTunesArtSourceTest::artworkUrlMapsTheFirstResultUpscased()
{
    const QJsonObject response = QJsonDocument::fromJson(
                                      QByteArrayLiteral(R"({
            "resultCount": 2,
            "results": [
                {"artworkUrl100": "http://img/a/100x100bb.jpg"},
                {"artworkUrl100": "http://img/b/100x100bb.jpg"}
            ]})"))
        .object();
    QCOMPARE(iTunesArtSource::artworkUrlFromJson(response),
             QString("http://img/a/600x600bb.jpg"));
}

void iTunesArtSourceTest::artworkUrlIsEmptyWithoutResults()
{
    const QJsonObject response = QJsonDocument::fromJson(
                                      QByteArrayLiteral(
                                          R"({"resultCount": 0, "results": []})"))
        .object();
    QCOMPARE(iTunesArtSource::artworkUrlFromJson(response), QString());
}

void iTunesArtSourceTest::artworkUrlIsEmptyWithoutArtwork()
{
    // A result without the field (a non-music hit): no art.
    const QJsonObject response = QJsonDocument::fromJson(
                                      QByteArrayLiteral(
                                          R"({"resultCount": 1, "results": [{"name": "No Art"}]})"))
        .object();
    QCOMPARE(iTunesArtSource::artworkUrlFromJson(response), QString());
}

void iTunesArtSourceTest::fetchSearchesAndDownloadsTheArt()
{
    const QString base = m_server->baseUrl();
    // The default's first pattern (the album's identity) hits: the
    // track's term is never searched.
    m_server->route(
        QStringLiteral("/search?term=A%20ALB&media=music&limit=1"), 200,
        QStringLiteral(
            R"({"resultCount":1,"results":[{"artworkUrl100":"%1/img/100x100bb.jpg"}]})")
                .arg(base)
                .toUtf8());
    const QByteArray art("cover-bytes");
    m_server->route(QStringLiteral("/img/600x600bb.jpg"), 200, art,
                    QStringLiteral("image/jpeg"));

    m_source.setBaseUrl(base);

    QString mime;
    QByteArray data;
    bool done = false;
    m_source.fetch(makeItem("A", "T", "ALB"), ArtKind::Art,
                   [&mime, &data, &done](const QString &m,
                                         const QByteArray &d) {
        mime = m;
        data = d;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);

    QCOMPARE(mime, QString("image/jpeg"));
    QCOMPARE(data, art);
    // The upscaled URL was the one fetched (not the 100x100 rendition).
    QCOMPARE(m_server->requestPaths().count(QStringLiteral("/img/600x600bb.jpg")),
             1);
    QCOMPARE(m_server->requestPaths().count(QStringLiteral("/img/100x100bb.jpg")),
             0);
    // The album term was searched, the track's was not.
    QCOMPARE(m_server->requestPaths()
                 .count(QStringLiteral("/search?term=A%20ALB&media=music&limit=1")),
             1);
    QCOMPARE(m_server->requestPaths()
                 .count(QStringLiteral("/search?term=A%20T&media=music&limit=1")),
             0);
}

void iTunesArtSourceTest::fetchFallsThroughOnAnEmptyPrimary()
{
    const QString base = m_server->baseUrl();
    m_server->route(
        QStringLiteral("/search?term=A%20ALB&media=music&limit=1"), 200,
        QByteArrayLiteral(R"({"resultCount":0,"results":[]})"));
    m_server->route(
        QStringLiteral("/search?term=A%20T&media=music&limit=1"), 200,
        QStringLiteral(
            R"({"resultCount":1,"results":[{"artworkUrl100":"%1/img/100x100bb.jpg"}]})")
                .arg(base)
                .toUtf8());
    const QByteArray art("cover-bytes");
    m_server->route(QStringLiteral("/img/600x600bb.jpg"), 200, art,
                    QStringLiteral("image/jpeg"));

    m_source.setBaseUrl(base);

    bool done = false;
    QByteArray data;
    m_source.fetch(makeItem("A", "T", "ALB"), ArtKind::Art,
                   [&data, &done](const QString &, const QByteArray &d) {
        data = d;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);

    QCOMPARE(data, art);
    QCOMPARE(m_server->requestPaths()
                 .count(QStringLiteral("/search?term=A%20ALB&media=music&limit=1")),
             1);
    QCOMPARE(m_server->requestPaths()
                 .count(QStringLiteral("/search?term=A%20T&media=music&limit=1")),
             1);
}

void iTunesArtSourceTest::fetchFallsThroughOnAnHttpError()
{
    const QString base = m_server->baseUrl();
    // The primary's route is absent (the server answers 404): the
    // track's term carries the lookup.
    m_server->route(
        QStringLiteral("/search?term=A%20T&media=music&limit=1"), 200,
        QStringLiteral(
            R"({"resultCount":1,"results":[{"artworkUrl100":"%1/img/100x100bb.jpg"}]})")
                .arg(base)
                .toUtf8());
    const QByteArray art("cover-bytes");
    m_server->route(QStringLiteral("/img/600x600bb.jpg"), 200, art,
                    QStringLiteral("image/jpeg"));

    m_source.setBaseUrl(base);

    bool done = false;
    QByteArray data;
    m_source.fetch(makeItem("A", "T", "ALB"), ArtKind::Art,
                   [&data, &done](const QString &, const QByteArray &d) {
        data = d;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);

    QCOMPARE(data, art);
    QCOMPARE(m_server->requestPaths().size(), 3); // 2 searches + the image
}

void iTunesArtSourceTest::fetchAnswersEmptyWhenEveryTermMisses()
{
    const QString base = m_server->baseUrl();
    // No routes at all: both terms 404.
    m_source.setBaseUrl(base);

    bool done = false;
    QByteArray data;
    m_source.fetch(makeItem("A", "T", "ALB"), ArtKind::Art,
                   [&data, &done](const QString &, const QByteArray &d) {
        data = d;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);

    QVERIFY(data.isEmpty());
    QCOMPARE(m_server->requestPaths().size(), 2); // both searches, no image
}

void iTunesArtSourceTest::fetchUsesTheSettingsPatterns()
{
    const QString base = m_server->baseUrl();
    m_server->route(
        QStringLiteral("/search?term=T&media=music&limit=1"), 200,
        QStringLiteral(
            R"({"resultCount":1,"results":[{"artworkUrl100":"%1/img/100x100bb.jpg"}]})")
                .arg(base)
                .toUtf8());
    const QByteArray art("cover-bytes");
    m_server->route(QStringLiteral("/img/600x600bb.jpg"), 200, art,
                    QStringLiteral("image/jpeg"));

    SettingsManager::getInstance()->setValue(
        iTunesArtSource::PatternsKey,
        QStringList{QStringLiteral("{title}")});
    m_source.setBaseUrl(base);

    bool done = false;
    QByteArray data;
    m_source.fetch(makeItem("A", "T", "ALB"), ArtKind::Art,
                   [&data, &done](const QString &, const QByteArray &d) {
        data = d;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);

    QCOMPARE(data, art);
    // The configured pattern, not the default's album-first term.
    QCOMPARE(m_server->requestPaths().size(), 2); // the search + the image
    QCOMPARE(m_server->requestPaths().first(),
             QStringLiteral("/search?term=T&media=music&limit=1"));
    SettingsManager::getInstance()
        ->resetToDefault(iTunesArtSource::PatternsKey);
}

void iTunesArtSourceTest::fetchWithoutIdentityAnswersEmptyAtOnce()
{
    bool done = false;
    QByteArray data;
    m_source.fetch(makeItem(QString(), QString()), ArtKind::Art,
                   [&data, &done](const QString &, const QByteArray &d) {
        data = d;
        done = true;
    });
    // Synchronous: the patterns resolve to nothing, no search goes out.
    QVERIFY(done);
    QVERIFY(data.isEmpty());
    QCOMPARE(m_server->requestPaths().size(), 0);
}

void iTunesArtSourceTest::fetchOfAKindNotServedAnswersEmptyAtOnce()
{
    // Lyrics is reserved: the art source does not serve it.
    bool done = false;
    QByteArray data;
    m_source.fetch(makeItem("A", "T"), ArtKind::Lyrics,
                   [&data, &done](const QString &, const QByteArray &d) {
        data = d;
        done = true;
    });
    QVERIFY(done);
    QVERIFY(data.isEmpty());
    QCOMPARE(m_server->requestPaths().size(), 0);
}

}

QTEST_MAIN(iTunesArtSourceTest)
#include "itunesartsource_test.moc"
