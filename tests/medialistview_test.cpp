#include <QtTest>
#include <QSignalSpy>

#include <QAction>
#include <QContextMenuEvent>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QMimeData>
#include <QTemporaryDir>
#include <QVariantMap>

#include <array>
#include <memory>

#include "mediafilterproxy.h"
#include "medialistmodel.h"
#include "medialistview.h"
#include "mediaitemstore.h"

namespace {

class MediaListViewTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void setItemsPopulatesRows();
    void playingColumnFirstByDefault();
    void columnReorderMovesHeaderSection();
    void columnStateCapturesOrderVisibilityWidths();
    void restoreColumnStateAppliesState();
    void restoreColumnStateIgnoresGarbageAndKeepsDefaults();
    void setItemsPreservesColumnState();
    void headerContextMenuTogglesColumnVisibility();
    void visibleColumnsReflectsAvailabilityAndHidden();
    void visibilityChangedEmittedOnToggleNotOnRestore();
    void unavailableColumnNotInHeaderMenu();
    void unavailableColumnExcludedFromColumnState();
    void restoreColumnStateIgnoresUnavailableColumn();
    void unavailableColumnCannotBeShown();
    void playingColumnVisibility();
    void setRowStatusReachesTheModel();
    void idAtResolvesThroughTheProxy();
    void listModelResolvesThroughTheProxy();
    void selectRowAndSelectedRows();
    void selectedIdsDeduplicatesRepeatedItems();
    void setOriginIdReachesTheModel();
    void enableDragAndDroppingEnablesBoth();
    void dropEventOnRowEmitsItemsDropped();
    void dropEventThroughProxyMapsToSource();
    void dragMoveOverEmptyModelAccepted();
    void dragMoveBelowLastRowAccepted();
    void fileDropEmitsFilesDropped();
    void urlDropEmitsUrlsDropped();
    void sortableHeaderCyclesAscendingDescendingOff();
    void unsortableHeaderIgnoresClicks();
    void headerClickOnPlayingColumnIsIgnored();

private:
    MediaItem makeItem(quint64 id, const QString &title, qint64 durationMs = 0);
    void selectRows(MediaListView &view, const QList<int> &rows);
    // Registers the items in the standalone store (the model resolves ids
    // against it) and sets them on the view.
    void setItems(MediaListView &view, const QList<MediaItem> &items);
    // Emits the header's sectionClicked (QTest cannot click the header).
    void clickHeader(MediaListView &view, int logical);

    MediaItemStore* m_store = nullptr;
    // The store is write-through: keep its database private to this test.
    std::unique_ptr<QTemporaryDir> m_dir;
};

void MediaListViewTest::init()
{
    m_store = MediaItemStore::getInstance();
    m_dir = std::make_unique<QTemporaryDir>();
    m_store->setStoragePath(m_dir->path() + "/doob.db");
    m_store->clear();
}

void MediaListViewTest::cleanup()
{
    m_store->clear();
}

MediaItem MediaListViewTest::makeItem(quint64 id, const QString &title, qint64 durationMs)
{
    MediaItem item;
    item.id = id;
    item.title = title;
    item.durationMs = durationMs;
    item.source = QUrl::fromLocalFile(QStringLiteral("/viewtest/%1.mp3").arg(id));
    return item;
}

void MediaListViewTest::selectRows(MediaListView &view, const QList<int> &rows)
{
    // A whole row (the view's SelectRows behavior): selectedRows() only
    // counts fully selected rows.
    for (int row : rows)
        view.selectionModel()->select(
            QItemSelection(view.listModel()->index(row, 0),
                           view.listModel()->index(row, view.listModel()->columnCount() - 1)),
            QItemSelectionModel::Select | QItemSelectionModel::Rows);
}

void MediaListViewTest::setItems(MediaListView &view, const QList<MediaItem> &items)
{
    for (MediaItem item : items) {
        m_store->remove(item.id);
        QVERIFY(m_store->insert(item));
    }
    QList<quint64> ids;
    for (const MediaItem &item : items)
        ids.append(item.id);
    view.setItemIds(ids);
}

void MediaListViewTest::clickHeader(MediaListView &view, int logical)
{
    QHeaderView *header = view.header();
    const int signalIndex = header->metaObject()->indexOfSignal("sectionClicked(int)");
    QVERIFY(signalIndex >= 0);
    std::array<void*, 2> args = { nullptr, &logical };
    QMetaObject::activate(header, signalIndex, args.data());
}

