#include "mediagroupmodel.h"

#include "medialistmodel.h"
#include "mediaitem.h"
#include "mediaitemstore.h"

#include <QMimeData>
#include <QSet>
#include <QtGlobal>

#include <algorithm>

MediaGroupModel::MediaGroupModel(QObject *parent)
    : QAbstractProxyModel(parent)
{
    m_root = new Node;
}

MediaGroupModel::~MediaGroupModel()
{
    delete m_root;
}

void MediaGroupModel::setSourceModel(MediaListModel *source)
{
    if (m_source == source)
        return;
    if (m_source != nullptr)
        disconnect(m_source, nullptr, this, nullptr);
    m_source = source;
    QAbstractProxyModel::setSourceModel(source);
    if (m_source != nullptr) {
        // The source's rows are the tree's leaves: a reset (a new id
        // list) and a data change (a resync re-tagging a row) rebuild the
        // tree — the group names and counts are tag-derived. The drop
        // logic is the source's (and its signals are forwarded).
        connect(m_source, &MediaListModel::modelReset, this,
                &MediaGroupModel::onSourceReset);
        connect(m_source, &MediaListModel::dataChanged, this,
                &MediaGroupModel::onSourceDataChanged);
        forwardDropSignals(m_source, this);
    }
    onSourceReset();
}

void MediaGroupModel::onSourceReset()
{
    beginResetModel();
    rebuildTree();
    endResetModel();
}

void MediaGroupModel::onSourceDataChanged()
{
    // A resync may have renamed an artist or album or renumbered a track:
    // the group structure can only change with the tags it is built from.
    onSourceReset();
}

void MediaGroupModel::setGrouping(Grouping grouping)
{
    if (grouping == m_grouping)
        return;
    m_grouping = grouping;
    onSourceReset();
}

void MediaGroupModel::setFilterText(const QString &text)
{
    if (text == m_filterText)
        return;
    m_filterText = text;
    onSourceReset();
}

QString MediaGroupModel::groupDisplayName(const QString &value)
{
    return value.isEmpty() ? QStringLiteral("(Unknown)") : value;
}

QString MediaGroupModel::tupleName(const MediaItem &item)
{
    if (item.artist.isEmpty() && item.album.isEmpty())
        return groupDisplayName(QString());
    return groupDisplayName(item.artist)
         + QStringLiteral(" — ")
         + groupDisplayName(item.album);
}

QString MediaGroupModel::trackText(const MediaItem &item)
{
    if (item.trackNumber > 0)
        return QStringLiteral("%1. %2").arg(item.trackNumber).arg(item.title);
    return item.title;
}

bool MediaGroupModel::trackMatches(const MediaItem &item) const
{
    if (m_filterText.isEmpty())
        return true;
    // A named local: the list must outlive both iterators.
    const QList<int> columns = MediaListModel::searchableColumns();
    return std::any_of(columns.cbegin(), columns.cend(),
                       [&item, this](int column) {
                           return MediaListModel::columnDisplay(item, column)
                               .contains(m_filterText, Qt::CaseInsensitive);
                       });
}

