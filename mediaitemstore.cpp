#include "mediaitemstore.h"

#include "playlistmanager.h"
#include "sourcemanager.h"
#include "trackmetadata.h"

#include <algorithm>

#include <QDebug>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimeZone>

namespace {
// The v1 schema of the items component of doob.db: the single item
// universe (library items and standalone items share the table; the
// in_library column marks the library's set). The playlists component
// shares the file and keeps its own tables and schema version.
const QStringList kV1ItemsStatements = {
    QStringLiteral(
        "CREATE TABLE IF NOT EXISTS media_items ("
        "id INTEGER PRIMARY KEY, "
        "source TEXT NOT NULL UNIQUE, "
        "in_library INTEGER NOT NULL DEFAULT 0, "
        "kind INTEGER NOT NULL DEFAULT 0, "
        "title TEXT NOT NULL DEFAULT '', "
        "artist TEXT NOT NULL DEFAULT '', "
        "album TEXT NOT NULL DEFAULT '', "
        "album_artist TEXT NOT NULL DEFAULT '', "
        "composer TEXT NOT NULL DEFAULT '', "
        "genre TEXT NOT NULL DEFAULT '', "
        "comment TEXT NOT NULL DEFAULT '', "
        "track_number INTEGER NOT NULL DEFAULT 0, "
        "track_total INTEGER NOT NULL DEFAULT 0, "
        "disc_number INTEGER NOT NULL DEFAULT 0, "
        "disc_total INTEGER NOT NULL DEFAULT 0, "
        "year INTEGER NOT NULL DEFAULT 0, "
        "duration_ms INTEGER NOT NULL DEFAULT 0, "
        "bitrate INTEGER NOT NULL DEFAULT 0, "
        "sample_rate INTEGER NOT NULL DEFAULT 0, "
        "channels INTEGER NOT NULL DEFAULT 0, "
        "provider TEXT NOT NULL DEFAULT '', "
        "imported_at INTEGER NOT NULL DEFAULT 0, "
        "last_played_at INTEGER NOT NULL DEFAULT 0, "
        "play_count INTEGER NOT NULL DEFAULT 0, "
        "metadata_json TEXT NOT NULL DEFAULT '{}')"
    ),
    // Arbitrary (non-standard) tags, one row per key: queryable by key or
    // value regardless of which tags a library happens to carry.
    QStringLiteral(
        "CREATE TABLE IF NOT EXISTS item_meta ("
        "item_id INTEGER NOT NULL REFERENCES media_items(id) ON DELETE CASCADE, "
        "key TEXT NOT NULL, "
        "value TEXT NOT NULL, "
        "PRIMARY KEY (item_id, key))"
    ),
    QStringLiteral("CREATE INDEX IF NOT EXISTS idx_item_meta_key ON item_meta (key)"),
    QStringLiteral("CREATE INDEX IF NOT EXISTS idx_item_meta_value ON item_meta (value)"),
    // No full-text index: the app keeps the whole store in memory and
    // filters there. A contentless FTS5 table would cost app-side
    // maintenance on every write for a table nothing reads — re-introduce
    // it with the search feature that will actually read it.
    // The current queries (full-table load on startup) never use these
    // indexes; they prepare SQL-side filtering (e.g. a future query
    // language or a tree view that groups by artist/album) on large
    // libraries.
    QStringLiteral("CREATE INDEX IF NOT EXISTS idx_items_title ON media_items (title)"),
    QStringLiteral("CREATE INDEX IF NOT EXISTS idx_items_artist ON media_items (artist)"),
    QStringLiteral("CREATE INDEX IF NOT EXISTS idx_items_album ON media_items (album)"),
    QStringLiteral("CREATE INDEX IF NOT EXISTS idx_items_album_artist ON media_items (album_artist)"),
    QStringLiteral("CREATE INDEX IF NOT EXISTS idx_items_composer ON media_items (composer)"),
    QStringLiteral("CREATE INDEX IF NOT EXISTS idx_items_genre ON media_items (genre)"),
    QStringLiteral("CREATE INDEX IF NOT EXISTS idx_items_year ON media_items (year)"),
    // The composites a tree representation groups on: artist -> album ->
    // track, and the canonical (album artist, album) album-identity key.
    QStringLiteral("CREATE INDEX IF NOT EXISTS idx_items_artist_album ON media_items (artist, album)"),
    QStringLiteral("CREATE INDEX IF NOT EXISTS idx_items_album_artist_album ON media_items (album_artist, album)"),
};

const QList<DatabaseMigration> kItemsMigrations = {
    { 1, [](QSqlDatabase &db) {
        return Database::execStatements(db, kV1ItemsStatements);
    }, },
    // v2: the artwork side table. One row per item (the blob is the
    // item's cover; an item shows one image). The cascade is documented
    // for tooling — the app's connection does not enable the foreign_keys
    // pragma, so the store's own delete deletes the row explicitly
    // (applyDelete), like the item_meta rows.
    { 2, [](QSqlDatabase &db) {
        return Database::execStatements(db, {
            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS item_artwork ("
                "item_id INTEGER PRIMARY KEY REFERENCES media_items(id) "
                "ON DELETE CASCADE, "
                "mime TEXT NOT NULL, "
                "data BLOB NOT NULL)"),
        });
    }, },
};

QString extraToJson(const QMap<QString, QString> &extra)
{
    QVariantMap m;
    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it)
        m.insert(it.key(), it.value());
    return QString::fromUtf8(
        QJsonDocument(QJsonObject::fromVariantMap(m)).toJson(QJsonDocument::Compact));
}