void MediaListViewTest::setItemsPopulatesRows()
{
    MediaListModel model;
    MediaListView view;
    view.setModel(&model);
    setItems(view, {
        makeItem(1, "Alpha"),
        makeItem(2, "Beta", 90000),
        makeItem(3, "Gamma"),
    });

    QCOMPARE(view.listModel()->rowCount(), 3);
    QCOMPARE(view.listModel()->columnCount(), MediaListModel::ColumnCount);
    QCOMPARE(view.rowCount(), 3);
    QCOMPARE(view.listModel()->data(view.listModel()->index(1, MediaListModel::Title),
                                    Qt::DisplayRole).toString(),
             QString("Beta"));
    QCOMPARE(view.listModel()->data(view.listModel()->index(1, MediaListModel::Duration),
                                    Qt::DisplayRole).toString(),
             QString("1:30"));
    QCOMPARE(view.listModel()->data(view.listModel()->index(2, 0), Qt::UserRole).toULongLong(),
             3ULL);
}

void MediaListViewTest::playingColumnFirstByDefault()
{
    MediaListModel model;
    MediaListView view;
    view.setModel(&model);
    QHeaderView *header = view.header();

    // Default visual order: the playing marker first, then the columns in
    // logical order (a hidden column keeps its visual position).
    QList<int> order;
    for (int visual = 0; visual < header->count(); ++visual)
        order.append(header->logicalIndex(visual));
    QCOMPARE(order,
             QList<int>({ MediaListModel::Playing, MediaListModel::Artwork,
                          MediaListModel::Title,
                          MediaListModel::Artist, MediaListModel::Album,
                          MediaListModel::AlbumArtist, MediaListModel::Composer,
                          MediaListModel::Genre, MediaListModel::Track,
                          MediaListModel::Disc, MediaListModel::Year,
                          MediaListModel::Duration, MediaListModel::Comment,
                          MediaListModel::Bitrate, MediaListModel::SampleRate,
                          MediaListModel::Channels }));
    // The artwork column is hidden by default (the header menu offers it).
    QVERIFY(header->isSectionHidden(MediaListModel::Artwork));
    QVERIFY(!header->isSectionHidden(MediaListModel::Title));
}

void MediaListViewTest::columnReorderMovesHeaderSection()
{
    MediaListModel model;
    MediaListView view;
    view.setModel(&model);
    setItems(view, { makeItem(1, "A") });

    QHeaderView *header = view.header();
    QVERIFY(header->sectionsMovable());
    // QHeaderView pins the section with logical index 0 (the Artwork
    // column — the visual order is irrelevant) by default; the view
    // un-pins it so every column can be reordered.
    QVERIFY(header->isFirstSectionMovable());

    // Default order: Playing, Artwork, Title, Artist.
    QCOMPARE(header->visualIndex(MediaListModel::Playing), 0);
    QCOMPARE(header->visualIndex(MediaListModel::Title), 2);

    // moveSection() takes visual indices on both sides.
    header->moveSection(header->visualIndex(MediaListModel::Artist), 0);

    QCOMPARE(header->visualIndex(MediaListModel::Artist), 0);
    QCOMPARE(header->visualIndex(MediaListModel::Playing), 1);
    QCOMPARE(header->visualIndex(MediaListModel::Artwork), 2);
    QCOMPARE(header->visualIndex(MediaListModel::Title), 3);
}

void MediaListViewTest::columnStateCapturesOrderVisibilityWidths()
{
    MediaListModel model;
    MediaListView view;
    view.setModel(&model);
    QHeaderView *header = view.header();

    header->moveSection(header->visualIndex(MediaListModel::Title), 0);
    header->setSectionHidden(MediaListModel::Artist, true);
    header->resizeSection(MediaListModel::Title, 123);

    const QVariantMap state = view.columnState();

    QCOMPARE(state.value("order").toStringList(),
             QStringList({ "title", "playing", "artwork", "artist", "album",
                           "albumartist",
                           "composer", "genre", "track", "disc", "year", "duration",
                           "comment", "bitrate", "samplerate", "channels" }));
    // Artist plus the columns hidden by default, in logical order.
    QCOMPARE(state.value("hidden").toStringList(),
             QStringList({ "artwork", "artist", "albumartist", "composer",
                           "genre", "comment", "bitrate", "samplerate",
                           "channels" }));
    QCOMPARE(state.value("widths").toMap().value("title").toInt(), 123);
}

void MediaListViewTest::restoreColumnStateAppliesState()
{
    QVariantMap state;
    state.insert("order", QVariantList({ "duration", "playing", "title", "artist" }));
    state.insert("hidden", QVariantList({ "artist" }));
    QVariantMap widths;
    widths.insert("title", 111);
    state.insert("widths", widths);

    MediaListModel model;
    MediaListView view;
    view.setModel(&model);
    view.restoreColumnState(state);
    QHeaderView *header = view.header();

    QCOMPARE(header->visualIndex(MediaListModel::Duration), 0);
    QCOMPARE(header->visualIndex(MediaListModel::Playing), 1);
    QCOMPARE(header->visualIndex(MediaListModel::Title), 2);
    QVERIFY(header->isSectionHidden(MediaListModel::Artist));
    QVERIFY(!header->isSectionHidden(MediaListModel::Title));
    // The saved state decides visibility wholesale: Artwork, not listed,
    // is shown (a state saved with it visible keeps it visible).
    QVERIFY(!header->isSectionHidden(MediaListModel::Artwork));
    QCOMPARE(header->sectionSize(MediaListModel::Title), 111);
}

