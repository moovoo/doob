#include "librarywidget.h"
#include "layoutmanager.h"
#include "mediagroupview.h"
#include "mediafilterproxy.h"
#include "medialistmodel.h"
#include "medialistview.h"
#include "playlistfile.h"
#include "librarymanager.h"
#include "mediaitemstore.h"
#include "metadatadialog.h"
#include "playlistmanager.h"
#include "rowclipboard.h"
#include "rowmenu.h"
#include "trackmetadata.h"
#include "urlimportdialog.h"
#include "urlwizard.h"
#include "playlisttoolbar.h"

#include <QVBoxLayout>
#include <QAction>
#include <QActionGroup>
#include <QButtonGroup>
#include <QIcon>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPaintEvent>
#include <QStackedWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QFileDialog>

#include <QtMath>

namespace {

// Small indeterminate spinner shown on the toolbar while a background
// library operation (an import, a scan, a sync) is running: eight spokes
// with a fading tail, rotated by a timer. Idle it is zero-width (never
// hidden): the toolbar re-shows its children when it is shown or
// re-laid out, and a hide() would fight that — hide() -> re-layout ->
// re-show -> hide() — a feedback loop that burns the main thread at
// 100% on mapping platforms. A zero-width "visible" widget takes no
// space and paints nothing, with no hide() to fight.
class BusyIndicator : public QWidget
{
public:
    explicit BusyIndicator(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(0, 16);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        m_timer.setInterval(80);
        connect(&m_timer, &QTimer::timeout, this, [this] {
            m_step = (m_step + 1) % 8;
            update();
        });
    }

    void busy(bool busy)
    {
        if (m_busy == busy)
            return;
        m_busy = busy;
        if (busy) {
            setFixedSize(16, 16);
            if (!m_timer.isActive())
                m_timer.start();
        } else {
            m_timer.stop();
            setFixedSize(0, 16);
        }
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const int center = width() / 2;
        const int radius = (width() / 2) - 2;
        QColor base = palette().color(QPalette::WindowText);
        for (int i = 0; i < 8; ++i) {
            base.setAlphaF(static_cast<float>(0.15 + ((0.85 * i) / 7.0)));
            QPen pen(base, 2);
            pen.setCapStyle(Qt::RoundCap);
            painter.setPen(pen);
            const double start = qDegreesToRadians(45.0 * (i + m_step));
            const double end = qDegreesToRadians((45.0 * (i + m_step)) + 24.0);
            painter.drawLine(
                QPointF(center + (radius * qCos(start)),
                        center + (radius * qSin(start))),
                QPointF(center + (radius * qCos(end)),
                        center + (radius * qSin(end))));
        }
    }

private:
    bool m_busy = false;
    QTimer m_timer{this};
    int m_step = 0;
};

// The persisted name of a view mode (the default List stores nothing).
QString viewModeKey(LibraryWidget::ViewMode mode)
{
    switch (mode) {
    case LibraryWidget::ViewMode::ByArtist:
        return QStringLiteral("artists");
    case LibraryWidget::ViewMode::ByAlbum:
        return QStringLiteral("albums");
    case LibraryWidget::ViewMode::ByArtistAlbum:
        return QStringLiteral("artistalbum");
    case LibraryWidget::ViewMode::List:
        break;
    }
    return {};
}

LibraryWidget::ViewMode viewModeFromKey(const QString &key)
{
    if (key == QStringLiteral("artists"))
        return LibraryWidget::ViewMode::ByArtist;
    if (key == QStringLiteral("albums"))
        return LibraryWidget::ViewMode::ByAlbum;
    if (key == QStringLiteral("artistalbum"))
        return LibraryWidget::ViewMode::ByArtistAlbum;
    return LibraryWidget::ViewMode::List;
}

}

