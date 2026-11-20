#include "privatepulse.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cerrno>
#include <csignal>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

namespace {
// The null sink the WebEngine's audio lands on (the server reports
// one sink, this one).
const char *kSinkName = "doob";
// The server's protocol socket (the libpulse address's file name).
const char *kSocketName = "native";
const char *kPidFile = "pid";

// The per-launch directory's name: unique per doob process (the pid)
// and per launch within it (the sequence).
QString dirName(int sequence)
{
    return QStringLiteral("pulse-%1-%2")
        .arg(static_cast<qlonglong>(::getpid()))
        .arg(sequence);
}

bool pidAlive(qint64 pid)
{
    return pid > 0 && ::kill(static_cast<pid_t>(pid), 0) == 0;
}

// The directory's pid (the pid file in the runtime dir); -1 when
// absent or unreadable (a stale directory).
qint64 dirPid(const QString &dir)
{
    QFile file(dir + "/" + kPidFile);
    if (!file.open(QIODevice::ReadOnly))
        return -1;
    return QString::fromLatin1(file.readLine().trimmed()).toLongLong();
}
} // namespace

PrivatePulse::PrivatePulse(QObject *parent)
    : QObject(parent)
    // The server's sink (the capture): the pinned s16le to the ring's
    // interleaved stereo float (the FFmpeg driver's ring format), the
    // ring's back-pressure paces the server. The buffer is the
    // server's thread's (one writer).
    , m_server([this](const uint8_t *pcm, size_t bytes) {
          const size_t samples = bytes / sizeof(int16_t);
          m_sinkBuf.resize(samples);
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
          const auto *s = reinterpret_cast<const int16_t *>(pcm);
          for (size_t i = 0; i < samples; ++i)
              // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
               m_sinkBuf[i] = static_cast<float>(s[i]) / 32768.0F;
          m_ring.write(m_sinkBuf.data(), samples / kChannels, 0);
      })
{
}

PrivatePulse::~PrivatePulse()
{
    stop();
}

void PrivatePulse::setBaseDir(const QString &dir)
{
    m_baseDir = dir;
}

const char *PrivatePulse::sinkName()
{
    return kSinkName;
}

bool PrivatePulse::running() const
{
    return m_server.running();
}

QString PrivatePulse::serverString() const
{
    return QStringLiteral("unix:%1/%2").arg(m_socketDir, QLatin1String(kSocketName));
}

QString PrivatePulse::makeSocketDir()
{
    QDir base(m_baseDir);
    // The stale sweep: a per-launch directory whose process is gone
    // (no pid file, or a dead pid) is a leftover from a crashed doob.
    for (const QFileInfo &info :
         base.entryInfoList({QStringLiteral("pulse-*")}, QDir::Dirs)) {
        if (pidAlive(dirPid(info.absoluteFilePath())))
            continue;
        if (!QDir(info.absoluteFilePath()).removeRecursively())
            qWarning("privatepulse: cannot remove the stale directory %s",
                     qPrintable(info.fileName()));
    }
    QString path = m_baseDir + "/" + dirName(m_sequence);
    while (QFileInfo::exists(path)) {
        ++m_sequence;
        path = m_baseDir + "/" + dirName(m_sequence);
    }
    if (!base.mkpath(path)) {
        qWarning("privatepulse: cannot create the socket directory %s",
                 qPrintable(path));
        return {};
    }
    ++m_sequence;
    return {path};
}

bool PrivatePulse::start()
{
    if (running())
        return false;
    if (m_baseDir.isEmpty()) {
        qWarning("privatepulse: no base dir set");
        return false;
    }
    m_socketDir = makeSocketDir();
    if (m_socketDir.isEmpty())
        return false;
    writePidFile();
    // A fresh capture: the old frames are dropped, and the writer's
    // stop flag (a full ring at the previous stop) is cleared.
    m_ring.drain();
    const QString socket = m_socketDir + "/" + QLatin1String(kSocketName);
    if (!m_server.start(socket.toLocal8Bit().constData())) {
        qWarning("privatepulse: the server would not bind the socket %s",
                 qPrintable(socket));
        m_socketDir.clear();
        return false;
    }
    return true;
}

void PrivatePulse::stop()
{
    if (!running()) {
        m_socketDir.clear();
        return;
    }
    // A writer parked on the full ring (the pump is gone): wake it, so
    // the server's thread can exit.
    m_ring.requestStop(0);
    m_server.stop();  // joins the thread, unlinks the socket
    m_socketDir.clear();
}

void PrivatePulse::writePidFile()
{
    QFile file(m_socketDir + "/" + kPidFile);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text))
        file.write(QByteArray::number(static_cast<qlonglong>(::getpid()))
                   + "\n");
}
