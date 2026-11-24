#include "playlistswidget.h"

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
#include "tabposition.h"
#include "urlwizard.h"

#include <QAction>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QIcon>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>
#include <QToolButton>

PlaylistsWidget::PlaylistsWidget(QWidget *parent)
    : BaseWidget(parent)
    , m_playingIcon(QStringLiteral(":/icons/play.svg"))
{
    delete layout();

    m_pl = PlaylistManager::getInstance();
    m_pm = PlaybackManager::getInstance();
    m_store = MediaItemStore::getInstance();

    m_tabs = new QTabWidget(this);
    // Tabs close by middle click (see eventFilter), so no close buttons.

    QToolButton* add = new QToolButton(this);
    add->setObjectName("addPlaylistButton");
    add->setIcon(QIcon(QStringLiteral(":/icons/plus.svg")));
    add->setIconSize(QSize(16, 16));
    add->setFixedSize(QSize(24, 24));
    add->setAutoRaise(true);
    add->setToolTip("Create a new playlist");
    m_tabs->setCornerWidget(add, Qt::TopRightCorner);

    QVBoxLayout* vlayout = new QVBoxLayout(this);
    vlayout->setContentsMargins(0, 0, 0, 0);

    // Toolbar (visible by default): "+" adds items to the displayed
    // tab's playlist — chosen files through the same import as a file
    // drop, URLs through the import wizard — "−" removes the displayed
    // tab's selection: the toolbar's clicks mirror the drag & drop paths,
    // with a file chooser (or the wizard) standing in for the drop.
    QToolBar* toolbar = createToolbar(true);
    const PlaylistToolbarButtons buttons =
        createPlaylistToolbar(toolbar, QStringLiteral("the displayed playlist"));
    addToolbarSpacer(toolbar);
    connect(buttons.addFilesAction, &QAction::triggered, this,
            &PlaylistsWidget::onAddFiles);
    connect(buttons.addUrlAction, &QAction::triggered, this,
            &PlaylistsWidget::onAddUrl);
    connect(buttons.removeButton, &QToolButton::clicked, this,
            &PlaylistsWidget::onRemoveSelected);

    // The wizard's sink: confirmed sources land in the tab that is
    // displayed when the user confirms the review.
    m_wizard = new UrlWizard(this);
    m_wizard->setSink([this](const QList<QUrl> &sources,
                             const UrlImportDialog::SourceMeta &) {
        QWidget *tab = m_tabs->currentWidget();
        if (tab == nullptr)
            return;
        m_pl->importSources(static_cast<quint64>(
            tab->property("playlistId").toULongLong()), sources);
    });

    vlayout->addWidget(m_tabs, 1);

    connect(m_pl, &PlaylistManager::playlistsChanged, this, &PlaylistsWidget::onPlaylistsChanged);
    connect(m_pl, &PlaylistManager::playlistChanged, this, &PlaylistsWidget::onPlaylistChanged);
    connect(m_pl, &PlaylistManager::currentIndexChanged, this, &PlaylistsWidget::onCurrentIndexChanged);
    connect(m_pm, &PlaybackManager::playingItemChanged, this, &PlaylistsWidget::onPlayingItemChanged);
    connect(m_pm, &PlaybackManager::resolvingItemChanged, this,
            [this](const MediaItem &) {
                for (MediaListView *view : m_tabViews)
                    updateStatusMarker(view);
            });
    // A failure can land without any other signal (a missing local file
    // stops before anything is in flight): refresh the marks for it.
    connect(m_pm, &PlaybackManager::playbackError, this,
            [this](const QString &) {
                for (MediaListView *view : m_tabViews)
                    updateStatusMarker(view);
            });
    connect(m_pm, &PlaybackManager::stateChanged, this, &PlaylistsWidget::onStateChanged);
    connect(m_store, &MediaItemStore::itemChanged, this, &PlaylistsWidget::onItemChanged);
    // An art change (an import's art lands) repaints the row in place the
    // same way.
    connect(m_store, &MediaItemStore::itemArtworkChanged, this,
            [this](const MediaItem &item) {
        for (auto it = m_tabViews.constBegin(); it != m_tabViews.constEnd(); ++it) {
            if (m_pl->playlist(it.key()).items.contains(item.id))
                it.value()->listModel()->updateItem(item);
        }
    });
    connect(add, &QToolButton::clicked, this, &PlaylistsWidget::onAddPlaylistClicked);
    // The tab bar is only as wide as its tabs; the empty strip to their right
    // belongs to the QTabWidget itself, so watch both.
    m_tabs->installEventFilter(this);
    m_tabs->tabBar()->installEventFilter(this);

    // The tabs drag-reorder the playlist list (the manager persists the
    // new order). The bar reorders the tabs itself — the tab widgets move
    // with their tabs, so the playlistsChanged that movePlaylist emits
    // must not rebuild the tabs: during a live drag a rebuild would
    // destroy the tab being dragged and abort the drag (see
    // onPlaylistsChanged).
    m_tabs->tabBar()->setMovable(true);
    connect(m_tabs->tabBar(), &QTabBar::tabMoved, this,
            [this](int from, int to) {
                Q_UNUSED(from)
                QWidget *w = m_tabs->widget(to);
                if (w == nullptr)
                    return;
                m_rebuildTabsSuppressed = true;
                if (!m_pl->movePlaylist(w->property("playlistId").toULongLong(), to)) {
                    m_rebuildTabsSuppressed = false; // nothing was emitted
                    rebuildTabs(); // the manager rolled back: restore the bar
                }
            });

    // Right-clicking a tab (outside edit mode) offers its playlist menu.
    m_tabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabs->tabBar(), &QTabBar::customContextMenuRequested,
            this, &PlaylistsWidget::onTabContextMenu);

    // The strip takes file drops: media files over a tab, playlist files
    // over the empty strip (see eventFilter for the accept rules).
    m_tabs->setAcceptDrops(true);
    m_tabs->tabBar()->setAcceptDrops(true);

    // Hovering a non-current tab switches to it after a short delay (the
    // timer restarts whenever the hovered tab changes).
    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setSingleShot(true);
    m_hoverTimer->setInterval(300);
    connect(m_hoverTimer, &QTimer::timeout, this, [this] {
        if (m_hoverTab >= 0 && m_hoverTab < m_tabs->count()
            && m_hoverTab != m_tabs->currentIndex())
            m_tabs->setCurrentIndex(m_hoverTab);
    });

    // A tab switch: the follow playback scrolls the now-shown tab to
    // the playing item (the switch does not touch the playback —
    // switching the manager's current playlist would stop it). A widget
    // created mid-playback starts in the right state through the first
    // tab's change.
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) {
        MediaListView *view = qobject_cast<MediaListView*>(m_tabs->currentWidget());
        if (view != nullptr)
            view->scrollToPlayingItem(m_pm->playingItem());
    });

    rebuildTabs();
}

