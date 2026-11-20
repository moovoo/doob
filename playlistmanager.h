#ifndef PLAYLISTMANAGER_H
#define PLAYLISTMANAGER_H

#include <QObject>
#include <QList>
#include <QSet>
#include <QThread>
#include <QUrl>

#include <functional>

#include "database.h"
#include "mediaitem.h"
#include "playlist.h"
#include "trackmetadata.h"

class PlaylistImportWorker;
class QMimeData;

class PlaylistManager : public QObject
{
    Q_OBJECT
public:
    static PlaylistManager* getInstance(QObject *parent = nullptr);
    ~PlaylistManager() override;

    // Current playlist. Items are referenced by id: the item data lives in
    // the single item store and itemAt()/currentItem() resolve it through
    // MediaItemStore::resolve() (a dangling id yields an invalid MediaItem).
    void addItems(const QList<quint64> &ids);
    void insertItems(int index, const QList<quint64> &ids);
    void removeAt(int index);
    bool removeItem(quint64 id);
    void clear();

    QList<quint64> itemIds() const;
    MediaItem itemAt(int index) const;
    int indexOf(quint64 id) const;
    int count() const;

    // Reorder: rows names the dragged rows (a playlist may hold the same
    // item in several rows, so the rows — not their ids — name what moves).
    void moveRows(const QList<int> &rows, int targetIndex);

    // Arbitrary playlist
    void insertItemsInto(quint64 playlistId, int index, const QList<quint64> &ids);
    void moveRowsInPlaylist(quint64 playlistId, const QList<int> &rows, int targetIndex);
    // Reorder the playlist list itself (the order the views show it in):
    // the playlist moves to toIndex (clamped; a no-op when it is already
    // there). The new order is persisted.
    bool movePlaylist(quint64 id, int toIndex);
    // The media views' drop policy, one place: a drag that started in the
    // playlist's own rows (reorder, decided by the view from the payload's
    // origin) moves the dragged rows; anything else is an insertion — one
    // row per dragged row, repetitions included, stale ids the store no
    // longer holds dropped.
    void applyDrop(quint64 playlistId, const QList<quint64> &ids,
                   const QList<int> &rows, bool reorder, int index);
    // Appends what a drop carries to the end of the playlist: a media-view
    // item drag (one row per dragged row, repetitions included, stale ids
    // the store no longer holds dropped), or files (media files, and
    // external playlist files flattened into their entries).
    void appendDrop(quint64 playlistId, const QMimeData *mime);
    // Removes the given rows of the playlist (a playlist may hold the same
    // item in several rows, so the rows — not their ids — name what is
    // removed); the current row is tracked through the removals.
    void removeRowsInPlaylist(quint64 playlistId, const QList<int> &rows);

    int currentIndex() const;
    MediaItem currentItem() const;
    void setCurrentIndex(int index);

    // Playlists
    quint64 addPlaylist(const QString &name = QString());
    bool removePlaylist(quint64 id);
    bool renamePlaylist(quint64 id, const QString &name);

    QList<Playlist> playlists() const { return m_playlists; }
    Playlist playlist(quint64 id) const;
    int playlistCount() const { return static_cast<int>(m_playlists.size()); }
    int playlistIndexOf(quint64 id) const;
    int indexOfItem(quint64 playlistId, quint64 itemId) const;
    // The ids referenced by at least one playlist (the item store's
    // save-prune uses it to decide which items the snapshot keeps).
    QSet<quint64> referencedIds() const;

    quint64 currentPlaylistId() const { return m_currentPlaylistId; }
    QString currentPlaylistName() const;
    void setCurrentPlaylist(quint64 id);
    // The playlist after (before) the current one in the list order,
    // wrapping at the ends: the playlist-switching shortcuts' targets. A
    // no-op with fewer than two playlists (switching stops playback, so
    // a single-playlist switch would be a pointless stop).
    void nextPlaylist();
    void previousPlaylist();

    void reset();

    void setStoragePath(const QString &path);
    // Persists the playlist snapshot. Returns false (having rolled back the
    // database part) when any statement fails — the previous snapshot stays
    // intact. The session state (the SettingsManager) is written
    // either way.
    bool save();
    void load();

    // Import: the tags of every source the item store does not hold are
    // read on a worker thread; the resulting items land in the playlist on
    // the GUI thread, in source order. Sources the store already holds are
    // reused as-is (no file read). A source that repeats an item the
    // target already holds appends another row — a playlist may repeat
    // an item, no add path dedups. One import at a time: a call while
    // importing() is running is refused (qWarning). An unknown playlist id
    // or a source list without usable sources is a no-op.
    // Sources: existing local files (tag read on the worker thread),
    // provider-claimed sources (fetched from their provider on the GUI
    // thread, sequentially in source order) and plain http(s) stream URLs
    // (kind Remote, no read). The imported items and rows are persisted as
    // they arrive (write-through). index (negative to append, the default)
    // is the row the imported block is inserted at: the items land one by
    // one in source order, each at index + landed, so the block ends up
    // exactly where the drop pointed.
    void importSources(quint64 playlistId, const QList<QUrl> &sources,
                       int index = -1);
    bool importing() const { return m_importing; }

