#include "playlistmanager.h"

#include "mediaitemstore.h"
#include "medialistmodel.h"
#include "playlistfile.h"
#include "settingsmanager.h"
#include "shortcutmanager.h"
#include "sourcemanager.h"
#include "trackmetadata.h"

#include <algorithm>
#include <utility>
#include <functional>

#include <QDebug>
#include <QFileInfo>
#include <QMimeData>
#include <QMetaObject>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariantMap>

namespace {
// The v1 schema of the playlist component of doob.db.
const QStringList kV1PlaylistStatements = {
    // The current index is not a column here: it is session state (which
    // row the user was on), not content — it is kept per playlist in
    // settings.json (persistSessionState), where losing it is a non-event.
    QStringLiteral(
        "CREATE TABLE IF NOT EXISTS playlists ("
        "id INTEGER PRIMARY KEY, "
        "name TEXT NOT NULL)"),
    // A playlist row is a pure reference: the item's metadata lives in the
    // single item store, never duplicated in the playlist.
    QStringLiteral(
        "CREATE TABLE IF NOT EXISTS playlist_items ("
        "playlist_id INTEGER NOT NULL, "
        "position INTEGER NOT NULL, "
        "item_id INTEGER NOT NULL, "
        "PRIMARY KEY (playlist_id, position))"),
};

const QList<DatabaseMigration> kPlaylistMigrations = {
    { 1, [](QSqlDatabase &db) {
        return Database::execStatements(db, kV1PlaylistStatements);
    }, },
    // v2: the playlist order (position — v1 ordered by id, the creation
    // order) and the external-import bookkeeping (imported, source_file,
    // the modified marker). Each column is added only when missing, so the
    // step is idempotent.
    { 2, [](QSqlDatabase &db) {
        const QStringList columns = Database::tableColumns(db, "playlists");
        QStringList statements;
        if (!columns.contains("position"))
            statements.append(QStringLiteral(
                "ALTER TABLE playlists ADD COLUMN position INTEGER NOT NULL DEFAULT 0"));
        if (!columns.contains("imported"))
            statements.append(QStringLiteral(
                "ALTER TABLE playlists ADD COLUMN imported INTEGER NOT NULL DEFAULT 0"));
        if (!columns.contains("source_file"))
            statements.append(QStringLiteral(
                "ALTER TABLE playlists ADD COLUMN source_file TEXT NOT NULL DEFAULT ''"));
        if (!columns.contains("dirty"))
            statements.append(QStringLiteral(
                "ALTER TABLE playlists ADD COLUMN dirty INTEGER NOT NULL DEFAULT 0"));
        if (!Database::execStatements(db, statements))
            return false;
        // The existing order is the id order: make it explicit.
        QSqlQuery q(db);
        if (!q.exec(QStringLiteral("SELECT id FROM playlists ORDER BY id")))
            return false;
        int position = 0;
        while (q.next()) {
            QSqlQuery up(db);
            up.prepare(QStringLiteral("UPDATE playlists SET position = ? WHERE id = ?"));
            up.bindValue(0, position++);
            up.bindValue(1, q.value(0));
            if (!up.exec())
                return false;
        }
        return true;
    }, },
};

// The items of `list` with the `movingRows` (sorted, unique) removed and
// re-inserted at the drop position, adjusted for the removed rows before
// it, keeping the moved rows' original order. `outAt` receives the
// insertion point used.
QList<quint64> applyMove(const QList<quint64> &list, const QList<int> &movingRows,
                         int targetIndex, int *outAt)
{
    QList<quint64> moving;
    QList<quint64> remaining;
    int removedBeforeTarget = 0;
    for (int i = 0; i < list.size(); ++i) {
        if (movingRows.contains(i)) {
            moving.append(list.at(i));
            if (i < targetIndex)
                removedBeforeTarget++;
        } else {
            remaining.append(list.at(i));
        }
    }

    const int at = qBound(0, targetIndex - removedBeforeTarget,
                          static_cast<int>(remaining.size()));
    if (outAt != nullptr)
        *outAt = at;
    for (int k = 0; k < moving.size(); ++k)
        remaining.insert(at + k, moving.at(k));
    return remaining;
}

// Where the current row ends up after the move: a moved row lands at the
// insertion point, in the moved rows' original order; an unmoved row
// shifts by the moved rows that crossed it.
int currentAfterMove(int current, const QList<int> &movingRows, int at, int movedCount)
{
    if (current < 0)
        return current;
    const int rank = static_cast<int>(movingRows.indexOf(current));
    if (rank >= 0)
        return at + rank;
    int c = current;
    for (int row : movingRows) {
        if (row < c)
            c--;
    }
    if (c >= at)
        c += movedCount;
    return c;
}

// The in-range unique rows, descending: removing the larger rows first
// keeps the smaller rows' indices valid.
QList<int> removalRows(const QList<quint64> &items, const QList<int> &rows)
{
    QList<int> toRemove;
    for (int row : rows) {
        if (row < 0 || row >= items.size())
            continue;
        if (!toRemove.contains(row))
            toRemove.append(row);
    }
    std::sort(toRemove.begin(), toRemove.end(), std::greater<>());
    return toRemove;
}
}

// Reads files on a background thread; touches no PlaylistManager state, so
// all results are applied on the GUI thread through the manager's slots.
// (Global namespace, not anonymous: the manager's header forward-declares it.)
class PlaylistImportWorker : public QObject
{
    Q_OBJECT
public:
    explicit PlaylistImportWorker(QObject *parent = nullptr) : QObject(parent) {}

public slots:
    void readSources(const QList<QUrl> &sources)
    {
        readAllSources(sources, [this](const QUrl &source, const TrackMeta &meta) {
            emit sourceRead(source, meta);
        });
        emit finished();
    }

signals:
    void sourceRead(const QUrl &source, const TrackMeta &meta);
    void finished();
};