void MediaListViewTest::restoreColumnStateIgnoresGarbageAndKeepsDefaults()
{
    MediaListModel model;
    MediaListView view;
    view.setModel(&model);
    QHeaderView *header = view.header();

    // An empty state leaves everything untouched.
    view.restoreColumnState({});
    QCOMPARE(header->visualIndex(MediaListModel::Playing), 0);

    // Unknown keys are dropped, duplicates deduped, missing columns appended.
    QVariantMap state;
    state.insert("order", QVariantList({ "bogus", "artist", "artist", "title" }));
    view.restoreColumnState(state);

    QCOMPARE(header->visualIndex(MediaListModel::Artist), 0);
    QCOMPARE(header->visualIndex(MediaListModel::Title), 1);
    QCOMPARE(header->visualIndex(MediaListModel::Artwork), 2);
    QCOMPARE(header->visualIndex(MediaListModel::Album), 3);
    QCOMPARE(header->visualIndex(MediaListModel::Playing), MediaListModel::ColumnCount - 1);
    // "hidden" absent: visibility is untouched (the defaults remain).
    QVERIFY(!header->isSectionHidden(MediaListModel::Title));
    QVERIFY(!header->isSectionHidden(MediaListModel::Duration));
    QVERIFY(header->isSectionHidden(MediaListModel::AlbumArtist));
    QVERIFY(header->isSectionHidden(MediaListModel::Artwork));
}

void MediaListViewTest::setItemsPreservesColumnState()
{
    MediaListModel model;
    MediaListView view;
    view.setModel(&model);
    setItems(view, { makeItem(1, "A"), makeItem(2, "B") });
    QHeaderView *header = view.header();

    header->moveSection(header->visualIndex(MediaListModel::Duration), 0);
    header->setSectionHidden(MediaListModel::Playing, true);
    header->resizeSection(MediaListModel::Title, 99);

    setItems(view, { makeItem(1, "A"), makeItem(3, "C") });

    QCOMPARE(header->visualIndex(MediaListModel::Duration), 0);
    QVERIFY(header->isSectionHidden(MediaListModel::Playing));
    QCOMPARE(header->sectionSize(MediaListModel::Title), 99);
}

void MediaListViewTest::headerContextMenuTogglesColumnVisibility()
{
    MediaListModel model;
    MediaListView view;
    view.setModel(&model);
    view.show();
    view.resize(400, 300);
    QCoreApplication::processEvents();

    QHeaderView *header = view.header();

    // A right-click on a header section is delivered to the header's
    // viewport (the scroll-area filter routes it to the header's policy
    // handling), so the event must target the viewport in tests too.
    QContextMenuEvent event(QContextMenuEvent::Mouse,
                            header->viewport()->rect().center(),
                            header->viewport()->rect().center());
    QCoreApplication::sendEvent(header->viewport(), &event);

    QMenu *menu = view.findChild<QMenu*>();
    if (menu == nullptr) {
        QFAIL("header context menu did not appear");
        return;
    }
    QCoreApplication::processEvents();
    QVERIFY(menu->isVisible());

    QStringList labels;
    QAction *artist = nullptr;
    for (QAction *a : menu->actions()) {
        labels << a->text();
        if (a->text() == "Artist")
            artist = a;
    }
    // Listed in visual order; the headerless status column still has a name.
    QCOMPARE(labels, QStringList({ "Status", "Artwork", "Title", "Artist", "Album",
                                   "Album Artist", "Composer", "Genre", "Track",
                                   "Disc", "Year", "Duration", "Comment", "Bitrate",
                                   "Sample Rate", "Channels" }));
    QVERIFY(artist != nullptr);
    QVERIFY(artist->isCheckable());
    QVERIFY(artist->isChecked());

    artist->trigger(); // uncheck -> hide the column
    QCoreApplication::processEvents();
    QVERIFY(header->isSectionHidden(MediaListModel::Artist));
    // A programmatic trigger does not close the menu the way a mouse click
    // does, so close it explicitly; aboutToHide schedules deleteLater.
    menu->hide();
    QCoreApplication::processEvents();
    QVERIFY(view.findChild<QMenu*>() == nullptr);

    // Re-checking brings the column back.
    QContextMenuEvent event2(QContextMenuEvent::Mouse,
                             header->viewport()->rect().center(),
                             header->viewport()->rect().center());
    QCoreApplication::sendEvent(header->viewport(), &event2);
    menu = view.findChild<QMenu*>();
    if (menu == nullptr) {
        QFAIL("header context menu did not reappear");
        return;
    }
    QCoreApplication::processEvents();
    for (QAction *a : menu->actions()) {
        if (a->text() != "Artist")
            continue;
        QVERIFY(!a->isChecked());
        a->trigger();
    }
    QCoreApplication::processEvents();
    QVERIFY(!header->isSectionHidden(MediaListModel::Artist));
    menu->hide();
    QCoreApplication::processEvents();
}

