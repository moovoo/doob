#include "currentplaylistwidget.h"
#include "layoutmanager.h"
#include "medialistmodel.h"
#include "medialistview.h"
#include "mediaitemstore.h"
#include "metadatadialog.h"
#include "playlistfile.h"
#include "playlistmanager.h"
#include "playbackmanager.h"
#include "playlisttoolbar.h"
#include "rowclipboard.h"
#include "rowmenu.h"
#include "urlwizard.h"

#include <QAction>
#include <QActionGroup>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

CurrentPlaylistWidget::CurrentPlaylistWidget(QWidget *parent)
    : BaseWidget(parent)
{
    delete layout();

    m_pl = PlaylistManager::getInstance();
    m_pm = PlaybackManager::getInstance();
    m_store = MediaItemStore::getInstance();
    // The playlist is an explicit sequence: the view shows the rows in
    // the given order, no sort (the order is the data).
    m_listModel = new MediaListModel(this);
    m_view = new MediaListView(this);
    m_view->setModel(m_listModel);
    m_view->enableDragAndDropping();

    QVBoxLayout* vlayout = new QVBoxLayout(this);
    vlayout->setContentsMargins(0, 0, 0, 0);

    // Toolbar (visible by default): the playlists button switches and
    // manages the playlists (its menu is rebuilt on every show — the
    // widget itself only shows the current playlist, so this is the
    // mouse-driven switch), "+" adds items to the current playlist —
    // chosen files through the same import as a file drop, URLs through
    // the import wizard — "−" removes the selection: the toolbar's
    // clicks mirror the drag & drop paths, with a file chooser (or the
    // wizard) standing in for the drop.
    QToolBar* toolbar = createToolbar(true);
    m_playlistsButton = new QToolButton(toolbar);
    m_playlistsButton->setObjectName("playlistsButton");
    m_playlistsButton->setText(QString(QChar(0x2261)));
    m_playlistsButton->setToolTip("Playlists: switch and manage");
    m_playlistsButton->setPopupMode(QToolButton::InstantPopup);
    m_playlistsMenu = new QMenu(m_playlistsButton);
    m_playlistsButton->setMenu(m_playlistsMenu);
    toolbar->addWidget(m_playlistsButton);
    // The entries' radio group (the menu's, the rebuild's actions are
    // its — a rebuilt action leaves the group when the menu clears it).
    m_playlistsGroup = new QActionGroup(m_playlistsMenu);
    m_playlistsGroup->setExclusive(true);
    connect(m_playlistsMenu, &QMenu::aboutToShow, this,
            &CurrentPlaylistWidget::onPlaylistsMenuAboutToShow);
    const PlaylistToolbarButtons buttons =
        createPlaylistToolbar(toolbar, QStringLiteral("the current playlist"));
    addToolbarSpacer(toolbar);
    connect(buttons.addFilesAction, &QAction::triggered, this,
            &CurrentPlaylistWidget::onAddFiles);
    connect(buttons.addUrlAction, &QAction::triggered, this,
            &CurrentPlaylistWidget::onAddUrl);
    connect(buttons.removeButton, &QToolButton::clicked, this,
            &CurrentPlaylistWidget::onRemoveSelected);

    // The wizard's sink: the confirmed sources land in the current
    // playlist (the metadata is the wizard's own re-read; the playlist
    // import reads its sources as a drop does).
    m_wizard = new UrlWizard(this);
    m_wizard->setSink([this](const QList<QUrl> &sources,
                             const UrlImportDialog::SourceMeta &) {
        m_pl->importSources(m_pl->currentPlaylistId(), sources);
    });

    vlayout->addWidget(m_view, 1);

    // Right-clicking the rows (outside edit mode) removes the selection —
    // the menu replaces the former Remove/Clear button strip. The policy is
    // on the view: a right-click lands on the viewport, but the scroll-area
    // filter routes it to the view's policy handling (the header keeps its
    // own column menu).
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QWidget::customContextMenuRequested,
            this, &CurrentPlaylistWidget::onViewContextMenu);

    connect(m_view, &MediaListView::itemsDropped, this, &CurrentPlaylistWidget::onItemsDropped);
    connect(m_view, &MediaListView::filesDropped, this, &CurrentPlaylistWidget::onFilesDropped);
    connect(m_view, &QAbstractItemView::doubleClicked, this, &CurrentPlaylistWidget::onItemDoubleClicked);
    connect(m_pl, &PlaylistManager::playlistChanged, this, &CurrentPlaylistWidget::onPlaylistChanged);
    connect(m_pl, &PlaylistManager::currentIndexChanged, this, &CurrentPlaylistWidget::onCurrentIndexChanged);
    connect(m_pl, &PlaylistManager::currentPlaylistChanged, this, &CurrentPlaylistWidget::onPlaylistChanged);
    connect(m_pm, &PlaybackManager::playingItemChanged, this, &CurrentPlaylistWidget::onPlayingItemChanged);
    connect(m_pm, &PlaybackManager::resolvingItemChanged, this,
            &CurrentPlaylistWidget::updateStatusMarker);
    connect(m_pm, &PlaybackManager::stateChanged, this,
            [this](PlaybackManager::State) { updateStatusMarker(); });
    // A failure can land without any other signal (a missing local file
    // stops before anything is in flight): refresh the mark for it.
    connect(m_pm, &PlaybackManager::playbackError, this,
            [this](const QString &) { updateStatusMarker(); });
    connect(m_store, &MediaItemStore::itemChanged, this, &CurrentPlaylistWidget::onItemChanged);
    // An art change (an import's art lands) repaints the row in place the
    // same way.
    connect(m_store, &MediaItemStore::itemArtworkChanged, this,
            [this](const MediaItem &item) {
        if (m_pl->itemIds().contains(item.id))
            m_view->listModel()->updateItem(item);
    });

    // The per-view row shortcuts: Delete removes the selection;
    // Ctrl+C/Ctrl+V copy and paste rows through the internal row clipboard.
    installRowShortcuts(
        m_view, this, [this] { onRemoveSelected(); },
        [this] { RowClipboard::getInstance()->copy(m_view->selectedRowIds()); },
        [this](const QList<quint64> &ids, int index) {
            m_pl->insertItemsInto(m_pl->currentPlaylistId(), index, ids);
        });

    refresh();
}