PlaylistManager::PlaylistManager(QObject *parent)
    : QObject(parent)
{
    // The session state, registered like any other setting: the current
    // playlist as id and name (the id is the exact reference — names are
    // not unique; the name keeps the file readable) and the per-playlist
    // current indices (id → row).
    SettingsManager* settings = SettingsManager::getInstance();
    settings->registerSetting(
        { QStringLiteral("playlist.current_id"),
          QStringLiteral("Current playlist id"),
          QStringLiteral("The current playlist's id (the exact "
                         "reference; the name is kept for "
                         "readability)."),
          static_cast<quint64>(0),
          QString(), });
    settings->registerSetting(
        { QStringLiteral("playlist.current_name"),
          QStringLiteral("Current playlist name"),
          QStringLiteral("The current playlist's name (the id wins; the "
                         "name keeps the file readable)."),
          QString(),
          QString(), });
    settings->registerSetting(
        { QStringLiteral("playlist.indexes"),
          QStringLiteral("Per-playlist current indices"),
          QStringLiteral("The current row of each playlist (playlist id "
                         "→ row)."),
          QVariantMap(),
          QString(), });
    // Where the File menu's "Open..." import lands: the current playlist
    // (the default, today's behaviour) or the playlist of the playlist
    // view that has keyboard focus (falling back to the current one when
    // no view has focus). The CLI path has no focus and a drop targets
    // the view it lands on, so neither uses the setting.
    settings->registerSetting(
        { QStringLiteral("import.target"),
          QStringLiteral("Import target"),
          QStringLiteral("Where the File menu's \"Open...\" import lands: "
                         "\"current\" (the current playlist) or "
                         "\"focused\" (the playlist of the focused view)."),
          QStringLiteral("current"),
          QString(), });

    m_importWorker = new PlaylistImportWorker;
    m_importWorker->moveToThread(&m_importThread);
    connect(m_importWorker, &PlaylistImportWorker::sourceRead,
            this, &PlaylistManager::onImportSourceRead);
    m_importThread.start();
    reset();

    // The playlist-switching shortcuts (the ShortcutManager's registered
    // actions): the "Shortcuts" preferences topic lists them, the user's
    // choices persist in the SettingsManager. Global by default — switching
    // playlists is driven from wherever the focus is.
    ShortcutManager* sc = ShortcutManager::getInstance();
    sc->registerAction({ QStringLiteral("playlist.next"),
                         QStringLiteral("Next playlist"), QStringLiteral("Playlist"),
                         QKeySequence(QStringLiteral("Ctrl+Shift+Right")), true,
                         [this] { nextPlaylist(); }, });
    sc->registerAction({ QStringLiteral("playlist.previous"),
                         QStringLiteral("Previous playlist"), QStringLiteral("Playlist"),
                         QKeySequence(QStringLiteral("Ctrl+Shift+Left")), true,
                         [this] { previousPlaylist(); }, });
}

PlaylistManager::~PlaylistManager()
{
    m_importThread.quit();
    m_importThread.wait();
    delete m_importWorker;
    m_importWorker = nullptr;
}

PlaylistManager* PlaylistManager::getInstance(QObject *parent)
{
    if (m_instance == nullptr) {
        m_instance = new PlaylistManager(parent);
    }
    return m_instance;
}

Playlist* PlaylistManager::playlistPtr(quint64 id)
{
    const int i = playlistIndexOf(id);
    if (i < 0)
        return nullptr;
    return &m_playlists[i];
}

Playlist* PlaylistManager::currentPlaylist()
{
    return playlistPtr(m_currentPlaylistId);
}

bool PlaylistManager::mutatePlaylist(Playlist &pl,
                                     const std::function<void()> &mutate)
{
    const QList<quint64> oldItems = pl.items;
    const int oldIndex = pl.currentIndex;
    const bool wasDirty = pl.dirty;
    mutate();
    // A content mutation marks the playlist modified (the views' * marker):
    // an import is the playlist's content arriving, not a modification of
    // it, so it is exempt (m_importing guards the whole import).
    if (pl.items != oldItems && !m_importing)
        pl.dirty = true;
    if (!syncPlaylistRows(pl.id)) {
        pl.items = oldItems;
        pl.currentIndex = oldIndex;
        pl.dirty = wasDirty;
        return false;
    }
    return true;
}

void PlaylistManager::addItems(const QList<quint64> &ids)
{
    Playlist *pl = currentPlaylist();
    if (pl == nullptr || ids.isEmpty())
        return;

    // A playlist may hold the same item any number of times: appending a
    // reference that is already there adds another row, not a no-op.
    if (!mutatePlaylist(*pl, [&pl, &ids] {
        pl->items.append(ids);
        if (pl->currentIndex < 0 && !pl->items.isEmpty())
            pl->currentIndex = 0;
    }))
        return;
    emit playlistChanged(pl->id);
    persistSessionState();  // the append may have set the current row
}

void PlaylistManager::insertItems(int index, const QList<quint64> &ids)
{
    insertItemsInto(m_currentPlaylistId, index, ids);
}

void PlaylistManager::insertItemsInto(quint64 playlistId, int index, const QList<quint64> &ids)
{
    Playlist *pl = playlistPtr(playlistId);
    if (pl == nullptr || ids.isEmpty())
        return;

    // One row per id, in order: a drag of two rows holding the same item
    // inserts two rows, and an item the playlist already holds is added
    // again rather than skipped.
    if (!mutatePlaylist(*pl, [&pl, index, &ids] {
        const int at = qBound(0, index, static_cast<int>(pl->items.size()));
        int pos = at;
        for (quint64 id : ids)
            pl->items.insert(pos++, id);

        if (at <= pl->currentIndex)
            pl->currentIndex += static_cast<int>(ids.size());
        pl->currentIndex = std::max(pl->currentIndex, 0);
    }))
        return;
    emit playlistChanged(playlistId);
    persistSessionState();  // the insertion shifts the current row
}

void PlaylistManager::removeAt(int index)
{
    removeRowsInPlaylist(m_currentPlaylistId, { index });
}