void PlaylistsWidget::rebuildTabs()
{
    // The tabs are rebuilt whenever the playlist set changes; carry each
    // tab's column state over so user adjustments survive, and remember the
    // visible tab so the rebuild (e.g. a new playlist) does not snap the
    // view back to the first tab.
    quint64 currentId = 0;
    if (const MediaListView *current = qobject_cast<const MediaListView*>(m_tabs->currentWidget()))
        currentId = static_cast<quint64>(current->property("playlistId").toULongLong());

    QHash<quint64, QVariantMap> columnStates;
    for (auto it = m_tabViews.constBegin(); it != m_tabViews.constEnd(); ++it)
        columnStates.insert(it.key(), it.value()->columnState());

    while (m_tabs->count() > 0) {
        QWidget *w = m_tabs->widget(0);
        m_tabs->removeTab(0);
        delete w;
    }
    m_tabViews.clear();

    for (const Playlist &pl : m_pl->playlists()) {
        // The playlist is an explicit sequence: the view shows the rows
        // in the given order, no sort (the order is the data).
        MediaListView *view = new MediaListView();
        view->setModel(new MediaListModel(view));
        view->setProperty("playlistId", static_cast<qulonglong>(pl.id));
        // Name the tab's playlist in the drag payload so drops started in
        // its own rows are reorders (the tab never follows the current
        // playlist, so the origin is set once, here).
        view->setOriginId(MediaListModel::playlistOrigin(pl.id));
        // The follow playback is the widget's state: a new tab inherits
        // it (a rebuilt tab would lose a per-view toggle).
        view->setFollowPlayback(m_followPlayback);
        if (columnStates.contains(pl.id))
            view->restoreColumnState(columnStates.value(pl.id));
        view->setItemIds(pl.items);
        updateStatusMarker(view);
        view->enableDragAndDropping();
        connect(view, &QAbstractItemView::doubleClicked, this, &PlaylistsWidget::onItemDoubleClicked);
        connect(view, &MediaListView::itemsDropped, this, &PlaylistsWidget::onItemsDropped);
        connect(view, &MediaListView::filesDropped, this, &PlaylistsWidget::onFilesDropped);
        // Right-clicking a tab's rows (outside edit mode) removes the
        // selection from that tab's playlist. The policy is on the view: a
        // right-click lands on the viewport, but the scroll-area filter
        // routes it to the view's policy handling (the header keeps its own
        // column menu).
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(view, &QWidget::customContextMenuRequested,
                this, &PlaylistsWidget::onViewContextMenu);

        // The per-view row shortcuts: Delete removes the selection from
        // this tab's playlist; Ctrl+C/Ctrl+V copy and paste rows through
        // the internal row clipboard.
        installRowShortcuts(
            view, this,
            [this, plId = pl.id, view] {
                const QList<int> rows = view->selectedRows();
                if (!rows.isEmpty())
                    m_pl->removeRowsInPlaylist(plId, rows);
            },
            [view] { RowClipboard::getInstance()->copy(view->selectedRowIds()); },
            [this, plId = pl.id](const QList<quint64> &ids, int index) {
                m_pl->insertItemsInto(plId, index, ids);
            });

        m_tabViews.insert(pl.id, view); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
        m_tabs->addTab(view, pl.displayName());
    }

    // A rebuild (e.g. a new playlist) must not drop the icon of the
    // playlist that is playing, and a widget created while something is
    // already playing must start with it (the signals only fire for
    // future changes).
    updatePlayingTabIcon();

    // Land back on the tab that was visible; if it was removed, the first
    // tab stays current.
    if (currentId != 0) {
        const MediaListView *view = m_tabViews.value(currentId, nullptr);
        if (view != nullptr)
            m_tabs->setCurrentIndex(m_tabs->indexOf(view));
    }
}

