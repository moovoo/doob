#include "librarymanager.h"

#include "mediaitemstore.h"
#include "sourcemanager.h"
#include "trackmetadata.h"

#include <algorithm>
#include <functional>

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QMetaObject>
#include <QPair>
#include <QSet>

namespace {
// Calls visit for every supported file under dir (recursively, in
// directory order); a missing directory visits nothing.
void forEachLibraryFile(const QUrl &dir, const std::function<void(const QUrl &)> &visit)
{
    if (!dir.isLocalFile() || !QDir(dir.toLocalFile()).exists())
        return;
    QSet<QString> supported;
    for (const QString &ext : LibraryManager::supportedExtensions())
        supported.insert(ext.toLower());
    QDirIterator it(dir.toLocalFile(), {}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        if (!supported.contains(fi.suffix().toLower()))
            continue;
        visit(QUrl::fromLocalFile(fi.absoluteFilePath()));
    }
}
}

// Reads files on a background thread; touches no LibraryManager state, so
// all results are applied on the GUI thread through the manager's slots.
// (Global namespace, not anonymous: the manager's header forward-declares it.)
class LibraryWorker : public QObject
{
    Q_OBJECT
public:
    explicit LibraryWorker(QObject *parent = nullptr) : QObject(parent) {}

    struct Source {
        quint64 id = 0;
        QUrl source;
    };

public slots:
    void readSources(const QList<QUrl> &sources)
    {
        readAllSources(sources, [this](const QUrl &source, const TrackMeta &meta) {
            emit sourceRead(source, meta);
        });
        emit finished();
    }

    void scanAndRead(const QUrl &dir, const QSet<QString> &existingSources)
    {
        forEachLibraryFile(dir, [this, &existingSources](const QUrl &url) {
            if (existingSources.contains(url.toString()))
                return;
            const TrackMeta meta = readTrackMetadata(url.toLocalFile());
            emit sourceRead(url, meta);
        });
        emit finished();
    }

    // Brings the library in line with the directory: reads the tags of
    // every supported file not in `existing` (sourceRead, for the adds)
    // and reports every `existing` item whose file is gone (itemGone, for
    // the removes).
    void syncAndRead(const QUrl &dir, const QList<Source> &existing)
    {
        QSet<QString> existingSources;
        for (const Source &s : existing)
            existingSources.insert(s.source.toString());

        QSet<QString> found;
        forEachLibraryFile(dir, [this, &existingSources, &found](const QUrl &url) {
            found.insert(url.toString());
            if (existingSources.contains(url.toString()))
                return;
            const TrackMeta meta = readTrackMetadata(url.toLocalFile());
            emit sourceRead(url, meta);
        });
        for (const Source &s : existing)
            if (!found.contains(s.source.toString()))
                emit itemGone(s.id);
        emit finished();
    }

    void resyncSources(const QList<Source> &sources)
    {
        for (const Source &s : sources) {
            const QFileInfo fi(s.source.toLocalFile());
            if (!fi.exists() || !fi.isFile()) {
                emit resyncRead(s.id, false, TrackMeta());
                continue;
            }
            const TrackMeta meta = readTrackMetadata(fi.absoluteFilePath());
            emit resyncRead(s.id, true, meta);
        }
        emit finished();
    }

signals:
    void sourceRead(const QUrl &source, const TrackMeta &meta);
    void itemGone(quint64 id);
    void resyncRead(quint64 id, bool ok, const TrackMeta &meta);
    void finished();
};
Q_DECLARE_METATYPE(LibraryWorker::Source)

LibraryManager::LibraryManager(QObject *parent)
    : QObject(parent)
{
    m_worker = new LibraryWorker;
    m_worker->moveToThread(&m_workerThread);
    connect(m_worker, &LibraryWorker::sourceRead, this, &LibraryManager::onSourceRead);
    connect(m_worker, &LibraryWorker::itemGone, this, &LibraryManager::onItemGone);
    connect(m_worker, &LibraryWorker::resyncRead, this, &LibraryManager::onResyncRead);
    connect(m_worker, &LibraryWorker::finished, this, &LibraryManager::onWorkerFinished);
    // Metadata replacements surface through the store (the metadata
    // editor — any writer): mirror the store's itemChanged for library
    // items, so the library views repaint for all of them.
    connect(MediaItemStore::getInstance(), &MediaItemStore::itemChanged,
            this, [this](const MediaItem &item) {
        if (m_libraryIds.contains(item.id))
            emit itemChanged(item);
    });
    m_workerThread.start();
}

