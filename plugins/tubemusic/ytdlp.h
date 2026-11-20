#ifndef YTDLP_H
#define YTDLP_H

#include <QObject>

#include <QProcess>

#include <functional>

// The setting the binary's path is stored under (the SettingsManager;
// the keys are prefixed with the plugin's id — a convention, the SettingsManager
// does not enforce it).
inline const char *kYtdlpKey = "tubemusic.ytdlp";

// The yt-dlp process runner: a small pool of QProcess objects shared by
// all runs — a finished process's pipes stay open until the object is
// destroyed, so a per-run process would exhaust the file-descriptor
// limit on a big import; the pool also caps the burst concurrency and
// queues the excess. The binary's path comes from the "tubemusic.ytdlp"
// setting (default "yt-dlp" on the PATH) and is re-read on every run, so
// a settings-page save applies to the next fetch/play.
class YtdlpRunner : public QObject
{
    Q_OBJECT
public:
    explicit YtdlpRunner(QObject *parent = nullptr);

    // Runs the binary with args asynchronously; done lands on the GUI
    // thread with the stdout (empty on any failure, including the
    // timeout and a failed start), exactly once. timeoutMs is the
    // per-run kill timeout (downloads get a generous one).
    void run(const QStringList &args,
             const std::function<void(const QString &)> &done,
             qint64 timeoutMs = 30000);

private:
    QString binaryPath() const;
    void startRun(const QStringList &args,
                  const std::function<void(const QString &)> &done,
                  qint64 timeoutMs);
    // Recycles a finished/failed process into the pool and drains the
    // queue.
    void finishRun(QProcess *proc);
    void drainQueue();

    // The process pool: at most kPoolSize live QProcess objects (a
    // constant number of pipes) — free and running. Runs beyond the
    // pool queue. A process's "gen" property tags its current run
    // (a stale watchdog must not kill a reused process).
    QList<QProcess*> m_free;
    int m_running = 0;
    struct Pending
    {
        QStringList args;
        std::function<void(const QString &)> done;
        qint64 timeoutMs = 30000;
    };
    QList<Pending> m_queue;
    static constexpr int kPoolSize = 4;
};

#endif // YTDLP_H