    // External playlist files (M3U/M3U8, PLS). importPlaylistFile parses
    // the file, creates a new playlist named after it, marks it imported
    // (the source file is remembered) and imports its entries — one
    // standalone playlist per file. Returns 0 when the file cannot be
    // parsed (or a playlist cannot be created). exportPlaylist writes the
    // playlist's resolvable entries to an M3U8 file (dangling ids are
    // skipped). savePlaylist is exportPlaylist plus clearing the modified
    // marker (the written file is now the content).
    quint64 importPlaylistFile(const QString &path);
    bool exportPlaylist(quint64 id, const QString &path) const;
    bool savePlaylist(quint64 id, const QString &path);
    // Marks the playlist imported from the file (the source is remembered
    // so a save can write it back); clears the modified marker.
    void markImported(quint64 id, const QString &sourceFile);

signals:
    void playlistChanged(quint64 playlistId);
    void currentIndexChanged(int index);
    void playlistsChanged();
    void currentPlaylistChanged(quint64 id);
    // Every source of an import has been processed (the items and rows are
    // already persisted).
    void importFinished(quint64 playlistId);

private:
    explicit PlaylistManager(QObject *parent = nullptr);

    Playlist* playlistPtr(quint64 id);
    Playlist* currentPlaylist();
    bool ensureOpen();
    // The row-mutation pattern, one place: captures the playlist's rows
    // and current index, runs mutate (in memory only), then persists with
    // syncPlaylistRows — a failed persist restores the captured state, so
    // RAM and the database never drift. Returns true when the mutation is
    // durable (the caller then emits its signals).
    bool mutatePlaylist(Playlist &pl, const std::function<void()> &mutate);
    // Writes the full playlist snapshot in one transaction. Returns false
    // (having rolled back) when any statement fails.
    bool writePlaylistSnapshot();
    // The write-through primitives, one transaction each; return false
    // (having rolled back) on any failure. The in-memory state is the
    // session authority — the mutation methods roll it back on failure.
    bool syncPlaylistRows(quint64 playlistId);
    bool insertPlaylistRow(const Playlist &pl);
    bool removePlaylistRow(quint64 playlistId);
    bool renamePlaylistRow(quint64 playlistId, const QString &name);
    // Writes the session state (current playlist id/name, per-playlist
    // current indices) to the SettingsManager. Called on every change
    // and from save().
    bool persistSessionState();
    // Writes the in-memory playlist order (position = list index) to the
    // database in one transaction; returns false (the caller rolls the
    // memory order back) when a statement fails.
    bool persistPositions();
    // Writes the playlist's import bookkeeping (imported, source_file,
    // dirty) to its row; returns false on failure (the caller rolls the
    // flags back).
    bool persistImportState(const Playlist &pl);
    // Applies the stored per-playlist current indices to the loaded
    // playlists (stale/out-of-range entries are ignored).
    void applyStoredIndices();
    // Reads a playlist's item rows (in position order) into pl.items.
    void loadPlaylistItems(QSqlDatabase &db, Playlist &pl);

    // The item that owns the source (the store holds every item); invalid
    // when the store does not hold it.
    MediaItem sourceOwner(const QUrl &source) const;
    // The usable sources in order: existing local files, provider-claimed
    // sources (stored as pasted) and plain http(s) stream URLs — whose
    // store items are built on the spot (no metadata to read).
    QList<QUrl> normalizeSources(const QList<QUrl> &sources) const;
    void appendImportedItem(quint64 id);
    // Appends the sources at the cursor that need no tag read (known to
    // the library or the store; vanished ones are skipped) and finishes
    // the import when the cursor reaches the end. When the cursor stops at
    // a pending provider source, starts its fetch.
    void advanceImport();
    // Starts the fetch of the provider source at the cursor (a no-op when
    // the cursor sits on a local read, which the worker is delivering).
    void fetchNextProviderSource();
    void finishImport();

private slots:
    void onImportSourceRead(const QUrl &source, const TrackMeta &meta);

private:
    QList<Playlist> m_playlists;
    quint64 m_currentPlaylistId = 0;
    quint64 m_nextPlaylistId = 1;
    QString m_storagePath = "doob.db";
    Database m_db;


    // Import state (one import at a time).
    QThread m_importThread;
    PlaylistImportWorker* m_importWorker = nullptr;
    bool m_importing = false;
    quint64 m_importPlaylistId = 0;
    QList<QUrl> m_importSources;   // the normalized sources, in order
    int m_importPos = 0;           // next source to append
    QSet<QString> m_importPending; // sources whose tag read is in flight
    int m_importAt = -1;           // the row the block is inserted at (-1: append)
    int m_importLanded = 0;        // sources already appended (the insert offset)

    inline static PlaylistManager* m_instance;
};

#endif // PLAYLISTMANAGER_H