LibraryManager::~LibraryManager()
{
    m_workerThread.quit();
    m_workerThread.wait();
    delete m_worker;
    m_worker = nullptr;
}

LibraryManager* LibraryManager::getInstance(QObject *parent)
{
    if (m_instance == nullptr) {
        m_instance = new LibraryManager(parent);
    }
    return m_instance;
}

QStringList LibraryManager::supportedExtensions()
{
    return { "mp3", "ogg", "oga", "opus", "flac", "wav", "m4a", "aac", "wma" };
}

QList<MediaItem> LibraryManager::items() const
{
    const MediaItemStore* store = MediaItemStore::getInstance();
    const QList<quint64> sorted = ids();
    QList<MediaItem> out;
    out.reserve(sorted.size());
    for (quint64 id : sorted)
        out.append(store->item(id));
    return out;
}

QList<quint64> LibraryManager::ids() const
{
    // QSet iteration is not ordered (this Qt implements it on a hash):
    // sort explicitly, the stable ascending-id order the views rely on.
    QList<quint64> out;
    out.reserve(m_libraryIds.size());
    for (quint64 id : m_libraryIds)
        out.append(id);
    std::sort(out.begin(), out.end());
    return out;
}

MediaItem LibraryManager::item(quint64 id) const
{
    return m_libraryIds.contains(id)
        ? MediaItemStore::getInstance()->item(id)
        : MediaItem();
}

bool LibraryManager::contains(quint64 id) const
{
    return m_libraryIds.contains(id);
}

MediaItem LibraryManager::findBySource(const QUrl &source) const
{
    const MediaItem item = MediaItemStore::getInstance()->findBySource(source);
    return item.inLibrary ? item : MediaItem();
}

void LibraryManager::load()
{
    m_libraryIds.clear();
    for (const MediaItem &item : MediaItemStore::getInstance()->items())
        if (item.inLibrary)
            m_libraryIds.insert(item.id);
}

QUrl LibraryManager::newSource(const QUrl &source) const
{
    // The one classification (local file / provider-claimed / plain
    // stream / unusable) — shared with the playlist import and the store.
    const SourceManager::ClassifiedSource c =
        SourceManager::getInstance()->classify(source);
    if (c.kind == SourceManager::SourceKind::Rejected)
        return {};
    // A source already in the library is not added again; a source the
    // store holds without the flag (imported into a playlist before) is
    // added under the same id.
    if (MediaItemStore::getInstance()->findBySource(c.url).inLibrary)
        return {};
    return c.url;
}

void LibraryManager::addFiles(const QList<QUrl> &sources,
                              const QHash<QString, TrackMeta> &metadata)
{
    if (m_busy) {
        qWarning("LibraryManager::addFiles: an operation is already running");
        return;
    }

    // The metadata the caller already holds for a source (the import
    // wizard's review): the hash is keyed by the pasted url, so match the
    // canonical form first, the pasted one as a fallback.
    const auto provided = [&metadata](const QUrl &pasted, const QUrl &usable) {
        return metadata.value(usable.toString(), metadata.value(pasted.toString()));
    };

    QList<QUrl> toRead;     // local files (the worker thread)
    QList<QUrl> toFetch;    // provider-claimed sources without provided metadata
    QList<QUrl> toRemote;   // plain stream URLs (no metadata to read)
    QList<QPair<QUrl, TrackMeta>> toImport;  // claimed sources the caller already read
    for (const QUrl &source : sources) {
        const QUrl usable = newSource(source);
        if (usable.isEmpty())
            continue;
        if (usable.isLocalFile()) {
            toRead.append(usable);
        } else if (SourceManager::getInstance()->providerFor(usable) != nullptr) {
            const TrackMeta given = provided(source, usable);
            if (given.isEmpty())
                toFetch.append(usable);
            else
                toImport.append({ usable, given });
        } else {
            toRemote.append(usable);
        }
    }
    if (toRead.isEmpty() && toFetch.isEmpty() && toRemote.isEmpty()
        && toImport.isEmpty())
        return;

    startOperation(Operation::Add);
    if (!toRead.isEmpty()) {
        // Queued invocation: the slot runs on the worker thread, so the
        // tag reading happens off the GUI thread and the results
        // (sourceRead / finished) come back to this thread through the
        // event loop.
        QMetaObject::invokeMethod(m_worker, &LibraryWorker::readSources,
                                  Qt::QueuedConnection, toRead);
    } else {
        m_workerDone = true;  // the worker is not started
    }
    for (const QUrl &source : toFetch)
        fetchProviderSource(source);
    for (const QUrl &source : toRemote)
        onSourceRead(source, TrackMeta());  // the item is built from the URL
    for (const QPair<QUrl, TrackMeta> &entry : toImport)
        onSourceRead(entry.first, entry.second);  // no re-fetch: already read
    // A synchronous fetch (and no worker) may have finished the operation
    // already.
    checkOperationFinished();
}

