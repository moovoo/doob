#ifndef APPINSTANCE_H
#define APPINSTANCE_H

#include <QByteArray>
#include <QLocalServer>
#include <QObject>

#include "cliimport.h"

// The single-instance gate: one doob per data directory. The first
// instance owns a local server socket named after its data directory;
// a second invocation connects to it, forwards its command line (the
// sources and the play/new-playlist flags) and exits — the "play with
// doob" / "append to playlist" context-menu use. The primary routes the
// forwarded request through the same CliImport as its own command line.
class AppInstance : public QObject
{
    Q_OBJECT
public:
    explicit AppInstance(QObject *parent = nullptr);

    // The server socket's name: derived from the data directory, so a
    // doob with its own DOOB_DATA_DIR is a separate instance.
    static QString serverName();

    // Takes the gate: when no doob is running, listens on serverName()
    // and returns true (this instance owns it). When one is, forwards
    // request to it and returns false (the caller exits). A socket file
    // left by a crashed predecessor is removed by the listen.
    bool acquire(const CliRequest &request);

    // The wire format (one line of JSON): the test seam and the encoding
    // of the forwarded command line, one place.
    static QByteArray encode(const CliRequest &request);
    static bool decode(const QByteArray &data, CliRequest &out);

signals:
    // A second doob forwarded its command line (decoded).
    void payloadReceived(const CliRequest &request);

private slots:
    void onNewConnection();

private:
    QLocalServer m_server;
};

#endif // APPINSTANCE_H
