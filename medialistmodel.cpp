#include "medialistmodel.h"

#include "mediaitemstore.h"

#include <algorithm>

#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QPixmap>
#include <QSet>
#include <QtGlobal>

namespace {

// "N/M" when a total is known, plain "N" otherwise; empty when the number
// is absent (0 means "not tagged").
QString formatNumberPair(int number, int total)
{
    if (number <= 0)
        return {};
    if (total > 0)
        return QStringLiteral("%1/%2").arg(number).arg(total);
    return QString::number(number);
}

// -1/0/1 for two numbers (this Qt has no qCompare — it was superseded by
// the ordering-returning qCompareThreeWay).
template <typename T>
int compareInts(T a, T b)
{
    return (a > b) - (a < b);
}

// The process-wide artwork pixmap cache: item id → the cover scaled to
// 64×64 (aspect preserved). The blob itself stays in the database (the
// store serves it on demand); every model instance maps an art change
// (the itemArtworkChanged connection in the ctor) to a cache removal and
// a row repaint, so a stale pixmap never shows.
QHash<quint64, QPixmap>& artworkCache()
{
    static QHash<quint64, QPixmap> cache;
    return cache;
}

const int kArtworkThumbnail = 64;

QPixmap artworkPixmapFor(quint64 id)
{
    QHash<quint64, QPixmap> &cache = artworkCache();
    const auto it = cache.constFind(id);
    if (it != cache.constEnd())
        return it.value();
    QPixmap pixmap;
    QString mime;
    QByteArray data;
    if (MediaItemStore::getInstance()->artwork(id, &mime, &data)) {
        // fromData() detects the format from the bytes (the mime is the
        // store's to record, not to parse).
        const QImage image = QImage::fromData(data);
        if (!image.isNull())
            pixmap = QPixmap::fromImage(
                image.scaled(kArtworkThumbnail, kArtworkThumbnail,
                             Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    // An undecodable (or absent) image is cached as invalid too: the
    // row repaints when the art changes, and the bytes are not re-parsed
    // per cell in the meantime.
    cache.insert(id, pixmap);
    return pixmap;
}

} // namespace

MediaListModel::MediaListModel(QObject *parent)
    : QAbstractItemModel(parent)
{
    // An art change (an import's art lands, a resync replaces it, a
    // removal) repaints the row: drop the cached pixmap, mark the row
    // dirty.
    connect(MediaItemStore::getInstance(),
            &MediaItemStore::itemArtworkChanged, this,
            [this](const MediaItem &item) {
        artworkCache().remove(item.id);
        updateItem(item);
    });
}

int MediaListModel::compareColumn(const MediaItem &a, const MediaItem &b, int column)
{
    switch (column) {
    // The numeric columns sort by the field, not the display string
    // ("10" must come after "9").
    case Track:
        return compareInts(a.trackNumber, b.trackNumber);
    case Disc:
        return compareInts(a.discNumber, b.discNumber);
    case Year:
        return compareInts(a.year, b.year);
    case Duration:
        return compareInts(a.durationMs, b.durationMs);
    case Bitrate:
        return compareInts(a.bitrate, b.bitrate);
    case SampleRate:
        return compareInts(a.sampleRate, b.sampleRate);
    case Channels:
        return compareInts(a.channels, b.channels);
    default:
        return columnDisplay(a, column).compare(columnDisplay(b, column),
                                                Qt::CaseInsensitive);
    }
}

QList<int> MediaListModel::searchableColumns()
{
    // Every data column (Title through Channels); the status marker is
    // model state, not searchable text, and the artwork is a pixmap, not
    // text.
    QList<int> columns;
    for (int column = 0; column < Playing; ++column)
        if (column != Artwork)
            columns.append(column);
    return columns;
}

QString MediaListModel::mediaMimeType()
{
    return "application/x-doob-media";
}

QString MediaListModel::formatDuration(qint64 ms, bool withHours)
{
    const int totalSeconds = static_cast<int>(qMax<qint64>(0, ms) / 1000);
    if (withHours && totalSeconds >= 3600) {
        return QStringLiteral("%1:%2:%3")
            .arg(totalSeconds / 3600)
            .arg((totalSeconds % 3600) / 60, 2, 10, QChar('0'))
            .arg(totalSeconds % 60, 2, 10, QChar('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(totalSeconds / 60)
        .arg(totalSeconds % 60, 2, 10, QChar('0'));
}

MediaListModel::RowStatus MediaListModel::rowStatusFor(
        quint64 rowId, const MediaItem &resolving, const MediaItem &playing,
        const MediaItem &failed, bool paused)
{
    if (rowId == 0)
        return RowStatus::None;
    if (resolving.isValid() && rowId == resolving.id)
        return RowStatus::Downloading;
    if (playing.isValid() && rowId == playing.id)
        return paused ? RowStatus::Paused : RowStatus::Playing;
    if (failed.isValid() && rowId == failed.id)
        return RowStatus::Error;
    return RowStatus::None;
}

QString MediaListModel::rowStatusChar(RowStatus status)
{
    switch (status) {
    case RowStatus::Playing:
        return QChar(0x25B6);  // ▶
    case RowStatus::Paused:
        return QChar(0x23F8);  // ⏸
    case RowStatus::Downloading:
        return QChar(0x2193);  // ↓
    case RowStatus::Error:
        return QChar(0x26A0);  // ⚠
    default:
        return {};
    }
}

QString MediaListModel::rowStatusText(RowStatus status)
{
    switch (status) {
    case RowStatus::Playing:
        return QStringLiteral("Playing");
    case RowStatus::Paused:
        return QStringLiteral("Paused");
    case RowStatus::Downloading:
        return QStringLiteral("Downloading…");
    case RowStatus::Error:
        return QStringLiteral("Cannot play");
    default:
        return {};
    }
}

QString MediaListModel::columnDisplay(const MediaItem &item, int column)
{
    switch (column) {
    case Artwork:
        // Pixmap only: the display is the DecorationRole, not text.
        return {};
    case Title:
        return item.title;
    case Artist:
        return item.artist;
    case Album:
        return item.album;
    case AlbumArtist:
        return item.albumArtist;
    case Composer:
        return item.composer;
    case Genre:
        return item.genre;
    case Track:
        return formatNumberPair(item.trackNumber, item.trackTotal);
    case Disc:
        return formatNumberPair(item.discNumber, item.discTotal);
    case Year:
        return item.year > 0 ? QString::number(item.year) : QString();
    case Duration:
        return formatDuration(item.durationMs);
    case Comment:
        return item.comment;
    case Bitrate:
        return item.bitrate > 0 ? QStringLiteral("%1 kbps").arg(item.bitrate) : QString();
    case SampleRate:
        return item.sampleRate > 0 ? QStringLiteral("%1 Hz").arg(item.sampleRate) : QString();
    case Channels:
        return item.channels > 0 ? QString::number(item.channels) : QString();
    default:
        return {};
    }
}

QString MediaListModel::columnKey(int logical)
{
    switch (logical) {
    case Artwork:
        return QStringLiteral("artwork");
    case Title:
        return QStringLiteral("title");
    case Artist:
        return QStringLiteral("artist");
    case Album:
        return QStringLiteral("album");
    case AlbumArtist:
        return QStringLiteral("albumartist");
    case Composer:
        return QStringLiteral("composer");
    case Genre:
        return QStringLiteral("genre");
    case Track:
        return QStringLiteral("track");
    case Disc:
        return QStringLiteral("disc");
    case Year:
        return QStringLiteral("year");
    case Duration:
        return QStringLiteral("duration");
    case Comment:
        return QStringLiteral("comment");
    case Bitrate:
        return QStringLiteral("bitrate");
    case SampleRate:
        return QStringLiteral("samplerate");
    case Channels:
        return QStringLiteral("channels");
    case Playing:
        return QStringLiteral("playing");
    default:
        return {};
    }
}

int MediaListModel::columnLogical(const QString &key)
{
    if (key == QLatin1String("artwork"))
        return Artwork;
    if (key == QLatin1String("title"))
        return Title;
    if (key == QLatin1String("artist"))
        return Artist;
    if (key == QLatin1String("album"))
        return Album;
    if (key == QLatin1String("albumartist"))
        return AlbumArtist;
    if (key == QLatin1String("composer"))
        return Composer;
    if (key == QLatin1String("genre"))
        return Genre;
    if (key == QLatin1String("track"))
        return Track;
    if (key == QLatin1String("disc"))
        return Disc;
    if (key == QLatin1String("year"))
        return Year;
    if (key == QLatin1String("duration"))
        return Duration;
    if (key == QLatin1String("comment"))
        return Comment;
    if (key == QLatin1String("bitrate"))
        return Bitrate;
    if (key == QLatin1String("samplerate"))
        return SampleRate;
    if (key == QLatin1String("channels"))
        return Channels;
    if (key == QLatin1String("playing"))
        return Playing;
    return -1;
}

QString MediaListModel::columnLabel(int logical)
{
    switch (logical) {
    case Artwork:
        return QStringLiteral("Artwork");
    case Title:
        return QStringLiteral("Title");
    case Artist:
        return QStringLiteral("Artist");
    case Album:
        return QStringLiteral("Album");
    case AlbumArtist:
        return QStringLiteral("Album Artist");
    case Composer:
        return QStringLiteral("Composer");
    case Genre:
        return QStringLiteral("Genre");
    case Track:
        return QStringLiteral("Track");
    case Disc:
        return QStringLiteral("Disc");
    case Year:
        return QStringLiteral("Year");
    case Duration:
        return QStringLiteral("Duration");
    case Comment:
        return QStringLiteral("Comment");
    case Bitrate:
        return QStringLiteral("Bitrate");
    case SampleRate:
        return QStringLiteral("Sample Rate");
    case Channels:
        return QStringLiteral("Channels");
    case Playing:
        return QStringLiteral("Status");
    default:
        return {};
    }
}

void MediaListModel::setItemIds(const QList<quint64> &ids)
{
    m_ids = ids;
    m_rowIndex.clear();
    m_rowIndexBuilt = false;
    beginResetModel();
    endResetModel();
}

QList<int> MediaListModel::uniqueRows(const QModelIndexList &indexes) const
{
    // The selection may name a row's cells several times; keep each row
    // once, ascending (the drag payload's row order is the model's).
    QSet<int> seen;
    QList<int> rows;
    for (const QModelIndex &index : indexes) {
        if (!index.isValid())
            continue;
        const int row = index.row();
        if (!seen.contains(row)) {
            seen.insert(row);
            rows.append(row);
        }
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

bool MediaListModel::updateItem(const MediaItem &item)
{
    if (!m_rowIndexBuilt) {
        m_rowIndex.clear();
        for (int row = 0; row < m_ids.size(); ++row)
            m_rowIndex[m_ids.at(row)].append(row);
        m_rowIndexBuilt = true;
    }
    // A playlist may hold the same item in several rows; every one of
    // them shows the item's data, so all of them must be repainted.
    const QList<int> rows = m_rowIndex.value(item.id);
    if (rows.isEmpty())
        return false;
    for (int row : rows)
        emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
    return true;
}

void MediaListModel::setRowStatus(int row, RowStatus status)
{
    if (status == RowStatus::None)
        row = -1;
    if (row == m_statusRow && status == m_status)
        return;
    m_statusRow = row;
    m_status = status;
    if (m_ids.isEmpty())
        return;
    emit dataChanged(index(0, 0),
                     index(static_cast<int>(m_ids.size()) - 1, ColumnCount - 1),
                     { Qt::DisplayRole, Qt::ToolTipRole });
}

quint64 MediaListModel::idAt(int row) const
{
    if (row < 0 || row >= m_ids.size())
        return 0;
    return m_ids.at(row);
}

quint64 MediaListModel::idAt(const QModelIndex &index) const
{
    return index.isValid() ? idAt(index.row()) : 0;
}

int MediaListModel::rowOf(quint64 id) const
{
    for (int row = 0; row < m_ids.size(); ++row) {
        if (m_ids.at(row) == id)
            return row;
    }
    return -1;
}

QModelIndex MediaListModel::index(int row, int column, const QModelIndex &parent) const
{
    // The model is flat: only the (invalid) root index has rows.
    if (parent.isValid())
        return {};
    if (row < 0 || row >= m_ids.size() || column < 0 || column >= ColumnCount)
        return {};
    return createIndex(row, column);
}

QModelIndex MediaListModel::parent(const QModelIndex &index) const
{
    Q_UNUSED(index)
    return {}; // flat: nothing has a parent
}

int MediaListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_ids.size());
}

int MediaListModel::columnCount(const QModelIndex &parent) const
{
    // The root (the header) always carries the full column set — even an
    // empty model keeps its header, so the column state stays addressable.
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant MediaListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};
    const quint64 id = idAt(index.row());
    if (id == 0)
        return {};

    if (index.column() == Playing) {
        // The status marker is model state, not item data.
        if (index.row() != m_statusRow)
            return {};
        switch (role) {
        case Qt::DisplayRole:
            return rowStatusChar(m_status);
        case Qt::ToolTipRole:
            return rowStatusText(m_status);
        default:
            return {};
        }
    }
    switch (role) {
    case Qt::DisplayRole:
        // Resolve per cell: the view only queries visible rows, so no
        // per-row cache is needed (one would grow to the full row count
        // and re-create the duplication ids are meant to remove). A
        // dangling id resolves to an invalid item and shows empty cells.
        return columnDisplay(MediaItemStore::resolve(id), index.column());
    case Qt::DecorationRole:
        if (index.column() == Artwork)
            return artworkPixmapFor(id);
        return {};
    case Qt::UserRole:
        return static_cast<qulonglong>(id);
    default:
        return {};
    }
}

QVariant MediaListModel::headerData(int section, Qt::Orientation orientation,
                                    int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    if (section == Playing) {
        // The status column is deliberately headerless: the marker is
        // identified by its glyph, and the header context menu uses
        // columnLabel() to name it.
        return {};
    }
    return columnLabel(section);
}

Qt::ItemFlags MediaListModel::flags(const QModelIndex &index) const
{
    // The root (invalid) index must accept drops so that the view accepts
    // drag moves over the empty area below the last row (and empty models).
    if (!index.isValid())
        return Qt::ItemIsDropEnabled;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled
         | Qt::ItemIsDropEnabled;
}

QStringList MediaListModel::mediaMimeTypes()
{
    // The media payload (drags from the app's own views) plus the
    // uri-list format: QMimeData::setUrls sets it for URL payloads, which
    // is what the file manager drops.
    return { mediaMimeType(), QStringLiteral("text/uri-list") };
}

QMimeData* MediaListModel::dragMimeData(const QList<quint64> &ids,
                                        const QList<int> &rows,
                                        const QString &origin)
{
    QMimeData *mime = new QMimeData();
    mime->setData(mediaMimeType(), dragDataToJson(ids, rows, origin));
    return mime;
}

Qt::DropActions MediaListModel::mediaDropActions()
{
    return Qt::CopyAction | Qt::MoveAction;
}

QStringList MediaListModel::mimeTypes() const
{
    return mediaMimeTypes();
}

QMimeData* MediaListModel::mimeData(const QModelIndexList &indexes) const
{
    // One entry per dragged row (ascending): a flat model's row names its
    // own id (repeated when the rows repeat an item) — the rows alone
    // cannot name the row when an item is duplicated, and the target
    // decides reorder vs. insertion from the origin.
    const QList<int> rows = uniqueRows(indexes);
    QList<quint64> ids;
    for (int row : rows)
        ids.append(m_ids.at(row));
    return dragMimeData(ids, rows, m_originId);
}

QByteArray MediaListModel::dragDataToJson(const QList<quint64> &ids,
                                          const QList<int> &rows,
                                          const QString &origin)
{
    QJsonArray idsArr;
    for (quint64 id : ids)
        idsArr.append(static_cast<double>(id));
    QJsonArray rowsArr;
    for (int row : rows)
        rowsArr.append(row);

    // The origin is what lets the target tell a reorder (a drag that
    // started in its own rows) from an insertion (library or another
    // playlist) — id membership alone cannot, since a dragged library row
    // may name an item the playlist already holds.
    QJsonObject obj;
    obj.insert(QStringLiteral("ids"), idsArr);
    obj.insert(QStringLiteral("rows"), rowsArr);
    obj.insert(QStringLiteral("origin"), origin);
    return QJsonDocument(obj).toJson();
}

int MediaListModel::dropIndexForRow(int row, const QModelIndex &parent, int rowCount)
{
    int index = row;
    if (index < 0)
        index = parent.isValid() ? parent.row() : rowCount;
    return std::min(index, rowCount);
}

bool MediaListModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row,
                                  int column, const QModelIndex &parent)
{
    Q_UNUSED(action)
    Q_UNUSED(column)
    const int index = dropIndexForRow(row, parent, static_cast<int>(m_ids.size()));

    if (data->hasFormat(mediaMimeType())) {
        const DragData drag = dragDataFromMimeData(data);
        if (drag.ids.isEmpty())
            return false;
        emit itemsDropped(drag.ids, drag.rows, drag.origin, index);
        return true;
    }

    // A drop from outside the app: local files (the file manager) and
    // non-local URLs (a browser). The model plumbs both (a mixed drop
    // emits both signals); the widget decides what to do with each half
    // (files import, the library sends the URLs to the import wizard).
    if (!data->hasUrls())
        return false;
    const QList<QUrl> urls = data->urls();
    QList<QUrl> files;
    QList<QUrl> links;
    for (const QUrl &url : urls) {
        if (url.isLocalFile())
            files.append(url);
        else
            links.append(url);
    }
    if (files.isEmpty() && links.isEmpty())
        return false;
    if (!files.isEmpty())
        emit filesDropped(files, index);
    if (!links.isEmpty())
        emit urlsDropped(links, index);
    return true;
}

Qt::DropActions MediaListModel::supportedDropActions() const
{
    return mediaDropActions();
}

MediaListModel::DragData MediaListModel::dragDataFromMimeData(const QMimeData *data)
{
    DragData drag;
    // The payload is {"ids": [...], "rows": [...], "origin": "…"}; a bare
    // id array (an older payload) parses with no rows and an empty origin.
    const QJsonDocument doc = QJsonDocument::fromJson(data->data(mediaMimeType()));
    QJsonArray arr;
    QJsonArray rowsArr;
    if (doc.isObject()) {
        const QJsonObject obj = doc.object();
        arr = obj.value(QStringLiteral("ids")).toArray();
        rowsArr = obj.value(QStringLiteral("rows")).toArray();
        drag.origin = obj.value(QStringLiteral("origin")).toString();
    } else {
        arr = doc.array();
    }
    for (const auto &v : arr)
        drag.ids.append(static_cast<quint64>(v.toVariant().toULongLong()));
    for (const auto &v : rowsArr)
        drag.rows.append(v.toInt());
    return drag;
}
