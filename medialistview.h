#ifndef MEDIALISTVIEW_H
#define MEDIALISTVIEW_H

#include <QSet>
#include <QTreeView>
#include <QVariantMap>

#include "medialistmodel.h"

// Flat-list presentation of media item references: a QTreeView over a
// MediaListModel (the playlist views) or over the library's
// MediaFilterProxyModel (filter + the three-state header sort). The tree
// machinery is switched off (the model is flat — the grouped tree is the
// MediaGroupView's job), so the rows render like a multi-column table.
// The column state (order, visibility, widths) is the view's: it
// survives the model's resets (the header is the view's) and is
// serialized for the layout.
class MediaListView : public QTreeView
{
    Q_OBJECT
public:
    explicit MediaListView(QWidget *parent = nullptr);

    // The source model behind the view (the view's model itself when it
    // is a MediaListModel, its source when it is the filter proxy).
    MediaListModel* listModel() const { return m_sourceModel; }
    // Sets the source's referenced item ids (the column state survives —
    // the header is the view's, not the model's).
    void setItemIds(const QList<quint64> &ids) const;
    // Marks the row that carries the playback status (see
    // MediaListModel::RowStatus).
    void setRowStatus(int row, MediaListModel::RowStatus status) const;
    // (Re)marks a row's playback status from the playback state: the row
    // carries the state that row's playback is in (a provider resolution
    // in flight, playing, paused, the last failed play), and is cleared
    // when it is in none of them.
    void updateStatusMarker(quint64 currentItemId, int row) const;
    // Follow playback: scroll the row of the playing item into view on
    // a track change (the owning widget wires the scroll to
    // playingItemChanged and to its list changes). Off by default; the
    // widget persists the state.
    void setFollowPlayback(bool follow) { m_followPlayback = follow; }
    bool followPlayback() const { return m_followPlayback; }
    // The follow playback's scroll: the row of `item` into view (a
    // no-op when follow is off, the item is invalid, or it is not in
    // the view's model). Scroll only — the selection stays the user's
    // (an auto-selection would move the row the Delete and copy
    // shortcuts act on).
    void scrollToPlayingItem(const MediaItem &item);
    // The playlist of the playlist view that has keyboard focus (0 when
    // none has it): the app's focus widget is walked up the ancestor
    // chain to the nearest MediaListView — a tab view resolves by its
    // playlistId property (the tab machinery's), a view without one
    // (the current playlist's) to the manager's current playlist.
    // `focusWidget` overrides the app's focus widget (a test seam: the
    // offscreen platform never sets one).
    static quint64 focusedPlaylistId(QWidget *focusWidget = nullptr);
    // The item id a (view) row index names, resolved through the proxy.
    quint64 idAt(const QModelIndex &index) const;
    // Where this view's rows come from (named in the drag payload, see
    // MediaListModel::setOriginId).
    void setOriginId(const QString &origin) const;
    void setPlayingColumnVisible(bool visible);
    bool playingColumnVisible() const;
    // An unavailable column is kept hidden, is not offered in the header
    // context menu, and is excluded from the serialized column state.
    void setColumnAvailable(MediaListModel::Column column, bool available);
    bool columnAvailable(MediaListModel::Column column) const;
    // The logical columns the user can currently see: available and not
    // hidden.
    QList<int> visibleColumns() const;
    // The logical columns that exist (available), in logical order — the
    // options a sort may use.
    QList<int> availableColumns() const;
    void enableDropping();
    void enableDragAndDropping();
    // QTreeView has no selectRow convenience: selects the whole view row
    // (replacing the selection) and makes its first cell current.
    void selectRow(int row);
    // Header-click sorting (opt-in; the library enables it, the playlist
    // views keep their explicit sequence and do not): clicking a header
    // section sorts by that column through the view's filter proxy
    // (ascending → descending → off, the proxy's cycleSort) and shows the
    // indicator. A no-op when the view's model is not the proxy.
    void setSortable(bool sortable);
    bool sortable() const { return m_sortable; }
    QList<quint64> selectedIds() const;
    // The selected rows (unique, ascending): row-based operations (remove,
    // reorder) address rows, because a playlist may hold the same item in
    // several rows.
    QList<int> selectedRows() const;
    // The item ids of the selected rows, one entry per row in row order,
    // duplicates preserved (a row copy keeps every copy — contrast
    // selectedIds(), which dedups for the id-based operations).
    QList<quint64> selectedRowIds() const;
    int rowCount() const { return model() != nullptr ? model()->rowCount() : 0; }

    // Column state as a plain map for WidgetConfiguration persistence
    // (unavailable columns are excluded):
    //   "order"  — column keys in the visual order
    //   "hidden" — keys of the hidden columns
    //   "widths" — per-key section sizes
    QVariantMap columnState() const;
    // Applies whatever of the above is present; unknown keys, duplicates
    // and unavailable columns are ignored, missing columns are appended
    // to the order.
    void restoreColumnState(const QVariantMap &state);

    // QAbstractItemView interface: re-connects the model's drag signals
    // (the view is created before its model).
    void setModel(QAbstractItemModel *model) override;

signals:
    void itemsDropped(const QList<quint64> &ids, const QList<int> &rows,
                      const QString &origin, int index);
    // A drop of local files from outside the app (see
    // MediaListModel::filesDropped): the widget decides what to do with
    // them.
    void filesDropped(const QList<QUrl> &urls, int index);
    // A drop of non-local URLs from outside the app (see
    // MediaListModel::urlsDropped): the widget decides what to do with
    // them.
    void urlsDropped(const QList<QUrl> &urls, int index);
    // Emitted when a column's hidden state changes through a user-facing
    // action (header menu, setColumnAvailable, setPlayingColumnVisible).
    // Deliberately not emitted by the restore path.
    void columnsVisibilityChanged();
    // The sort state changed (the rows were re-sorted).
    void sortChanged();

private slots:
    void onHeaderContextMenu(const QPoint &pos);
    void onHeaderClicked(int logical);
    void onSortChanged();

private:
    // Sets the hidden state and emits columnsVisibilityChanged() only
    // when it actually changes.
    void setColumnHidden(int logical, bool hidden);
    // The header's defaults (applied when a model is set): the status
    // column first, the extended columns hidden.
    void applyHeaderDefaults();
    // The indicator follows the proxy's sort (hidden when off).
    void updateSortIndicator();

    QSet<int> m_unavailableColumns;
    bool m_sortable = false;
    // The follow playback's scroll (see setFollowPlayback).
    bool m_followPlayback = false;
    // The flat source behind the view's model (a non-owning cache of the
    // qobject_cast in setModel).
    MediaListModel* m_sourceModel = nullptr;
};

#endif // MEDIALISTVIEW_H
