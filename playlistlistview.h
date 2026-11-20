#ifndef PLAYLISTLISTVIEW_H
#define PLAYLISTLISTVIEW_H

#include <QList>
#include <QTreeWidget>

class QMimeData;

// The playlist list's view: a QTreeWidget that drags its rows (one row per
// playlist, SingleSelection) with a custom MIME payload (the playlist's
// id). Drops are handled by the owning PlaylistListWidget's event filter
// on the viewport: a drop of a row reorders the list, a drop of items (a
// media drag) or of files is appended to the playlist of the row under
// the pointer. The built-in internal move is deliberately not used: it
// depends on drag-session state and is not unit-testable (the same reason
// MediaListModel plumbs its own drops).
class PlaylistListView : public QTreeWidget
{
    Q_OBJECT
public:
    explicit PlaylistListView(QWidget *parent = nullptr);

    // The MIME type of a dragged row (the payload is the playlist's id,
    // as a plain decimal string).
    static QString playlistMimeType();

    // The drag payload of a row (the virtual is protected on QTreeWidget).
    QMimeData* dragMimeData(QTreeWidgetItem *item) const;

protected:
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QList<QTreeWidgetItem*> &items) const override;
    Qt::DropActions supportedDropActions() const override;
};

#endif // PLAYLISTLISTVIEW_H