void MediaListViewTest::visibleColumnsReflectsAvailabilityAndHidden()
{
    MediaListModel model;
    MediaListView view;
    view.setModel(&model);
    view.setColumnAvailable(MediaListModel::Playing, false);
    setItems(view, { makeItem(1, "A") });
    QHeaderView *header = view.header();

    // Playing is unavailable; the standard columns are visible by default
    // (the artwork column hidden), the extra ones hidden.
    QCOMPARE(view.visibleColumns(),
             QList<int>({ MediaListModel::Title, MediaListModel::Artist,
                          MediaListModel::Album, MediaListModel::Track,
                          MediaListModel::Disc, MediaListModel::Year,
                          MediaListModel::Duration }));

    header->setSectionHidden(MediaListModel::Artist, true);
    QCOMPARE(view.visibleColumns(),
             QList<int>({ MediaListModel::Title, MediaListModel::Album,
                          MediaListModel::Track,
                          MediaListModel::Disc, MediaListModel::Year,
                          MediaListModel::Duration }));
}

void MediaListViewTest::visibilityChangedEmittedOnToggleNotOnRestore()
{
    MediaListModel model;
    MediaListView view;
    view.setModel(&model);
    setItems(view, { makeItem(1, "A") });
    QSignalSpy spy(&view, &MediaListView::columnsVisibilityChanged);

    // The restore path must stay quiet: setItemIds() keeps the column
    // state internally, which would loop with a filter refresh.
    setItems(view, { makeItem(1, "A"), makeItem(2, "B") });
    QCOMPARE(spy.count(), 0);
    QVariantMap state;
    state.insert("hidden", QVariantList({ "artist" }));
    view.restoreColumnState(state);
    QCOMPARE(spy.count(), 0);
    QVERIFY(view.header()->isSectionHidden(MediaListModel::Artist));

    // The visibility APIs emit on real changes only.
    view.setPlayingColumnVisible(false);
    QCOMPARE(spy.count(), 1);
    view.setPlayingColumnVisible(false); // no change -> no emit
    QCOMPARE(spy.count(), 1);
    view.setPlayingColumnVisible(true);
    QCOMPARE(spy.count(), 2);

    // Making a visible column unavailable hides it: one emit. Re-enabling
    // keeps it hidden: none.
    view.setColumnAvailable(MediaListModel::Title, false);
    QCOMPARE(spy.count(), 3);
    view.setColumnAvailable(MediaListModel::Title, true);
    QCOMPARE(spy.count(), 3);
    QVERIFY(view.header()->isSectionHidden(MediaListModel::Title));
}

void MediaListViewTest::unavailableColumnNotInHeaderMenu()
{
    MediaListModel model;
    MediaListView view;
    view.setModel(&model);
    view.setColumnAvailable(MediaListModel::Playing, false);
    setItems(view, { makeItem(1, "A") });
    view.show();
    view.resize(400, 300);
    QCoreApplication::processEvents();

    QHeaderView *header = view.header();
    QContextMenuEvent event(QContextMenuEvent::Mouse,
                            header->viewport()->rect().center(),
                            header->viewport()->rect().center());
    QCoreApplication::sendEvent(header->viewport(), &event);

    QMenu *menu = view.findChild<QMenu*>();
    if (menu == nullptr) {
        QFAIL("header context menu did not appear");
        return;
    }
    QStringList labels;
    for (QAction *a : menu->actions())
        labels << a->text();
    // Only the available columns, in visual order.
    QCOMPARE(labels, QStringList({ "Artwork", "Title", "Artist", "Album",
                                   "Album Artist",
                                   "Composer", "Genre", "Track", "Disc", "Year",
                                   "Duration", "Comment", "Bitrate", "Sample Rate",
                                   "Channels" }));
    menu->hide();
    QCoreApplication::processEvents();
}

void MediaListViewTest::unavailableColumnExcludedFromColumnState()
{
    MediaListModel model;
    MediaListView view;
    view.setModel(&model);
    view.setColumnAvailable(MediaListModel::Playing, false);
    setItems(view, { makeItem(1, "A") });

    const QVariantMap state = view.columnState();
    QCOMPARE(state.value("order").toStringList(),
             QStringList({ "artwork", "title", "artist", "album", "albumartist",
                           "composer",
                           "genre", "track", "disc", "year", "duration", "comment",
                           "bitrate", "samplerate", "channels" }));
    QCOMPARE(state.value("hidden").toStringList(),
             QStringList({ "artwork", "albumartist", "composer", "genre",
                           "comment", "bitrate", "samplerate", "channels" }));
    const QVariantMap widths = state.value("widths").toMap();
    QCOMPARE(widths.size(), 15);
    QVERIFY(!widths.contains("playing"));
}