void PlaylistManager::removeRowsInPlaylist(quint64 playlistId, const QList<int> &rows)
{
    Playlist *pl = playlistPtr(playlistId);
    if (pl == nullptr || rows.isEmpty())
        return;

    const QList<int> toRemove = removalRows(pl->items, rows);
    if (toRemove.isEmpty())
        return;

    QList<quint64> removedIds;
    if (!mutatePlaylist(*pl, [&pl, &toRemove, &removedIds] {
        for (int row : toRemove) {
            removedIds.append(pl->items.at(row));
            pl->items.removeAt(row);
            if (row == pl->currentIndex)
                // The current row is gone: the next row slides into its
                // place (or the playlist becomes empty).
                pl->currentIndex = pl->items.isEmpty() ? -1 : row;
            else if (row < pl->currentIndex)
                pl->currentIndex--;
        }

        if (pl->currentIndex >= pl->items.size())
            pl->currentIndex = pl->items.isEmpty() ? -1
                                                   : static_cast<int>(pl->items.size()) - 1;
    }))
        return;
    emit playlistChanged(playlistId);
    persistSessionState();  // the removals track the current row

    // Prune-at-mutation: a removed row that no longer names a referenced,
    // library, non-local item deletes the item's rows right now.
    MediaItemStore* store = MediaItemStore::getInstance();
    for (quint64 id : removedIds)
        store->pruneIfUnreferenced(id);
}

bool PlaylistManager::removeItem(quint64 id)
{
    const int i = indexOf(id);
    if (i < 0)
        return false;
    removeAt(i);
    return true;
}

void PlaylistManager::clear()
{
    Playlist *pl = currentPlaylist();
    if (pl == nullptr)
        return;
    if (pl->items.isEmpty() && pl->currentIndex < 0)
        return;

    if (!mutatePlaylist(*pl, [&pl] {
        pl->items.clear();
        pl->currentIndex = -1;
    }))
        return;
    emit playlistChanged(pl->id);
    persistSessionState();
}

QList<quint64> PlaylistManager::itemIds() const
{
    const int i = playlistIndexOf(m_currentPlaylistId);
    if (i < 0)
        return {};
    return m_playlists.at(i).items;
}

MediaItem PlaylistManager::itemAt(int index) const
{
    const int i = playlistIndexOf(m_currentPlaylistId);
    if (i < 0)
        return {};
    const QList<quint64> &ids = m_playlists.at(i).items;
    if (index < 0 || index >= ids.size())
        return {};
    // Resolved at read time: a resync of the item is visible without any
    // copy to refresh. A dangling id (the store does not hold the item)
    // yields an invalid MediaItem.
    return MediaItemStore::resolve(ids.at(index));
}

int PlaylistManager::indexOf(quint64 id) const
{
    const int i = playlistIndexOf(m_currentPlaylistId);
    if (i < 0)
        return -1;
    return static_cast<int>(m_playlists.at(i).items.indexOf(id));
}

int PlaylistManager::count() const
{
    const int i = playlistIndexOf(m_currentPlaylistId);
    if (i < 0)
        return 0;
    return static_cast<int>(m_playlists.at(i).items.size());
}

void PlaylistManager::moveRows(const QList<int> &rows, int targetIndex)
{
    moveRowsInPlaylist(m_currentPlaylistId, rows, targetIndex);
}

void PlaylistManager::moveRowsInPlaylist(quint64 playlistId, const QList<int> &rows, int targetIndex)
{
    Playlist *pl = playlistPtr(playlistId);
    if (pl == nullptr)
        return;

    // The dragged rows (sorted, unique); out-of-range entries are dropped.
    QList<int> movingRows;
    for (int row : rows) {
        if (row < 0 || row >= pl->items.size())
            continue;
        if (!movingRows.contains(row))
            movingRows.append(row);
    }
    std::sort(movingRows.begin(), movingRows.end());
    if (movingRows.isEmpty())
        return;

    // Dropping the selection onto a position inside its own span is a
    // no-op: the rows return to their original places. (For a single row
    // the position math already yields a no-op; for a non-contiguous
    // selection applyMove would reorder the rows in between.)
    if (targetIndex >= movingRows.first() && targetIndex <= movingRows.last())
        return;

    // The rows — not their ids — name what moves: a playlist may hold the
    // same item in several rows, and dragging one copy must not drag the
    // others along.
    if (!mutatePlaylist(*pl, [&pl, &movingRows, targetIndex] {
        int at = 0;
        pl->items = applyMove(pl->items, movingRows, targetIndex, &at);
        pl->currentIndex = currentAfterMove(pl->currentIndex, movingRows, at,
                                            static_cast<int>(movingRows.size()));
    }))
        return;
    emit playlistChanged(playlistId);
    persistSessionState();  // the move may move the current row
}

void PlaylistManager::applyDrop(quint64 playlistId, const QList<quint64> &ids,
                                const QList<int> &rows, bool reorder, int index)
{
    if (ids.isEmpty())
        return;
    if (reorder) {
        moveRowsInPlaylist(playlistId, rows, index);
        return;
    }
    // Insert the resolvable ids; a stale drag that names an unknown id is
    // dropped.
    QList<quint64> toInsert;
    for (quint64 id : ids)
        if (MediaItemStore::resolve(id).isValid())
            toInsert.append(id);
    insertItemsInto(playlistId, index, toInsert);
}

void PlaylistManager::appendDrop(quint64 playlistId, const QMimeData *mime)
{
    // A drag of items from a media view: one row per dragged row,
    // repetitions included, stale ids the store no longer holds dropped.
    if (mime->hasFormat(MediaListModel::mediaMimeType())) {
        const MediaListModel::DragData drag =
            MediaListModel::dragDataFromMimeData(mime);
        applyDrop(playlistId, drag.ids, drag.rows, false,
                  static_cast<int>(playlist(playlistId).items.size()));
        return;
    }
    // Files: media files, and external playlist files flattened into
    // their entries.
    const QList<QUrl> sources = flattenDropFiles(mime->urls());
    if (!sources.isEmpty())
        importSources(playlistId, sources);
}