// The row tree: the source's tracks (filtered) nested by tag — artists →
// albums → tracks, albums → tracks, or (artist, album) tuples → tracks.
void MediaGroupModel::rebuildTree()
{
    delete m_root;
    m_root = new Node;
    m_sourceRow.clear();
    m_trackNode.clear();

    if (m_source == nullptr)
        return;

    // One name→node table per level: the top groups and, in ByArtist,
    // the albums under each artist.
    QHash<QString, Node*> topGroups;
    QHash<Node*, QHash<QString, Node*>> albumGroups;

    for (int row = 0; row < m_source->rowCount(); ++row) {
        const quint64 id = m_source->idAt(row);
        if (id == 0)
            continue;
        if (!m_sourceRow.contains(id))
            m_sourceRow.insert(id, row);
        const MediaItem item = MediaItemStore::resolve(id);
        if (!trackMatches(item))
            continue;

        Node *leaf = new Node;
        leaf->itemId = id;
        if (!m_trackNode.contains(id))
            m_trackNode.insert(id, leaf);

        switch (m_grouping) {
        case Grouping::ByArtist: {
            Node *artist =
                ensureGroup(m_root, topGroups, groupDisplayName(item.artist));
            appendLeaf(ensureGroup(artist, albumGroups[artist],
                                   groupDisplayName(item.album)),
                       leaf);
            break;
        }
        case Grouping::ByAlbum:
            appendLeaf(ensureGroup(m_root, topGroups,
                                   groupDisplayName(item.album)),
                       leaf);
            break;
        case Grouping::ByArtistAlbum:
            appendLeaf(ensureGroup(m_root, topGroups, tupleName(item)), leaf);
            break;
        }
    }

    // The groups by their name; the tracks by the natural album order.
    sortGroups(m_root);
    for (Node *top : m_root->children) {
        if (m_grouping == Grouping::ByArtist) {
            sortGroups(top);
            for (Node *album : top->children)
                sortTracks(album);
        } else {
            sortTracks(top);
        }
    }

    int order = 0;
    assignRowsAndCounts(m_root, order);
}

void MediaGroupModel::appendLeaf(Node *parent, Node *leaf)
{
    leaf->parent = parent;
    leaf->rowInParent = static_cast<int>(parent->children.size());
    parent->children.append(leaf);
}

// Creates the child of `parent` named `name` when `map` does not hold it
// yet.
MediaGroupModel::Node* MediaGroupModel::ensureGroup(Node *parent,
                                                    QHash<QString, Node*> &map,
                                                    const QString &name)
{
    Node *node = map.value(name);
    if (node == nullptr) {
        node = new Node;
        node->name = name;
        node->parent = parent;
        node->rowInParent = static_cast<int>(parent->children.size());
        parent->children.append(node);
        map.insert(name, node);
    }
    return node;
}

void MediaGroupModel::sortGroups(Node *parent)
{
    // The names order the groups (case-insensitive); "(Unknown)" always
    // comes last — an untagged catch-all, not a name that competes in the
    // sort.
    const QString unknown = groupDisplayName(QString());
    // An unknown part always comes last: the fully unknown group and, in
    // the tuple grouping, the unknown-artist tuples ("(Unknown) — Al").
    const QString unknownLead = unknown + QStringLiteral(" — ");
    const auto isUnknown = [&unknown, &unknownLead](const Node *node) {
        return node->name == unknown || node->name.startsWith(unknownLead);
    };
    std::stable_sort(parent->children.begin(), parent->children.end(),
                     [&isUnknown](const Node *a, const Node *b) {
                         const bool aUnknown = isUnknown(a);
                         const bool bUnknown = isUnknown(b);
                         if (aUnknown != bUnknown)
                             return !aUnknown;
                         return a->name.compare(b->name, Qt::CaseInsensitive) < 0;
                     });
    for (int i = 0; i < parent->children.size(); ++i)
        parent->children[i]->rowInParent = i;
}

int MediaGroupModel::compareTracks(const MediaItem &a, const MediaItem &b)
{
    if (a.discNumber != b.discNumber)
        return a.discNumber < b.discNumber ? -1 : 1;
    if (a.trackNumber != b.trackNumber)
        return a.trackNumber < b.trackNumber ? -1 : 1;
    return MediaListModel::columnDisplay(a, MediaListModel::Title)
        .compare(MediaListModel::columnDisplay(b, MediaListModel::Title),
                 Qt::CaseInsensitive);
}

void MediaGroupModel::sortTracks(Node *parent)
{
    std::stable_sort(parent->children.begin(), parent->children.end(),
                     [](Node *a, Node *b) {
                         return compareTracks(MediaItemStore::resolve(a->itemId),
                                              MediaItemStore::resolve(b->itemId)) < 0;
                     });
    for (int i = 0; i < parent->children.size(); ++i)
        parent->children[i]->rowInParent = i;
}