QMap<QString, QString> extraFromJson(const QString &json)
{
    QMap<QString, QString> extra;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject())
        return extra;
    // Named local: iterating doc.object() directly would hand out
    // QJsonValueRefs into a temporary destroyed at the end of the
    // full expression, leaving the loop working on freed memory.
    const QJsonObject obj = doc.object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
        extra.insert(it.key(), it.value().toString());
    return extra;
}

// The timestamps are epoch milliseconds in the database (SQLite has no
// date type); 0 is "unset" and maps to a null QDateTime.
qint64 toEpochMs(const QDateTime &when)
{
    return when.isNull() ? 0 : when.toMSecsSinceEpoch();
}

QDateTime fromEpochMs(qint64 ms)
{
    return ms == 0 ? QDateTime()
                   : QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::utc());
}

// The 24 metadata fields of media_items (not the id) in table order —
// the one C++ list of the field set; the DDL and the SQL statements name
// the same columns, and these two helpers are where the C++<->column
// mapping lives (insert and update bind through it, load reads through
// it). Adding a metadata column: the DDL, the statements and these.
void bindItemFields(QSqlQuery &q, const MediaItem &item, int start)
{
    q.bindValue(start + 0, Database::nonNull(item.source.toString()));
    q.bindValue(start + 1, item.inLibrary);
    q.bindValue(start + 2, static_cast<int>(item.kind));
    q.bindValue(start + 3, Database::nonNull(item.title));
    q.bindValue(start + 4, Database::nonNull(item.artist));
    q.bindValue(start + 5, Database::nonNull(item.album));
    q.bindValue(start + 6, Database::nonNull(item.albumArtist));
    q.bindValue(start + 7, Database::nonNull(item.composer));
    q.bindValue(start + 8, Database::nonNull(item.genre));
    q.bindValue(start + 9, Database::nonNull(item.comment));
    q.bindValue(start + 10, item.trackNumber);
    q.bindValue(start + 11, item.trackTotal);
    q.bindValue(start + 12, item.discNumber);
    q.bindValue(start + 13, item.discTotal);
    q.bindValue(start + 14, item.year);
    q.bindValue(start + 15, item.durationMs);
    q.bindValue(start + 16, item.bitrate);
    q.bindValue(start + 17, item.sampleRate);
    q.bindValue(start + 18, item.channels);
    q.bindValue(start + 19, Database::nonNull(item.provider));
    q.bindValue(start + 20, toEpochMs(item.importedAt));
    q.bindValue(start + 21, toEpochMs(item.lastPlayedAt));
    q.bindValue(start + 22, item.playCount);
    q.bindValue(start + 23, extraToJson(item.extra));
}

