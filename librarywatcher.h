#ifndef LIBRARYWATCHER_H
#define LIBRARYWATCHER_H

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>

// Watches the library's watch directories: on load it imports the
// supported media files it finds (LibraryManager::syncDirectory), and it
// keeps watching afterwards — a file copied into a watched directory is
// imported, a deleted file is removed from the library (the same sync
// runs on every change). Subdirectories are watched individually (the
// filesystem watcher is not recursive) and re-registered as they appear.
//
// The directory list persists as the "library.watch.dirs" setting (the
// SettingsManager, written on every change). The syncs are serialized on
// the library's single operation: changes that arrive while the library
// is busy are queued and drained when it goes idle. The syncs are also
// coalesced: a directory is synced once it has been quiet for a moment
// (kDebounceMs), so a churn of events (a sync tool swapping files)
// triggers one rescan, not one per event.
class LibraryWatcher : public QObject
{
    Q_OBJECT
public:
    static LibraryWatcher* getInstance();

    // Reads the persisted watch directories, starts watching them and
    // imports their contents.
    void load();

    QStringList watchDirs() const { return m_dirs; }

    // Adds a directory to the watch list (persisted), starts watching it
    // and imports its contents. Fails (false, a qWarning) when the path
    // is not a directory or is already watched.
    bool addWatchDir(const QString &path);
    // Removes a directory from the watch list (persisted) and stops
    // watching it; the items already in the library are kept.
    bool removeWatchDir(const QString &path);

    // Test seam: drops the list, the queue and the watches.
    void clear();

signals:
    void watchDirsChanged();

private:
    explicit LibraryWatcher(QObject *parent = nullptr);

    void persist();
    // addPath for the directory and every existing subdirectory
    // (idempotent).
    void watchTree(const QString &dir);
    // Queues a directory for a sync and starts it when the library is
    // idle.
    void requestSync(const QString &dir);
    void drainPending();

    void onDirectoryChanged(const QString &dir);
    void onBusyChanged(bool busy);

    // A directory is synced after it has been quiet for this long.
    static constexpr int kDebounceMs = 500;

    QFileSystemWatcher m_watcher;
    QStringList m_dirs;             // the top-level watch directories
    QSet<QString> m_pending;        // directories waiting for the library to idle
    QHash<QString, QTimer *> m_debounce;  // the per-directory quiet clocks

    inline static LibraryWatcher* m_instance;
};

#endif // LIBRARYWATCHER_H
