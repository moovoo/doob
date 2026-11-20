#include "ytdlp.h"

#include <doob/settingsmanager.h>

#include <QTimer>

YtdlpRunner::YtdlpRunner(QObject *parent)
    : QObject(parent)
{
}

QString YtdlpRunner::binaryPath() const
{
    const QString path =
        SettingsManager::getInstance()->value(kYtdlpKey).toString();
    return path.isEmpty() ? QStringLiteral("yt-dlp") : path;
}

void YtdlpRunner::run(const QStringList &args,
                      const std::function<void(const QString &)> &done,
                      qint64 timeoutMs)
{
    if (m_running >= kPoolSize) {
        // The pool is busy: the run queues until a process is free (a
        // burst — a big import, a resync — must not start more
        // processes than the pool).
        m_queue.append({ args, done, timeoutMs });
        return;
    }
    startRun(args, done, timeoutMs);
}

void YtdlpRunner::startRun(const QStringList &args,
                           const std::function<void(const QString &)> &done,
                           qint64 timeoutMs)
{
    QProcess* proc = nullptr;
    if (m_free.isEmpty())
        proc = new QProcess(this);
    else
        proc = m_free.takeFirst();
    // A recycled process still carries its previous run's connections:
    // dropping them keeps this run's result on this run's callback
    // (the stale per-run watchdogs are harmless — the generation guard
    // below spares a reused process).
    proc->disconnect();
    ++m_running;
    // A per-process run tag: a watchdog of a superseded (reused) process
    // must not kill its successor, while a genuinely hung process keeps
    // its tag and gets killed.
    const int gen = proc->property("gen").toInt() + 1;
    proc->setProperty("gen", gen);
    proc->setProperty("active", true);

    // The full command is the first thing a failure report needs to
    // reproduce the run.
    qDebug("tubemusic: running %s %s", qPrintable(binaryPath()),
           qPrintable(args.join(QLatin1Char(' '))));
    QObject::connect(proc, &QProcess::finished, this,
                     [this, proc, done](int code, QProcess::ExitStatus status) {
        const QString out =
            (code == 0 && status == QProcess::NormalExit)
                ? QString::fromUtf8(proc->readAllStandardOutput())
                : QString();
        // yt-dlp writes its diagnostics to stderr: a failed run (a 403,
        // a 429, a bot check) is undiagnosable without it, so it is
        // logged in full — on success at debug level, on failure as a
        // warning.
        const QString err = QString::fromUtf8(proc->readAllStandardError());
        if (out.isEmpty())
            qWarning("tubemusic: yt-dlp failed (exit %d, %s): %s", code,
                     status == QProcess::NormalExit ? "normal" : "crash",
                     qPrintable(err));
        else
            qDebug("tubemusic: yt-dlp: %s", qPrintable(err));
        finishRun(proc);
        done(out);
    });

    // A start that cannot even spawn (a missing binary, exhausted file
    // descriptors) emits errorOccurred and never finished: without this
    // the caller (an import cursor) would wait for a result that never
    // comes.
    QObject::connect(proc, &QProcess::errorOccurred, this,
                     [this, proc, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return; // a fatal error ends in finished
        qWarning("tubemusic: cannot start %s: %s", qPrintable(binaryPath()),
                 qPrintable(proc->errorString()));
        finishRun(proc);
        done(QString());
    });

    // A hung run (a stalled network) must not wedge the pool: kill it
    // after the run's timeout. The tag guard spares a process that has
    // been reused for a newer run in the meantime.
    QTimer::singleShot(timeoutMs, proc, [proc, gen] {
        if (proc->property("gen").toInt() == gen
            && proc->state() != QProcess::NotRunning)
            proc->kill();
    });

    proc->start(binaryPath(), args);
}

void YtdlpRunner::finishRun(QProcess *proc)
{
    if (!proc->property("active").toBool())
        return; // already recycled (an error and a finish both landed)
    proc->setProperty("active", false);
    if (proc->state() != QProcess::NotRunning)
        proc->kill();
    m_free.append(proc);
    --m_running;
    drainQueue();
}

void YtdlpRunner::drainQueue()
{
    while (m_running < kPoolSize && !m_queue.isEmpty()) {
        const Pending run = m_queue.takeFirst();
        startRun(run.args, run.done, run.timeoutMs);
    }
}