bool PlaylistManager::movePlaylist(quint64 id, int toIndex)
{
    const int from = playlistIndexOf(id);
    if (from < 0 || toIndex < 0 || toIndex >= m_playlists.size())
        return false;
    if (from == toIndex)
        return false;

    const QList<Playlist> old = m_playlists;
    const Playlist moved = m_playlists.takeAt(from);
    // takeAt + insert lands the playlist at toIndex in both directions
    // (the take shifts the rows after it, so the target index does not
    // need adjusting).
    m_playlists.insert(toIndex, moved);

    if (!persistPositions()) {
        m_playlists = old;
        return false;
    }
    emit playlistsChanged();
    return true;
}

int PlaylistManager::currentIndex() const
{
    const int i = playlistIndexOf(m_currentPlaylistId);
    if (i < 0)
        return -1;
    return m_playlists.at(i).currentIndex;
}

MediaItem PlaylistManager::currentItem() const
{
    return itemAt(currentIndex());
}

void PlaylistManager::setCurrentIndex(int index)
{
    Playlist *pl = currentPlaylist();
    if (pl == nullptr)
        return;

    const int clamped = qBound(-1, index, static_cast<int>(pl->items.size()) - 1);
    if (clamped == pl->currentIndex)
        return;
    pl->currentIndex = clamped;
    emit currentIndexChanged(clamped);
    persistSessionState();
}

quint64 PlaylistManager::addPlaylist(const QString &name)
{
    Playlist pl;
    pl.id = m_nextPlaylistId++;
    pl.name = name.isEmpty() ? QStringLiteral("Playlist") : name;
    m_playlists.append(pl);
    if (!insertPlaylistRow(pl)) {
        m_playlists.removeLast();
        return 0;  // the playlist does not exist
    }
    emit playlistsChanged();
    return pl.id;
}

bool PlaylistManager::removePlaylist(quint64 id)
{
    const int i = playlistIndexOf(id);
    if (i < 0)
        return false;

    const Playlist old = m_playlists.at(i);
    const quint64 oldCurrent = m_currentPlaylistId;
    m_playlists.removeAt(i);

    bool currentChanged = false;
    if (m_playlists.isEmpty()) {
        // The manager always keeps at least one playlist.
        Playlist fresh;
        fresh.id = m_nextPlaylistId++;
        fresh.name = QStringLiteral("Playlist");
        m_playlists.append(fresh);
        m_currentPlaylistId = fresh.id;
        currentChanged = true;
    } else if (id == m_currentPlaylistId) {
        m_currentPlaylistId = m_playlists.first().id;
        currentChanged = true;
    }

    if (!removePlaylistRow(id)) {
        m_playlists.insert(i, old);
        m_currentPlaylistId = oldCurrent;
        return false;
    }
    if (currentChanged)
        emit currentPlaylistChanged(m_currentPlaylistId);
    emit playlistsChanged();
    // The removed playlist's index entry is dropped (the map is rebuilt
    // from the remaining playlists).
    persistSessionState();
    return true;
}

bool PlaylistManager::renamePlaylist(quint64 id, const QString &name)
{
    const int i = playlistIndexOf(id);
    if (i < 0)
        return false;
    const QString oldName = m_playlists[i].name;
    m_playlists[i].name = name;
    if (!renamePlaylistRow(id, name)) {
        m_playlists[i].name = oldName;
        return false;
    }
    emit playlistsChanged();
    persistSessionState();  // the readable name in settings.json follows
    return true;
}

Playlist PlaylistManager::playlist(quint64 id) const
{
    const int i = playlistIndexOf(id);
    if (i < 0)
        return {};
    return m_playlists.at(i);
}

int PlaylistManager::playlistIndexOf(quint64 id) const
{
    for (int i = 0; i < m_playlists.size(); ++i) {
        if (m_playlists.at(i).id == id)
            return i;
    }
    return -1;
}

int PlaylistManager::indexOfItem(quint64 playlistId, quint64 itemId) const
{
    const int i = playlistIndexOf(playlistId);
    if (i < 0)
        return -1;
    return static_cast<int>(m_playlists.at(i).items.indexOf(itemId));
}

QSet<quint64> PlaylistManager::referencedIds() const
{
    QSet<quint64> out;
    for (const Playlist &pl : m_playlists)
        for (quint64 id : pl.items)
            out.insert(id);
    return out;
}

QString PlaylistManager::currentPlaylistName() const
{
    return playlist(m_currentPlaylistId).name;
}

void PlaylistManager::setCurrentPlaylist(quint64 id)
{
    if (id == m_currentPlaylistId || playlistIndexOf(id) < 0)
        return;
    m_currentPlaylistId = id;
    emit currentPlaylistChanged(id);
    persistSessionState();
}

void PlaylistManager::nextPlaylist()
{
    if (m_playlists.size() < 2)
        return;
    const int n = static_cast<int>(m_playlists.size());
    const int i = playlistIndexOf(m_currentPlaylistId);
    // A stale current id (nothing selected) starts at the first playlist.
    setCurrentPlaylist(m_playlists.at((i < 0) ? 0 : (i + 1) % n).id);
}

void PlaylistManager::previousPlaylist()
{
    if (m_playlists.size() < 2)
        return;
    const int n = static_cast<int>(m_playlists.size());
    const int i = playlistIndexOf(m_currentPlaylistId);
    setCurrentPlaylist(m_playlists.at((i < 0) ? 0 : (i - 1 + n) % n).id);
}