WidgetConfiguration PlaylistsWidget::storeWidget()
{
    WidgetConfiguration config = BaseWidget::storeWidget();
    // The visible tab is stored as an index (the playlists themselves are
    // persisted by the playlist manager); it is the tab, not the manager's
    // current playlist.
    config.insert("current", m_tabs->currentIndex());

    // Only a non-default position is stored, so a stale layout document can't
    // reintroduce the default.
    if (m_tabs->tabPosition() != QTabWidget::North)
        config.insert("tabPosition", TabPosition::name(m_tabs->tabPosition()));

    QVariantMap views;
    for (auto it = m_tabViews.constBegin(); it != m_tabViews.constEnd(); ++it)
        views.insert(QString::number(it.key()), it.value()->columnState());
    config.insert("views", views);
    // The follow playback is stored only when on (the default is off),
    // so a fresh layout document cannot pin it.
    if (m_followPlayback)
        config.insert("followPlayback", true);
    storeToolbar(config);
    return config;
}

void PlaylistsWidget::restoreWidget(WidgetConfiguration &config)
{
    BaseWidget::restoreWidget(config);
    // The position is applied only when present (a missing key keeps
    // the default, as before).
    if (config.contains("tabPosition"))
        m_tabs->setTabPosition(TabPosition::fromName(config.value("tabPosition").toString()));

    const QVariantMap views = config.value("views").toMap();
    for (auto it = views.constBegin(); it != views.constEnd(); ++it) {
        const quint64 playlistId = it.key().toULongLong();
        MediaListView *view = m_tabViews.value(playlistId, nullptr);
        if (view != nullptr)
            view->restoreColumnState(it.value().toMap());
    }

    // Land back on the tab that was visible when the layout was saved (the
    // playlists are loaded before the layout, so the tab count matches);
    // an out-of-range index (a playlist was removed in the meantime) keeps
    // the first tab.
    const int current = config.value("current", 0).toInt();
    if (current >= 0 && current < m_tabs->count())
        m_tabs->setCurrentIndex(current);

    // The follow playback (a missing key keeps the default, off): the
    // widget's state, applied to every tab view; the now-shown tab
    // scrolls to the playing item when it says to.
    m_followPlayback = config.value("followPlayback").toBool();
    for (MediaListView *view : m_tabViews)
        view->setFollowPlayback(m_followPlayback);
    if (MediaListView *shown = qobject_cast<MediaListView*>(m_tabs->currentWidget()))
        shown->scrollToPlayingItem(m_pm->playingItem());
    restoreToolbar(config);
}

