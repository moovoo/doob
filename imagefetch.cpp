#include "imagefetch.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QTimer>

#include <QDebug>

namespace {

// The image mime of a URL with no (usable) Content-Type: from the last
// path segment's suffix.
QString mimeFromSuffix(const QUrl &url)
{
    const QString suffix = url.path()
                               .section(QLatin1Char('/'), -1)
                               .section(QLatin1Char('.'), -1)
                               .toLower();
    if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg"))
        return QStringLiteral("image/jpeg");
    if (suffix == QLatin1String("png"))
        return QStringLiteral("image/png");
    if (suffix == QLatin1String("gif"))
        return QStringLiteral("image/gif");
    if (suffix == QLatin1String("webp"))
        return QStringLiteral("image/webp");
    if (suffix == QLatin1String("bmp"))
        return QStringLiteral("image/bmp");
    return {};
}

// The reply's Content-Type (the parameters stripped), falling back to the
// URL's suffix when the server sends nothing usable.
QString mimeForReply(const QNetworkReply *reply, const QUrl &url)
{
    const QString header =
        reply->header(QNetworkRequest::ContentTypeHeader).toString();
    QString type = header.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
    if (type.startsWith(QLatin1String("image/")))
        return type;
    return mimeFromSuffix(url);
}

} // namespace

ImageFetch::ImageFetch()
{
    m_manager = new QNetworkAccessManager;
}

void ImageFetch::reset()
{
    delete m_instance;
    m_instance = nullptr;
}

void ImageFetch::fetch(const QUrl &url, const Callback &cb)
{
    if (!url.isValid() || url.isRelative()
        || (url.scheme() != QLatin1String("http")
            && url.scheme() != QLatin1String("https"))) {
        cb(QByteArray(), QString());
        return;
    }

    const QString key = url.toString();
    const auto cached = m_cache.constFind(key);
    if (cached != m_cache.constEnd()) {
        cb(cached.value().data, cached.value().mime);
        return;
    }

    QNetworkRequest request{url};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_manager->get(request);
    // The byte cap is enforced while the download runs (a reply that
    // grows past it is aborted), and the timeout aborts a stalled one.
    QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply] {
        if (reply->size() > kMaxBytes)
            reply->abort();
    });
    QTimer::singleShot(kTimeoutMs, reply, [reply] {
        reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, reply,
            [this, reply, url, cb] {
        const QNetworkReply::NetworkError error = reply->error();
        const QByteArray data = reply->readAll();
        const QString mime = mimeForReply(reply, url);
        reply->deleteLater();
        if (error != QNetworkReply::NoError || data.isEmpty()
            || data.size() > kMaxBytes) {
            cb(QByteArray(), QString());
            return;
        }
        const QString key = url.toString();
        auto it = m_cache.find(key);
        if (it != m_cache.end()) {
            m_cacheSize += data.size() - it.value().data.size();
            it.value() = Entry{data, mime};
        } else {
            m_cache.insert(key, Entry{data, mime});
            m_order.enqueue(key);
            m_cacheSize += data.size();
        }
        // The size cap: evict the oldest entries until it is met again.
        while (m_cacheSize > kCacheBytes && !m_order.isEmpty()) {
            const QString old = m_order.dequeue();
            m_cacheSize -= m_cache.value(old).data.size();
            m_cache.remove(old);
        }
        cb(data, mime);
    });
}

void ImageFetch::clearCache()
{
    m_cache.clear();
    while (!m_order.isEmpty())
        m_order.dequeue();
    m_cacheSize = 0;
}
