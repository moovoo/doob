#include "medialistview.h"

#include "basewidget.h"
#include "mediafilterproxy.h"
#include "playbackmanager.h"
#include "playlistmanager.h"

#include <algorithm>

#include <QAbstractProxyModel>
#include <QAction>
#include <QApplication>
#include <QHeaderView>
#include <QMenu>

namespace {

// The column state is restored piece by piece; each piece is applied only
// when it is present, so a partial (e.g. pre-feature) state leaves the
// other pieces untouched. Unavailable columns are never touched by a
// restore, and order/widths are only applied to available ones.

void applyColumnOrder(QHeaderView *header, const QVariant &orderValue,
                      const QSet<int> &unavailable)
{
    if (!orderValue.isValid())
        return;

    // Accept a permutation of the known keys; append the missing columns at
    // the end so a stale state can never lose one.
    QStringList order;
    QSet<QString> seen;
    for (const QVariant &value : orderValue.toList()) {
        const QString key = value.toString();
        const int logical = MediaListModel::columnLogical(key);
        if (logical >= 0 && !unavailable.contains(logical) && !seen.contains(key)) {
            order.append(key);
            seen.insert(key);
        }
    }
    for (int logical = 0; logical < MediaListModel::ColumnCount; ++logical) {
        if (unavailable.contains(logical))
            continue;
        const QString key = MediaListModel::columnKey(logical);
        if (!seen.contains(key))
            order.append(key);
    }

    // moveSection() takes visual indices on both sides.
    for (int visual = 0; visual < order.size(); ++visual) {
        const int logical = MediaListModel::columnLogical(order.at(visual));
        const int current = header->visualIndex(logical);
        if (current != visual)
            header->moveSection(current, visual);
    }
}

void applyColumnVisibility(QHeaderView *header, const QVariant &hiddenValue,
                           const QSet<int> &unavailable)
{
    if (!hiddenValue.isValid())
        return;

    const QStringList hidden = hiddenValue.toStringList();
    for (int logical = 0; logical < MediaListModel::ColumnCount; ++logical) {
        if (unavailable.contains(logical))
            continue;
        header->setSectionHidden(logical,
                                 hidden.contains(MediaListModel::columnKey(logical)));
    }
}

void applyColumnWidths(QHeaderView *header, const QVariant &widthsValue,
                       const QSet<int> &unavailable)
{
    if (!widthsValue.isValid())
        return;

    const QVariantMap widths = widthsValue.toMap();
    for (auto it = widths.constBegin(); it != widths.constEnd(); ++it) {
        const int logical = MediaListModel::columnLogical(it.key());
        const int size = it.value().toInt();
        if (logical >= 0 && !unavailable.contains(logical) && size > 0)
            header->resizeSection(logical, size);
    }
}

} // namespace

MediaListView::MediaListView(QWidget *parent)
    : QTreeView(parent)
{
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setAlternatingRowColors(true);
    // The model is flat (no row has children) and never becomes a tree:
    // render the rows like a table — no branch column, no expand
    // indicators, no indent (and a double-click must not expand a row,
    // it activates it).
    setRootIsDecorated(false);
    setItemsExpandable(false);
    setExpandsOnDoubleClick(false);
    // A drop on a row arrives as the insert row (not "overwrite the row"):
    // the insert position is what the drop targets want.
    setDragDropOverwriteMode(false);

    // Right-clicking the header offers a checkable list of columns.
    header()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header(), &QWidget::customContextMenuRequested,
            this, &MediaListView::onHeaderContextMenu);
    // A header click sorts by the column (the sortable views; the
    // playlist views keep their explicit sequence).
    connect(header(), &QHeaderView::sectionClicked, this,
            &MediaListView::onHeaderClicked);
}

