#include "mediafilterproxy.h"

#include "medialistmodel.h"
#include "mediaitem.h"
#include "mediaitemstore.h"

#include <QAbstractItemModel>
#include <QMimeData>
#include <QSet>

#include <algorithm>

MediaFilterProxyModel::MediaFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    // Data changes in the source (a resync repaint, a row change) re-run
    // the filter and the sort, so the view stays current without a reset.
    setDynamicSortFilter(true);
}

void MediaFilterProxyModel::setSourceModel(QAbstractItemModel *source)
{
    if (m_source != nullptr)
        disconnect(m_source, nullptr, this, nullptr);
    QSortFilterProxyModel::setSourceModel(source);
    m_source = qobject_cast<MediaListModel*>(source);
    // The source owns the drop logic (and its signals): a drop that lands
    // on the proxy is mapped to a source index and forwarded.
    if (m_source != nullptr)
        forwardDropSignals(m_source, this);
}

void MediaFilterProxyModel::setFilterText(const QString &text)
{
    if (text == m_filterText)
        return;
    beginFilterChange();
    m_filterText = text;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

void MediaFilterProxyModel::setSort(int column, Qt::SortOrder order)
{
    if (column < 0 || column >= MediaListModel::ColumnCount) {
        column = -1;
        order = Qt::AscendingOrder;
    }
    if (column == sortColumn() && order == sortOrder())
        return;
    // sort(-1) stable-sorts the rows back to source order, so "off" is a
    // real state.
    sort(column, order);
    emit sortChanged();
}

void MediaFilterProxyModel::cycleSort(int column)
{
    if (column < 0 || column >= MediaListModel::ColumnCount)
        return;

    if (sortColumn() != column)
        setSort(column, Qt::AscendingOrder);
    else if (sortOrder() == Qt::AscendingOrder)
        setSort(column, Qt::DescendingOrder);
    else
        setSort(-1, Qt::AscendingOrder); // off: back to the given order
}

bool MediaFilterProxyModel::filterAcceptsRow(int sourceRow,
                                             const QModelIndex &sourceParent) const
{
    if (m_filterText.isEmpty())
        return true;
    // The source model resolves the row's item per cell (a dangling id
    // shows empty cells and matches nothing).
    const QAbstractItemModel *source = sourceModel();
    // A named local: the list must outlive both iterators.
    const QList<int> columns = MediaListModel::searchableColumns();
    return std::any_of(columns.cbegin(), columns.cend(),
                       [source, sourceRow, &sourceParent,
                        this](int column) {
                           return source->data(
                                      source->index(sourceRow, column, sourceParent),
                                      Qt::DisplayRole)
                               .toString()
                               .contains(m_filterText, Qt::CaseInsensitive);
                       });
}

bool MediaFilterProxyModel::lessThan(const QModelIndex &left,
                                     const QModelIndex &right) const
{
    // Ascending only (the direction is the proxy's sort order). The
    // comparison is by the field, not the display string, for the numeric
    // columns; a tie keeps the given order (stable sort).
    const MediaListModel *source = qobject_cast<const MediaListModel*>(sourceModel());
    if (source == nullptr)
        return false;
    const MediaItem a = MediaItemStore::resolve(source->idAt(left.row()));
    const MediaItem b = MediaItemStore::resolve(source->idAt(right.row()));
    return MediaListModel::compareColumn(a, b, left.column()) < 0;
}

QMimeData* MediaFilterProxyModel::mimeData(const QModelIndexList &indexes) const
{
    if (m_source == nullptr)
        return nullptr;

    // The selection may name a row's cells several times; keep each proxy
    // row once, ascending, then map to the source rows (the payload names
    // the source model's rows — what a reorder moves).
    QSet<int> seen;
    QList<int> selected;
    for (const QModelIndex &index : indexes) {
        if (!index.isValid() || seen.contains(index.row()))
            continue;
        seen.insert(index.row());
        selected.append(index.row());
    }
    std::sort(selected.begin(), selected.end());

    QList<quint64> ids;
    QList<int> mapped;
    for (int row : selected) {
        const QModelIndex sourceIndex = mapToSource(index(row, 0, QModelIndex()));
        if (!sourceIndex.isValid())
            continue;
        ids.append(m_source->idAt(sourceIndex.row()));
        mapped.append(sourceIndex.row());
    }
    return MediaListModel::dragMimeData(ids, mapped, m_source->originId());
}

bool MediaFilterProxyModel::dropMimeData(const QMimeData *data, Qt::DropAction action,
                                         int row, int column,
                                         const QModelIndex &parent)
{
    if (m_source == nullptr)
        return false;
    // The drop row is a proxy row (the insert position): map it to the
    // source row of the row it lands before. A drop in the middle of a row
    // arrives with a negative row and the row itself as parent (insert at
    // that row, mapped); a drop below the last visible row lands at the
    // end of the source.
    int sourceIndex = m_source->rowCount();
    if (row >= 0) {
        const QModelIndex sourceIndexMapped = mapToSource(index(row, 0, QModelIndex()));
        if (sourceIndexMapped.isValid())
            sourceIndex = sourceIndexMapped.row();
    } else if (parent.isValid()) {
        const QModelIndex sourceParent = mapToSource(parent);
        if (sourceParent.isValid())
            sourceIndex = sourceParent.row();
    }
    return m_source->dropMimeData(data, action, sourceIndex, column, QModelIndex());
}

Qt::DropActions MediaFilterProxyModel::supportedDropActions() const
{
    if (m_source == nullptr)
        return {};
    return m_source->supportedDropActions();
}