QMenu* PlaylistsWidget::widgetContextMenu()
{
    // The base's entries (the style action, and the toolbar entries since
    // this widget carries a toolbar), then — below a separator — the tab
    // position switch.
    QMenu* menu = BaseWidget::widgetContextMenu();
    menu->addSeparator();

    TabPosition::positionMenu(menu, m_tabs, [this](QTabWidget::TabPosition p) {
        m_tabs->setTabPosition(p);
    });

    menu->addSeparator();

    // The follow playback: the widget's state, applied to every tab view
    // (a plain checkable action, the toolbar toggle's pattern).
    QAction* follow = menu->addAction("Follow playback");
    follow->setCheckable(true);
    follow->setChecked(m_followPlayback);
    connect(follow, &QAction::toggled, this, [this](bool on) {
        m_followPlayback = on;
        for (MediaListView *view : m_tabViews)
            view->setFollowPlayback(on);
    });

    return menu;
}

void PlaylistsWidget::onPlaylistsChanged()
{
    // The playlistsChanged of a tab reorder (tabMoved → movePlaylist) is
    // consumed without a rebuild: the bar has already reordered the tabs
    // (the tab widgets move with their tabs, so their content is current),
    // and during a live drag a rebuild would destroy the tab being dragged
    // and abort the drag.
    if (m_rebuildTabsSuppressed) {
        m_rebuildTabsSuppressed = false;
        return;
    }
    rebuildTabs();
}

void PlaylistsWidget::onPlaylistChanged(quint64 playlistId)
{
    refreshTab(playlistId);
}

void PlaylistsWidget::refreshTab(quint64 playlistId)
{
    MediaListView *view = m_tabViews.value(playlistId, nullptr);
    if (view == nullptr)
        return;

    const Playlist pl = m_pl->playlist(playlistId);
    if (!pl.isValid())
        return;

    view->setItemIds(pl.items);
    updateStatusMarker(view);
    // The dirty flag rides the item mutations (and a save): keep the tab's
    // * marker in step without a rebuild.
    m_tabs->setTabText(m_tabs->indexOf(view), pl.displayName());
}

void PlaylistsWidget::onItemDoubleClicked(const QModelIndex &index)
{
    MediaListView *view = qobject_cast<MediaListView*>(sender());
    if (view == nullptr)
        return;

    const quint64 plId = static_cast<quint64>(view->property("playlistId").toULongLong());

    // The clicked row is the row: a playlist may hold the same item in
    // several rows, so the item's id cannot name which copy to play.
    m_pm->playFromPlaylist(plId, index.row());
}

void PlaylistsWidget::updateStatusMarker(MediaListView *view)
{
    // The marker belongs to the current playlist's current row, in the
    // state that row's playback is in (a provider resolution in flight,
    // the playing item, or the last failed play). The other tabs show no
    // marker — their rows are not the ones playing — so a duplicated item
    // is marked in exactly one row.
    const quint64 plId = static_cast<quint64>(view->property("playlistId").toULongLong());
    if (plId != m_pl->currentPlaylistId()) {
        view->updateStatusMarker(0, -1);
        return;
    }
    const Playlist pl = m_pl->playlist(plId);
    const int row = pl.currentIndex;
    const quint64 id = row >= 0 && row < static_cast<int>(pl.items.size())
                           ? pl.items.at(row)
                           : 0;
    view->updateStatusMarker(id, row);
}

