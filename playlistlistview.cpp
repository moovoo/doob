#include "playlistlistview.h"

#include <QMimeData>

PlaylistListView::PlaylistListView(QWidget *parent)
    : QTreeWidget(parent)
{
    // A drop on a row edge, not over it: the widget's drop handler
    // (the event filter) interprets the position, the view only keeps
    // the drag side working.
    setDragDropOverwriteMode(false);
    setDragEnabled(true);
    setAcceptDrops(true);
}

QString PlaylistListView::playlistMimeType()
{
    return QStringLiteral("application/x-doob-playlist");
}

QStringList PlaylistListView::mimeTypes() const
{
    return { playlistMimeType() };
}

QMimeData* PlaylistListView::mimeData(const QList<QTreeWidgetItem*> &items) const
{
    // SingleSelection: the drag names exactly one row.
    if (items.isEmpty())
        return nullptr;
    const QTreeWidgetItem *item = items.constFirst();

    QMimeData *mime = new QMimeData();
    mime->setData(playlistMimeType(),
                  QString::number(item->data(0, Qt::UserRole).toULongLong())
                      .toUtf8());
    return mime;
}

Qt::DropActions PlaylistListView::supportedDropActions() const
{
    // Move for the row drags, Copy for the file drags from the file
    // manager.
    return Qt::MoveAction | Qt::CopyAction;
}

QMimeData* PlaylistListView::dragMimeData(QTreeWidgetItem *item) const
{
    return mimeData({ item });
}