void PlaylistManager::reset()
{
    // A running import would otherwise keep appending to (and saving)
    // playlists that are about to be replaced; the reads that are already
    // in flight land in onImportSourceRead, which finds no cursor and a
    // finished import, and is a no-op.
    if (m_importing) {
        m_importing = false;
        m_importPlaylistId = 0;
        m_importSources.clear();
        m_importPos = 0;
        m_importPending.clear();
        m_importAt = -1;
        m_importLanded = 0;
    }

    m_playlists.clear();
    m_nextPlaylistId = 1;

    Playlist def;
    def.id = m_nextPlaylistId++;
    def.name = QStringLiteral("Playlist");
    m_playlists.append(def);

    const quint64 old = m_currentPlaylistId;
    m_currentPlaylistId = def.id;

    emit playlistsChanged();
    if (old != def.id)
        emit currentPlaylistChanged(def.id);
    // Deliberately no persistSessionState(): reset() is a state factory
    // (constructor, tests), not a user change — persisting the fresh
    // default would clobber the stored state in a save/reset/load roundtrip.
}

void PlaylistManager::setStoragePath(const QString &path)
{
    m_storagePath = path;
    m_db.close();
}

bool PlaylistManager::ensureOpen()
{
    if (m_db.isOpen())
        return true;
    return m_db.open(m_storagePath, QStringLiteral("playlists"), kPlaylistMigrations);
}

// The write-through primitives: one transaction each on the component's
// connection. The in-memory state is the session authority — the callers
// roll it back when a primitive fails.
bool PlaylistManager::syncPlaylistRows(quint64 playlistId)
{
    const Playlist *pl = playlistPtr(playlistId);
    if (pl == nullptr)
        return false;
    if (!ensureOpen())
        return false;

    return Database::inTransaction(m_db.database(),
        [this, pl, playlistId](QSqlDatabase &db) {
        QSqlQuery q(db);
        // The playlist's own row must exist before its items: a freshly
        // created playlist may not have been persisted yet (its first
        // mutation is this call).
        q.prepare(QStringLiteral(
            "INSERT INTO playlists (id, name) VALUES (?, ?) "
            "ON CONFLICT (id) DO NOTHING"));
        q.bindValue(0, playlistId);
        q.bindValue(1, Database::nonNull(pl->name));
        bool ok = q.exec();
        if (ok) {
            q.prepare(QStringLiteral(
                "DELETE FROM playlist_items WHERE playlist_id = ?"));
            q.bindValue(0, playlistId);
            ok = q.exec();
        }
        if (ok) {
            QSqlQuery ins(db);
            ins.prepare(QStringLiteral(
                "INSERT INTO playlist_items (playlist_id, position, item_id) "
                "VALUES (?, ?, ?)"));
            for (int pos = 0; pos < pl->items.size() && ok; ++pos) {
                ins.bindValue(0, playlistId);
                ins.bindValue(1, pos);
                ins.bindValue(2, pl->items.at(pos));
                ok = ins.exec();
            }
        }
        // The import bookkeeping rides the row's write-through: the dirty
        // flag changes with every content mutation that lands here.
        if (ok) {
            q.prepare(QStringLiteral(
                "UPDATE playlists SET imported = ?, source_file = ?, dirty = ? "
                "WHERE id = ?"));
            q.bindValue(0, pl->imported ? 1 : 0);
            q.bindValue(1, Database::nonNull(pl->sourceFile));
            q.bindValue(2, pl->dirty ? 1 : 0);
            q.bindValue(3, playlistId);
            ok = q.exec();
        }
        if (!ok)
            qWarning("PlaylistManager::syncPlaylistRows: rows of playlist %1 "
                     "not written", static_cast<qlonglong>(playlistId));
        return ok;
    }, QStringLiteral("PlaylistManager::syncPlaylistRows"));
}

bool PlaylistManager::insertPlaylistRow(const Playlist &pl)
{
    if (!ensureOpen())
        return false;

    return Database::inTransaction(m_db.database(),
        [this, &pl](QSqlDatabase &db) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO playlists (id, name, position, imported, source_file, dirty) "
            "VALUES (?, ?, ?, ?, ?, ?)"));
        q.bindValue(0, pl.id);
        q.bindValue(1, Database::nonNull(pl.name));
        // The playlist's order is its place in the in-memory list.
        q.bindValue(2, playlistIndexOf(pl.id));
        q.bindValue(3, pl.imported ? 1 : 0);
        q.bindValue(4, Database::nonNull(pl.sourceFile));
        q.bindValue(5, pl.dirty ? 1 : 0);
        const bool ok = q.exec();
        if (!ok)
            qWarning("PlaylistManager::insertPlaylistRow: playlist %1 not "
                     "written", static_cast<qlonglong>(pl.id));
        return ok;
    }, QStringLiteral("PlaylistManager::insertPlaylistRow"));
}

bool PlaylistManager::removePlaylistRow(quint64 playlistId)
{
    if (!ensureOpen())
        return false;

    return Database::inTransaction(m_db.database(),
        [this, playlistId](QSqlDatabase &db) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("DELETE FROM playlist_items WHERE playlist_id = ?"));
        q.bindValue(0, playlistId);
        bool ok = q.exec();
        if (ok) {
            q.prepare(QStringLiteral("DELETE FROM playlists WHERE id = ?"));
            q.bindValue(0, playlistId);
            ok = q.exec();
        }
        if (!ok)
            qWarning("PlaylistManager::removePlaylistRow: playlist %1 not "
                     "removed", static_cast<qlonglong>(playlistId));
        return ok;
    }, QStringLiteral("PlaylistManager::removePlaylistRow"));
}

bool PlaylistManager::renamePlaylistRow(quint64 playlistId, const QString &name)
{
    if (!ensureOpen())
        return false;

    return Database::inTransaction(m_db.database(),
        [this, playlistId, &name](QSqlDatabase &db) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("UPDATE playlists SET name = ? WHERE id = ?"));
        q.bindValue(0, Database::nonNull(name));
        q.bindValue(1, playlistId);
        const bool ok = q.exec();
        if (!ok)
            qWarning("PlaylistManager::renamePlaylistRow: playlist %1 not "
                     "renamed", static_cast<qlonglong>(playlistId));
        return ok;
    }, QStringLiteral("PlaylistManager::renamePlaylistRow"));
}

