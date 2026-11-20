#ifndef MEDIALISTMODEL_H
#define MEDIALISTMODEL_H

#include <QAbstractItemModel>
#include <QHash>
#include <QList>
#include <QUrl>

#include "mediaitem.h"

// Flat table model of media item references: one row per item id, in the
// order given — the order is the data (a playlist's sequence, the
// library's ingestion order). No grouping, no sorting here: the library's
// filter/sort is a proxy over this (MediaFilterProxyModel) and the
// group trees (the library's artist/album views) are another
// (MediaGroupModel). The model resolves each row against the canonical
// item store per visible cell, so it costs 8 bytes per entry and a resync
// is visible with no copy (updateItem repaints in place).
class MediaListModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Column {
        // The item's artwork: a pixmap only (no display text, not
        // searchable). Hidden by default, hideable/reorderable like
        // every column (the column state is key-based, so the shift of
        // the other columns' values is invisible to a saved state).
        Artwork = 0,
        Title,
        Artist,
        Album,
        AlbumArtist,
        Composer,
        Genre,
        Track,
        Disc,
        Year,
        Duration,
        Comment,
        Bitrate,
        SampleRate,
        Channels,
        // The status column: the row's playback state (playing, paused,
        // downloading, a failed play). Deliberately headerless, the
        // marker is identified by its glyph.
        Playing,
        ColumnCount,
    };

    // The playback state shown in the status column. Only one row of one
    // playlist can carry it at a time — the row a playback state points
    // at (the current playlist's current row).
    enum class RowStatus {
        None = 0,
        Playing,
        Paused,
        Downloading,  // a provider resolution (a download) is in flight
        Error,        // the last play attempt produced no stream
    };

    explicit MediaListModel(QObject *parent = nullptr);

    static QString mediaMimeType();
    // The one duration formatter (the views' duration column, the position
    // slider, the current-track display; the import wizard passes
    // withHours for long videos): M:SS, or H:MM:SS when the hours are
    // non-zero and asked for; negative (unset) input is 0:00.
    static QString formatDuration(qint64 ms, bool withHours = false);
    // The status column's glyph and its tooltip text.
    static QString rowStatusChar(RowStatus status);
    static QString rowStatusText(RowStatus status);
    // The state that a row (its item id) is in against the playback: a
    // provider resolution in flight, the playing item (paused by the
    // state the caller passes) or the last failed play — none when the
    // row is none of them. The one decision behind the status markers of
    // the playlist views.
    static RowStatus rowStatusFor(quint64 rowId, const MediaItem &resolving,
                                  const MediaItem &playing, const MediaItem &failed,
                                  bool paused);

    // The display text of one column of one item: the single source of
    // truth shared by data() (what the view shows), the filter (what the
    // user can find) and the group tree (its track-row labels). The
    // status marker is model state (setRowStatus), not item data.
    static QString columnDisplay(const MediaItem &item, int column);

    // Stable string key per logical column ("title", "artist", "album",
    // ..., "playing") used in serialized column state; columnLogical() is
    // the inverse (-1 for unknown keys). columnLabel() is the
    // human-readable name used in the header context menu (the status
    // column itself has an empty header; it keeps the historical "playing"
    // key so a saved column state survives the rename).
    static QString columnKey(int logical);
    static int columnLogical(const QString &key);
    static QString columnLabel(int logical);
    // One sort key between two items: numeric for the numeric columns
    // (by the field, not the display string), case-insensitive text
    // otherwise. Shared by the filter proxy's lessThan and the group
    // model's track ordering.
    static int compareColumn(const MediaItem &a, const MediaItem &b, int column);
    // The columns the search filter matches over: every data column
    // (the status marker is model state, not searchable text). One set
    // for the filter proxy and the group tree, so what is typed is found
    // the same way in every view mode.
    static QList<int> searchableColumns();

    // Sets the referenced item ids (a change resets the model).
    void setItemIds(const QList<quint64> &ids);
    // Marks the row(s) of the given item id for repainting (emits
    // dataChanged) so that a resync can update rows without a model reset:
    // the model holds ids only, the canonical store was updated. Returns
    // false when the id is not in the model.
    bool updateItem(const MediaItem &item);
    // Marks the row that carries the playback status. A playlist may hold
    // the same item in several rows, so the marker is a row number (the
    // row the playlist's current index points at), not an id. -1 (or
    // None) clears it.
    void setRowStatus(int row, RowStatus status);
    int statusRow() const { return m_statusRow; }
    RowStatus rowStatus() const { return m_status; }
    quint64 idAt(int row) const;
    // The item id of a row index (0 for an invalid index or a dangling
    // reference).
    quint64 idAt(const QModelIndex &index) const;
    // The first row that carries the given item id (-1 when the id is
    // not in the model): a playlist may hold the same item in several
    // rows, and the first one names it (the follow playback's scroll).
    // The walk mirrors the repaint helper's (the ids are the data).
    int rowOf(quint64 id) const;

    // QAbstractItemModel interface. The model is flat: only the (invalid)
    // root index has rows.
    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList &indexes) const override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row,
                      int column, const QModelIndex &parent) override;
    Qt::DropActions supportedDropActions() const override;

    // The origin of the rows this model presents, named in the drag
    // payload (mimeData) so a drop target can tell a reorder (the drag
    // started in this playlist's own rows) from an insertion (the
    // library or another playlist).
    static QString libraryOrigin() { return QStringLiteral("library"); }
    static QString playlistOrigin(quint64 playlistId)
    {
        return QStringLiteral("playlist:") + QString::number(playlistId);
    }
    void setOriginId(const QString &origin) { m_originId = origin; }
    QString originId() const { return m_originId; }

    // The payload of a media drag: the item ids (one per dragged row, in
    // order — repeated when the rows repeat an item), the row numbers in
    // the source model and the origin. Drop targets parse a drag through
    // this: the media views (which decide reorder vs. insertion from the
    // origin) and the append-only targets (the tab strip, the playlist
    // list) that feed ids and rows to PlaylistManager::applyDrop. A mime
    // that is not a media drag parses to empty.
    struct DragData
    {
        QList<quint64> ids;
        QList<int> rows;
        QString origin;
    };
    static DragData dragDataFromMimeData(const QMimeData *data);
    // The media drag payload as JSON bytes ({"ids": [...], "rows": [...],
    // "origin": "…"}); the proxies that forward a drag build it with this.
    static QByteArray dragDataToJson(const QList<quint64> &ids,
                                     const QList<int> &rows,
                                     const QString &origin);
    // The payload as a QMimeData under the media type (the list model's
    // own mimeData and the proxies' build it with this; each computes its
    // own ids/rows — what a selection names — and the origin).
    static QMimeData* dragMimeData(const QList<quint64> &ids, const QList<int> &rows,
                                   const QString &origin);
    // The mime types a media view reports for a drag: the media payload
    // (drags from the app's own views) plus the uri-list format
    // (QMimeData::setUrls sets it for URL payloads, which is what the file
    // manager drops). Shared by the list model and the group model.
    static QStringList mediaMimeTypes();
    // The drop actions a media view supports.
    static Qt::DropActions mediaDropActions();

    // The insert index a drop lands at: QAbstractItemView (with
    // dragDropOverwriteMode off) reports a drop on a row edge as the
    // insert row; a drop in the middle of a row (and a drop on the empty
    // area below the last row) arrives with a negative row — the former
    // names its row in `parent` (insert there), the latter has no parent
    // (append at the end).
    static int dropIndexForRow(int row, const QModelIndex &parent, int rowCount);