void MediaListView::setModel(QAbstractItemModel *model)
{
    if (this->model() != nullptr)
        disconnect(this->model(), nullptr, this, nullptr);
    QTreeView::setModel(model);
    m_sourceModel = nullptr;
    if (model == nullptr)
        return;

    // The header is the view's and keeps its customizations across model
    // resets; apply the defaults for a fresh header (the widget restores
    // a persisted state over them, if it has one).
    applyHeaderDefaults();

    // Re-emit the model's drag signals under the view's name (the view is
    // created before its model, so the wiring happens here), and cache the
    // flat source behind whatever model it is.
    if (MediaListModel *list = qobject_cast<MediaListModel*>(model)) {
        m_sourceModel = list;
        forwardDropSignals(list, this);
    } else if (MediaFilterProxyModel *proxy =
                   qobject_cast<MediaFilterProxyModel*>(model)) {
        m_sourceModel = proxy->listSource();
        forwardDropSignals(proxy, this);
        connect(proxy, &MediaFilterProxyModel::sortChanged, this,
                &MediaListView::onSortChanged);
    }
    updateSortIndicator();
}

void MediaListView::setItemIds(const QList<quint64> &ids) const
{
    if (MediaListModel *source = listModel())
        source->setItemIds(ids);
}

void MediaListView::applyHeaderDefaults()
{
    QHeaderView *hdr = header();
    hdr->setStretchLastSection(false);
    hdr->setSectionsMovable(true);
    // QHeaderView additionally pins the section with logical index 0
    // (Title) by default, regardless of its visual position: un-pin it so
    // every column can be reordered.
    hdr->setFirstSectionMovable(true);
    // The status column is the first column by default. moveSection()
    // takes visual indices on both sides.
    if (hdr->count() > MediaListModel::Playing)
        hdr->moveSection(hdr->visualIndex(MediaListModel::Playing), 0);

    // Columns that exist but are hidden by default (the header context
    // menu still offers them); a persisted column state overrides this.
    for (const int column : { MediaListModel::Artwork,
                              MediaListModel::AlbumArtist, MediaListModel::Composer,
                              MediaListModel::Genre, MediaListModel::Comment,
                              MediaListModel::Bitrate, MediaListModel::SampleRate,
                              MediaListModel::Channels, })
        hdr->setSectionHidden(column, true);
    // Unavailable columns are hidden whatever a stored state says (the
    // flag may have been set before the model was).
    for (int column : m_unavailableColumns)
        hdr->setSectionHidden(column, true);
}

void MediaListView::setPlayingColumnVisible(bool visible)
{
    // An unavailable column (e.g. Playing in the library view) cannot be
    // shown, whatever a stored state says.
    if (!columnAvailable(MediaListModel::Playing))
        return;
    setColumnHidden(MediaListModel::Playing, !visible);
}

bool MediaListView::playingColumnVisible() const
{
    return header() != nullptr
           && !header()->isSectionHidden(MediaListModel::Playing);
}

void MediaListView::setColumnAvailable(MediaListModel::Column column, bool available)
{
    const int logical = static_cast<int>(column);
    if (logical < 0 || logical >= MediaListModel::ColumnCount)
        return;
    if (available) {
        m_unavailableColumns.remove(logical);
        return;
    }
    m_unavailableColumns.insert(logical);
    setColumnHidden(logical, true);
}

QList<int> MediaListView::visibleColumns() const
{
    QList<int> columns;
    for (int logical = 0; logical < MediaListModel::ColumnCount; ++logical) {
        if (m_unavailableColumns.contains(logical))
            continue;
        if (header() != nullptr && !header()->isSectionHidden(logical))
            columns.append(logical);
    }
    return columns;
}

QList<int> MediaListView::availableColumns() const
{
    QList<int> columns;
    for (int logical = 0; logical < MediaListModel::ColumnCount; ++logical)
        if (!m_unavailableColumns.contains(logical))
            columns.append(logical);
    return columns;
}

void MediaListView::setColumnHidden(int logical, bool hidden)
{
    if (header() == nullptr || header()->isSectionHidden(logical) == hidden)
        return;
    header()->setSectionHidden(logical, hidden);
    emit columnsVisibilityChanged();
}

