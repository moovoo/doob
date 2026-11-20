#ifndef MEDIAFILTERPROXY_H
#define MEDIAFILTERPROXY_H

#include <QSortFilterProxyModel>

#include <QUrl>

class MediaListModel;

// The library's List-mode filter and sort: a flat proxy over the
// MediaListModel. The search text is a case-insensitive substring match
// over the searchable columns (what is typed is found, whatever columns
// the view happens to show); the sort is the header's single column with
// the three states ascending → descending → off (off restores the
// source's given order — QSortFilterProxyModel's sort(-1) sorts the rows
// back to source order, so "off" is a real state, not a missing one).
// The source model's order is never touched: this is a view over it.
// The model is flat and the proxy maps rows one-to-one, so the payload
// of a media drag (ids, rows, origin) is built from the mapped source
// rows and a drop is forwarded to the source model.
class MediaFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    explicit MediaFilterProxyModel(QObject *parent = nullptr);

    // The source (set before the view is shown; the drag signals are
    // forwarded from it).
    void setSourceModel(QAbstractItemModel *source) override;
    // The flat list model behind the proxy.
    MediaListModel* listSource() const { return m_source; }

    // The search text (a change re-filters the rows). Empty matches
    // everything.
    void setFilterText(const QString &text);
    QString filterText() const { return m_filterText; }

    // The three-state column sort: a column not sorted becomes the sole
    // key ascending, an ascending key flips to descending, a descending
    // key is removed (back to the given order).
    void cycleSort(int column);
    // Sets the sort directly (column -1 clears it, back to the given
    // order); a no-op when the sort is unchanged. The active sort is the
    // base class's sortColumn() (-1 when off) and sortOrder().
    void setSort(int column, Qt::SortOrder order);

    // QAbstractItemModel interface: the flat pass-through of the source.
    QMimeData* mimeData(const QModelIndexList &indexes) const override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row,
                      int column, const QModelIndex &parent) override;
    Qt::DropActions supportedDropActions() const override;

signals:
    // Forwarded from the source model (a drop that landed here).
    void itemsDropped(const QList<quint64> &ids, const QList<int> &rows,
                      const QString &origin, int index);
    void filesDropped(const QList<QUrl> &urls, int index);
    void urlsDropped(const QList<QUrl> &urls, int index);
    // The sort state changed (the rows were re-sorted).
    void sortChanged();

protected:
    // A row matches when the text is a substring of the displayed value
    // of any searchable column (case-insensitive).
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    // Ascending only (the direction is the proxy's sort order): the
    // numeric-aware column comparison, a tie keeps the given order
    // (stable sort).
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    QString m_filterText;
    MediaListModel* m_source = nullptr;
};

#endif // MEDIAFILTERPROXY_H
