#ifndef MEDIAITEMSTORE_H
#define MEDIAITEMSTORE_H

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>

#include <functional>

#include "database.h"
#include "mediaitem.h"
#include "trackmetadata.h"

// The single item universe: the canonical home of every item the app knows —
// library items (inLibrary) and standalone items (imported directly into a
// playlist). One table, one id counter: an id identifies exactly one item
// and a source appears at most once, and playlists, views and playback all
// resolve ids through this store (resolve()).
//
// Library membership is the in_library column on the item: the
// LibraryManager flips it (setInLibrary), the store persists it. Removing an
// item from the library clears the flag only — the item stays here, any
// playlist reference keeps resolving, and the next save prunes it when
// nothing references it anymore.
//
// The next id is not persisted — it is derived at load from the highest id
// in the table (explicit ids move the counter past them on insert).
//
// Mutations are **write-through**: each one updates the in-memory state
// and, in one transaction, the item's rows in the database. A failed
// persist rolls the memory change back, so the two never drift — a crash
// at any point leaves the database consistent and current.
//
// Pruning is at mutation time, not save time: when an item leaves the
// library or its last playlist reference is removed, pruneIfUnreferenced()
// deletes the row if it is not in the library, is referenced by no
// playlist and is kind == Local — a non-local row may be the only copy of
// its metadata. (save() still omits such items from its snapshot.)
class MediaItemStore : public QObject
{
    Q_OBJECT
public:
    static MediaItemStore* getInstance(QObject *parent = nullptr);

    void setStoragePath(const QString &path);
    void load();
    // Persists the full snapshot (repair/migration tooling, not the normal
    // path — mutations are write-through). Omits items that are not in the
    // library, not referenced by any playlist and are kind == Local.
    // Returns false (having rolled back) when any statement fails — the
    // previous snapshot stays intact.
    bool save();

    // Allocates a fresh id from the counter (monotonic, never reused within
    // a session).
    quint64 allocateId();
    // Inserts an item into the store and its rows into the database. An
    // item with id 0 is allocated an id (written back into the argument);
    // an item with an explicit id keeps it (the counter moves past it).
    // Fails when the id or the source is already used (the argument's id is
    // untouched), or when the database write fails (the memory change is
    // rolled back).
    bool insert(MediaItem &item);
    // Removes the item from the store and the database. Fails (having
    // rolled the memory change back) when the id is unknown or the database
    // write fails.
    bool remove(quint64 id);
    // Deletes the item when it is prunable: not in the library, referenced
    // by no playlist and kind == Local. A no-op otherwise (and when the id
    // is unknown). The removal call sites (library removal, playlist row
    // removal) are the callers.
    void pruneIfUnreferenced(quint64 id);
    bool contains(quint64 id) const;
    MediaItem item(quint64 id) const;
    MediaItem findBySource(const QUrl &source) const;
    // The import primitive: the store's item for the source when it holds
    // it (meta is ignored, so an import is idempotent), otherwise a new
    // item built from meta. A new item requires an existing local file;
    // anything else yields an invalid MediaItem.
    MediaItem ensureItem(const QUrl &source, const TrackMeta &meta);
    // Flips the library-membership flag (memory and database).
    bool setInLibrary(quint64 id, bool inLibrary);
    // Replaces the stored item's metadata in place, memory and database
    // (the id must exist; the source is kept as stored); emits itemChanged
    // on success. The library's resync is the caller. Returns false when
    // the id is unknown or the database write fails (memory restored).
    bool updateItem(const MediaItem &item);
    // Records one play of the item (playCount++, lastPlayedAt = now),
    // write-through; emits itemChanged on success. The playback manager is
    // the caller. A no-op (false) when the id is unknown.
    bool registerPlay(quint64 id);
    // All items / their ids, ascending id.
    QList<MediaItem> items() const;
    QList<quint64> ids() const;
    int count() const { return static_cast<int>(m_items.size()); }

    // The item's artwork (a side table, item_artwork — the blob never
    // rides the MediaItem value). The RAM state is the set of ids that
    // have art; the bytes are served from the database on demand.
    bool hasArtwork(quint64 id) const;
    // The item's art (mime + bytes); false when the item has none.
    bool artwork(quint64 id, QString *mime, QByteArray *data);
    // Replaces the item's art, write-through (one transaction, the
    // memory set and the database move together); empty data removes it.
    // Emits itemArtworkChanged on success. A no-op (false) when the id
    // is unknown. The import paths (after the item lands) and the
    // resync's appliers are the callers.
    bool setArtwork(quint64 id, const QString &mime, const QByteArray &data);

    // Test seam: drops the in-memory items (the counter is not reset, ids
    // are never reused).
    void clear();

    // Canonical lookup: the item with the id, or an invalid MediaItem when
    // unknown (a dangling reference renders empty/invalid without dropping
    // the row that names it).
    static MediaItem resolve(quint64 id);

signals:
    // The item's metadata was replaced in place (resync); the id is
    // unchanged.
    void itemChanged(const MediaItem &item);
    // The item's artwork was set, replaced or removed; the id is
    // unchanged (the item is the payload, hasArtwork() says which).
    void itemArtworkChanged(const MediaItem &item);

private:
    explicit MediaItemStore(QObject *parent = nullptr);

    bool ensureOpen();
    static QUrl normalizedSource(const QUrl &source);
    // Runs apply inside one transaction on the component's connection
    // (opening/migrating it when needed). Returns false (having rolled
    // back) when the open, apply or the commit fails.
    bool persistInTransaction(const std::function<bool(QSqlDatabase &)> &apply);
    // The write-through primitives: one item's rows (media_items plus its
    // item_meta EAV rows) inside a caller-owned transaction.
    static bool applyInsert(QSqlDatabase &db, const MediaItem &item);
    static bool applyUpdate(QSqlDatabase &db, const MediaItem &item);
    static bool applyDelete(QSqlDatabase &db, quint64 id);

private:
    QHash<quint64, MediaItem> m_items;
    QHash<QString, quint64> m_bySource;
    // The ids that carry art (the blobs stay in the database).
    QSet<quint64> m_artworkIds;
    quint64 m_nextId = 1;
    QString m_storagePath = "doob.db";
    Database m_db;

    inline static MediaItemStore* m_instance;
};

#endif // MEDIAITEMSTORE_H
