#include "playlistlistwidget.h"

#include "layoutmanager.h"
#include "medialistmodel.h"
#include "playlistfile.h"
#include "playlistlistview.h"
#include "playlistmanager.h"
#include "playlisttoolbar.h"

#include <QAction>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include <QMimeData>
#include <QMenu>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

PlaylistListWidget::PlaylistListWidget(QWidget *parent)
    : BaseWidget(parent)
    , m_playingIcon(QStringLiteral(":/icons/play.svg"))
{
    delete layout();

    m_pl = PlaylistManager::getInstance();
    m_pm = PlaybackManager::getInstance();

    m_list = new PlaylistListView(this);
    m_list->setObjectName("list");
    m_list->setColumnCount(ColumnCount);
    m_list->setHeaderLabels({ QStringLiteral("Playing"),
                              QStringLiteral("Name"),
                              QStringLiteral("Tracks"), });
    // A flat list: no branch column, nothing to expand (and a double-click
    // must not expand a row, it plays the playlist).
    m_list->setRootIsDecorated(false);
    m_list->setItemsExpandable(false);
    m_list->setExpandsOnDoubleClick(false);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);

    QVBoxLayout* vlayout = new QVBoxLayout(this);
    vlayout->setContentsMargins(0, 0, 0, 0);
    vlayout->addWidget(m_list, 1);

    connect(m_pl, &PlaylistManager::playlistsChanged, this, &PlaylistListWidget::onPlaylistsChanged);
    connect(m_pl, &PlaylistManager::playlistChanged, this, &PlaylistListWidget::onPlaylistChanged);
    connect(m_pl, &PlaylistManager::currentPlaylistChanged, this, &PlaylistListWidget::onCurrentPlaylistChanged);
    connect(m_pm, &PlaybackManager::playingItemChanged, this, &PlaylistListWidget::onPlayingItemChanged);
    connect(m_pm, &PlaybackManager::stateChanged, this, &PlaylistListWidget::onStateChanged);
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, &PlaylistListWidget::onItemDoubleClicked);
    connect(m_list, &QTreeWidget::customContextMenuRequested, this, &PlaylistListWidget::onContextMenu);
    // The rows' drops (a row drag, an item drag, a file drop) are handled
    // here, on the viewport: the drop's position names the row the drag
    // landed on, which the model's index-based dropMimeData would only
    // give as an insert position (the lower half of a row is ambiguous).
    m_list->viewport()->installEventFilter(this);

    // Initialize from the current state: signals only fire for future
    // changes, so a widget inserted while something is already playing
    // would otherwise start without the marker.
    rebuild();
}

void PlaylistListWidget::rebuild()
{
    m_list->clear();
    for (const Playlist &pl : m_pl->playlists()) {
        QTreeWidgetItem *item = new QTreeWidgetItem(m_list);
        item->setData(Playing, Qt::UserRole, static_cast<qulonglong>(pl.id));
        item->setText(Name, pl.displayName());
        item->setText(Tracks, QString::number(pl.items.size()));
    }
    updatePlayingMarker();
}

void PlaylistListWidget::onPlaylistsChanged()
{
    // A playlist was added, removed or renamed: rebuild the rows.
    rebuild();
}

void PlaylistListWidget::onPlaylistChanged(quint64 playlistId)
{
    // The playlist's content changed (the track count is what this widget
    // shows): update the row in place so the selection survives.
    QTreeWidgetItem *item = row(playlistId);
    if (item == nullptr)
        return;
    const Playlist pl = m_pl->playlist(playlistId);
    if (!pl.isValid())
        return;
    // The name carries the dirty marker, which rides the same signal.
    item->setText(Name, pl.displayName());
    item->setText(Tracks, QString::number(pl.items.size()));
}

void PlaylistListWidget::moveRowTo(quint64 playlistId, int index)
{
    const int count = m_list->topLevelItemCount();
    if (index < 0 || count == 0)
        return;
    // The drop index is an insert position (0..count); the manager takes a
    // landing row (0..count-1). A drop on the empty area (index == count)
    // means "move to the end".
    if (index >= count)
        index = count - 1;
    m_pl->movePlaylist(playlistId, index);
}