bool MediaListView::columnAvailable(MediaListModel::Column column) const
{
    return !m_unavailableColumns.contains(static_cast<int>(column));
}

void MediaListView::setRowStatus(int row, MediaListModel::RowStatus status) const
{
    if (MediaListModel *source = listModel())
        source->setRowStatus(row, status);
}

void MediaListView::updateStatusMarker(quint64 currentItemId, int row) const
{
    PlaybackManager *pm = PlaybackManager::getInstance();
    const MediaListModel::RowStatus status = MediaListModel::rowStatusFor(
        currentItemId, pm->resolvingItem(), pm->playingItem(),
        pm->failedItem(), pm->state() == PlaybackManager::Paused);
    setRowStatus(status == MediaListModel::RowStatus::None ? -1 : row, status);
}

void MediaListView::scrollToPlayingItem(const MediaItem &item)
{
    if (!m_followPlayback || !item.isValid() || m_sourceModel == nullptr)
        return;
    const int row = m_sourceModel->rowOf(item.id);
    if (row < 0)
        return;
    // Scroll only: the selection is the user's (an auto-selection would
    // move the row the Delete and copy shortcuts act on).
    scrollTo(model()->index(row, 0), QAbstractItemView::EnsureVisible);
}

quint64 MediaListView::focusedPlaylistId(QWidget *focusWidget)
{
    if (focusWidget == nullptr)
        focusWidget = QApplication::focusWidget();
    for (QWidget *w = focusWidget; w != nullptr; w = w->parentWidget()) {
        const MediaListView *view = qobject_cast<const MediaListView *>(w);
        if (view == nullptr)
            continue;
        const QVariant property = view->property("playlistId");
        if (property.isValid())
            return static_cast<quint64>(property.toULongLong());
        // A view without the property (the current playlist's): its rows
        // are the current playlist.
        return PlaylistManager::getInstance()->currentPlaylistId();
    }
    return 0;
}

void MediaListView::selectRow(int row)
{
    if (model() == nullptr || selectionModel() == nullptr
            || row < 0 || row >= model()->rowCount())
        return;
    // The selection model indexes the view's model (the proxy, when there
    // is one), so the row is taken from it.
    const QModelIndex topLeft = model()->index(row, 0);
    const QModelIndex bottomRight = model()->index(row, model()->columnCount() - 1);
    selectionModel()->select(QItemSelection(topLeft, bottomRight),
                             QItemSelectionModel::Select
                             | QItemSelectionModel::Rows);
    setCurrentIndex(topLeft);
}

quint64 MediaListView::idAt(const QModelIndex &index) const
{
    if (MediaListModel *source = listModel()) {
        // Through the proxy when there is one (the identity for a bare
        // list model).
        int row = index.row();
        if (const QAbstractProxyModel *proxy =
                qobject_cast<const QAbstractProxyModel*>(index.model()))
            row = proxy->mapToSource(index).row();
        return source->idAt(row);
    }
    return 0;
}

void MediaListView::setOriginId(const QString &origin) const
{
    if (MediaListModel *source = listModel())
        source->setOriginId(origin);
}

void MediaListView::enableDropping()
{
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DropOnly);
}

void MediaListView::enableDragAndDropping()
{
    // setDragDropMode() (re)sets dragEnabled/acceptDrops itself — call it last.
    setDefaultDropAction(Qt::MoveAction);
    setDragDropMode(QAbstractItemView::DragDrop);
}

void MediaListView::setSortable(bool sortable)
{
    m_sortable = sortable;
}

void MediaListView::onHeaderClicked(int logical)
{
    if (!m_sortable)
        return;
    // The status marker is model state, not a sortable column.
    if (logical == MediaListModel::Playing)
        return;
    if (auto *proxy = qobject_cast<MediaFilterProxyModel*>(model()))
        proxy->cycleSort(logical);
}