WidgetConfiguration CurrentPlaylistWidget::storeWidget()
{
    WidgetConfiguration config = BaseWidget::storeWidget();
    config.insert("columns", m_view->columnState());
    // The follow playback is stored only when on (the default is off),
    // so a fresh layout document cannot pin it.
    if (m_view->followPlayback())
        config.insert("followPlayback", true);
    storeToolbar(config);
    return config;
}

void CurrentPlaylistWidget::restoreWidget(WidgetConfiguration &config)
{
    BaseWidget::restoreWidget(config);
    m_view->restoreColumnState(config.value("columns").toMap());
    // A missing key keeps the default (off); the now-shown list scrolls
    // to the playing item when the restored state says to.
    m_view->setFollowPlayback(config.value("followPlayback").toBool());
    m_view->scrollToPlayingItem(m_pm->playingItem());
    restoreToolbar(config);
}

QMenu* CurrentPlaylistWidget::widgetContextMenu()
{
    // The base's entries (the style action, the toolbar entries), then
    // this widget's own: the follow playback toggle (a plain checkable
    // action, the toolbar toggle's pattern).
    QMenu* menu = BaseWidget::widgetContextMenu();
    menu->addSeparator();

    QAction* follow = menu->addAction("Follow playback");
    follow->setCheckable(true);
    follow->setChecked(m_view->followPlayback());
    connect(follow, &QAction::toggled, this, [this](bool on) {
        m_view->setFollowPlayback(on);
    });

    return menu;
}

void CurrentPlaylistWidget::refresh()
{
    m_view->setItemIds(m_pl->itemIds());
    // The widget follows the current playlist: name it in the drag
    // payload so drops started in its own rows are reorders.
    m_view->setOriginId(MediaListModel::playlistOrigin(m_pl->currentPlaylistId()));
    updateStatusMarker();
    selectCurrentRow();
    // The list now shown: the follow playback scrolls to the playing
    // item (a widget created mid-playback starts in the right state —
    // the signals only fire for future changes).
    m_view->scrollToPlayingItem(m_pm->playingItem());
}

