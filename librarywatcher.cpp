#include "librarywatcher.h"

#include "librarymanager.h"
#include "settingsmanager.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QUrl>

#include <algorithm>

LibraryWatcher* LibraryWatcher::getInstance()
{
    if (m_instance == nullptr)
        m_instance = new LibraryWatcher;
    return m_instance;
}

LibraryWatcher::LibraryWatcher(QObject *parent)
    : QObject(parent)
{
    // The watch list (like the other registrations, idempotent).
    SettingsManager::getInstance()->registerSetting(
        { QStringLiteral("library.watch.dirs"),
          QStringLiteral("Watched directories"),
          QStringLiteral("The directories kept in line with the library: "
                         "files added to them are imported, deleted files "
                         "are removed."),
          QStringList(),
          QString(), });

    connect(&m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &LibraryWatcher::onDirectoryChanged);
    connect(LibraryManager::getInstance(), &LibraryManager::busyChanged,
            this, &LibraryWatcher::onBusyChanged);
}

void LibraryWatcher::load()
{
    m_dirs = SettingsManager::getInstance()->value("library.watch.dirs")
                 .toStringList();
    for (const QString &dir : m_dirs) {
        watchTree(dir);
        requestSync(dir);  // the initial import (serialized via the queue)
    }
}

bool LibraryWatcher::addWatchDir(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isDir()) {
        qWarning("LibraryWatcher::addWatchDir: %s is not a directory",
                 qPrintable(path));
        return false;
    }
    const QString dir = info.absoluteFilePath();
    if (m_dirs.contains(dir)) {
        qWarning("LibraryWatcher::addWatchDir: %s is already watched",
                 qPrintable(dir));
        return false;
    }
    m_dirs.append(dir);
    persist();
    watchTree(dir);
    emit watchDirsChanged();
    requestSync(dir);  // the initial import
    return true;
}

bool LibraryWatcher::removeWatchDir(const QString &path)
{
    const QString dir = QFileInfo(path).absoluteFilePath();
    if (!m_dirs.removeOne(dir))
        return false;

    // The directory and the subdirectories watched beneath it.
    QStringList toRemove;
    for (const QString &p : m_watcher.directories())
        if (p == dir || p.startsWith(dir + "/"))
            toRemove.append(p);
    m_watcher.removePaths(toRemove);
    m_pending.remove(dir);
    if (QTimer *timer = m_debounce.take(dir))
        timer->deleteLater();
    persist();
    emit watchDirsChanged();
    return true;
}

void LibraryWatcher::clear()
{
    m_watcher.removePaths(m_watcher.directories());
    m_dirs.clear();
    m_pending.clear();
    for (QTimer *timer : std::as_const(m_debounce))
        timer->deleteLater();
    m_debounce.clear();
}

void LibraryWatcher::persist()
{
    SettingsManager::getInstance()->setValue("library.watch.dirs", m_dirs);
}

void LibraryWatcher::watchTree(const QString &dir)
{
    if (QFileInfo(dir).isDir() && !m_watcher.directories().contains(dir))
        m_watcher.addPath(dir);
    QDirIterator it(dir, QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString sub = it.next();
        if (!m_watcher.directories().contains(sub))
            m_watcher.addPath(sub);
    }
}

void LibraryWatcher::requestSync(const QString &dir)
{
    m_pending.insert(dir);
    // Coalesce the burst: the clock restarts on every event, so the
    // sync runs once the directory has been quiet (a rescan of a big
    // tree per event would keep the library permanently busy).
    QTimer *timer = m_debounce.value(dir);
    if (timer == nullptr) {
        timer = new QTimer(this);
        timer->setSingleShot(true);
        timer->setInterval(kDebounceMs);
        connect(timer, &QTimer::timeout, this, [this] { drainPending(); });
        m_debounce.insert(dir, timer);
    }
    timer->start();
}

void LibraryWatcher::drainPending()
{
    if (LibraryManager::getInstance()->busy() || m_pending.isEmpty())
        return;
    // QSet iteration is unordered: pick a deterministic (sorted) first.
    QStringList sorted(m_pending.values());
    std::sort(sorted.begin(), sorted.end());
    const QString dir = sorted.first();
    m_pending.remove(dir);
    // A directory that is no longer watched (it was deleted) is synced
    // once, and its clock is retired with it.
    if (!m_dirs.contains(dir) && m_debounce.contains(dir)) {
        m_debounce.take(dir)->deleteLater();
    }
    LibraryManager::getInstance()->syncDirectory(QUrl::fromLocalFile(dir));
}

void LibraryWatcher::onDirectoryChanged(const QString &dir)
{

    // A watched subdirectory can have appeared: re-register the tree
    // (idempotent) so changes inside it are caught from now on.
    watchTree(dir);

    // Sync the top-level watch directory the change belongs to (the
    // sync walks the whole tree, so a nested change is covered).
    for (const QString &top : m_dirs) {
        if (dir == top || dir.startsWith(top + "/")) {
            if (dir == top && !QFileInfo(dir).isDir()) {
                // The directory itself is gone: unwatch it (which also
                // cancels any pending sync) and request the sync again —
                // it walks the vanished tree and removes the items.
                removeWatchDir(dir);
                requestSync(dir);
            } else {
                requestSync(top);
            }
            return;
        }
    }
    // The directory is no longer in the list (e.g. it was removed while
    // the event was in flight): nothing to do.
}

void LibraryWatcher::onBusyChanged(bool busy)
{
    if (busy)
        return;
    drainPending();
}