// The row a SELECT over the whole media_items table (id first, then the
// fields in table order) yields.
MediaItem readItem(QSqlQuery &q)
{
    MediaItem item;
    item.id = q.value(0).toULongLong();
    item.source = QUrl(q.value(1).toString());
    item.inLibrary = q.value(2).toBool();
    item.kind = static_cast<MediaItem::Kind>(q.value(3).toInt());
    item.title = q.value(4).toString();
    item.artist = q.value(5).toString();
    item.album = q.value(6).toString();
    item.albumArtist = q.value(7).toString();
    item.composer = q.value(8).toString();
    item.genre = q.value(9).toString();
    item.comment = q.value(10).toString();
    item.trackNumber = q.value(11).toInt();
    item.trackTotal = q.value(12).toInt();
    item.discNumber = q.value(13).toInt();
    item.discTotal = q.value(14).toInt();
    item.year = q.value(15).toInt();
    item.durationMs = q.value(16).toLongLong();
    item.bitrate = q.value(17).toInt();
    item.sampleRate = q.value(18).toInt();
    item.channels = q.value(19).toInt();
    item.provider = q.value(20).toString();
    item.importedAt = fromEpochMs(q.value(21).toLongLong());
    item.lastPlayedAt = fromEpochMs(q.value(22).toLongLong());
    item.playCount = q.value(23).toULongLong();
    item.extra = extraFromJson(q.value(24).toString());
    return item;
}

}

MediaItemStore::MediaItemStore(QObject *parent)
    : QObject(parent)
{
}

MediaItemStore* MediaItemStore::getInstance(QObject *parent)
{
    if (m_instance == nullptr) {
        m_instance = new MediaItemStore(parent);
    }
    return m_instance;
}

void MediaItemStore::setStoragePath(const QString &path)
{
    m_storagePath = path;
    m_db.close();
}

quint64 MediaItemStore::allocateId()
{
    return m_nextId++;
}

QUrl MediaItemStore::normalizedSource(const QUrl &source)
{
    if (source.isLocalFile())
        return QUrl::fromLocalFile(QFileInfo(source.toLocalFile()).absoluteFilePath());
    // The claiming provider canonicalizes its own sources (it strips the
    // URL's incidental arguments), so a re-pasted link with extra
    // arguments hits the same item (lookup and insert both go through
    // here).
    const SourceProvider *provider = SourceManager::getInstance()->providerFor(source);
    if (provider != nullptr)
        return provider->canonicalUrl(source);
    return source;
}

bool MediaItemStore::insert(MediaItem &item)
{
    if (item.source.isEmpty())
        return false;
    item.source = normalizedSource(item.source);
    if (item.id == 0) {
        item.id = allocateId();
    } else {
        // An explicit id must never be handed out by the counter later.
        m_nextId = qMax(m_nextId, item.id + 1);
    }
    // The import time is the store's to set (written back like the id).
    if (item.importedAt.isNull())
        item.importedAt = QDateTime::currentDateTimeUtc();

    if (m_items.contains(item.id))
        return false;
    const QString key = item.source.toString();
    if (m_bySource.contains(key))
        return false;

    m_items.insert(item.id, item);
    m_bySource.insert(key, item.id);
    if (!persistInTransaction([&item](QSqlDatabase &db) {
            return applyInsert(db, item);
        })) {
        // The persist failed: roll the memory change back so the two never
        // drift (the id stays allocated — the counter never moves back).
        m_bySource.remove(key);
        m_items.remove(item.id);
        return false;
    }
    return true;
}