int MediaGroupModel::assignRowsAndCounts(Node *base, int &order)
{
    int count = 0;
    for (int i = 0; i < base->children.size(); ++i) {
        Node *node = base->children[i];
        node->rowInParent = i;
        node->displayOrder = order++;
        if (node->itemId == 0) {
            const int childCount = assignRowsAndCounts(node, order);
            node->count = childCount;
            count += childCount;
        } else {
            count += 1;
        }
    }
    return count;
}

QList<quint64> MediaGroupModel::leafIds(const Node *node)
{
    QList<quint64> ids;
    if (node == nullptr)
        return ids;
    if (node->itemId != 0)
        ids.append(node->itemId);
    else
        for (const Node *child : node->children)
            ids.append(leafIds(child));
    return ids;
}

const MediaGroupModel::Node* MediaGroupModel::nodeOf(const QModelIndex &index) const
{
    if (!index.isValid())
        return nullptr;
    const Node *base = static_cast<const Node*>(index.internalPointer());
    if (base == nullptr || index.row() < 0 || index.row() >= base->children.size())
        return nullptr;
    return base->children.at(index.row());
}

QString MediaGroupModel::rowText(const QModelIndex &index) const
{
    const Node *node = nodeOf(index);
    if (node == nullptr)
        return {};
    if (node->itemId == 0)
        return QStringLiteral("%1 (%2)").arg(node->name).arg(node->count);
    return trackText(MediaItemStore::resolve(node->itemId));
}

bool MediaGroupModel::isGroupRow(const QModelIndex &index) const
{
    const Node *node = nodeOf(index);
    return node != nullptr && node->itemId == 0;
}

quint64 MediaGroupModel::idAt(const QModelIndex &index) const
{
    const Node *node = nodeOf(index);
    return node == nullptr ? 0 : node->itemId;
}

QList<quint64> MediaGroupModel::idsUnder(const QModelIndex &index) const
{
    return leafIds(nodeOf(index));
}

int MediaGroupModel::countUnder(const QModelIndex &index) const
{
    const Node *node = nodeOf(index);
    return node == nullptr ? 0 : node->count;
}

QModelIndex MediaGroupModel::index(int row, int column, const QModelIndex &parent) const
{
    // The index's internal pointer is the node's parent (the root for a
    // top-level row) — what parent() needs to walk back up.
    const Node *base = parent.isValid() ? nodeOf(parent) : m_root;
    if (base == nullptr)
        return {};
    if (row < 0 || row >= base->children.size() || column != 0)
        return {};
    return createIndex(row, column, base);
}

QModelIndex MediaGroupModel::parent(const QModelIndex &index) const
{
    // The parent's own index: its row in the grandparent (the parent's
    // coordinates are relative to its parent, not to the child).
    const Node *node = nodeOf(index);
    if (node == nullptr || node->parent == nullptr || node->parent == m_root)
        return {};
    return createIndex(node->parent->rowInParent, 0, node->parent->parent);
}

int MediaGroupModel::rowCount(const QModelIndex &parent) const
{
    const Node *base = parent.isValid() ? nodeOf(parent) : m_root;
    if (base == nullptr)
        return 0;
    return static_cast<int>(base->children.size());
}

int MediaGroupModel::columnCount(const QModelIndex &index) const
{
    Q_UNUSED(index)
    // The single column (the tree's labels) — for the root (the header)
    // and the rows alike.
    return 1;
}

QVariant MediaGroupModel::data(const QModelIndex &index, int role) const
{
    const Node *node = nodeOf(index);
    if (node == nullptr)
        return {};
    switch (role) {
    case Qt::DisplayRole:
    case Qt::ToolTipRole:
        return rowText(index);
    case Qt::UserRole:
        return static_cast<qulonglong>(node->itemId);
    default:
        return {};
    }
}

