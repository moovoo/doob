#ifndef MEDIAGROUPVIEW_H
#define MEDIAGROUPVIEW_H

#include <QTreeView>

#include "mediagroupmodel.h"

// Tree presentation of media item references: a single-column QTreeView
// over the MediaGroupModel (the library's artist/album modes). The tree's
// shape is the information — no header, no columns to move, no horizontal
// scroll — so there is no column state to keep. The tree comes up fully
// collapsed: a rebuild (a new id list, the grouping, the filter) resets
// the model, and the user expands what they want.
class MediaGroupView : public QTreeView
{
    Q_OBJECT
public:
    explicit MediaGroupView(QWidget *parent = nullptr);

    MediaGroupModel* groupModel() const { return m_model; }

    // The tree shape (the model's grouping).
    void setGrouping(MediaGroupModel::Grouping grouping);
    // The search text (the model's filter).
    void setFilterText(const QString &text);

    void enableDragAndDropping();

    // The selected rows' ids (a group selection expands to its tracks,
    // each id once).
    QList<quint64> selectedIds() const;
    // The ids a row names: its own for a track row, every track beneath
    // it for a group row.
    QList<quint64> idsUnder(const QModelIndex &index) const;

signals:
    void itemsDropped(const QList<quint64> &ids, const QList<int> &rows,
                      const QString &origin, int index);
    // A drop of local files from outside the app (see
    // MediaGroupModel::filesDropped): the widget decides what to do with
    // them.
    void filesDropped(const QList<QUrl> &urls, int index);
    // A drop of non-local URLs from outside the app (see
    // MediaGroupModel::urlsDropped): the widget decides what to do with
    // them.
    void urlsDropped(const QList<QUrl> &urls, int index);

private:
    MediaGroupModel* m_model;
};

#endif // MEDIAGROUPVIEW_H