void MediaListView::onSortChanged()
{
    updateSortIndicator();
    emit sortChanged();
}

void MediaListView::updateSortIndicator()
{
    if (auto *proxy = qobject_cast<MediaFilterProxyModel*>(model())) {
        const int column = proxy->sortColumn();
        if (column < 0) {
            header()->setSortIndicatorShown(false);
            return;
        }
        header()->setSortIndicatorShown(true);
        header()->setSortIndicator(column, proxy->sortOrder());
        return;
    }
    header()->setSortIndicatorShown(false);
}

QVariantMap MediaListView::columnState() const
{
    const QHeaderView *hdr = header();
    if (hdr == nullptr)
        return {};

    QVariantList order;
    for (int visual = 0; visual < hdr->count(); ++visual) {
        const int logical = hdr->logicalIndex(visual);
        if (!m_unavailableColumns.contains(logical))
            order.append(MediaListModel::columnKey(logical));
    }

    QVariantList hidden;
    QVariantMap widths;
    for (int logical = 0; logical < MediaListModel::ColumnCount; ++logical) {
        if (m_unavailableColumns.contains(logical))
            continue;
        const QString key = MediaListModel::columnKey(logical);
        if (hdr->isSectionHidden(logical))
            hidden.append(key);
        widths.insert(key, hdr->sectionSize(logical));
    }

    QVariantMap state;
    state.insert("order", order);
    state.insert("hidden", hidden);
    state.insert("widths", widths);
    return state;
}

void MediaListView::restoreColumnState(const QVariantMap &state)
{
    QHeaderView *hdr = header();
    if (hdr == nullptr)
        return;
    applyColumnOrder(hdr, state.value("order"), m_unavailableColumns);
    applyColumnVisibility(hdr, state.value("hidden"), m_unavailableColumns);
    applyColumnWidths(hdr, state.value("widths"), m_unavailableColumns);
}

void MediaListView::onHeaderContextMenu(const QPoint &pos)
{
    QHeaderView *hdr = header();
    if (hdr == nullptr)
        return;

    // List the columns in the order the user sees them. Unavailable
    // columns are not offered at all.
    QList<int> logicals;
    for (int visual = 0; visual < hdr->count(); ++visual) {
        const int logical = hdr->logicalIndex(visual);
        if (!m_unavailableColumns.contains(logical))
            logicals.append(logical);
    }

    QMenu *menu = transientMenu(this);
    for (const int logical : logicals) {
        QAction *action = menu->addAction(MediaListModel::columnLabel(logical));
        action->setCheckable(true);
        action->setChecked(!hdr->isSectionHidden(logical));
        connect(action, &QAction::triggered, this, [this, logical](bool checked) {
            setColumnHidden(logical, !checked);
        });
    }

    menu->popup(hdr->viewport()->mapToGlobal(pos));
}

QList<quint64> MediaListView::selectedIds() const
{
    QList<quint64> ids;
    if (selectionModel() == nullptr)
        return ids;
    for (const QModelIndex &index : selectionModel()->selectedRows(0)) {
        const quint64 id = idAt(index);
        // A playlist may hold the same item in several rows; the ids
        // (metadata edits, removals) name it once.
        if (id != 0 && !ids.contains(id))
            ids.append(id);
    }
    return ids;
}

QList<int> MediaListView::selectedRows() const
{
    QList<int> rows;
    if (selectionModel() == nullptr)
        return rows;
    for (const QModelIndex &index : selectionModel()->selectedRows(0)) {
        const int row = index.row();
        if (!rows.contains(row))
            rows.append(row);
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

QList<quint64> MediaListView::selectedRowIds() const
{
    // selectedRows(0) yields the indices in row order.
    QList<quint64> ids;
    if (selectionModel() == nullptr)
        return ids;
    for (const QModelIndex &index : selectionModel()->selectedRows(0)) {
        const quint64 id = idAt(index);
        if (id != 0)
            ids.append(id);
    }
    return ids;
}