bool PlaylistListWidget::eventFilter(QObject *obj, QEvent *event)
{
    // The rows' drops (the event lands on the viewport, where the drag
    // began and where it is dropped).
    if (obj == m_list->viewport()
        && (event->type() == QEvent::DragEnter || event->type() == QEvent::Drop)) {
        if (event->type() == QEvent::DragEnter) {
            QDragEnterEvent *de = static_cast<QDragEnterEvent*>(event);
            const QModelIndex index = m_list->indexAt(de->position().toPoint());
            if (listDropAccepted(de->mimeData(), index.isValid()))
                de->acceptProposedAction();
            return true;
        }
        QDropEvent *drop = static_cast<QDropEvent*>(event);
        onListDrop(drop->position().toPoint(), drop->mimeData());
        return true;
    }
    return BaseWidget::eventFilter(obj, event);
}

// The list's drop policy: a drag of a row (a reorder) lands anywhere,
// including the empty area below the last row (it appends to the end); a
// drag of items or a drop of files needs the row of the target playlist
// under the pointer.
bool PlaylistListWidget::listDropAccepted(const QMimeData *mime, bool overRow)
{
    if (mime->hasFormat(PlaylistListView::playlistMimeType()))
        return true;
    if (!overRow)
        return false;
    if (mime->hasFormat(MediaListModel::mediaMimeType()))
        return true;
    const QList<QUrl> urls = mime->urls();
    return std::all_of(urls.cbegin(), urls.cend(),
                       [](const QUrl &url) { return url.isLocalFile(); });
}

void PlaylistListWidget::onListDrop(const QPoint &pos, const QMimeData *mime)
{
    // A drop of one of the list's own rows reorders the list.
    if (mime->hasFormat(PlaylistListView::playlistMimeType())) {
        bool ok = false;
        const quint64 id = static_cast<quint64>(
            mime->data(PlaylistListView::playlistMimeType()).toULongLong(&ok));
        if (!ok || id == 0)
            return;
        moveRowTo(id, dropTargetRow(pos));
        return;
    }

    // An item drag or a file drop is appended to the playlist of the row
    // under the pointer (the drag enter admitted these over a row only).
    const QModelIndex index = m_list->indexAt(pos);
    if (!index.isValid())
        return;
    const quint64 plId = playlistIdAtRow(index.row());

    m_pl->appendDrop(plId, mime);
}

int PlaylistListWidget::dropTargetRow(const QPoint &pos) const
{
    const int count = m_list->topLevelItemCount();
    const QModelIndex index = m_list->indexAt(pos);
    if (!index.isValid())
        return count - 1; // the empty area below the last row: the end
    QTreeWidgetItem *item = m_list->topLevelItem(index.row());
    if (item == nullptr)
        return count - 1;
    // The row's upper half lands at the row, the lower half after it (the
    // insert position; moveRowTo clamps it to a landing row). The pointer
    // and the item's rect are both in the viewport's coordinates.
    const QRect rect = m_list->visualItemRect(item);
    return index.row() + (pos.y() >= rect.center().y() ? 1 : 0);
}

quint64 PlaylistListWidget::playlistIdAtRow(int row) const
{
    QTreeWidgetItem *item = m_list->topLevelItem(row);
    return item != nullptr
        ? static_cast<quint64>(item->data(Playing, Qt::UserRole).toULongLong())
        : 0;
}

void PlaylistListWidget::onCurrentPlaylistChanged(quint64 id)
{
    Q_UNUSED(id)
    updatePlayingMarker();
}

void PlaylistListWidget::onPlayingItemChanged(const MediaItem &item)
{
    Q_UNUSED(item)
    updatePlayingMarker();
}

void PlaylistListWidget::onStateChanged(PlaybackManager::State state)
{
    Q_UNUSED(state)
    updatePlayingMarker();
}

void PlaylistListWidget::updatePlayingMarker()
{
    // The row of the playlist that is actually playing — the current
    // playlist while the state is Playing — carries the play icon; pause
    // and stop clear it, and the other rows never show it.
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_list->topLevelItem(i);
        const quint64 plId =
            static_cast<quint64>(item->data(Playing, Qt::UserRole).toULongLong());
        item->setIcon(Playing, m_pm->isPlayingPlaylist(plId)
                                    ? m_playingIcon : QIcon());
    }
}