LibraryWidget::LibraryWidget(QWidget *parent)
    : BaseWidget(parent)
{
    delete layout();

    m_manager = LibraryManager::getInstance();
    m_pl = PlaylistManager::getInstance();

    // The "Add by URL" wizard (non-modal, a single instance raised on a
    // second trigger): the shared plumbing routes its confirmed sources —
    // with the metadata the wizard already read — to the library import.
    m_wizard = new UrlWizard(this);
    m_wizard->setSink([this](const QList<QUrl> &sources,
                             const UrlImportDialog::SourceMeta &metadata) {
        m_manager->addFiles(sources, metadata);
    });

    // One source model, two presentations: the flat table (List mode,
    // through the filter/sort proxy) and the group tree (the artist/
    // album modes). Both resolve the same ids against the store, so
    // switching modes changes only the arrangement.
    m_listModel = new MediaListModel(this);
    // Name the library in the drag payload: a drop of these rows on a
    // playlist is an insertion, whatever the playlist already holds.
    m_listModel->setOriginId(MediaListModel::libraryOrigin());
    m_listView = new MediaListView(this);
    MediaFilterProxyModel *proxy = new MediaFilterProxyModel(m_listView);
    proxy->setSourceModel(m_listModel);
    m_listView->setModel(proxy);
    // The library doesn't play tracks — the Playing column is playlist-
    // only, so it is unavailable (hidden and out of the header menu),
    // not merely hidden.
    m_listView->setColumnAvailable(MediaListModel::Playing, false);
    // Dragging to a playlist inserts, dropping files from the file
    // manager imports them (a dropped external playlist file is flattened
    // into its entries).
    m_listView->enableDragAndDropping();
    // The rows are sortable: a header click sorts by the column
    // (ascending → descending → off, the proxy's cycleSort); the
    // playlist views keep their explicit sequence and stay unsortable.
    m_listView->setSortable(true);
    connect(m_listView, &MediaListView::filesDropped, this,
            &LibraryWidget::onFilesDropped);
    connect(m_listView, &MediaListView::urlsDropped, this,
            &LibraryWidget::onUrlsDropped);

    m_groupView = new MediaGroupView(this);
    m_groupView->groupModel()->setSourceModel(m_listModel);
    m_groupView->enableDragAndDropping();
    connect(m_groupView, &MediaGroupView::filesDropped, this,
            &LibraryWidget::onFilesDropped);
    connect(m_groupView, &MediaGroupView::urlsDropped, this,
            &LibraryWidget::onUrlsDropped);

    // Delete removes the selection (the toolbar's "-" button's action),
    // in both presentations — the stacked widget keeps focus on the active
    // page's view, and the shortcuts fire only while that view has focus.
    installRowShortcuts(m_listView, this, [this] { onRemoveSelected(); });
    installRowShortcuts(m_groupView, this, [this] { onRemoveSelected(); });

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName("viewStack");
    m_stack->addWidget(m_listView);
    m_stack->addWidget(m_groupView);

    QVBoxLayout* vlayout = new QVBoxLayout(this);
    vlayout->setContentsMargins(0, 0, 0, 0);

    // Toolbar (visible by default): "+" opens an add menu, "−" removes
    // the selected items.
    QToolBar* toolbar = createToolbar(true);
    const PlaylistToolbarButtons buttons =
        createPlaylistToolbar(toolbar, QStringLiteral("the library"));
    m_addButton = buttons.addButton;
    m_removeButton = buttons.removeButton;
    connect(buttons.addFilesAction, &QAction::triggered, this,
            &LibraryWidget::onAddFiles);
    connect(buttons.addUrlAction, &QAction::triggered, this,
            &LibraryWidget::onAddUrl);
    connect(m_removeButton, &QToolButton::clicked, this,
            &LibraryWidget::onRemoveSelected);
    // The library's add menu carries one extra source: a whole folder.
    QAction* addFolder = new QAction("Add folder...", buttons.addMenu);
    addFolder->setObjectName("addFolderAction");
    buttons.addMenu->insertAction(buttons.addUrlAction, addFolder);
    connect(addFolder, &QAction::triggered, this, &LibraryWidget::onAddFolder);

    // The view mode: the flat table (List) or a group tree (Artists:
    // artists → albums → tracks, Albums: albums → tracks, Artist + Album:
    // (artist, album) pairs → tracks) — exclusive toggle buttons; the
    // edit-mode menu offers the same switch.
    m_viewButtons = new QButtonGroup(this);
    m_viewButtons->setExclusive(true);
    const QChar arrow = QChar(0x2192); // →
    struct ViewButton
    {
        LibraryWidget::ViewMode mode;
        const char* objectName;
        const char* text;
        QString tooltip;
    };
    for (const ViewButton &vb :
         QList<ViewButton>{
             { ViewMode::List, "viewListButton",
               "List", "Every track as its own row", },
             { ViewMode::ByArtist, "viewArtistButton",
               "Artists", QStringLiteral("Artists ") + arrow
                                + QStringLiteral(" albums ") + arrow
                                + QStringLiteral(" tracks"), },
             { ViewMode::ByAlbum, "viewAlbumButton",
               "Albums", QStringLiteral("Albums ") + arrow
                                + QStringLiteral(" tracks"), },
             { ViewMode::ByArtistAlbum, "viewArtistAlbumButton",
               "Artist + Album",
               QStringLiteral("Artists and albums as pairs ") + arrow
                                + QStringLiteral(" tracks"), }, } ) {
        QToolButton* button = new QToolButton(toolbar);
        button->setObjectName(vb.objectName);
        button->setText(vb.text);
        button->setToolTip(vb.tooltip);
        button->setCheckable(true);
        connect(button, &QToolButton::clicked, this,
                [this, mode = vb.mode] { setViewMode(mode); });
        m_viewButtons->addButton(button);
        toolbar->addWidget(button);
    }
    syncViewButtons();

    // Shown while a background library operation runs.
    m_spinner = new BusyIndicator(toolbar);
    m_spinner->setObjectName("busyIndicator");
    toolbar->addWidget(m_spinner);

    // Keep the buttons left-aligned.
    addToolbarSpacer(toolbar);

    // Search box (right side): any text filters the active presentation
    // by case-insensitive partial match over the item's data columns.
    m_search = new QLineEdit(toolbar);
    m_search->setObjectName("searchBox");
    m_search->setPlaceholderText("Search...");
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumWidth(160);
    QAction* searchIcon = m_search->addAction(QIcon(":/icons/search.svg"),
                                              QLineEdit::LeadingPosition);
    searchIcon->setObjectName("searchIconAction");
    toolbar->addWidget(m_search);

    vlayout->addWidget(m_stack, 1);

    connect(m_listView, &QAbstractItemView::doubleClicked,
            this, &LibraryWidget::onItemDoubleClicked);
    connect(m_groupView, &QAbstractItemView::doubleClicked,
            this, &LibraryWidget::onItemDoubleClicked);
    m_listView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_listView, &QWidget::customContextMenuRequested,
            this, &LibraryWidget::onViewContextMenu);
    m_groupView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_groupView, &QWidget::customContextMenuRequested,
            this, &LibraryWidget::onViewContextMenu);
    connect(m_search, &QLineEdit::textChanged, this, &LibraryWidget::applySearch);

    connect(m_manager, &LibraryManager::itemAdded, this, [this](const MediaItem&) { refresh(); });
    connect(m_manager, &LibraryManager::itemRemoved, this, [this](const MediaItem&) { refresh(); });
    connect(m_manager, &LibraryManager::itemsCleared, this, &LibraryWidget::refresh);
    // A metadata edit updates the table rows in place (no model reset,
    // selection preserved); the group tree rebuilds itself from the data
    // change (group names and counts are tag-derived).
    connect(m_manager, &LibraryManager::itemChanged, this, &LibraryWidget::onItemChanged);
    // An art change (an import's art lands) repaints the row in place the
    // same way (updateItem is a no-op for ids the model does not show).
    connect(MediaItemStore::getInstance(), &MediaItemStore::itemArtworkChanged,
            this, [this](const MediaItem &item) {
        m_listModel->updateItem(item);
    });
    connect(m_manager, &LibraryManager::busyChanged, this, &LibraryWidget::setOperationBusy);

    // A background operation may already be running (e.g. a second library
    // widget created mid-import): sync from the current manager state.
    setOperationBusy(m_manager->busy());

    refresh();
}