void MediaListViewTest::restoreColumnStateIgnoresUnavailableColumn()
{
    MediaListModel model;
    MediaListView view;
    view.setModel(&model);
    view.setColumnAvailable(MediaListModel::Playing, false);
    view.show();
    view.resize(400, 300);
    QCoreApplication::processEvents();

    QHeaderView *header = view.header();
    const int playingWidth = header->sectionSize(MediaListModel::Playing);

    // A state predating the column becoming unavailable: it lists Playing
    // first, leaves it visible and sizes it.
    QVariantMap state;
    state.insert("order", QStringList({ "playing", "duration", "title", "artist" }));
    state.insert("hidden", QStringList());
    state.insert("widths", QVariantMap({ { "playing", 99 }, { "title", 123 } }));
    view.restoreColumnState(state);

    // Playing stays hidden and out of the order; the rest is applied.
    QVERIFY(header->isSectionHidden(MediaListModel::Playing));
    QCOMPARE(header->visualIndex(MediaListModel::Duration), 0);
    QCOMPARE(header->visualIndex(MediaListModel::Title), 1);
    QCOMPARE(header->visualIndex(MediaListModel::Artist), 2);
    QCOMPARE(header->sectionSize(MediaListModel::Title), 123);
    QCOMPARE(header->sectionSize(MediaListModel::Playing), playingWidth);
    // And it does not leak back into a freshly captured state.
    const QVariantMap captured = view.columnState();
    QVERIFY(!captured.value("order").toStringList().contains("playing"));
    QVERIFY(!captured.value("widths").toMap().contains("playing"));
}

void MediaListViewTest::unavailableColumnCannotBeShown()
{
    MediaListModel model;
    MediaListView view;
    view.setModel(&model);
    QVERIFY(view.columnAvailable(MediaListModel::Playing));

    view.setColumnAvailable(MediaListModel::Playing, false);
    QVERIFY(!view.columnAvailable(MediaListModel::Playing));
    QVERIFY(view.header()->isSectionHidden(MediaListModel::Playing));

    // While unavailable, the visibility API has no effect.
    view.setPlayingColumnVisible(true);
    QVERIFY(!view.playingColumnVisible());

    // Re-enabling makes the column offerable again but does not show it.
    view.setColumnAvailable(MediaListModel::Playing, true);
    QVERIFY(!view.playingColumnVisible());
    view.setPlayingColumnVisible(true);
    QVERIFY(view.playingColumnVisible());
}

void MediaListViewTest::playingColumnVisibility()
{
    MediaListModel model;
    MediaListView view;
    view.setModel(&model);
    QVERIFY(view.playingColumnVisible());
    QVERIFY(!view.header()->isSectionHidden(MediaListModel::Playing));

    view.setPlayingColumnVisible(false);
    QVERIFY(!view.playingColumnVisible());
    QVERIFY(view.header()->isSectionHidden(MediaListModel::Playing));

    // A model reset (setItemIds) must not resurrect the hidden column.
    setItems(view, { makeItem(1, "A") });
    QVERIFY(view.header()->isSectionHidden(MediaListModel::Playing));

    view.setPlayingColumnVisible(true);
    QVERIFY(!view.header()->isSectionHidden(MediaListModel::Playing));
}

void MediaListViewTest::setRowStatusReachesTheModel()
{
    MediaListModel model;
    MediaListView view;
    view.setModel(&model);
    setItems(view, { makeItem(1, "A"), makeItem(2, "B") });

    view.setRowStatus(1, MediaListModel::RowStatus::Playing);
    QCOMPARE(model.statusRow(), 1);
    QCOMPARE(model.data(model.index(1, MediaListModel::Playing), Qt::DisplayRole).toString(),
             QString(QChar(0x25B6)));
    QCOMPARE(model.data(model.index(0, MediaListModel::Playing), Qt::DisplayRole).toString(),
             QString());

    view.setRowStatus(-1, MediaListModel::RowStatus::None);
    QCOMPARE(model.statusRow(), -1);
}

void MediaListViewTest::idAtResolvesThroughTheProxy()
{
    // The view's rows are the proxy's rows: idAt maps them to the source
    // (a bare list model is the identity).
    MediaListModel model;
    MediaFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    MediaListView view;
    view.setModel(&proxy);
    setItems(view, {
        makeItem(1, "keep"),
        makeItem(2, "hidden"),
        makeItem(3, "keep"),
    });
    proxy.setFilterText("keep");

    QCOMPARE(view.idAt(proxy.index(0, 0)), 1ULL);
    QCOMPARE(view.idAt(proxy.index(1, 0)), 3ULL);

    // The same call on a bare list model (the playlist views).
    MediaListView bare;
    bare.setModel(&model);
    QCOMPARE(bare.idAt(model.index(2, 0)), 3ULL);
}