bool PlaylistManager::persistPositions()
{
    if (!ensureOpen())
        return false;

    return Database::inTransaction(m_db.database(),
        [this](QSqlDatabase &db) {
        // An upsert: a playlist that was never mutated (the fresh default)
        // has no row yet, and a move of it must make it durable.
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO playlists (id, name, position, imported, source_file, dirty) "
            "VALUES (?, ?, ?, ?, ?, ?) "
            "ON CONFLICT (id) DO UPDATE SET position = excluded.position"));
        bool ok = true;
        for (int i = 0; i < m_playlists.size() && ok; ++i) {
            const Playlist &pl = m_playlists.at(i);
            q.bindValue(0, pl.id);
            q.bindValue(1, Database::nonNull(pl.name));
            q.bindValue(2, i);
            q.bindValue(3, pl.imported ? 1 : 0);
            q.bindValue(4, Database::nonNull(pl.sourceFile));
            q.bindValue(5, pl.dirty ? 1 : 0);
            ok = q.exec();
        }
        if (!ok)
            qWarning("PlaylistManager::persistPositions: the playlist order "
                     "was not written");
        return ok;
    }, QStringLiteral("PlaylistManager::persistPositions"));
}

bool PlaylistManager::persistImportState(const Playlist &pl)
{
    if (!ensureOpen())
        return false;

    return Database::inTransaction(m_db.database(),
        [&pl](QSqlDatabase &db) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "UPDATE playlists SET imported = ?, source_file = ?, dirty = ? "
            "WHERE id = ?"));
        q.bindValue(0, pl.imported ? 1 : 0);
        q.bindValue(1, Database::nonNull(pl.sourceFile));
        q.bindValue(2, pl.dirty ? 1 : 0);
        q.bindValue(3, pl.id);
        const bool ok = q.exec();
        if (!ok)
            qWarning("PlaylistManager::persistImportState: the import state "
                     "of playlist %1 was not written",
                     static_cast<qlonglong>(pl.id));
        return ok;
    }, QStringLiteral("PlaylistManager::persistImportState"));
}

bool PlaylistManager::writePlaylistSnapshot()
{
    return Database::inTransaction(m_db.database(),
        [this](QSqlDatabase &db) {
        QSqlQuery q(db);
        if (!q.exec(QStringLiteral("DELETE FROM playlist_items"))
                || !q.exec(QStringLiteral("DELETE FROM playlists"))) {
            qWarning("PlaylistManager::save: %s", qPrintable(q.lastError().text()));
            return false;
        }

        QSqlQuery insPl(db);
        insPl.prepare(QStringLiteral(
            "INSERT INTO playlists (id, name, position, imported, source_file, dirty) "
            "VALUES (?, ?, ?, ?, ?, ?)"));
        QSqlQuery insIt(db);
        insIt.prepare(QStringLiteral(
            "INSERT INTO playlist_items (playlist_id, position, item_id) "
            "VALUES (?, ?, ?)"));

        for (int i = 0; i < m_playlists.size(); ++i) {
            const Playlist &pl = m_playlists.at(i);
            insPl.bindValue(0, pl.id);
            insPl.bindValue(1, Database::nonNull(pl.name));
            insPl.bindValue(2, i);
            insPl.bindValue(3, pl.imported ? 1 : 0);
            insPl.bindValue(4, Database::nonNull(pl.sourceFile));
            insPl.bindValue(5, pl.dirty ? 1 : 0);
            if (!insPl.exec()) {
                qWarning("PlaylistManager::save: %s",
                         qPrintable(insPl.lastError().text()));
                return false;
            }

            for (int pos = 0; pos < pl.items.size(); ++pos) {
                insIt.bindValue(0, pl.id);
                insIt.bindValue(1, pos);
                insIt.bindValue(2, pl.items.at(pos));
                if (!insIt.exec()) {
                    qWarning("PlaylistManager::save: %s",
                             qPrintable(insIt.lastError().text()));
                    return false;
                }
            }
        }
        return true;
    }, QStringLiteral("PlaylistManager::writePlaylistSnapshot"));
}

bool PlaylistManager::save()
{
    // The database keeps the playlist content only.
    bool dbSaved = true;
    if (ensureOpen())
        dbSaved = writePlaylistSnapshot();

    // The session state (current playlist, per-playlist current indices)
    // goes to the SettingsManager.
    const bool settingsSaved = persistSessionState();
    return dbSaved && settingsSaved;
}

bool PlaylistManager::persistSessionState()
{
    // The SettingsManager (settings.json): the current playlist as id
    // and name and the per-playlist current indices (id → row). Session
    // state, not content: written on every change, so it survives a
    // crash; the write is atomic (tmp + rename), so a crash mid-write
    // cannot tear the file.
    SettingsManager* settings = SettingsManager::getInstance();
    QVariantMap indices;
    for (const Playlist &pl : m_playlists)
        indices.insert(QString::number(pl.id), pl.currentIndex);
    settings->setValue("playlist.current_id",
                     static_cast<qulonglong>(m_currentPlaylistId));
    settings->setValue("playlist.current_name", currentPlaylistName());
    settings->setValue("playlist.indexes", indices);
    return true;
}