WidgetConfiguration LibraryWidget::storeWidget()
{
    WidgetConfiguration config = BaseWidget::storeWidget();
    config.insert("columns", m_listView->columnState());
    // The view mode and the row sort ride with the layout like the column
    // state: each is stored only when it differs from the default (List,
    // the ingestion order), so a fresh layout document stays lean.
    const ViewMode mode = viewMode();
    if (mode != ViewMode::List)
        config.insert("view", viewModeKey(mode));
    // The sort is a single column + direction (stored only when active).
    MediaFilterProxyModel *proxy =
        qobject_cast<MediaFilterProxyModel*>(m_listView->model());
    if (proxy != nullptr && proxy->sortColumn() >= 0) {
        QVariantMap sort;
        sort.insert("column", MediaListModel::columnKey(proxy->sortColumn()));
        sort.insert("ascending", proxy->sortOrder() == Qt::AscendingOrder);
        config.insert("sort", sort);
    }
    storeToolbar(config);
    return config;
}

void LibraryWidget::restoreWidget(WidgetConfiguration &config)
{
    BaseWidget::restoreWidget(config);
    m_listView->restoreColumnState(config.value("columns").toMap());
    // The view mode (stored only when non-default) and the row sort apply
    // before the search below.
    setViewMode(viewModeFromKey(config.value("view").toString()));
    // The sort: a single {column, ascending} map; the legacy format was a
    // list of such maps (the first key wins).
    const QVariant sortValue = config.value("sort");
    QVariantMap sort;
    if (sortValue.typeId() == QVariant::Map) {
        sort = sortValue.toMap();
    } else {
        const QVariantList legacy = sortValue.toList();
        if (!legacy.isEmpty()) {
            sort = legacy.first().toMap();
        }
    }
    if (!sort.isEmpty()) {
        const int column =
            MediaListModel::columnLogical(sort.value("column").toString());
        if (column >= 0) {
            const Qt::SortOrder order = sort.value("ascending", true).toBool()
                ? Qt::AscendingOrder
                : Qt::DescendingOrder;
            qobject_cast<MediaFilterProxyModel*>(m_listView->model())->setSort(column, order);
        }
    }
    syncViewButtons();
    restoreToolbar(config);
    // The restored presentation may differ from the constructor's: apply
    // the search to it.
    applySearch();
}

