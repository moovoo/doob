#include <QtTest>

#include <QNetworkProxyFactory>

#include "imagefetch.h"
#include "testhttpserver.h"

#include <memory>

namespace {

class ImageFetchTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();

    void fetchSuccessReturnsBytesAndContentType();
    void fetchMimeFallsBackToUrlSuffix();
    void fetchMimeEmptyWithoutTypeAndSuffix();
    void httpErrorAnswersEmpty();
    void emptyBodyAnswersEmpty();
    void invalidAndNonHttpUrlsAnswerEmptyAtOnce();
    void secondFetchIsACacheHit();
    void clearCacheForcesRefetch();
    void oversizedReplyAnswersEmpty();
    void cacheEvictsOldestEntriesBeyondTheCap();

private:
    std::unique_ptr<MiniHttpServer> m_server;
};

void ImageFetchTest::initTestCase()
{
    // The fetches reach a local fake server: no system proxy in between
    // (a dead proxy would break the loopback requests).
    QNetworkProxyFactory::setUseSystemConfiguration(false);
}

void ImageFetchTest::cleanupTestCase()
{
    // The singleton's QNetworkAccessManager is parent-less and would
    // outlive the test process state.
    ImageFetch::reset();
}

void ImageFetchTest::init()
{
    m_server = std::make_unique<MiniHttpServer>();
    QVERIFY(m_server->baseUrl().startsWith("http://127.0.0.1:"));
    ImageFetch::getInstance()->clearCache();
}

