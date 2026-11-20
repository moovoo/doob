#ifndef LIBRARYMANAGER_H
#define LIBRARYMANAGER_H

#include <QHash>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QUrl>

#include <functional>

#include "mediaitem.h"
#include "trackmetadata.h"

class LibraryWorker;
class SourceProvider;

// The library is the set of items the user added to their collection. The
// items themselves live in the single item universe (MediaItemStore);
// membership is the in_library flag on each item, owned here in RAM as a set
// of ids (m_libraryIds) and persisted by the store's save() — the library
// has no database of its own. Removing an item clears the flag only: the
// item stays in the store (any playlist reference keeps resolving) and the
// store's next save prunes it when nothing references it anymore.
//
// File operations (addFiles, scanDirectory, syncDirectory, readMetadata)
// read tags on a background worker thread — one operation at a time
// (busyChanged) — and apply their results on the manager's (GUI) thread
// through the item signals. readMetadata is the only read-only operation:
// it hands the re-read tags to its callback and writes nothing.
class LibraryManager : public QObject
{
    Q_OBJECT
public:
    static LibraryManager* getInstance(QObject *parent = nullptr);
    ~LibraryManager() override;

    // Asynchronous: local files have their tags read on the worker thread
    // and provider-claimed sources are resolved by their provider (GUI
    // thread); plain http(s) stream URLs need no read and are added
    // synchronously (kind Remote). The item (if new) arrives via
    // itemAdded(). No-op for missing files, unclaimed non-local sources
    // and for sources already in the library; a source the store holds
    // without the flag (imported into a playlist before) is added under
    // the same id. `metadata` maps a source's string form to metadata the
    // caller already holds (the import wizard's review gathered it from
    // the list's own listing): such a source is imported straight from it
    // (synchronously, no provider re-fetch — the wizard already did the
    // reading); a claimed source without provided metadata is fetched as
    // usual, and a failed fetch skips it (a re-add can retry).
    void addFile(const QUrl &source) { addFiles({ source }); }
    void addFiles(const QList<QUrl> &sources,
                  const QHash<QString, TrackMeta> &metadata = {});
    // Asynchronous: enumerates the directory (recursively) on the worker
    // thread and adds new supported files via itemAdded().
    void scanDirectory(const QUrl &dir);
    // Asynchronous: brings the library in line with the directory —
    // supported files in it that are not in the library are added
    // (itemAdded), and library items whose local file lived under the
    // directory but is gone are removed (itemRemoved). The operation a
    // watched directory runs on every change (LibraryWatcher).
    void syncDirectory(const QUrl &dir);
    // Asynchronous, read-only: re-reads the tags of the given items from
    // their local files (worker thread) and hands the result to `finished`
    // on the GUI thread — nothing is written, the caller decides what to
    // do with the read values (the metadata editor previews them in its
    // table). `progress` reports (resolved, total), (0, total) up front.
    // Items that are not local files, or whose file is missing, are simply
    // not in the result. A call while an operation is running is refused
    // (the result is empty).
    void readMetadata(
        const QList<quint64> &ids,
        const std::function<void(int resolved, int total)> &progress,
        const std::function<void(const QHash<quint64, TrackMeta> &)> &finished);
    bool busy() const { return m_busy; }

    // Removes the item from the library (clears the flag); the item itself
    // stays in the store.
    bool removeItem(quint64 id);
    // Adds the items to the library (sets the flag): synchronous, the
    // items already live in the store (a playlist row, a drop). One
    // itemAdded per newly added item; ids the store does not hold are
    // refused with a qWarning.
    void addItems(const QList<quint64> &ids);
    // Removes every item from the library (clears all the flags).
    void clear();

    // Library items, ascending id.
    QList<MediaItem> items() const;
    QList<quint64> ids() const;
    MediaItem item(quint64 id) const;
    bool contains(quint64 id) const;
    // The library item with this source (normalized to an absolute local
    // path); invalid when the library does not hold it.
    MediaItem findBySource(const QUrl &source) const;
    int count() const { return static_cast<int>(m_libraryIds.size()); }

    // Builds the membership set from the store's items (the in_library
    // flag); the store must be loaded first.
    void load();

    static QStringList supportedExtensions();

signals:
    void itemAdded(const MediaItem &item);
    // Carries the removed item (the item itself stays in the store).
    void itemRemoved(const MediaItem &item);
    void itemsCleared();
    // A library item's metadata was replaced in place — mirrored from the
    // store's itemChanged for the library's items (the metadata editor,
    // any writer).
    void itemChanged(const MediaItem &item);
    void busyChanged(bool busy);
    // A source an import operation could not import (the provider's
    // metadata fetch failed — a re-add can retry it): the source as
    // pasted and the provider's failure hint (may be empty — no specific
    // reason).
    void sourceSkipped(const QUrl &source, const QString &reason);

private:
    enum class Operation { None, Add, Scan, Sync, Read };

    explicit LibraryManager(QObject *parent = nullptr);

    // Normalizes the source and checks it is usable and not yet in the
    // library: an existing local file, a provider-claimed source (stored
    // as pasted) or a plain http(s) stream URL. Returns an empty QUrl when
    // the source should not be added.
    QUrl newSource(const QUrl &source) const;
    void startOperation(Operation op);
    // The worker's finished signal plus every in-flight provider fetch
    // must have landed before the operation ends.
    void checkOperationFinished();
    // Dispatches a provider fetch on the GUI thread (the provider object
    // is GUI-threaded; the worker never touches it).
    void fetchProviderSource(const QUrl &source);

private slots:
    void onSourceRead(const QUrl &source, const TrackMeta &meta);
    // A library item's file disappeared from a synced directory.
    void onItemGone(quint64 id);
    void onResyncRead(quint64 id, bool ok, const TrackMeta &meta);
    void onWorkerFinished();

private:
    void onOperationFinished();

private:
    // The membership set: the ids of the store's items whose in_library
    // flag is on. The set is unordered (hash-based); items() and ids()
    // sort explicitly into the stable ascending-id order.
    QSet<quint64> m_libraryIds;
    QThread m_workerThread;
    LibraryWorker* m_worker = nullptr;
    Operation m_operation = Operation::None;
    bool m_busy = false;
    // Operation completion: the worker's finished signal (not started when
    // an operation has no local files) plus the in-flight provider fetches.
    bool m_workerDone = true;
    int m_providerPending = 0;
    // The read-only operation's state (the Read operation is the only one
    // with callbacks): the progress/finished callbacks and the result
    // collected from the worker's per-file reads.
    std::function<void(int resolved, int total)> m_readProgress;
    std::function<void(const QHash<quint64, TrackMeta> &)> m_readFinished;
    QHash<quint64, TrackMeta> m_readResults;
    int m_readResolved = 0;
    int m_readTotal = 0;

    inline static LibraryManager* m_instance;
};

Q_DECLARE_METATYPE(QList<MediaItem>)

#endif // LIBRARYMANAGER_H
