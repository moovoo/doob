#include "spotifyipc.h"

#include <QJsonDocument>
#include <QProcess>

SpotifyIpc::SpotifyIpc(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
{
    // The helper's stderr (Chromium and Qt noise, the page's console
    // mirrored by the helper) goes to the parent's stderr.
    m_process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    connect(m_process, &QProcess::readyReadStandardOutput, this,
            &SpotifyIpc::onStdout);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(
                           &QProcess::finished),
            this, &SpotifyIpc::onExited);
}

SpotifyIpc::~SpotifyIpc()
{
    stop();
}

bool SpotifyIpc::start(const QString &program, const QStringList &arguments,
                       const QProcessEnvironment &environment)
{
    if (m_process->state() != QProcess::NotRunning)
        return false;
    m_buffer.clear();
    m_stopping = false;
    // This Qt's QProcess has no environment-taking start() overload:
    // the environment is applied at the start.
    m_process->setProcessEnvironment(environment);
    m_process->start(program, arguments);
    return m_process->waitForStarted(5000);
}

void SpotifyIpc::stop()
{
    if (m_process->state() == QProcess::NotRunning)
        return;
    m_stopping = true;
    send({{QStringLiteral("cmd"), QStringLiteral("quit")}});
    if (!m_process->waitForFinished(2000)) {
        m_process->terminate();
        if (!m_process->waitForFinished(2000))
            m_process->kill();
        m_process->waitForFinished(1000);
    }
}

bool SpotifyIpc::isRunning() const
{
    return m_process->state() == QProcess::Running;
}

void SpotifyIpc::send(const QJsonObject &command)
{
    if (m_process->state() != QProcess::Running)
        return;
    const QByteArray line =
        QJsonDocument(command).toJson(QJsonDocument::Compact) + '\n';
    // QProcess is a QIODevice whose write() goes straight to the pipe
    // (this Qt has no flush() to ask for).
    m_process->write(line);
}

void SpotifyIpc::onStdout()
{
    m_buffer.append(m_process->readAllStandardOutput());
    qsizetype newline = m_buffer.indexOf('\n');
    while (newline >= 0) {
        const QByteArray line = m_buffer.left(newline).trimmed();
        m_buffer.remove(0, newline + 1);
        if (!line.isEmpty())
            handleEvent(QJsonDocument::fromJson(line).object());
        newline = m_buffer.indexOf('\n');
    }
}

void SpotifyIpc::onExited(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(status);
    if (m_stopping)
        return;  // the clean exit
    emit helperExited(exitCode);
}

void SpotifyIpc::handleEvent(const QJsonObject &event)
{
    switch (event.value(QStringLiteral("type")).toString().toLatin1()[0]) {
    case 'r':
        if (event.value(QStringLiteral("device")).toString().isEmpty())
            break;
        emit ready(event.value(QStringLiteral("device")).toString());
        break;
    case 'n':
        emit notReady();
        break;
    case 'c':
        emit connectResult(event.value(QStringLiteral("ok")).toBool());
        break;
    case 's':
        emit state(event);
        break;
    case 'p':
        if (event.value(QStringLiteral("type"))
                .toString()
                == QLatin1String("play-result"))
            emit playResult(event.value(QStringLiteral("ok")).toBool(),
                            event.value(QStringLiteral("error")).toString());
        else
            emit position(event.value(QStringLiteral("position"))
                              .toVariant()
                              .toLongLong(),
                          event.value(QStringLiteral("duration"))
                              .toVariant()
                              .toLongLong());
        break;
    case 'e':
        emit playerError(event);
        break;
    case 'l':
        emit logLine(event.value(QStringLiteral("line")).toString());
        break;
    default:
        break;
    }
}