void LibraryWidget::refresh()
{
    // The source model takes the library's ids; the proxy re-filters and
    // re-sorts them, the group tree rebuilds itself (both listen to the
    // model's reset).
    m_listModel->setItemIds(m_manager->ids());
}

LibraryWidget::ViewMode LibraryWidget::viewMode() const
{
    if (m_stack->currentIndex() == 0)
        return ViewMode::List;
    switch (m_groupView->groupModel()->grouping()) {
    case MediaGroupModel::Grouping::ByArtist:
        return ViewMode::ByArtist;
    case MediaGroupModel::Grouping::ByAlbum:
        return ViewMode::ByAlbum;
    case MediaGroupModel::Grouping::ByArtistAlbum:
        return ViewMode::ByArtistAlbum;
    }
    return ViewMode::List;
}

void LibraryWidget::setViewMode(ViewMode mode)
{
    if (mode == viewMode()) {
        return;
    }
    if (mode == ViewMode::List) {
        m_stack->setCurrentIndex(0);
    } else {
        MediaGroupModel::Grouping grouping = MediaGroupModel::Grouping::ByArtist;
        switch (mode) {
        case ViewMode::ByAlbum:
            grouping = MediaGroupModel::Grouping::ByAlbum;
            break;
        case ViewMode::ByArtistAlbum:
            grouping = MediaGroupModel::Grouping::ByArtistAlbum;
            break;
        case ViewMode::ByArtist:
        case ViewMode::List:
            break;
        }
        m_groupView->setGrouping(grouping);
        m_stack->setCurrentIndex(1);
    }
    syncViewButtons();
    applySearch();
}

void LibraryWidget::syncViewButtons()
{
    const ViewMode mode = viewMode();
    int index = 0;
    if (mode == ViewMode::ByArtist)
        index = 1;
    else if (mode == ViewMode::ByAlbum)
        index = 2;
    else if (mode == ViewMode::ByArtistAlbum)
        index = 3;
    // buttons() lists them in registration order (List, Artists, Albums,
    // Artist + Album); QButtonGroup::button() would look them up by id,
    // and the ids start at 1.
    const QList<QAbstractButton*> buttons = m_viewButtons->buttons();
    if (index < buttons.size())
        buttons.at(index)->setChecked(true);
}

QMenu* LibraryWidget::widgetContextMenu()
{
    QMenu *menu = BaseWidget::widgetContextMenu();

    // The view mode (the toolbar's toggle buttons are the same switch).
    QMenu *view = menu->addMenu("View");
    QActionGroup *group = new QActionGroup(view);
    group->setExclusive(true);
    struct Entry
    {
        LibraryWidget::ViewMode mode;
        const char *text;
    };
    for (const Entry &e :
         QList<Entry>{
             { ViewMode::List, "List" },
             { ViewMode::ByArtist, "Artists" },
             { ViewMode::ByAlbum, "Albums" },
             { ViewMode::ByArtistAlbum, "Artist + Album" }, } ) {
        QAction *action = view->addAction(e.text);
        action->setCheckable(true);
        action->setChecked(viewMode() == e.mode);
        group->addAction(action);
        connect(action, &QAction::triggered, this,
                [this, mode = e.mode] { setViewMode(mode); });
    }
    return menu;
}

void LibraryWidget::applySearch()
{
    // The search text applies to the active presentation: the filter
    // proxy in List mode, the group model's filter in the tree modes
    // (same substring semantics over the item's data columns).
    const QString text = m_search->text();
    if (viewMode() == ViewMode::List) {
        qobject_cast<MediaFilterProxyModel*>(m_listView->model())
            ->setFilterText(text);
    } else {
        m_groupView->setFilterText(text);
    }
}

