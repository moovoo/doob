#ifndef TESTHTTPSERVER_H
#define TESTHTTPSERVER_H

#include <QHostAddress>
#include <QHash>
#include <QObject>
#include <QPair>
#include <QTcpServer>
#include <QTcpSocket>

#include <memory>

// A minimal HTTP server (a route table, one Connection: close reply per
// request) the tests point the fetch's thumbnail URLs at (a fake yt-dlp
// answers with a JSON whose image points here) — the hermetic target
// for the thumbnail download (no network in a test). No Q_OBJECT (a
// shared header cannot provide the moc to the per-test translation
// units): the connection is a lambda, the base metaobject suffices.
class MiniHttpServer : public QObject
{
public:
    MiniHttpServer()
    {
        m_server = new QTcpServer(this);
        connect(m_server, &QTcpServer::newConnection, this, [this] {
            onNewConnection();
        });
        if (!m_server->listen(QHostAddress::LocalHost, 0))
            qWarning("testhttpserver: listen failed: %s",
                     qPrintable(m_server->errorString()));
    }

    QString baseUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1")
            .arg(m_server->serverPort());
    }
    void route(const QString &path, int status, const QByteArray &body)
    {
        m_routes.insert(path, {status, body});
    }
    QStringList requestPaths() const { return m_paths; }

private:
    void onNewConnection()
    {
        QTcpSocket *sock = m_server->nextPendingConnection();
        auto buf = std::make_shared<QByteArray>();
        connect(sock, &QTcpSocket::readyRead, this, [this, sock, buf] {
            buf->append(sock->readAll());
            if (!buf->contains("\r\n\r\n"))
                return;
            // The request line's path (query included — the routes key
            // on the full target).
            const qsizetype end = buf->indexOf("\r\n\r\n");
            const QString path =
                QString::fromUtf8(buf->left(end)).section(' ', 1, 1);
            m_paths.append(path);
            const auto it = m_routes.constFind(path);
            const bool found = it != m_routes.constEnd();
            const int status = found ? it->first : 404;
            const QByteArray body =
                found
                    ? it->second
                    : QStringLiteral(
                          R"({"error":{"status":404,"message":"no route"}})")
                          .toUtf8();
            const QByteArray head =
                (QStringLiteral("HTTP/1.1 %1 %2")
                     .arg(status)
                     .arg(status == 200 ? QStringLiteral("OK")
                                        : QStringLiteral("Forbidden"))
                 + "\r\nContent-Type: application/json\r\nContent-Length: "
                 + QByteArray::number(body.size())
                 + "\r\nConnection: close\r\n\r\n")
                    .toUtf8();
            sock->write(head + body);
            sock->disconnectFromHost();
        });
        connect(sock, &QTcpSocket::disconnected, sock,
                &QTcpSocket::deleteLater);
    }

    QTcpServer *m_server = nullptr;
    QHash<QString, QPair<int, QByteArray>> m_routes;
    QStringList m_paths;
};

#endif // TESTHTTPSERVER_H