signals:
    // The dragged rows, one entry per row: the item ids (repeated when the
    // rows repeat an item) and the row numbers in the source model (what a
    // reorder moves), the origin of the drag, and the drop index in this
    // model.
    void itemsDropped(const QList<quint64> &ids, const QList<int> &rows,
                      const QString &origin, int index);
    // A drop of local files from outside the app (the file manager): the
    // URLs in drop order and the insert index. The model only plumbs; the
    // widget decides what to do with them (media files, external playlist
    // files, ...).
    void filesDropped(const QList<QUrl> &urls, int index);
    // A drop of non-local URLs from outside the app (a browser): the URLs
    // in drop order and the insert index. A mixed drop emits both signals.
    // The model only plumbs; the widget decides (the library opens the
    // import wizard, the playlist views have no use for them).
    void urlsDropped(const QList<QUrl> &urls, int index);

private:
    // The rows a drop/selection names, unique and ascending.
    QList<int> uniqueRows(const QModelIndexList &indexes) const;

    QList<quint64> m_ids; // the item ids, in row order
    // Lazy id→rows index for updateItem() (the resync is the only caller):
    // built on first use, dropped by setItemIds(). A playlist may hold the
    // same item in several rows, so one id names several rows.
    QHash<quint64, QList<int>> m_rowIndex;
    bool m_rowIndexBuilt = false;
    // The row that shows the status marker (-1 for none) and its status.
    int m_statusRow = -1;
    RowStatus m_status = RowStatus::None;
    QString m_originId;
};

// Wires a model's drop signals (itemsDropped, filesDropped, urlsDropped)
// to the identically named signals of a proxy or view that re-emits them
// — the re-emit step of the model → proxy → view chain (every link
// declares the three signals with the same signatures). The view is
// created before its model, so the wiring happens where the model (or
// proxy) is attached.
template <typename Sender, typename Receiver>
inline void forwardDropSignals(Sender *sender, Receiver *receiver)
{
    QObject::connect(sender, &Sender::itemsDropped, receiver,
                     &Receiver::itemsDropped);
    QObject::connect(sender, &Sender::filesDropped, receiver,
                     &Receiver::filesDropped);
    QObject::connect(sender, &Sender::urlsDropped, receiver,
                     &Receiver::urlsDropped);
}

#endif // MEDIALISTMODEL_H