void PlaylistsWidget::onPlayingItemChanged(const MediaItem &item)
{
    for (MediaListView *view : m_tabViews) {
        updateStatusMarker(view);
        // The follow playback scrolls each tab that shows the new row
        // (a stop is an invalid item: the scroll is a no-op for it).
        view->scrollToPlayingItem(item);
    }
    updatePlayingTabIcon();
}

void PlaylistsWidget::onStateChanged(PlaybackManager::State state)
{
    Q_UNUSED(state)
    // The tab icon follows the state, and the paused state changes the
    // current row's marker glyph.
    updatePlayingTabIcon();
    for (MediaListView *view : m_tabViews)
        updateStatusMarker(view);
}

void PlaylistsWidget::updatePlayingTabIcon()
{
    // The tab of the playlist that is actually playing — the current
    // playlist while the state is Playing — carries the play icon before
    // its title; pause and stop clear it, and the other tabs never show
    // it.
    for (auto it = m_tabViews.constBegin(); it != m_tabViews.constEnd(); ++it) {
        m_tabs->setTabIcon(m_tabs->indexOf(it.value()),
                           m_pm->isPlayingPlaylist(it.key())
                               ? m_playingIcon : QIcon());
    }
}

void PlaylistsWidget::onCurrentIndexChanged(int index)
{
    Q_UNUSED(index)
    // Only the current playlist's tab can carry the marker; update it as
    // the current row moves (auto-advance, a manual index change).
    MediaListView *view = m_tabViews.value(m_pl->currentPlaylistId(), nullptr);
    if (view != nullptr)
        updateStatusMarker(view);
}

void PlaylistsWidget::onItemChanged(const MediaItem &item)
{
    // A resync may have re-tagged an item some tab shows: repaint its row
    // in place (the models hold ids, the store was updated).
    for (auto it = m_tabViews.constBegin(); it != m_tabViews.constEnd(); ++it) {
        if (m_pl->playlist(it.key()).items.contains(item.id))
            it.value()->listModel()->updateItem(item);
    }
}

void PlaylistsWidget::onViewContextMenu(const QPoint &pos)
{
    MediaListView *view = qobject_cast<MediaListView*>(sender());
    if (view == nullptr)
        return;

    const quint64 plId = static_cast<quint64>(view->property("playlistId").toULongLong());
    // The menu is modal, so the selection cannot change between popup and
    // trigger: capture the rows and ids (values) rather than the view.
    const QList<int> rows = view->selectedRows();
    const QList<quint64> ids = view->selectedIds();

    QMenu *menu = transientMenu(this);

    QAction *remove = menu->addAction("Remove selection");
    remove->setEnabled(!rows.isEmpty());
    connect(remove, &QAction::triggered, this, [this, plId, rows] {
        m_pl->removeRowsInPlaylist(plId, rows);
    });

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
    // Copy URL), the tab's playlist named by the view's property.
    addRowActions(menu, this, ids, rows, plId);

    menu->popup(view->viewport()->mapToGlobal(pos));
}

void PlaylistsWidget::editMetadata(const QList<quint64> &ids)
{
    openMetadataEditor(window(), ids);
}

void PlaylistsWidget::editSingleMetadata(quint64 id)
{
    openSingleMetadataEditor(window(), id);
}

void PlaylistsWidget::onItemsDropped(const QList<quint64> &ids, const QList<int> &rows,
                                     const QString &origin, int index)
{
    MediaListView *view = qobject_cast<MediaListView*>(sender());
    if (view == nullptr)
        return;

    const quint64 plId = static_cast<quint64>(view->property("playlistId").toULongLong());

    // The origin (in the payload) is what decides: only a drag that
    // started in this tab's own rows is a reorder. The list math is the
    // manager's (applyDrop).
    const bool reorder = origin == MediaListModel::playlistOrigin(plId);
    m_pl->applyDrop(plId, ids, rows, reorder, index);
}