void PlaylistManager::load()
{
    // The database keeps the playlist content only.
    if (ensureOpen()) {
        QSqlDatabase db = m_db.database();

        QList<Playlist> loaded;
        QSet<quint64> loadedIds;
        quint64 maxPlId = 0;

        QSqlQuery q(db);
        q.exec(QStringLiteral(
            "SELECT id, name, imported, source_file, dirty "
            "FROM playlists ORDER BY position, id"));
        while (q.next()) {
            Playlist pl;
            pl.id = q.value(0).toULongLong();
            pl.name = q.value(1).toString();
            pl.imported = q.value(2).toInt() != 0;
            pl.sourceFile = q.value(3).toString();
            pl.dirty = q.value(4).toInt() != 0;
            maxPlId = qMax(maxPlId, pl.id);
            loadPlaylistItems(db, pl);
            loadedIds.insert(pl.id);
            loaded.append(pl);
        }

        // Nothing persisted yet: keep the in-memory playlists.
        if (!loaded.isEmpty()) {
            m_playlists = loaded;
            m_nextPlaylistId = maxPlId + 1;
        }

        // Playlists that exist in memory only (the fresh default, before
        // anything was persisted) get their row now, and the rows their
        // items already wrote (write-through) are loaded — otherwise a
        // first session that adds a second playlist would lose the default
        // on the next load.
        for (Playlist &pl : m_playlists) {
            if (loadedIds.contains(pl.id))
                continue;
            insertPlaylistRow(pl);
            loadPlaylistItems(db, pl);
        }
    }

    // The current playlist comes from the SettingsManager, stored as id
    // and name: the id wins (names are not unique); a missing or unknown
    // id falls back to the first playlist with the stored name; a
    // missing or unknown name falls back to the first playlist.
    SettingsManager* settings = SettingsManager::getInstance();
    const quint64 storedId = static_cast<quint64>(
        settings->value("playlist.current_id").toULongLong());
    quint64 currentId = playlistIndexOf(storedId) >= 0 ? storedId : 0;
    if (currentId == 0) {
        const QString name =
            settings->value("playlist.current_name").toString();
        if (!name.isEmpty()) {
            for (const Playlist &pl : m_playlists) {
                if (pl.name == name) {
                    currentId = pl.id;
                    break;
                }
            }
        }
    }
    if (currentId == 0)
        currentId = m_playlists.first().id;
    m_currentPlaylistId = currentId;

    applyStoredIndices();
}

void PlaylistManager::loadPlaylistItems(QSqlDatabase &db, Playlist &pl)
{
    QSqlQuery it(db);
    it.prepare(QStringLiteral(
        "SELECT item_id FROM playlist_items WHERE playlist_id = ? "
        "ORDER BY position"));
    it.bindValue(0, pl.id);
    it.exec();
    while (it.next())
        pl.items.append(it.value(0).toULongLong());
}

void PlaylistManager::applyStoredIndices()
{
    // The per-playlist current indices are session state in the
    // SettingsManager (not content): an entry is applied only when it
    // names an existing playlist and an in-range row; anything else
    // stays at the default.
    const QVariantMap stored = SettingsManager::getInstance()
                                   ->value("playlist.indexes")
                                   .value<QVariantMap>();
    for (auto it = stored.constBegin(); it != stored.constEnd(); ++it) {
        Playlist *pl = playlistPtr(it.key().toULongLong());
        if (pl == nullptr)
            continue;  // stale entry: the playlist no longer exists
        const int idx = it.value().toInt();
        if (idx >= 0 && idx < pl->items.size())
            pl->currentIndex = idx;
    }
}

MediaItem PlaylistManager::sourceOwner(const QUrl &source) const
{
    // The store holds every item (library and standalone): one lookup.
    return MediaItemStore::getInstance()->findBySource(source);
}

void PlaylistManager::appendImportedItem(quint64 id)
{
    Playlist *pl = playlistPtr(m_importPlaylistId);
    if (pl == nullptr)
        return;
    // An item the playlist already holds is appended again: a source list
    // (an imported playlist file, a re-run of the same folder) may repeat
    // an item, and the repetition is kept.
    // With an insert anchor (an import at a drop position) every landing
    // goes at anchor + landed: the items arrive in source order, so the
    // block lands exactly where the drop pointed.
    // Write-through: the row is durable the moment it lands (the item
    // itself was persisted by ensureItem).
    if (!mutatePlaylist(*pl, [this, &pl, id] {
        if (m_importAt < 0) {
            pl->items.append(id);
        } else {
            const int at = m_importAt + m_importLanded;
            pl->items.insert(at, id);
            if (at <= pl->currentIndex)
                pl->currentIndex++;
        }
        pl->currentIndex = std::max(pl->currentIndex, 0);
    }))
        return;
    ++m_importLanded;
    emit playlistChanged(m_importPlaylistId);
}

void PlaylistManager::advanceImport()
{
    while (m_importPos < m_importSources.size()) {
        const QUrl source = m_importSources.at(m_importPos);
        if (m_importPending.contains(source.toString()))
            break;  // wait for its tag read / metadata fetch
        // Re-look-up: the owner could have appeared (concurrent add) or
        // vanished (concurrent removal) since the dispatch-time check; a
        // vanished owner contributes no row.
        const MediaItem owner = sourceOwner(source);
        if (owner.isValid())
            appendImportedItem(owner.id);
        ++m_importPos;
    }
    if (m_importPos >= m_importSources.size()) {
        finishImport();
        return;
    }
    // The cursor is stopped at a pending source: if it is a provider
    // source, start its fetch (a local read is the worker's, already
    // dispatched in order).
    fetchNextProviderSource();
}

void PlaylistManager::fetchNextProviderSource()
{
    const QUrl source = m_importSources.at(m_importPos);
    if (!m_importPending.contains(source.toString()))
        return;
    if (source.isLocalFile())
        return;  // the worker is reading it
    SourceProvider* provider = SourceManager::getInstance()->providerFor(source);
    if (provider == nullptr) {
        // The provider vanished after the dispatch-time claim: the source
        // cannot be read (it is skipped, like a vanished file).
        m_importPending.remove(source.toString());
        ++m_importPos;
        advanceImport();
        return;
    }
    // Sequential: only the fetch at the cursor is in flight, so the reads
    // resolve in source order and the cursor math stays intact.
    provider->fetchMetadata(source, [this, source](const TrackMeta &meta) {
        onImportSourceRead(source, meta);
    });
}