void MediaListViewTest::listModelResolvesThroughTheProxy()
{
    MediaListModel model;
    MediaFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    MediaListView view;
    view.setModel(&proxy);

    QCOMPARE(view.listModel(), &model);

    MediaListView bare;
    bare.setModel(&model);
    QCOMPARE(bare.listModel(), &model);
}

void MediaListViewTest::selectRowAndSelectedRows()
{
    MediaListModel model;
    MediaListView view;
    view.setModel(&model);
    setItems(view, { makeItem(1, "A"), makeItem(2, "B"), makeItem(3, "C") });

    view.selectRow(1);
    QCOMPARE(view.selectedRows(), QList<int>({ 1 }));
    QCOMPARE(view.selectedIds(), QList<quint64>({ 2 }));

    // selectRow replaces the selection (Select flag).
    view.selectRow(2);
    QCOMPARE(view.selectedRows(), QList<int>({ 2 }));
}

void MediaListViewTest::selectedIdsDeduplicatesRepeatedItems()
{
    // A playlist may hold the same item in several rows: the ids (metadata
    // edits, removals) name it once, the rows name each copy.
    MediaListModel model;
    MediaListView view;
    view.setModel(&model);
    setItems(view, { makeItem(1, "A"), makeItem(2, "B") });
    view.setItemIds({ 1, 2, 1 });

    selectRows(view, { 0, 1, 2 });
    QCOMPARE(view.selectedRows(), QList<int>({ 0, 1, 2 }));
    QCOMPARE(view.selectedIds(), QList<quint64>({ 1, 2 }));
}

void MediaListViewTest::setOriginIdReachesTheModel()
{
    MediaListModel model;
    MediaListView view;
    view.setModel(&model);

    view.setOriginId(MediaListModel::playlistOrigin(7));
    QCOMPARE(model.originId(), MediaListModel::playlistOrigin(7));
}

void MediaListViewTest::enableDragAndDroppingEnablesBoth()
{
    MediaListModel model;
    MediaListView view;
    view.setModel(&model);
    view.enableDragAndDropping();

    QVERIFY(view.dragEnabled());
    QVERIFY(view.acceptDrops());
    QCOMPARE(view.dragDropMode(), QAbstractItemView::DragDrop);
    // Drops on a row arrive as insert rows (not "overwrite the row").
    QVERIFY(!view.dragDropOverwriteMode());
}

void MediaListViewTest::dropEventOnRowEmitsItemsDropped()
{
    MediaListModel model;
    MediaListView view;
    view.setModel(&model);
    view.enableDropping();
    setItems(view, { makeItem(1, "A"), makeItem(2, "B"), makeItem(3, "C") });
    view.show();
    view.resize(400, 300);
    QCoreApplication::processEvents();

    QSignalSpy spy(&view, &MediaListView::itemsDropped);

    // The drag events live in the viewport's coordinate system, which is
    // what visualRect() returns (a visible column: a hidden section's rect
    // is empty). The view drops in insert mode (overwrite off): a drop on
    // a row reports row -1 with the row as parent, and the model maps that
    // to the row's index — insert at that row.
    const QRect rect =
        view.visualRect(view.listModel()->index(1, MediaListModel::Title));
    const QPointF pos = rect.topLeft() + QPointF(5, 5);
    QMimeData md;
    md.setData(MediaListModel::mediaMimeType(), QJsonDocument(QJsonArray({ 7, 8 })).toJson());
    // Drag events must be sent to the viewport, and a drop is only
    // dispatched to the widget that accepted a prior drag enter.
    QDragEnterEvent enter(pos.toPoint(), Qt::CopyAction, &md, Qt::NoButton,
                          Qt::NoModifier);
    QVERIFY(QCoreApplication::sendEvent(view.viewport(), &enter));
    QDropEvent drop(pos.toPoint(), Qt::CopyAction, &md, Qt::NoButton,
                    Qt::NoModifier);
    QVERIFY(QCoreApplication::sendEvent(view.viewport(), &drop));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<QList<quint64>>(), QList<quint64>({ 7, 8 }));
    QCOMPARE(spy.at(0).at(3).toInt(), 1);
}