void CurrentPlaylistWidget::updateStatusMarker()
{
    // The marker belongs to the playlist's current row, in the state
    // that row's playback is in — a provider resolution in flight
    // ("downloading"), the playing item (playing/paused), or the last
    // failed play ("cannot play"). A playlist may hold the same item in
    // several rows, so the id alone cannot name the row.
    const int i = m_pl->currentIndex();
    m_view->updateStatusMarker(m_pl->itemIds().value(i, 0), i);
}

void CurrentPlaylistWidget::onPlayingItemChanged(const MediaItem &item)
{
    updateStatusMarker();
    // The follow playback scrolls to the new row (a stop is an invalid
    // item: the scroll is a no-op for it).
    m_view->scrollToPlayingItem(item);
}

void CurrentPlaylistWidget::onItemChanged(const MediaItem &item)
{
    // A resync may have re-tagged an item this playlist shows: repaint its
    // row in place (the model holds ids, the store was updated).
    if (m_pl->itemIds().contains(item.id))
        m_view->listModel()->updateItem(item);
}

void CurrentPlaylistWidget::onPlaylistChanged(quint64 playlistId)
{
    if (playlistId != m_pl->currentPlaylistId())
        return;
    refresh();
}

void CurrentPlaylistWidget::onCurrentIndexChanged(int index)
{
    Q_UNUSED(index)
    selectCurrentRow();
    updateStatusMarker();
}

void CurrentPlaylistWidget::selectCurrentRow()
{
    const int i = m_pl->currentIndex();
    if (i < 0 || i >= m_view->rowCount()) {
        m_view->clearSelection();
        return;
    }
    m_view->selectRow(i);
}

void CurrentPlaylistWidget::onItemsDropped(const QList<quint64> &ids, const QList<int> &rows,
                                    const QString &origin, int index)
{
    // The origin (in the payload) is what decides: only a drag that
    // started in this playlist's own rows is a reorder. The list math is
    // the manager's (applyDrop).
    const bool reorder =
        origin == MediaListModel::playlistOrigin(m_pl->currentPlaylistId());
    m_pl->applyDrop(m_pl->currentPlaylistId(), ids, rows, reorder, index);
}

void CurrentPlaylistWidget::onFilesDropped(const QList<QUrl> &urls, int index)
{
    // A drop from the file manager: media files are imported, external
    // playlist files are flattened into their entries. The block lands at
    // the drop position (a drop on the empty area is an append).
    const QList<QUrl> sources = flattenDropFiles(urls);
    if (!sources.isEmpty())
        m_pl->importSources(m_pl->currentPlaylistId(), sources, index);
}

QStringList CurrentPlaylistWidget::chooseFiles()
{
    return chooseAudioFiles(this);
}

QString CurrentPlaylistWidget::askPlaylistName()
{
    bool ok = false;
    const QString name = QInputDialog::getText(window(), "New playlist",
                                               "Playlist name:",
                                               QLineEdit::Normal, QString(),
                                               &ok);
    return ok ? name : QString();
}

QString CurrentPlaylistWidget::askRenameName(const QString &current)
{
    return askPlaylistRename(window(), current);
}

QString CurrentPlaylistWidget::chooseExportPath(const QString &suggested)
{
    return choosePlaylistExportPath(window(), suggested);
}

void CurrentPlaylistWidget::onAddFiles()
{
    // The chosen files are imported as a file drop would be (external
    // playlist files flattened into their entries), appended at the
    // playlist's end — a drop on the empty area appends the same way.
    const QList<QUrl> sources = flattenLocalFiles(chooseFiles());
    if (!sources.isEmpty())
        m_pl->importSources(m_pl->currentPlaylistId(), sources);
}

void CurrentPlaylistWidget::onAddUrl()
{
    // The import wizard (non-modal, a single instance raised on a
    // second trigger): its confirmed sources land in the current
    // playlist, the same flow a drop of non-local URLs opens.
    m_wizard->open();
}

void CurrentPlaylistWidget::onRemoveSelected()
{
    // The rows (not their ids) name what is removed: a playlist may hold
    // the same item in several rows, and removing one copy must not take
    // the others.
    m_pl->removeRowsInPlaylist(m_pl->currentPlaylistId(), m_view->selectedRows());
}