void PlaylistsWidget::onFilesDropped(const QList<QUrl> &urls, int index)
{
    MediaListView *view = qobject_cast<MediaListView*>(sender());
    if (view == nullptr)
        return;

    const quint64 plId = static_cast<quint64>(view->property("playlistId").toULongLong());

    // A drop from the file manager: media files are imported, external
    // playlist files are flattened into their entries, at the drop
    // position (a drop on the empty area is an append).
    const QList<QUrl> sources = flattenDropFiles(urls);
    if (!sources.isEmpty())
        m_pl->importSources(plId, sources, index);
}

QStringList PlaylistsWidget::chooseFiles()
{
    return chooseAudioFiles(this);
}

void PlaylistsWidget::onAddFiles()
{
    // The displayed tab names the playlist; the chosen files are imported
    // as a file drop would be (playlist files flattened), appended at the
    // playlist's end — a drop on the empty area appends the same way.
    QWidget *tab = m_tabs->currentWidget();
    if (tab == nullptr)
        return;
    const quint64 plId =
        static_cast<quint64>(tab->property("playlistId").toULongLong());

    const QList<QUrl> sources = flattenLocalFiles(chooseFiles());
    if (!sources.isEmpty())
        m_pl->importSources(plId, sources);
}

void PlaylistsWidget::onAddUrl()
{
    // The import wizard (non-modal, a single instance raised on a
    // second trigger); its confirmed sources land in the tab that is
    // displayed when the user confirms the review.
    m_wizard->open();
}

void PlaylistsWidget::onRemoveSelected()
{
    // The displayed tab names the playlist and holds the selection; the
    // rows (not their ids) name what is removed, as everywhere a playlist
    // may hold the same item in several rows.
    QWidget *tab = m_tabs->currentWidget();
    if (tab == nullptr)
        return;
    const quint64 plId =
        static_cast<quint64>(tab->property("playlistId").toULongLong());
    MediaListView *view = m_tabViews.value(plId, nullptr);
    if (view == nullptr)
        return;
    const QList<int> rows = view->selectedRows();
    if (!rows.isEmpty())
        m_pl->removeRowsInPlaylist(plId, rows);
}

void PlaylistsWidget::onAddPlaylistClicked()
{
    m_pl->addPlaylist(QString("Playlist %1").arg(m_pl->playlistCount() + 1));
}

void PlaylistsWidget::closeTabAt(int index)
{
    if (index < 0 || index >= m_tabs->count())
        return;

    MediaListView *view = qobject_cast<MediaListView*>(m_tabs->widget(index));
    if (view == nullptr)
        return;

    m_pl->removePlaylist(view->property("playlistId").toULongLong());
}

bool PlaylistsWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonDblClick) {
        QMouseEvent *me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            QTabBar *bar = m_tabs->tabBar();
            const QPoint pos = me->position().toPoint();
            const QPoint barPos = (obj == bar) ? pos : bar->mapFrom(static_cast<QWidget*>(obj), pos);
            if (bar->tabAt(barPos) < 0) {
                onAddPlaylistClicked();
                return true;
            }
        }
        return BaseWidget::eventFilter(obj, event);
    }

    if (event->type() == QEvent::MouseButtonPress && obj == m_tabs->tabBar()) {
        QMouseEvent *me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::MiddleButton) {
            const int index = static_cast<QTabBar*>(obj)->tabAt(me->position().toPoint());
            if (index >= 0) {
                closeTabAt(index);
                return true;
            }
        }
    }

    // File drops on the strip. The events land on the tab bar over a tab
    // and on the QTabWidget itself over the empty strip (the tab views
    // take the drops over the content area).
    if ((event->type() == QEvent::DragEnter || event->type() == QEvent::Drop)
        && (obj == m_tabs || obj == m_tabs->tabBar()))
        return onTabStripDropEvent(obj, event);

    // The hover switch: the timer restarts on every change of the hovered
    // tab and is cancelled when the pointer leaves the bar.
    if (event->type() == QEvent::MouseMove && obj == m_tabs->tabBar()) {
        QMouseEvent *me = static_cast<QMouseEvent*>(event);
        hoverTab(static_cast<QTabBar*>(obj)->tabAt(me->position().toPoint()));
        return BaseWidget::eventFilter(obj, event);
    }
    if (event->type() == QEvent::Leave && obj == m_tabs->tabBar())
        hoverTab(-1);

    return BaseWidget::eventFilter(obj, event);
}