bool MediaItemStore::remove(quint64 id)
{
    if (!m_items.contains(id))
        return false;
    // take() removes the entry and hands over the value: the copy must
    // survive a failed persist, when the item is restored.
    const MediaItem old = m_items.take(id);
    m_bySource.remove(old.source.toString());
    const bool hadArt = m_artworkIds.remove(id);
    if (!persistInTransaction([id](QSqlDatabase &db) {
            return applyDelete(db, id);
        })) {
        m_items.insert(old.id, old);
        m_bySource.insert(old.source.toString(), old.id);
        if (hadArt)
            m_artworkIds.insert(id);
        return false;
    }
    return true;
}

void MediaItemStore::pruneIfUnreferenced(quint64 id)
{
    const MediaItem item = this->item(id);
    if (!item.isValid() || item.inLibrary || item.kind != MediaItem::Local)
        return;
    if (PlaylistManager::getInstance()->referencedIds().contains(id))
        return;
    remove(id);
}

bool MediaItemStore::contains(quint64 id) const
{
    return m_items.contains(id);
}

MediaItem MediaItemStore::item(quint64 id) const
{
    return m_items.value(id);
}

MediaItem MediaItemStore::findBySource(const QUrl &source) const
{
    const quint64 id = m_bySource.value(normalizedSource(source).toString(), 0);
    return id != 0 ? m_items.value(id) : MediaItem();
}

MediaItem MediaItemStore::ensureItem(const QUrl &source, const TrackMeta &meta)
{
    // Already imported (library or standalone): returned as-is (no new id,
    // no metadata refresh).
    MediaItem stored = findBySource(source);
    if (stored.isValid())
        return stored;

    // The one classification (shared with the library adds and the
    // playlist import): an existing local file (kind Local), a source a
    // registered provider claims (kind Plugin, the claimant's id is
    // recorded, the source in the claimant's canonical form), a plain
    // http(s) stream URL (kind Remote, as pasted) or nothing importable.
    const SourceManager::ClassifiedSource c =
        SourceManager::getInstance()->classify(source);
    // The read's art travels with a new item (a source hit returned early
    // above: its art is left as stored).
    const auto storeArt = [&](const MediaItem &item) {
        if (!meta.artData.isEmpty())
            setArtwork(item.id, meta.artMime, meta.artData);
    };
    if (c.kind == SourceManager::SourceKind::Local) {
        MediaItem item = mediaItemFromMeta(allocateId(), c.url, meta);
        if (!insert(item))
            return {};
        storeArt(item);
        return item;
    }
    if (c.kind == SourceManager::SourceKind::Provider) {
        MediaItem item = mediaItemFromMeta(allocateId(), c.url, meta,
                                           MediaItem::Plugin);
        item.provider = c.provider->id();
        if (!insert(item))
            return {};
        storeArt(item);
        return item;
    }
    if (c.kind == SourceManager::SourceKind::Remote) {
        MediaItem item = mediaItemFromMeta(allocateId(), c.url, meta,
                                           MediaItem::Remote);
        if (!insert(item))
            return {};
        storeArt(item);
        return item;
    }
    return {};
}

bool MediaItemStore::setInLibrary(quint64 id, bool inLibrary)
{
    auto it = m_items.find(id);
    if (it == m_items.end())
        return false;
    const bool old = it.value().inLibrary;
    it.value().inLibrary = inLibrary;
    if (!persistInTransaction([&](QSqlDatabase &db) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("UPDATE media_items SET in_library = ? "
                                     "WHERE id = ?"));
            q.bindValue(0, inLibrary);
            q.bindValue(1, id);
            if (!q.exec()) {
                qWarning("MediaItemStore::setInLibrary: %s",
                         qPrintable(q.lastError().text()));
                return false;
            }
            return true;
        })) {
        it = m_items.find(id);
        if (it != m_items.end())
            it.value().inLibrary = old;
        return false;
    }
    return true;
}