void MediaListViewTest::dropEventThroughProxyMapsToSource()
{
    // The library's view sits on the filter proxy: a drop on a visible row
    // lands at the source row it maps to (the hidden rows shift the index).
    MediaListModel model;
    MediaFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    MediaListView view;
    view.setModel(&proxy);
    view.enableDropping();
    setItems(view, {
        makeItem(1, "keep one"),
        makeItem(2, "hidden"),
        makeItem(3, "keep two"),
        makeItem(4, "keep three"),
    });
    proxy.setFilterText("keep");
    view.show();
    view.resize(400, 300);
    QCoreApplication::processEvents();

    QSignalSpy spy(&view, &MediaListView::itemsDropped);

    // Proxy row 1 is source row 2 (the hidden row is skipped). A drop on
    // the row inserts at it (a visible column: a hidden section's rect is
    // empty).
    const QRect rect = view.visualRect(proxy.index(1, MediaListModel::Title));
    const QPointF pos = rect.topLeft() + QPointF(5, 5);
    QMimeData md;
    md.setData(MediaListModel::mediaMimeType(), QJsonDocument(QJsonArray({ 9 })).toJson());
    QDragEnterEvent enter(pos.toPoint(), Qt::CopyAction, &md, Qt::NoButton,
                          Qt::NoModifier);
    QVERIFY(QCoreApplication::sendEvent(view.viewport(), &enter));
    QDropEvent drop(pos.toPoint(), Qt::CopyAction, &md, Qt::NoButton,
                    Qt::NoModifier);
    QVERIFY(QCoreApplication::sendEvent(view.viewport(), &drop));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<QList<quint64>>(), QList<quint64>({ 9 }));
    QCOMPARE(spy.at(0).at(3).toInt(), 2);
}

void MediaListViewTest::dragMoveOverEmptyModelAccepted()
{
    MediaListModel model;
    MediaListView view;
    view.setModel(&model);
    view.enableDropping();
    view.show();
    view.resize(400, 300);
    QCoreApplication::processEvents();

    QMimeData md;
    md.setData(MediaListModel::mediaMimeType(), QJsonDocument(QJsonArray({ 7 })).toJson());

    // Drag events must be sent to the viewport, and a move is only
    // dispatched to the widget that accepted a prior drag enter.
    const QPointF pos(50, 150);
    QDragEnterEvent enter(pos.toPoint(), Qt::CopyAction, &md, Qt::NoButton,
                          Qt::NoModifier);
    QVERIFY(QCoreApplication::sendEvent(view.viewport(), &enter));
    QVERIFY(enter.isAccepted());

    QDragMoveEvent move(pos.toPoint(), Qt::CopyAction, &md, Qt::NoButton,
                        Qt::NoModifier);
    QVERIFY(QCoreApplication::sendEvent(view.viewport(), &move));
    QVERIFY(move.isAccepted());
}

void MediaListViewTest::dragMoveBelowLastRowAccepted()
{
    MediaListModel model;
    MediaListView view;
    view.setModel(&model);
    view.enableDropping();
    setItems(view, { makeItem(1, "A"), makeItem(2, "B") });
    view.show();
    view.resize(400, 300);
    QCoreApplication::processEvents();

    // Below the last row (visualRect() is the viewport's coordinate
    // system, so the point needs no mapping; a visible column: a hidden
    // section's rect is empty).
    const QPointF pos = view
                            .visualRect(view.listModel()->index(1, MediaListModel::Title))
                            .bottomRight() + QPoint(5, 20);
    QMimeData md;
    md.setData(MediaListModel::mediaMimeType(), QJsonDocument(QJsonArray({ 7 })).toJson());

    QDragEnterEvent enter(pos.toPoint(), Qt::CopyAction, &md, Qt::NoButton,
                          Qt::NoModifier);
    QVERIFY(QCoreApplication::sendEvent(view.viewport(), &enter));
    QVERIFY(enter.isAccepted());

    QDragMoveEvent move(pos.toPoint(), Qt::CopyAction, &md, Qt::NoButton,
                        Qt::NoModifier);
    QVERIFY(QCoreApplication::sendEvent(view.viewport(), &move));
    QVERIFY(move.isAccepted());
}

void MediaListViewTest::fileDropEmitsFilesDropped()
{
    MediaListModel model;
    MediaListView view;
    view.setModel(&model);
    view.enableDropping();
    setItems(view, { makeItem(1, "A"), makeItem(2, "B") });
    view.show();
    view.resize(400, 300);
    QCoreApplication::processEvents();

    QSignalSpy spy(&view, &MediaListView::filesDropped);

    // A drop on row 0: the insert position is row 0 (a visible column: a
    // hidden section's rect is empty).
    const QRect rect =
        view.visualRect(view.listModel()->index(0, MediaListModel::Title));
    const QPointF pos = rect.topLeft() + QPointF(5, 5);
    QMimeData md;
    md.setUrls({ QUrl::fromLocalFile("/tmp/a.mp3"),
                 QUrl::fromLocalFile("/tmp/b.flac"), });
    QDragEnterEvent enter(pos.toPoint(), Qt::CopyAction, &md, Qt::NoButton,
                          Qt::NoModifier);
    QVERIFY(QCoreApplication::sendEvent(view.viewport(), &enter));
    QDropEvent drop(pos.toPoint(), Qt::CopyAction, &md, Qt::NoButton,
                    Qt::NoModifier);
    QVERIFY(QCoreApplication::sendEvent(view.viewport(), &drop));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<QList<QUrl>>(),
             QList<QUrl>({ QUrl::fromLocalFile("/tmp/a.mp3"),
                           QUrl::fromLocalFile("/tmp/b.flac") }));
    QCOMPARE(spy.at(0).at(1).toInt(), 0);
}