void LibraryManager::scanDirectory(const QUrl &dir)
{
    if (m_busy) {
        qWarning("LibraryManager::scanDirectory: an operation is already running");
        return;
    }
    if (!dir.isLocalFile())
        return;

    // Files already in the library are skipped; store items without the
    // flag are not (scanning a directory that holds imported files adds
    // them to the library under their existing ids).
    QSet<QString> existingSources;
    for (const MediaItem &item : items())
        existingSources.insert(item.source.toString());

    startOperation(Operation::Scan);
    QMetaObject::invokeMethod(m_worker, &LibraryWorker::scanAndRead,
                              Qt::QueuedConnection, dir, existingSources);
}

void LibraryManager::syncDirectory(const QUrl &dir)
{
    if (m_busy) {
        qWarning("LibraryManager::syncDirectory: an operation is already running");
        return;
    }
    if (!dir.isLocalFile())
        return;

    // The library items whose local file lives under the directory: the
    // candidates for a removal when their file is gone. A missing
    // directory syncs to "everything under it is gone".
    const QString path = dir.toLocalFile();
    QList<LibraryWorker::Source> existing;
    for (const MediaItem &item : items()) {
        if (item.kind != MediaItem::Local)
            continue;
        const QString file = item.source.toLocalFile();
        if (file.startsWith(path + "/"))
            existing.append({ item.id, item.source });
    }

    startOperation(Operation::Sync);
    QMetaObject::invokeMethod(m_worker, &LibraryWorker::syncAndRead,
                              Qt::QueuedConnection, dir, existing);
}

void LibraryManager::readMetadata(
    const QList<quint64> &ids,
    const std::function<void(int, int)> &progress,
    const std::function<void(const QHash<quint64, TrackMeta> &)> &finished)
{
    if (m_busy) {
        qWarning("LibraryManager::readMetadata: an operation is already "
                 "running");
        finished({});
        return;
    }
    MediaItemStore* store = MediaItemStore::getInstance();
    QList<LibraryWorker::Source> sources;
    for (quint64 id : ids) {
        const MediaItem item = store->item(id);
        if (item.kind == MediaItem::Local)
            sources.append({ id, item.source });
        // A Remote or provider item has no local file to re-read: it is
        // skipped.
    }
    if (sources.isEmpty()) {
        finished({});
        return;
    }

    m_readProgress = progress;
    m_readFinished = finished;
    m_readResults.clear();
    m_readResolved = 0;
    m_readTotal = static_cast<int>(sources.size());
    startOperation(Operation::Read);
    m_readProgress(0, m_readTotal);
    QMetaObject::invokeMethod(m_worker, &LibraryWorker::resyncSources,
                              Qt::QueuedConnection, sources);
}

void LibraryManager::startOperation(Operation op)
{
    m_operation = op;
    m_busy = true;
    m_workerDone = false;
    m_providerPending = 0;
    emit busyChanged(true);
}

void LibraryManager::onWorkerFinished()
{
    m_workerDone = true;
    checkOperationFinished();
}

void LibraryManager::checkOperationFinished()
{
    if (m_workerDone && m_providerPending == 0)
        onOperationFinished();
}