bool MediaItemStore::updateItem(const MediaItem &item)
{
    auto it = m_items.find(item.id);
    if (it == m_items.end())
        return false;
    const MediaItem old = it.value();
    it.value() = item;
    if (!persistInTransaction([&item](QSqlDatabase &db) {
            return applyUpdate(db, item);
        })) {
        it = m_items.find(item.id);
        if (it != m_items.end())
            it.value() = old;
        return false;
    }
    emit itemChanged(item);
    return true;
}

bool MediaItemStore::registerPlay(quint64 id)
{
    auto it = m_items.find(id);
    if (it == m_items.end())
        return false;
    const MediaItem old = it.value();
    MediaItem updated = old;
    updated.playCount = old.playCount + 1;
    updated.lastPlayedAt = QDateTime::currentDateTimeUtc();
    it.value() = updated;
    if (!persistInTransaction([&updated](QSqlDatabase &db) {
            return applyUpdate(db, updated);
        })) {
        it = m_items.find(id);
        if (it != m_items.end())
            it.value() = old;
        return false;
    }
    emit itemChanged(updated);
    return true;
}

bool MediaItemStore::hasArtwork(quint64 id) const
{
    return m_artworkIds.contains(id);
}

bool MediaItemStore::artwork(quint64 id, QString *mime, QByteArray *data)
{
    if (!m_artworkIds.contains(id))
        return false;
    if (!ensureOpen())
        return false;
    QSqlQuery q(m_db.database());
    q.prepare(QStringLiteral("SELECT mime, data FROM item_artwork WHERE item_id = ?"));
    q.bindValue(0, id);
    if (!q.exec() || !q.next()) {
        // The RAM set says the art is there; the database says otherwise
        // (a hand-edited file). Trust the database and drop the stale
        // membership.
        m_artworkIds.remove(id);
        return false;
    }
    if (mime != nullptr)
        *mime = q.value(0).toString();
    if (data != nullptr)
        *data = q.value(1).toByteArray();
    return true;
}

bool MediaItemStore::setArtwork(quint64 id, const QString &mime,
                                const QByteArray &data)
{
    if (!m_items.contains(id))
        return false;
    const bool had = m_artworkIds.contains(id);
    const bool will = !data.isEmpty();
    m_artworkIds.insert(id);
    if (!persistInTransaction([&](QSqlDatabase &db) {
            QSqlQuery q(db);
            if (!will) {
                q.prepare(QStringLiteral("DELETE FROM item_artwork WHERE item_id = ?"));
                q.bindValue(0, id);
            } else {
                q.prepare(QStringLiteral(
                    "INSERT INTO item_artwork (item_id, mime, data) "
                    "VALUES (?, ?, ?) "
                    "ON CONFLICT (item_id) DO UPDATE SET "
                    "mime = excluded.mime, data = excluded.data"));
                q.bindValue(0, id);
                q.bindValue(1, Database::nonNull(mime));
                q.bindValue(2, data);
            }
            if (!q.exec()) {
                qWarning("MediaItemStore::setArtwork: %s",
                         qPrintable(q.lastError().text()));
                return false;
            }
            return true;
        })) {
        // The persist failed: roll the memory set back so the two never
        // drift.
        if (had)
            m_artworkIds.insert(id);
        else
            m_artworkIds.remove(id);
        return false;
    }
    if (will)
        m_artworkIds.insert(id);
    else
        m_artworkIds.remove(id);
    emit itemArtworkChanged(item(id));
    return true;
}

QList<quint64> MediaItemStore::ids() const
{
    QList<quint64> out;
    out.reserve(m_items.size());
    for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it)
        out.append(it.key());
    std::sort(out.begin(), out.end());
    return out;
}

QList<MediaItem> MediaItemStore::items() const
{
    const QList<quint64> sorted = ids();
    QList<MediaItem> out;
    out.reserve(sorted.size());
    for (quint64 id : sorted)
        out.append(m_items.value(id));
    return out;
}