void ImageFetchTest::fetchSuccessReturnsBytesAndContentType()
{
    m_server->route("/cover.jpg", 200, QByteArray("jpeg-bytes"),
                    QStringLiteral("image/jpeg"));

    QByteArray data;
    QString mime;
    bool done = false;
    ImageFetch::getInstance()->fetch(m_server->baseUrl() + "/cover.jpg",
                                     [&](const QByteArray &d,
                                         const QString &m) {
        data = d;
        mime = m;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);

    QCOMPARE(data, QByteArray("jpeg-bytes"));
    QCOMPARE(mime, QString("image/jpeg"));
}

void ImageFetchTest::fetchMimeFallsBackToUrlSuffix()
{
    // No usable Content-Type (the server sends application/json): the
    // mime is the URL's suffix.
    m_server->route("/cover.png", 200, QByteArray("png-bytes"));

    QByteArray data;
    QString mime;
    bool done = false;
    ImageFetch::getInstance()->fetch(m_server->baseUrl() + "/cover.png",
                                     [&](const QByteArray &d,
                                         const QString &m) {
        data = d;
        mime = m;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);

    QCOMPARE(data, QByteArray("png-bytes"));
    QCOMPARE(mime, QString("image/png"));
}

void ImageFetchTest::fetchMimeEmptyWithoutTypeAndSuffix()
{
    // No image Content-Type and no image suffix: the bytes still land,
    // the mime stays empty.
    m_server->route("/cover", 200, QByteArray("raw-bytes"));

    QByteArray data;
    QString mime;
    bool done = false;
    ImageFetch::getInstance()->fetch(m_server->baseUrl() + "/cover",
                                     [&](const QByteArray &d,
                                         const QString &m) {
        data = d;
        mime = m;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);

    QCOMPARE(data, QByteArray("raw-bytes"));
    QVERIFY(mime.isEmpty());
}

void ImageFetchTest::httpErrorAnswersEmpty()
{
    m_server->route("/missing.jpg", 404, QByteArray());

    QByteArray data;
    QString mime;
    bool done = false;
    ImageFetch::getInstance()->fetch(m_server->baseUrl() + "/missing.jpg",
                                     [&](const QByteArray &d,
                                         const QString &m) {
        data = d;
        mime = m;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);

    QVERIFY(data.isEmpty());
    QVERIFY(mime.isEmpty());
}

void ImageFetchTest::emptyBodyAnswersEmpty()
{
    m_server->route("/empty.jpg", 200, QByteArray());

    QByteArray data;
    bool done = false;
    ImageFetch::getInstance()->fetch(m_server->baseUrl() + "/empty.jpg",
                                     [&](const QByteArray &d, const QString &) {
        data = d;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);

    QVERIFY(data.isEmpty());
}

void ImageFetchTest::invalidAndNonHttpUrlsAnswerEmptyAtOnce()
{
    // No request goes out: each answers synchronously (the flag is set
    // before fetch returns, no event loop needed).
    struct Case
    {
        QUrl url;
    };
    const QList<Case> cases = {
        { QUrl() },                              // invalid
        { QUrl("example.com/cover.jpg") },       // relative (no scheme)
        { QUrl("ftp://example.com/cover.jpg") }, // not http(s)
        { QUrl("file:///tmp/cover.jpg") },       // not http(s)
    };
    for (const Case &c : cases) {
        bool done = false;
        QByteArray data;
        ImageFetch::getInstance()->fetch(
            c.url, [&](const QByteArray &d, const QString &) {
                data = d;
                done = true;
            });
        QVERIFY(done);
        QVERIFY(data.isEmpty());
    }
    // Nothing was sent to the server.
    QCOMPARE(m_server->requestPaths().size(), 0);
}

void ImageFetchTest::secondFetchIsACacheHit()
{
    m_server->route("/cached.jpg", 200, QByteArray("cached-bytes"),
                    QStringLiteral("image/jpeg"));

    auto fetchInto = [this](QByteArray &data, bool &done) {
        data.clear();
        done = false;
        ImageFetch::getInstance()->fetch(m_server->baseUrl() + "/cached.jpg",
                                         [&](const QByteArray &d,
                                             const QString &) {
            data = d;
            done = true;
        });
    };

    QByteArray first;
    bool done1 = false;
    fetchInto(first, done1);
    QTRY_VERIFY_WITH_TIMEOUT(done1, 5000);
    QCOMPARE(first, QByteArray("cached-bytes"));

    // The second fetch of the same URL is served from the RAM cache: the
    // server sees exactly one request for the route.
    QByteArray second;
    bool done2 = false;
    fetchInto(second, done2);
    QTRY_VERIFY_WITH_TIMEOUT(done2, 5000);
    QCOMPARE(second, first);
    QCOMPARE(m_server->requestPaths().count("/cached.jpg"), 1);
}

void ImageFetchTest::clearCacheForcesRefetch()
{
    m_server->route("/refetch.jpg", 200, QByteArray("refetch-bytes"));

    QByteArray data;
    bool done = false;
    ImageFetch::getInstance()->fetch(m_server->baseUrl() + "/refetch.jpg",
                                     [&](const QByteArray &d, const QString &) {
        data = d;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QCOMPARE(m_server->requestPaths().count("/refetch.jpg"), 1);

    ImageFetch::getInstance()->clearCache();

    done = false;
    ImageFetch::getInstance()->fetch(m_server->baseUrl() + "/refetch.jpg",
                                     [&](const QByteArray &d, const QString &) {
        data = d;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QCOMPARE(data, QByteArray("refetch-bytes"));
    QCOMPARE(m_server->requestPaths().count("/refetch.jpg"), 2);
}

void ImageFetchTest::oversizedReplyAnswersEmpty()
{
    // Over the byte cap (10 MB): the reply is aborted, the callback gets
    // empty data (and nothing is cached).
    const QByteArray big(ImageFetch::kMaxBytes + 16, 'x');
    m_server->route("/big.jpg", 200, big);

    QByteArray data;
    bool done = false;
    ImageFetch::getInstance()->fetch(m_server->baseUrl() + "/big.jpg",
                                     [&](const QByteArray &d, const QString &) {
        data = d;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);

    QVERIFY(data.isEmpty());
}

void ImageFetchTest::cacheEvictsOldestEntriesBeyondTheCap()
{
    // Four entries of 9 MB each (36 MB > the 32 MB cap): the fourth
    // insert evicts the oldest (the first) until the cap is met again.
    const QByteArray body(static_cast<qsizetype>(9) * 1024 * 1024, 'a');
    for (const QByteArray &suffix :
         { QByteArray("a"), QByteArray("b"), QByteArray("c"), QByteArray("d") })
        m_server->route("/large-" + QString::fromLatin1(suffix) + ".jpg", 200, body);

    auto fetchInto = [this](const QString &path, QByteArray &data,
                            bool &done) {
        data.clear();
        done = false;
        ImageFetch::getInstance()->fetch(m_server->baseUrl() + path,
                                         [&](const QByteArray &d,
                                             const QString &) {
            data = d;
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
    };

    QByteArray data;
    for (const QByteArray &suffix :
         { QByteArray("a"), QByteArray("b"), QByteArray("c"), QByteArray("d") }) {
        const QString path = "/large-" + QString::fromLatin1(suffix) + ".jpg";
        bool done = false;
        fetchInto(path, data, done);
        QCOMPARE(data.size(), body.size());
    }
    QCOMPARE(m_server->requestPaths().count("/large-a.jpg"), 1);

    // a was evicted: refetching it hits the server again. b, c, d are
    // still cached.
    bool done = false;
    data.clear();
    ImageFetch::getInstance()->fetch(m_server->baseUrl() + "/large-a.jpg",
                                     [&](const QByteArray &d, const QString &) {
        data = d;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QCOMPARE(data.size(), body.size());
    QCOMPARE(m_server->requestPaths().count("/large-a.jpg"), 2);
    for (const QByteArray &suffix :
         { QByteArray("b"), QByteArray("c"), QByteArray("d") })
        QCOMPARE(m_server->requestPaths().count("/large-" +
                                                  QString::fromLatin1(suffix) +
                                                  ".jpg"),
                 1);
}

}

QTEST_MAIN(ImageFetchTest)
#include "imagefetch_test.moc"
