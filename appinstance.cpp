#include "appinstance.h"

#include "settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>

#include <utility>

QString AppInstance::serverName()
{
    // Per data directory: two doobs with separate DOOB_DATA_DIRs are
    // separate instances. The name must stay a plain server name (on
    // Unix it keys a socket file), so everything but letters and
    // numbers is flattened to dashes.
    QString name = QLatin1String("doob-") + Settings::instance()->dataDir();
    for (QChar &c : name) {
        if (!c.isLetterOrNumber())
            c = QLatin1Char('-');
    }
    return name;
}

AppInstance::AppInstance(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QLocalServer::newConnection,
            this, &AppInstance::onNewConnection);
}

bool AppInstance::acquire(const CliRequest &request)
{
    QLocalSocket probe;
    probe.connectToServer(serverName());
    if (probe.waitForConnected(1000)) {
        // A running doob: hand it the command line and let the caller
        // exit. The write lands in the socket buffer (waitForBytesWritten
        // guarantees it), so the primary reads it when its event loop
        // turns — an ack round-trip would only delay the exit.
        probe.write(encode(request));
        probe.waitForBytesWritten(1000);
        return false;
    }

    // No one answered: this instance is the primary (a stale socket
    // file of a crashed predecessor is removed by the listen).
    if (!m_server.listen(serverName())) {
        // Not fatal: the app runs without the gate (a second invocation
        // would just fail to forward and exit).
        qWarning("AppInstance: cannot listen on %s: %s",
                 qPrintable(serverName()), qPrintable(m_server.errorString()));
    }
    return true;
}

void AppInstance::onNewConnection()
{
    while (m_server.hasPendingConnections()) {
        QLocalSocket *socket = m_server.nextPendingConnection();
        // The secondary sends one line and exits: take the line, decode
        // it, and let the disconnect delete the socket.
        connect(socket, &QLocalSocket::readyRead, socket, [this, socket] {
            const QByteArray data = socket->readAll();
            CliRequest request;
            if (decode(data, request))
                emit payloadReceived(request);
        });
        connect(socket, &QLocalSocket::disconnected, socket,
                &QObject::deleteLater);
    }
}

QByteArray AppInstance::encode(const CliRequest &request)
{
    QJsonObject object;
    QJsonArray sources;
    for (const QString &source : request.sources)
        sources.append(source);
    object.insert(QStringLiteral("sources"), sources);
    object.insert(QStringLiteral("play"), request.play);
    object.insert(QStringLiteral("newPlaylist"), request.newPlaylist);
    object.insert(QStringLiteral("name"), request.newPlaylistName);
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}

bool AppInstance::decode(const QByteArray &data, CliRequest &out)
{
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
        return false;
    const QJsonObject object = doc.object();

    CliRequest request;
    for (const QJsonValueRef &value : object.value(QStringLiteral("sources")).toArray())
        request.sources.append(value.toString());
    request.play = object.value(QStringLiteral("play")).toBool();
    request.newPlaylist = object.value(QStringLiteral("newPlaylist")).toBool();
    request.newPlaylistName = object.value(QStringLiteral("name")).toString();
    out = std::move(request);
    return true;
}