bool PlaylistsWidget::onTabStripDropEvent(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::DragEnter)
        return onTabStripDragEnter(static_cast<QDragEnterEvent*>(event),
                                   obj == m_tabs);

    QDropEvent *drop = static_cast<QDropEvent*>(event);
    QTabBar *bar = m_tabs->tabBar();
    const QPoint pos = drop->position().toPoint();
    const QPoint barPos = (obj == bar) ? pos
                                       : bar->mapFrom(static_cast<QWidget*>(obj),
                                                      pos);
    onTabStripDrop(drop->mimeData(), bar->tabAt(barPos));
    return true;
}

// The strip's drop policy: over a tab, local files and item drags from a
// media view; over the empty strip (obj == m_tabs) only external playlist
// files — a drop there starts a new standalone playlist, so media files
// and item drags are refused.
bool PlaylistsWidget::onTabStripDragEnter(QDragEnterEvent *de, bool overStrip)
{
    const QMimeData *mime = de->mimeData();
    // A drag of items from a media view: it is appended to the tab under
    // the pointer (over the empty strip there is no target playlist).
    if (mime->hasFormat(MediaListModel::mediaMimeType())) {
        if (!overStrip)
            de->acceptProposedAction();
        return true;
    }
    const QList<QUrl> urls = mime->urls();
    bool ok = !urls.isEmpty();
    for (const QUrl &url : urls) {
        if (!url.isLocalFile())
            ok = false;
    }
    if (ok && overStrip)
        ok = std::any_of(urls.cbegin(), urls.cend(),
                         [](const QUrl &url) { return isPlaylistFile(url); });
    if (ok)
        de->acceptProposedAction();
    return true;
}

void PlaylistsWidget::hoverTab(int index)
{
    if (index == m_hoverTab)
        return;
    m_hoverTab = index;
    // A singleShot timer's start is a cancel + new window: the switch only
    // happens after the pointer has stayed on the tab for the delay.
    m_hoverTimer->start();
}

void PlaylistsWidget::onTabStripDrop(const QMimeData *mime, int tab)
{
    if (tab < 0) {
        // Over the empty strip: each external playlist file becomes a new
        // standalone playlist (the drag enter admitted playlist files
        // only).
        for (const QUrl &url : mime->urls()) {
            if (isPlaylistFile(url))
                m_pl->importPlaylistFile(url.toLocalFile());
        }
        return;
    }

    // Over a tab: the drag is appended to that tab's playlist.
    QWidget *w = m_tabs->widget(tab);
    if (w == nullptr)
        return;
    const quint64 plId =
        static_cast<quint64>(w->property("playlistId").toULongLong());

    m_pl->appendDrop(plId, mime);
}

void PlaylistsWidget::onTabContextMenu(const QPoint &pos)
{
    if (LayoutManager::getInstance()->isEditMode())
        return;

    QTabBar *bar = m_tabs->tabBar();
    const int index = bar->tabAt(pos);
    if (index < 0)
        return;

    QWidget *w = m_tabs->widget(index);
    if (w == nullptr)
        return;
    const quint64 plId =
        static_cast<quint64>(w->property("playlistId").toULongLong());
    const Playlist pl = m_pl->playlist(plId);
    if (!pl.isValid())
        return;

    QMenu *menu = transientMenu(this);

    addPlaylistVerbActions(menu, plId,
                           [this](const QString &current) {
                               return askPlaylistName(current);
                           },
                           [this](const QString &suggested) {
                               return chooseExportPath(suggested);
                           });

    menu->popup(bar->mapToGlobal(pos));
}

QString PlaylistsWidget::askPlaylistName(const QString &current)
{
    return askPlaylistRename(window(), current);
}

QString PlaylistsWidget::chooseExportPath(const QString &suggested)
{
    return choosePlaylistExportPath(window(), suggested);
}