void MediaItemStore::clear()
{
    m_items.clear();
    m_bySource.clear();
    m_artworkIds.clear();
}

MediaItem MediaItemStore::resolve(quint64 id)
{
    return getInstance()->item(id);
}

bool MediaItemStore::ensureOpen()
{
    if (m_db.isOpen())
        return true;
    return m_db.open(m_storagePath, QStringLiteral("items"), kItemsMigrations);
}

bool MediaItemStore::persistInTransaction(
        const std::function<bool(QSqlDatabase &)> &apply)
{
    if (!ensureOpen())
        return false;
    return Database::inTransaction(m_db.database(), apply,
                                   QStringLiteral("MediaItemStore"));
}

bool MediaItemStore::applyInsert(QSqlDatabase &db, const MediaItem &item)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO media_items (id, source, in_library, kind, title, artist, "
        "album, album_artist, composer, genre, comment, track_number, track_total, "
        "disc_number, disc_total, year, duration_ms, bitrate, sample_rate, channels, "
        "provider, imported_at, last_played_at, play_count, metadata_json) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.bindValue(0, item.id);
    bindItemFields(q, item, 1);
    if (!q.exec()) {
        qWarning("MediaItemStore::applyInsert: %s",
                 qPrintable(q.lastError().text()));
        return false;
    }

    if (item.extra.isEmpty())
        return true;
    QSqlQuery meta(db);
    meta.prepare(QStringLiteral(
        "INSERT INTO item_meta (item_id, key, value) VALUES (?, ?, ?)"));
    for (auto it = item.extra.constBegin(); it != item.extra.constEnd(); ++it) {
        meta.bindValue(0, item.id);
        meta.bindValue(1, it.key());
        meta.bindValue(2, it.value());
        if (!meta.exec()) {
            qWarning("MediaItemStore::applyInsert: %s",
                     qPrintable(meta.lastError().text()));
            return false;
        }
    }
    return true;
}

bool MediaItemStore::applyUpdate(QSqlDatabase &db, const MediaItem &item)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "UPDATE media_items SET source = ?, in_library = ?, kind = ?, "
        "title = ?, artist = ?, album = ?, album_artist = ?, composer = ?, "
        "genre = ?, comment = ?, track_number = ?, track_total = ?, "
        "disc_number = ?, disc_total = ?, year = ?, duration_ms = ?, "
        "bitrate = ?, sample_rate = ?, channels = ?, provider = ?, "
        "imported_at = ?, last_played_at = ?, play_count = ?, metadata_json = ? "
        "WHERE id = ?"));
    bindItemFields(q, item, 0);
    q.bindValue(24, item.id);
    if (!q.exec()) {
        qWarning("MediaItemStore::applyUpdate: %s",
                 qPrintable(q.lastError().text()));
        return false;
    }

    // The EAV rows are replaced wholesale (delete all, reinsert).
    QSqlQuery meta(db);
    meta.prepare(QStringLiteral("DELETE FROM item_meta WHERE item_id = ?"));
    meta.bindValue(0, item.id);
    if (!meta.exec()) {
        qWarning("MediaItemStore::applyUpdate: %s",
                 qPrintable(meta.lastError().text()));
        return false;
    }
    meta.prepare(QStringLiteral(
        "INSERT INTO item_meta (item_id, key, value) VALUES (?, ?, ?)"));
    for (auto it = item.extra.constBegin(); it != item.extra.constEnd(); ++it) {
        meta.bindValue(0, item.id);
        meta.bindValue(1, it.key());
        meta.bindValue(2, it.value());
        if (!meta.exec()) {
            qWarning("MediaItemStore::applyUpdate: %s",
                     qPrintable(meta.lastError().text()));
            return false;
        }
    }
    return true;
}