void PlaylistListWidget::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column)
    if (item == nullptr)
        return;

    const quint64 plId =
        static_cast<quint64>(item->data(Playing, Qt::UserRole).toULongLong());
    // Resume from the playlist's own cursor when it is valid, otherwise
    // from the start (an empty playlist stops playback).
    const Playlist pl = m_pl->playlist(plId);
    const int index = (pl.currentIndex >= 0 && pl.currentIndex < pl.items.size())
        ? pl.currentIndex
        : 0;
    m_pm->playFromPlaylist(plId, index);
}

QString PlaylistListWidget::askPlaylistName()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, "New playlist", "Playlist name:",
                                               QLineEdit::Normal, QString(), &ok);
    return ok ? name : QString();
}

QString PlaylistListWidget::askRenameName(const QString &current)
{
    return askPlaylistRename(this, current);
}

QString PlaylistListWidget::chooseExportPath(const QString &suggested)
{
    return choosePlaylistExportPath(this, suggested);
}

void PlaylistListWidget::onContextMenu(const QPoint &pos)
{
    QMenu *menu = transientMenu(this);

    QAction *add = menu->addAction("New playlist");
    connect(add, &QAction::triggered, this, [this] {
        const QString name = askPlaylistName().trimmed();
        if (!name.isEmpty())
            m_pl->addPlaylist(name);
    });

    // The menu is modal, so the selection cannot change between popup and
    // trigger: capture the id (value) rather than the view.
    const quint64 selectedId = selectedPlaylistId();
    if (selectedId != 0)
        addSelectedActions(menu, selectedId);

    menu->popup(m_list->viewport()->mapToGlobal(pos));
}

void PlaylistListWidget::addSelectedActions(QMenu *menu, quint64 selectedId)
{
    const Playlist selected = m_pl->playlist(selectedId);
    if (selected.isValid()) {
        QAction *rename = menu->addAction("Rename...");
        connect(rename, &QAction::triggered, this, [this, selectedId] {
            const Playlist current = m_pl->playlist(selectedId);
            if (!current.isValid())
                return;
            const QString name = askRenameName(current.name).trimmed();
            if (!name.isEmpty())
                m_pl->renamePlaylist(selectedId, name);
        });

        QAction *exportAction = menu->addAction("Export to file...");
        connect(exportAction, &QAction::triggered, this, [this, selectedId] {
            const Playlist current = m_pl->playlist(selectedId);
            if (!current.isValid())
                return;
            const QString path =
                chooseExportPath(current.name + QStringLiteral(".m3u"));
            if (!path.isEmpty())
                m_pl->exportPlaylist(selectedId, path);
        });

        // A save writes the imported playlist back to the file it
        // came from.
        QAction *save = menu->addAction("Save");
        save->setEnabled(selected.imported && !selected.sourceFile.isEmpty());
        connect(save, &QAction::triggered, this, [this, selectedId] {
            const Playlist current = m_pl->playlist(selectedId);
            if (current.isValid() && current.imported
                && !current.sourceFile.isEmpty())
                m_pl->savePlaylist(selectedId,
                                   QUrl(current.sourceFile).toLocalFile());
        });
    }

    QAction *remove = menu->addAction("Delete playlist");
    connect(remove, &QAction::triggered, this, [this, selectedId] {
        m_pl->removePlaylist(selectedId);
    });
}

QTreeWidgetItem* PlaylistListWidget::row(quint64 playlistId) const
{
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_list->topLevelItem(i);
        if (static_cast<quint64>(item->data(Playing, Qt::UserRole).toULongLong()) == playlistId)
            return item;
    }
    return nullptr;
}

quint64 PlaylistListWidget::selectedPlaylistId() const
{
    const QTreeWidgetItem *item = m_list->currentItem();
    if (item == nullptr)
        return 0;
    return static_cast<quint64>(item->data(Playing, Qt::UserRole).toULongLong());
}