void MediaListViewTest::urlDropEmitsUrlsDropped()
{
    MediaListModel model;
    MediaListView view;
    view.setModel(&model);
    view.enableDropping();
    setItems(view, { makeItem(1, "A"), makeItem(2, "B") });
    view.show();
    view.resize(400, 300);
    QCoreApplication::processEvents();

    QSignalSpy spy(&view, &MediaListView::urlsDropped);

    // A drop on row 0: the insert position is row 0 (a visible column: a
    // hidden section's rect is empty).
    const QRect rect =
        view.visualRect(view.listModel()->index(0, MediaListModel::Title));
    const QPointF pos = rect.topLeft() + QPointF(5, 5);
    QMimeData md;
    md.setUrls({ QUrl("https://example.com/a.mp3"),
                 QUrl("https://example.com/b.mp3"), });
    QDragEnterEvent enter(pos.toPoint(), Qt::CopyAction, &md, Qt::NoButton,
                          Qt::NoModifier);
    QVERIFY(QCoreApplication::sendEvent(view.viewport(), &enter));
    QDropEvent drop(pos.toPoint(), Qt::CopyAction, &md, Qt::NoButton,
                    Qt::NoModifier);
    QVERIFY(QCoreApplication::sendEvent(view.viewport(), &drop));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<QList<QUrl>>(),
             QList<QUrl>({ QUrl("https://example.com/a.mp3"),
                           QUrl("https://example.com/b.mp3") }));
    QCOMPARE(spy.at(0).at(1).toInt(), 0);
}

void MediaListViewTest::sortableHeaderCyclesAscendingDescendingOff()
{
    MediaListModel model;
    MediaFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    MediaListView view;
    view.setModel(&proxy);
    view.setSortable(true);
    setItems(view, {
        makeItem(1, "beta"),
        makeItem(2, "Alpha"),
        makeItem(3, "gamma"),
    });

    QSignalSpy spy(&view, &MediaListView::sortChanged);
    QHeaderView *header = view.header();
    QVERIFY(!header->isSortIndicatorShown());

    clickHeader(view, MediaListModel::Title);
    QCOMPARE(proxy.sortColumn(), MediaListModel::Title);
    QCOMPARE(proxy.sortOrder(), Qt::AscendingOrder);
    QCOMPARE(spy.count(), 1);
    QVERIFY(header->isSortIndicatorShown());
    QCOMPARE(header->sortIndicatorSection(), MediaListModel::Title);
    QCOMPARE(view.idAt(proxy.index(0, 0)), 2ULL); // Alpha

    clickHeader(view, MediaListModel::Title);
    QCOMPARE(proxy.sortOrder(), Qt::DescendingOrder);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(view.idAt(proxy.index(0, 0)), 3ULL); // gamma

    clickHeader(view, MediaListModel::Title);
    QCOMPARE(proxy.sortColumn(), -1);
    QCOMPARE(spy.count(), 3);
    QVERIFY(!header->isSortIndicatorShown());
    QCOMPARE(view.idAt(proxy.index(0, 0)), 1ULL); // back to the given order
}

void MediaListViewTest::unsortableHeaderIgnoresClicks()
{
    // The playlist views keep their explicit sequence: the header click is
    // a no-op (the model is a bare list model, and the view is not
    // sortable).
    MediaListModel model;
    MediaListView view;
    view.setModel(&model);
    setItems(view, { makeItem(1, "beta"), makeItem(2, "Alpha") });

    clickHeader(view, MediaListModel::Title);
    QCOMPARE(view.idAt(model.index(0, 0)), 1ULL); // untouched

    // Even when sortable, a non-proxy model has no sort to cycle.
    view.setSortable(true);
    clickHeader(view, MediaListModel::Title);
    QCOMPARE(view.idAt(model.index(0, 0)), 1ULL);
}

void MediaListViewTest::headerClickOnPlayingColumnIsIgnored()
{
    // The status marker is model state, not a sortable column.
    MediaListModel model;
    MediaFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    MediaListView view;
    view.setModel(&proxy);
    view.setSortable(true);
    setItems(view, { makeItem(1, "beta"), makeItem(2, "Alpha") });

    clickHeader(view, MediaListModel::Playing);
    QCOMPARE(proxy.sortColumn(), -1);
    QVERIFY(!view.header()->isSortIndicatorShown());
}
}

QTEST_MAIN(MediaListViewTest)
#include "medialistview_test.moc"