bool MediaItemStore::applyDelete(QSqlDatabase &db, quint64 id)
{
    // The EAV and art rows go explicitly: the connection does not enable
    // the foreign_keys pragma, so the ON DELETE CASCADE never fires.
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM item_artwork WHERE item_id = ?"));
    q.bindValue(0, id);
    if (!q.exec()) {
        qWarning("MediaItemStore::applyDelete: %s",
                 qPrintable(q.lastError().text()));
        return false;
    }
    q.prepare(QStringLiteral("DELETE FROM item_meta WHERE item_id = ?"));
    q.bindValue(0, id);
    if (!q.exec()) {
        qWarning("MediaItemStore::applyDelete: %s",
                 qPrintable(q.lastError().text()));
        return false;
    }
    q.prepare(QStringLiteral("DELETE FROM media_items WHERE id = ?"));
    q.bindValue(0, id);
    if (!q.exec()) {
        qWarning("MediaItemStore::applyDelete: %s",
                 qPrintable(q.lastError().text()));
        return false;
    }
    return true;
}

bool MediaItemStore::save()
{
    if (!ensureOpen())
        return false;

    return Database::inTransaction(m_db.database(),
        [this](QSqlDatabase &db) {
        QSqlQuery q(db);
        if (!q.exec(QStringLiteral("DELETE FROM media_items"))
                || !q.exec(QStringLiteral("DELETE FROM item_meta"))) {
            qWarning("MediaItemStore::save: %s", qPrintable(q.lastError().text()));
            return false;
        }

        // Prune: an item survives the snapshot when it is in the library
        // or a playlist references it. Local metadata is regenerable from
        // the file, so dead local items are simply left out (the snapshot
        // rewrite does the deleting); non-local items are never pruned —
        // the row may be the only copy of their metadata.
        const QSet<quint64> referenced =
            PlaylistManager::getInstance()->referencedIds();
        for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it) {
            const MediaItem &item = it.value();
            if (!item.inLibrary && !referenced.contains(item.id)
                    && item.kind == MediaItem::Local) {
                // The pruned item's art row goes with it (survivors' rows
                // stay: their ids are stable).
                QSqlQuery art(db);
                art.prepare(QStringLiteral(
                    "DELETE FROM item_artwork WHERE item_id = ?"));
                art.bindValue(0, item.id);
                if (!art.exec()) {
                    qWarning("MediaItemStore::save: %s",
                             qPrintable(art.lastError().text()));
                    return false;
                }
                continue;
            }

            if (!applyInsert(db, item)) {
                qWarning("MediaItemStore::save: item %1 not written",
                         static_cast<qlonglong>(item.id));
                return false;
            }
        }
        return true;
    }, QStringLiteral("MediaItemStore::save"));
}

void MediaItemStore::load()
{
    if (!ensureOpen())
        return;

    QSqlDatabase db = m_db.database();
    m_items.clear();
    m_bySource.clear();
    quint64 maxId = 0;

    QSqlQuery q(db);
    q.exec(QStringLiteral(
        "SELECT id, source, in_library, kind, title, artist, album, album_artist, "
        "composer, genre, comment, track_number, track_total, disc_number, "
        "disc_total, year, duration_ms, bitrate, sample_rate, channels, "
        "provider, imported_at, last_played_at, play_count, "
        "metadata_json FROM media_items ORDER BY id"));
    while (q.next()) {
        const MediaItem item = readItem(q);
        m_items.insert(item.id, item);
        m_bySource.insert(item.source.toString(), item.id);
        maxId = qMax(maxId, item.id);
    }

    m_artworkIds.clear();
    QSqlQuery art(db);
    art.exec(QStringLiteral("SELECT item_id FROM item_artwork"));
    while (art.next()) {
        m_artworkIds.insert(art.value(0).toULongLong());
    }

    // The counter never moves backwards: the highest stored id is the
    // highest id in use.
    m_nextId = qMax(m_nextId, maxId + 1);
}

#include "mediaitemstore.moc"