QVariant MediaGroupModel::headerData(int section, Qt::Orientation orientation,
                                     int role) const
{
    Q_UNUSED(section)
    Q_UNUSED(orientation)
    Q_UNUSED(role)
    // The tree has no header: the single column is identified by the
    // tree's shape.
    return {};
}

Qt::ItemFlags MediaGroupModel::flags(const QModelIndex &index) const
{
    // The root (invalid) index must accept drops so that the view accepts
    // drag moves over the empty area below the last row (and empty trees).
    if (!index.isValid())
        return Qt::ItemIsDropEnabled;
    const Node *node = nodeOf(index);
    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable
                         | Qt::ItemIsDragEnabled;
    // A group row names its content, not a drop position: only track rows
    // (and the empty area) accept drops.
    if (node != nullptr && node->itemId != 0)
        flags |= Qt::ItemIsDropEnabled;
    return flags;
}

bool MediaGroupModel::hasChildren(const QModelIndex &parent) const
{
    const Node *base = parent.isValid() ? nodeOf(parent) : m_root;
    return base != nullptr && !base->children.isEmpty();
}

QModelIndex MediaGroupModel::mapToSource(const QModelIndex &proxyIndex) const
{
    const Node *node = nodeOf(proxyIndex);
    if (node == nullptr || m_source == nullptr)
        return {};
    // A group maps to the source row of its first track (a track to its
    // own row).
    const QList<quint64> ids = leafIds(node);
    if (ids.isEmpty())
        return {};
    const int row = m_sourceRow.value(ids.first(), -1);
    if (row < 0)
        return {};
    return m_source->index(row, 0);
}

QModelIndex MediaGroupModel::mapFromSource(const QModelIndex &sourceIndex) const
{
    if (m_source == nullptr)
        return {};
    const quint64 id = m_source->idAt(sourceIndex.row());
    const Node *node = m_trackNode.value(id, nullptr);
    if (node == nullptr || node->parent == nullptr)
        return {};
    return createIndex(node->rowInParent, 0, node->parent);
}

QStringList MediaGroupModel::mimeTypes() const
{
    return MediaListModel::mediaMimeTypes();
}

QMimeData* MediaGroupModel::mimeData(const QModelIndexList &indexes) const
{
    // One node per dragged row (a selection may name a row's cells
    // several times), in depth-first tree order — the selection is not
    // guaranteed to iterate in row order.
    QSet<const Node*> seen;
    QList<const Node*> nodes;
    for (const QModelIndex &index : indexes) {
        const Node *node = nodeOf(index);
        if (node != nullptr && !seen.contains(node)) {
            seen.insert(node);
            nodes.append(node);
        }
    }
    std::sort(nodes.begin(), nodes.end(),
              [](const Node *a, const Node *b) {
                  return a->displayOrder < b->displayOrder;
              });

    // A track row names its own id, a group row names every track beneath
    // it (in tree order) — a drag of an artist or an album means its
    // whole content. rows stay empty: the tree has no reorder, the target
    // inserts.
    QList<quint64> ids;
    for (const Node *node : nodes)
        for (quint64 id : leafIds(node))
            ids.append(id);

    const QString origin = m_source != nullptr ? m_source->originId()
                                               : QString();
    return MediaListModel::dragMimeData(ids, QList<int>(), origin);
}

bool MediaGroupModel::dropMimeData(const QMimeData *data, Qt::DropAction action,
                                   int row, int column, const QModelIndex &parent)
{
    Q_UNUSED(row)
    Q_UNUSED(parent)
    if (m_source == nullptr)
        return false;
    // The drop position is meaningless in a tree (and the library — the
    // only tree host — ignores it): append at the end of the source.
    return m_source->dropMimeData(data, action, m_source->rowCount(),
                                  column, QModelIndex());
}

Qt::DropActions MediaGroupModel::supportedDropActions() const
{
    return MediaListModel::mediaDropActions();
}