void LibraryManager::fetchProviderSource(const QUrl &source)
{
    SourceProvider* provider = SourceManager::getInstance()->providerFor(source);
    ++m_providerPending;
    provider->fetchMetadata(source, [this, source, provider](const TrackMeta &meta) {
        --m_providerPending;
        // A failed fetch (an empty TrackMeta): the source is not imported
        // (a re-add can retry it), like the worker's missing-file path —
        // never an empty-metadata item. The skip is user-visible (a
        // not-logged-in provider's hint carries the reason).
        if (meta.isEmpty()) {
            qWarning("LibraryManager: metadata fetch failed for %s",
                     qPrintable(source.toString()));
            emit sourceSkipped(source, provider->fetchFailureHint());
        } else {
            onSourceRead(source, meta);  // the worker's result path, unchanged
        }
        checkOperationFinished();
    });
}

void LibraryManager::onSourceRead(const QUrl &source, const TrackMeta &meta)
{
    MediaItemStore* store = MediaItemStore::getInstance();
    const MediaItem item = store->ensureItem(source, meta);
    if (!item.isValid())
        return;
    // Re-check: the file could have been added meanwhile (e.g. a manual
    // add during a scan of a directory containing it).
    if (item.inLibrary)
        return;
    store->setInLibrary(item.id, true);
    m_libraryIds.insert(item.id);
    emit itemAdded(store->item(item.id));
}

void LibraryManager::onItemGone(quint64 id)
{
    // The file of a library item disappeared from a synced directory:
    // remove the item from the library (it stays in the store while
    // anything references it).
    removeItem(id);
}

void LibraryManager::onResyncRead(quint64 id, bool ok, const TrackMeta &meta)
{
    // The read-only operation collects its result here (the caller applies
    // it, the store is not touched). A missing file is simply absent from
    // the result.
    if (!ok)
        return;
    m_readResults.insert(id, meta);
    ++m_readResolved;
    m_readProgress(m_readResolved, m_readTotal);
}

void LibraryManager::onOperationFinished()
{
    m_busy = false;
    if (m_operation == Operation::Read) {
        std::function<void(const QHash<quint64, TrackMeta> &)> finished =
            m_readFinished;
        m_readFinished = nullptr;
        m_readProgress = nullptr;
        finished(m_readResults);
    }
    m_operation = Operation::None;
    emit busyChanged(false);
}

bool LibraryManager::removeItem(quint64 id)
{
    if (!m_libraryIds.contains(id))
        return false;
    m_libraryIds.remove(id);
    MediaItemStore* store = MediaItemStore::getInstance();
    // The payload is the item with the flag already cleared — captured
    // before the prune, which may remove it from the store (a resolve
    // afterwards would yield an invalid item).
    MediaItem item = store->item(id);
    item.inLibrary = false;
    store->setInLibrary(id, false);
    // The item stays in the store while any playlist references it; when
    // this was the last reference (and it is local, non-library) the rows
    // are pruned right now.
    store->pruneIfUnreferenced(id);
    emit itemRemoved(item);
    return true;
}

void LibraryManager::addItems(const QList<quint64> &ids)
{
    MediaItemStore* store = MediaItemStore::getInstance();
    for (quint64 id : ids) {
        // Already a library item: the flag is set, nothing to do (a
        // duplicate add is not a second addition).
        if (m_libraryIds.contains(id))
            continue;
        const MediaItem item = store->item(id);
        if (!item.isValid()) {
            qWarning("LibraryManager::addItems: the store does not hold "
                     "item %llu",
                     static_cast<unsigned long long>(id));
            continue;
        }
        // The write-through chain, like onSourceRead: the flag first
        // (the store persists it), then the membership set, then the
        // signal.
        store->setInLibrary(id, true);
        m_libraryIds.insert(id);
        emit itemAdded(store->item(id));
    }
}

void LibraryManager::clear()
{
    if (m_libraryIds.isEmpty())
        return;
    MediaItemStore* store = MediaItemStore::getInstance();
    for (quint64 id : m_libraryIds) {
        store->setInLibrary(id, false);
        store->pruneIfUnreferenced(id);
    }
    m_libraryIds.clear();
    emit itemsCleared();
}

#include "librarymanager.moc"
