#ifndef MEDIAGROUPMODEL_H
#define MEDIAGROUPMODEL_H

#include <QAbstractProxyModel>
#include <QHash>
#include <QList>
#include <QUrl>

class MediaListModel;
class MediaItem;

// The library's grouped views: a single-column tree proxy over a
// MediaListModel. The groupings:
//   ByArtist      — artists → albums → tracks
//   ByAlbum       — albums → tracks
//   ByArtistAlbum — (artist, album) tuples → tracks (one level)
// A group row shows "Name (N)" (the number of tracks in the group at the
// end of the line); a track row shows "N. Title" (the number prefix only
// when the track number is tagged). There are no columns, no header and
// no sorting in the tree: the groups are ordered by name (case-
// insensitive, "(Unknown)" last), the tracks keep the natural album
// order (disc, track number, title). A search text keeps only the
// matching tracks (the searchable columns, as in the flat view) and a
// group without a matching track disappears. Dragging a group drags its
// whole content (the payload expands it to its tracks); the tree has no
// reorder, so the payload's rows are empty.
class MediaGroupModel : public QAbstractProxyModel
{
    Q_OBJECT
public:
    enum class Grouping { ByArtist = 0, ByAlbum, ByArtistAlbum };

    explicit MediaGroupModel(QObject *parent = nullptr);
    ~MediaGroupModel() override;

    // The source to group (the tree rebuilds on the source's reset and
    // data changes; the drag signals are forwarded from it).
    void setSourceModel(MediaListModel *source);
    MediaListModel* listSource() const { return m_source; }

    // The tree shape (a change rebuilds the tree, resetting the model).
    void setGrouping(Grouping grouping);
    Grouping grouping() const { return m_grouping; }
    // The search text (a change rebuilds the tree, resetting the model).
    void setFilterText(const QString &text);
    QString filterText() const { return m_filterText; }

    // The display text of a row: the group's "Name (N)", the track's
    // "N. Title".
    QString rowText(const QModelIndex &index) const;
    // A group row (true) or a track row (false).
    bool isGroupRow(const QModelIndex &index) const;
    // The item id of a row (0 for a group row, an invalid index or a
    // dangling reference).
    quint64 idAt(const QModelIndex &index) const;
    // The item ids a row names, in tree order: the row's own id for a
    // track row, every id beneath it for a group row (dragging — or
    // removing — an artist or an album means its whole content).
    QList<quint64> idsUnder(const QModelIndex &index) const;
    // The number of tracks beneath a group row (0 for a track row).
    int countUnder(const QModelIndex &index) const;

    // QAbstractItemModel interface: a single column (the tree's labels).
    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool hasChildren(const QModelIndex &parent = QModelIndex()) const override;

    QModelIndex mapToSource(const QModelIndex &proxyIndex) const override;
    QModelIndex mapFromSource(const QModelIndex &sourceIndex) const override;

    QStringList mimeTypes() const override;
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

private slots:
    void onSourceReset();
    void onSourceDataChanged();

private:
    // One row of the tree: a track row (itemId != 0, no children) or a
    // group row (itemId == 0, its tracks as children, its name shown with
    // its count). The nodes are heap-allocated; the indexes point at
    // their parent (the root for a top-level row), so the pointers stay
    // valid until the tree is rebuilt (a model reset invalidates the
    // indexes anyway).
    struct Node
    {
        QString name;        // a group row's display name (empty for a track)
        quint64 itemId = 0;  // 0: a group row
        Node *parent = nullptr;
        int rowInParent = 0;
        int count = 0;       // the tracks beneath (a group row)
        int displayOrder = 0; // depth-first order (the drag payload's order)
        QList<Node*> children; // owned
        ~Node() { for (Node *child : children) delete child; }
    };

    // Rebuilds the row tree from the source's ids, the grouping and the
    // filter. The caller wraps it in begin/endResetModel.
    void rebuildTree();
    // The display name of a group (an empty tag becomes "(Unknown)" — a
    // nameless group row would read as a broken row).
    static QString groupDisplayName(const QString &value);
    // The (artist, album) tuple's name: the parts joined by " — ", each
    // missing part "(Unknown)", a fully missing pair a single "(Unknown)".
    static QString tupleName(const MediaItem &item);
    // The track row's text: "N. Title" (the prefix only when tagged).
    static QString trackText(const MediaItem &item);
    // The group-tree building blocks: a track leaf under its parent, a
    // group child looked up (or created) by name.
    static void appendLeaf(Node *parent, Node *leaf);
    static Node* ensureGroup(Node *parent, QHash<QString, Node*> &map,
                             const QString &name);
    // A track matches the filter when the text is a substring of any
    // searchable column's display value (case-insensitive).
    bool trackMatches(const MediaItem &item) const;
    static void sortGroups(Node *parent);
    // The natural track order: disc, track number, title (case-
    // insensitive) — the order an album is played in.
    static int compareTracks(const MediaItem &a, const MediaItem &b);
    static void sortTracks(Node *parent);
    // Depth-first: assigns each node its row in the parent, its display
    // order and (for a group) its track count; returns the tracks beneath
    // base.
    static int assignRowsAndCounts(Node *base, int &order);
    static QList<quint64> leafIds(const Node *node);
    // The node an index names: the index's internal pointer is the node's
    // parent (the QAbstractItemModel convention parent() relies on).
    const Node* nodeOf(const QModelIndex &index) const;

    Node* m_root = nullptr; // owns its children; always non-null
    MediaListModel* m_source = nullptr;
    Grouping m_grouping = Grouping::ByArtist;
    QString m_filterText;
    // id → the source's first row of the id (mapToSource) and the first
    // track node that holds it (mapFromSource); rebuilt with the tree.
    QHash<quint64, int> m_sourceRow;
    QHash<quint64, Node*> m_trackNode;
};

#endif // MEDIAGROUPMODEL_H
