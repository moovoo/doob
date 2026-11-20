#include "mediagroupview.h"

#include "medialistmodel.h"

#include <QHeaderView>

MediaGroupView::MediaGroupView(QWidget *parent)
    : QTreeView(parent)
    , m_model(new MediaGroupModel(this))
{
    setModel(m_model);
    // The single column has no header: the tree's shape identifies the
    // rows (a group's name with its count, a track's "N. Title").
    header()->hide();
    setRootIsDecorated(true);
    setItemsExpandable(true);
    setExpandsOnDoubleClick(true);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setAlternatingRowColors(true);
    setDragDropOverwriteMode(false);

    forwardDropSignals(m_model, this);
}

void MediaGroupView::setGrouping(MediaGroupModel::Grouping grouping)
{
    m_model->setGrouping(grouping);
}

void MediaGroupView::setFilterText(const QString &text)
{
    m_model->setFilterText(text);
}

void MediaGroupView::enableDragAndDropping()
{
    // setDragDropMode() (re)sets dragEnabled/acceptDrops itself — call it last.
    setDefaultDropAction(Qt::MoveAction);
    setDragDropMode(QAbstractItemView::DragDrop);
}

QList<quint64> MediaGroupView::selectedIds() const
{
    QList<quint64> ids;
    if (selectionModel() == nullptr)
        return ids;
    for (const QModelIndex &index : selectionModel()->selectedRows(0)) {
        // A group row (an artist, an album, a tuple) names every track
        // beneath it: removing — or editing — the selection means its
        // whole content.
        for (quint64 id : m_model->idsUnder(index))
            if (id != 0 && !ids.contains(id))
                ids.append(id);
    }
    return ids;
}

QList<quint64> MediaGroupView::idsUnder(const QModelIndex &index) const
{
    return m_model->idsUnder(index);
}