void LibraryWidget::setOperationBusy(bool busy)
{
    m_addButton->setEnabled(!busy);
    m_removeButton->setEnabled(!busy);
    static_cast<BusyIndicator*>(m_spinner)->busy(busy);
}

void LibraryWidget::onItemChanged(const MediaItem &item)
{
    // The table repaints the row in place (no model reset, selection
    // preserved); the group tree rebuilds whole from the data change (a
    // rename can only change the group structure through the tags).
    m_listModel->updateItem(item);
}

QStringList LibraryWidget::chooseFiles()
{
    return chooseAudioFiles(this);
}

QString LibraryWidget::chooseDirectory()
{
    return QFileDialog::getExistingDirectory(this, "Add folder");
}

void LibraryWidget::onAddFiles()
{
    // The chosen files are imported as a file drop would be (external
    // playlist files flattened into their entries).
    const QList<QUrl> sources = flattenLocalFiles(chooseFiles());
    if (!sources.isEmpty())
        m_manager->addFiles(sources);
}

void LibraryWidget::onAddFolder()
{
    const QString dir = chooseDirectory();
    if (dir.isEmpty())
        return;
    m_manager->scanDirectory(QUrl::fromLocalFile(dir));
}

void LibraryWidget::onAddUrl()
{
    // The shared wizard plumbing: raises an open wizard, else runs a
    // fresh one (the wizard drives the whole flow — the input stage, the
    // list expansion and metadata fetch, the per-item review — and its
    // confirmed sources, with the metadata it already read, land in the
    // library through the sink set in the constructor).
    m_wizard->open();
}

void LibraryWidget::onFilesDropped(const QList<QUrl> &urls, int index)
{
    Q_UNUSED(index); // the library is a set: position does not apply
    const QList<QUrl> sources = flattenDropFiles(urls);
    if (!sources.isEmpty())
        m_manager->addFiles(sources);
}

void LibraryWidget::onUrlsDropped(const QList<QUrl> &urls, int index)
{
    Q_UNUSED(index); // the library is a set: position does not apply
    // The non-local URLs go through the import wizard (the same flow as
    // "Add by URL..." — the review shows every item the import would
    // create), opened pre-filled with the dropped URLs. An already-open
    // wizard is only raised: its flow belongs to the trigger that started
    // it (the dropped URLs can be pasted into a fresh one) — open()
    // starts with the lines only when it creates the wizard.
    QStringList lines;
    for (const QUrl &url : urls)
        lines << url.toString();
    m_wizard->open(lines);
}

void LibraryWidget::onRemoveSelected()
{
    const QList<quint64> ids =
        viewMode() == ViewMode::List ? m_listView->selectedIds()
                                     : m_groupView->selectedIds();
    for (quint64 id : ids)
        m_manager->removeItem(id);
}

void LibraryWidget::onItemDoubleClicked(const QModelIndex &index)
{
    // The clicked row names its tracks: a track row its own id, a group
    // row (an artist, an album, a tuple) every track beneath it.
    const QList<quint64> ids =
        viewMode() == ViewMode::List ? QList<quint64>{ m_listView->idAt(index) }
                                     : m_groupView->idsUnder(index);
    QList<quint64> added = ids;
    added.removeAll(0);
    if (!added.isEmpty())
        m_pl->addItems(added);
}

void LibraryWidget::onViewContextMenu(const QPoint &pos)
{
    // The menu is modal, so the selection cannot change between popup and
    // trigger: capture the ids (values) rather than the view.
    const QList<quint64> ids =
        viewMode() == ViewMode::List ? m_listView->selectedIds()
                                     : m_groupView->selectedIds();

    QWidget *view = m_stack->currentWidget();
    QMenu *menu = transientMenu(this);

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

    // The shared row verbs (Play now / Play next / Copy URL — no
    // "Add to library", the rows are library items). The library's rows
    // are not playlist rows: playlistId 0, no row numbers.
    addRowActions(menu, this, ids, {}, 0);

    menu->popup(static_cast<QAbstractScrollArea*>(view)
                    ->viewport()
                    ->mapToGlobal(pos));
}

void LibraryWidget::editMetadata(const QList<quint64> &ids)
{
    openMetadataEditor(window(), ids);
}

void LibraryWidget::editSingleMetadata(quint64 id)
{
    openSingleMetadataEditor(window(), id);
}