void CurrentPlaylistWidget::onPlaylistsMenuAboutToShow()
{
    // The menu is rebuilt on every show: the playlist set is dynamic
    // (adds, removals, renames and current changes all alter it).
    m_playlistsMenu->clear();

    // One entry per playlist, in list order: the exclusive group makes
    // them radio options, the current playlist checked. A selection
    // switches the current playlist (stopping playback, the switch's
    // invariant — the same as the switch shortcuts). The group is the
    // menu's own (created once in the constructor): the rebuilt actions
    // leave it on their deletion, so no group of a previous show lingers.
    QActionGroup *group = m_playlistsGroup;
    for (const Playlist &pl : m_pl->playlists()) {
        QAction *action = m_playlistsMenu->addAction(pl.displayName());
        action->setCheckable(true);
        action->setChecked(pl.id == m_pl->currentPlaylistId());
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, id = pl.id] {
            m_pl->setCurrentPlaylist(id);
        });
    }

    m_playlistsMenu->addSeparator();

    // The management verbs of the current playlist: a new playlist, the
    // shared set (rename / export / save) and the delete.
    QAction *add = m_playlistsMenu->addAction("New playlist");
    connect(add, &QAction::triggered, this, [this] {
        const QString name = askPlaylistName().trimmed();
        if (!name.isEmpty())
            m_pl->addPlaylist(name);
    });
    const quint64 currentId = m_pl->currentPlaylistId();
    addPlaylistVerbActions(m_playlistsMenu, currentId,
                           [this](const QString &current) {
                               return askRenameName(current);
                           },
                           [this](const QString &suggested) {
                               return chooseExportPath(suggested);
                           });
    QAction *remove = m_playlistsMenu->addAction("Delete playlist");
    connect(remove, &QAction::triggered, this, [this] {
        m_pl->removePlaylist(m_pl->currentPlaylistId());
    });
}

void CurrentPlaylistWidget::onViewContextMenu(const QPoint &pos)
{
    QMenu *menu = transientMenu(this);

    QAction *remove = menu->addAction("Remove selection");
    remove->setEnabled(!m_view->selectedRows().isEmpty());
    connect(remove, &QAction::triggered, this, &CurrentPlaylistWidget::onRemoveSelected);

    // The ids (not the rows) name what is edited: metadata is an item
    // property, and a duplicate item is edited once for all its rows.
    const QList<quint64> ids = m_view->selectedIds();
    // The single-item editor is the full tag view (one item only); the
    // bulk editor is the table over any selection.
    QAction *edit = menu->addAction("Edit metadata...");
    edit->setEnabled(ids.size() == 1);
    connect(edit, &QAction::triggered, this, [this, ids] {
        editSingleMetadata(ids.first());
    });

    QAction *bulkEdit = menu->addAction("Bulk edit metadata...");
    bulkEdit->setEnabled(!ids.isEmpty());
    connect(bulkEdit, &QAction::triggered, this, [this, ids] {
        editMetadata(ids);
    });

    // The shared row verbs (Play now / Play next / Add to library /
    // Copy URL), appended after the menu's own actions.
    addRowActions(menu, this, ids, m_view->selectedRows(),
                  m_pl->currentPlaylistId());

    menu->popup(m_view->viewport()->mapToGlobal(pos));
}

void CurrentPlaylistWidget::editMetadata(const QList<quint64> &ids)
{
    openMetadataEditor(window(), ids);
}

void CurrentPlaylistWidget::editSingleMetadata(quint64 id)
{
    openSingleMetadataEditor(window(), id);
}

void CurrentPlaylistWidget::onItemDoubleClicked(const QModelIndex &index)
{
    // The clicked row is the row: a playlist may hold the same item in
    // several rows, so the item's id cannot name which copy to play.
    // Routing through playFromPlaylist (not setCurrentIndex) also covers
    // the row that is already current: a double-click is a play request,
    // so it (re)starts that row even when it is already the current one.
    m_pm->playFromPlaylist(m_pl->currentPlaylistId(), index.row());
}