void PlaylistManager::finishImport()
{
    if (!m_importing)
        return;
    const quint64 playlistId = m_importPlaylistId;
    m_importing = false;
    m_importPlaylistId = 0;
    m_importSources.clear();
    m_importPos = 0;
    m_importPending.clear();
    // Nothing to save: the imported items (ensureItem) and the playlist
    // rows (appendImportedItem) were persisted as they arrived.
    emit importFinished(playlistId);
}

QList<QUrl> PlaylistManager::normalizeSources(const QList<QUrl> &sources) const
{
    // Keep the usable sources: existing local files, sources a provider
    // claims (stored as pasted) and plain http(s) stream URLs — the one
    // classification, shared with the library adds and the store.
    QList<QUrl> normalized;
    for (const QUrl &source : sources) {
        const SourceManager::ClassifiedSource c =
            SourceManager::getInstance()->classify(source);
        if (c.kind == SourceManager::SourceKind::Rejected)
            continue;
        normalized.append(c.url);
    }
    // A plain stream URL has no metadata to read: build its item now (the
    // cursor appends it in source order; the queued worker reads cannot
    // land before this returns).
    for (const QUrl &source : normalized) {
        if (source.isLocalFile() || sourceOwner(source).isValid())
            continue;
        if (SourceManager::getInstance()->providerFor(source) != nullptr)
            continue;
        MediaItemStore::getInstance()->ensureItem(source, TrackMeta());
    }
    return normalized;
}

void PlaylistManager::importSources(quint64 playlistId, const QList<QUrl> &sources,
                                    int index)
{
    if (m_importing) {
        qWarning("PlaylistManager::importSources: an import is already running");
        return;
    }
    Playlist *pl = playlistPtr(playlistId);
    if (pl == nullptr)
        return;

    const QList<QUrl> normalized = normalizeSources(sources);
    if (normalized.isEmpty())
        return;

    // Only sources the store does not hold need a read; the rest are
    // reused as-is (no I/O for them). Local files go to the worker;
    // provider sources are fetched from advanceImport, in cursor order.
    QSet<QString> pending;
    QList<QUrl> toRead;
    for (const QUrl &source : normalized) {
        if (sourceOwner(source).isValid())
            continue;
        pending.insert(source.toString());
        if (source.isLocalFile())
            toRead.append(source);
    }

    m_importPlaylistId = playlistId;
    m_importSources = normalized;
    m_importPos = 0;
    m_importPending = std::move(pending);
    m_importAt = index < 0 ? -1
                           : qBound(0, index, static_cast<int>(pl->items.size()));
    m_importLanded = 0;
    m_importing = true;

    if (!toRead.isEmpty()) {
        // Queued invocation: the slot runs on the worker thread, so the tag
        // reading happens off the GUI thread and the results (sourceRead)
        // come back to this thread through the event loop, in order.
        QMetaObject::invokeMethod(m_importWorker, &PlaylistImportWorker::readSources,
                                  Qt::QueuedConnection, toRead);
    }
    advanceImport();
}

void PlaylistManager::onImportSourceRead(const QUrl &source, const TrackMeta &meta)
{
    // The worker reads in dispatch order and the cursor stops at a pending
    // source, so this read is always the one for the source at the cursor.
    if (m_importPos < m_importSources.size()
        && m_importSources.at(m_importPos) == source) {
        if (meta.isEmpty()) {
            // A failed provider fetch: the source contributes no row
            // (like a vanished file) — never an empty-metadata item.
            qWarning("PlaylistManager::importSources: metadata fetch "
                     "failed for %s", qPrintable(source.toString()));
        } else {
            const MediaItem item =
                MediaItemStore::getInstance()->ensureItem(source, meta);
            if (item.isValid())
                appendImportedItem(item.id);
        }
        ++m_importPos;
    } else {
        qWarning("PlaylistManager::importSources: read for %s arrived out "
                 "of order, skipping", qPrintable(source.toString()));
    }
    m_importPending.remove(source.toString());
    advanceImport();
}

void PlaylistManager::markImported(quint64 id, const QString &sourceFile)
{
    Playlist *pl = playlistPtr(id);
    if (pl == nullptr)
        return;

    const bool wasImported = pl->imported;
    const QString wasSource = pl->sourceFile;
    const bool wasDirty = pl->dirty;
    pl->imported = true;
    pl->sourceFile = sourceFile;
    pl->dirty = false;
    if (!persistImportState(*pl)) {
        pl->imported = wasImported;
        pl->sourceFile = wasSource;
        pl->dirty = wasDirty;
        return;
    }
}

quint64 PlaylistManager::importPlaylistFile(const QString &path)
{
    PlaylistFile file;
    if (!parsePlaylistFile(path, file)) {
        qWarning("PlaylistManager::importPlaylistFile: cannot parse %s",
                 qPrintable(path));
        return 0;
    }
    const quint64 id = addPlaylist(file.name);
    if (id == 0)
        return 0;
    markImported(id, QUrl::fromLocalFile(path).toString());
    // Async like every other import: the entries land in source order.
    importSources(id, file.entries);
    return id;
}

bool PlaylistManager::exportPlaylist(quint64 id, const QString &path) const
{
    const Playlist pl = playlist(id);
    if (!pl.isValid())
        return false;
    QList<QUrl> entries;
    for (quint64 itemId : pl.items) {
        const MediaItem item = MediaItemStore::resolve(itemId);
        if (item.isValid())
            entries.append(item.source);
    }
    return writePlaylistFile(path, entries);
}

bool PlaylistManager::savePlaylist(quint64 id, const QString &path)
{
    if (!exportPlaylist(id, path))
        return false;
    Playlist *pl = playlistPtr(id);
    if (pl == nullptr)
        return false;
    const bool wasDirty = pl->dirty;
    pl->dirty = false;
    if (!persistImportState(*pl)) {
        pl->dirty = wasDirty;
        return false;
    }
    // The views drop the * marker.
    emit playlistsChanged();
    return true;
}

#include "playlistmanager.moc"
