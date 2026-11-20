#include <QtTest>
#include <QApplication>
#include <QTemporaryDir>

#include <QAction>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFile>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMenu>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QToolBar>
#include <QTreeView>
#include <QVBoxLayout>

#include <memory>

#include "layoutmanager.h"
#include "librarymanager.h"
#include "medialistview.h"
#include "currentplaylistwidget.h"
#include "rowclipboard.h"
#include "urlimportdialog.h"
#include "sourcemanager.h"
#include "taggedfile.h"
#include "testproviders.h"
#include "testsupport.h"

Q_DECLARE_METATYPE(QList<quint64>)
Q_DECLARE_METATYPE(QList<int>)

namespace {

// Records the ids handed to the metadata editor (the modal dialog is
// untestable offscreen).
class TestCurrentPlaylistWidget : public CurrentPlaylistWidget
{
public:
    using CurrentPlaylistWidget::CurrentPlaylistWidget;

    QList<quint64> m_editMetadataIds;
    void editMetadata(const QList<quint64> &ids) override
    {
        m_editMetadataIds = ids;
    }
    quint64 m_editSingleMetadataId = 0;
    void editSingleMetadata(quint64 id) override
    {
        m_editSingleMetadataId = id;
    }

    // The toolbar's chooser seam (QFileDialog is untestable offscreen).
    QStringList m_chosenFiles;
    QStringList chooseFiles() override { return m_chosenFiles; }

    // The toolbar visibility seam (the base's protected setter, reached
    // through the subclass).
    void showToolbar(bool visible) { BaseWidget::setToolbarVisible(visible); }
};

class CurrentPlaylistWidgetTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void showsPlaylistItems();
    void dropFromLibraryInsertsAtPosition();
    void dropWithinPlaylistReorders();
    void dropOfExistingTrackFromLibraryAppendsCopy();
    void dropWithRepeatedIdsAppendsEachCopy();
    void dropWithUnknownIdsInsertsNothing();
    void dropFilesImportsAtPosition();
    void dropPlaylistFileImportsItsEntriesAtPosition();
    void dropOfUnreadableFileAddsNothing();
    void contextMenuRemoveRemovesSelected();
    void contextMenuRemoveRemovesSecondCopyOfDuplicate();
    void contextMenuRemoveDisabledWithNoSelection();
    void deleteKeyRemovesSelectedRows();
    void deleteKeyRemovesSecondCopyOfDuplicate();
    void deleteKeyWithoutSelectionRemovesNothing();
    void copyPasteInsertsRowsAtTheCursor();
    void copyKeepsEveryCopiedRow();
    void contextMenuSlotShowsInEditMode();
    void contextMenuBulkEditPassesSelectedIds();
    void contextMenuEditMetadataSingleItem();
    void contextMenuEditMetadataDisabledWithNoSelection();
    void contextMenuPlayNowPlaysTheSelectedRow();
    void contextMenuPlayNextInsertsAfterCurrentRow();
    void contextMenuAddToLibraryFlagsItems();
    void contextMenuCopyUrlCopiesSourceUrls();
    void doubleClickSetsCurrent();
    void doubleClickOfSecondCopyTargetsThatRow();
    void currentIndexChangedSelectsRow();
    void switchingCurrentPlaylistUpdatesList();
    void doubleClickPlaysTrack();
    void doubleClickOfCurrentRowReplaysTrack();
    void playingColumnMarksCurrentTrack();
    void playingColumnMarksSecondCopyOfDuplicate();
    void statusColumnShowsDownloadingWhileResolving();
    void statusColumnShowsErrorWhenProviderFails();
    void statusColumnShowsErrorWhenLocalFileMissing();
    void tableViewSupportsRowDragging();
    void columnStateStoredAndRestored();
    void registeredInLayoutManager();
    void toolbarVisibleWithAddAndRemoveButtons();
    void toolbarAddMenuOffersFilesAndUrl();
    void toolbarAddFilesImportsChosenFiles();
    void toolbarAddFilesEmptyChoiceImportsNothing();
    void toolbarAddFilesFlattensChosenPlaylistFile();
    void removeButtonRemovesSelectedRows();
    void removeButtonWithoutSelectionRemovesNothing();
    void removeButtonRemovesOneCopyOfDuplicate();
    void addUrlOpensWizardOnWidget();
    void addUrlSecondTriggerRaisesOpenWizard();
    void addUrlConfirmedStreamUrlImportsIntoPlaylist();
    void toolbarStateStoredOnlyWhenNonDefault();
    void followPlaybackScrollsToThePlayingItem();
    void followPlaybackStoredOnlyWhenOn();
    void editMenuOffersFollowPlaybackToggle();

private:
    MediaItem addLibraryFile(const QString &name);
    void invokeDropped(const QList<quint64> &ids, const QList<int> &rows,
                       const QString &origin, int index);
    // The store id of the item whose source is the given url (0 for none).
    quint64 storeIdFor(const QUrl &source);

    QMenu* findOpenMenuFor(QWidget *widget);
    QMenu* findOpenMenu() { return findOpenMenuFor(m_widget); }
    QAction* findAction(QMenu *menu, const QString &text);
    void sendContextMenu(MediaListView *view);
    // Multi-selects whole rows (selectRow clears the previous selection
    // when the new row is not selected already).
    void selectRows(MediaListView *view, const QList<int> &rows);
    // True when the row's cells intersect the visible area.
    bool rowVisible(MediaListView *view, int row) const;

    FakePlayer m_fake{this};
    DeferredProvider m_deferred{this};
    LibraryManager* m_lib;
    PlaylistManager* m_pl;
    PlaybackManager* m_pm;
    MediaItemStore* m_store;
    CurrentPlaylistWidget* m_widget;
    std::unique_ptr<QTemporaryDir> m_dir;
};

void CurrentPlaylistWidgetTest::init()
{
    m_lib = LibraryManager::getInstance();
    m_pl = PlaylistManager::getInstance();
    m_store = MediaItemStore::getInstance();
    m_dir = std::make_unique<QTemporaryDir>();
    test::usePrivateDatabase(m_dir->path());
    m_lib->clear();
    m_pl->reset();
    m_store->clear();

    m_pm = PlaybackManager::getInstance();
    test::installFakePlayer(m_pm, &m_fake);
    RowClipboard::reset();

    m_widget = new CurrentPlaylistWidget();
    m_widget->show();
}

void CurrentPlaylistWidgetTest::cleanup()
{
    // A QTest::keyClick with a modifier sticks the app's keyboard modifier
    // state (the synthetic press is delivered through
    // qt_sendShortcutOverrideEvent, and no event ever releases the
    // modifier key itself): a later QAbstractItemView::setCurrentIndex()
    // would read the stuck Control as a Toggle command and toggle the
    // selection off. Reset the state with a modifier-less key press.
    QTest::keyClick(m_widget, Qt::Key_unknown, Qt::NoModifier);
    delete m_widget;
    m_pm->stop();
    m_lib->clear();
    m_pl->reset();
    m_store->clear();
    RowClipboard::reset();
    SourceManager::getInstance()->clear();
    // The context-menu tests toggle edit mode; restore the default so it does
    // not leak into the next test.
    LayoutManager::getInstance()->setEditMode(true);
}

// addFile is asynchronous (the tag reading runs on a worker thread):
// write the file, then the shared add + itemAdded + idle wait.
MediaItem CurrentPlaylistWidgetTest::addLibraryFile(const QString &name)
{
    const QString path = m_dir->path() + "/" + name;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("addLibraryFile: cannot open %s", qPrintable(path));
        return {};
    }
    f.write("\x00\x01\x02");
    f.close();
    return test::addLibraryFileAndWait(m_lib, QUrl::fromLocalFile(path));
}

void CurrentPlaylistWidgetTest::invokeDropped(const QList<quint64> &ids, const QList<int> &rows,
                                       const QString &origin, int index)
{
    QMetaObject::invokeMethod(m_widget, "onItemsDropped",
                              Q_ARG(QList<quint64>, ids), Q_ARG(QList<int>, rows),
                              Q_ARG(QString, origin), Q_ARG(int, index));
}

quint64 CurrentPlaylistWidgetTest::storeIdFor(const QUrl &source)
{
    for (const MediaItem &item : m_store->items())
        if (item.source == source)
            return item.id;
    return 0;
}

QMenu* CurrentPlaylistWidgetTest::findOpenMenuFor(QWidget *widget)
{
    const QList<QMenu*> menus = widget->findChildren<QMenu*>();
    for (QMenu *menu : menus)
        if (menu->isVisible())
            return menu;
    return nullptr;
}

QAction* CurrentPlaylistWidgetTest::findAction(QMenu *menu, const QString &text)
{
    for (QAction *a : menu->actions())
        if (a->text() == text)
            return a;
    return nullptr;
}

// Right-clicks the rows: sends the context-menu event the way Qt does after
// a right-button release on the viewport (which carries the CustomContextMenu
// policy).
void CurrentPlaylistWidgetTest::sendContextMenu(MediaListView *view)
{
    QWidget *viewport = view->viewport();
    const QPoint pos = viewport->rect().center();
    QContextMenuEvent cme(QContextMenuEvent::Mouse, pos,
                          viewport->mapToGlobal(pos), Qt::NoModifier);
    qApp->sendEvent(viewport, &cme);
    qApp->processEvents();
}

void CurrentPlaylistWidgetTest::selectRows(MediaListView *view,
                                           const QList<int> &rows)
{
    QItemSelectionModel *sm = view->selectionModel();
    for (int i = 0; i < rows.size(); ++i)
        sm->select(QItemSelection(view->model()->index(rows.at(i), 0),
                                  view->model()->index(rows.at(i), 0)),
                   i == 0
                       ? QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows
                       : QItemSelectionModel::Select | QItemSelectionModel::Rows);
}

void CurrentPlaylistWidgetTest::showsPlaylistItems()
{
    const MediaItem a = addLibraryFile("one.mp3");
    const MediaItem b = addLibraryFile("two.flac");
    m_pl->addItems({ a.id, b.id });

    MediaListView *list = m_widget->listView();
    QCOMPARE(list->rowCount(), 2);
    QCOMPARE(list->listModel()->data(list->listModel()->index(0, MediaListModel::Title), Qt::DisplayRole).toString(), QString("one"));
    QCOMPARE(list->listModel()->data(list->listModel()->index(1, MediaListModel::Title), Qt::DisplayRole).toString(), QString("two"));
}

void CurrentPlaylistWidgetTest::dropFromLibraryInsertsAtPosition()
{
    const MediaItem a = addLibraryFile("alpha.mp3");
    const MediaItem b = addLibraryFile("beta.mp3");
    const MediaItem c = addLibraryFile("gamma.mp3");
    m_pl->addItems({ a.id, c.id });

    invokeDropped({ b.id }, { 0 }, MediaListModel::libraryOrigin(), 1);

    QCOMPARE(m_pl->count(), 3);
    QCOMPARE(m_pl->itemIds().at(0), a.id);
    QCOMPARE(m_pl->itemIds().at(1), b.id);
    QCOMPARE(m_pl->itemIds().at(2), c.id);
    QCOMPARE(m_widget->listView()->rowCount(), 3);
}

void CurrentPlaylistWidgetTest::dropWithinPlaylistReorders()
{
    const MediaItem a = addLibraryFile("one.mp3");
    const MediaItem b = addLibraryFile("two.mp3");
    const MediaItem c = addLibraryFile("three.mp3");
    m_pl->addItems({ a.id, b.id, c.id });

    // The drag began on row 0 of this playlist: row 0 is what moves.
    invokeDropped({ a.id }, { 0 }, MediaListModel::playlistOrigin(m_pl->currentPlaylistId()), 3);

    QCOMPARE(m_pl->itemIds().at(0), b.id);
    QCOMPARE(m_pl->itemIds().at(1), c.id);
    QCOMPARE(m_pl->itemIds().at(2), a.id);
}

void CurrentPlaylistWidgetTest::dropOfExistingTrackFromLibraryAppendsCopy()
{
    // The track is already in the playlist: a library drag of it is an
    // insertion — a playlist may hold the same item any number of times,
    // so the drag appends another row instead of moving the existing one.
    const MediaItem a = addLibraryFile("one.mp3");
    const MediaItem b = addLibraryFile("two.mp3");
    m_pl->addItems({ a.id, b.id });

    invokeDropped({ a.id }, { 0 }, MediaListModel::libraryOrigin(), 0);

    QCOMPARE(m_pl->count(), 3);
    QCOMPARE(m_pl->itemIds(), QList<quint64>({ a.id, a.id, b.id }));
}

void CurrentPlaylistWidgetTest::dropWithRepeatedIdsAppendsEachCopy()
{
    // A drag from another playlist that lists the track twice inserts one
    // row per dragged row (the rows refer to the source model and are
    // ignored by the insertion).
    const MediaItem a = addLibraryFile("one.mp3");
    const MediaItem b = addLibraryFile("two.mp3");
    m_pl->addItems({ b.id });

    invokeDropped({ a.id, a.id }, { 0, 1 }, MediaListModel::playlistOrigin(999), 0);

    QCOMPARE(m_pl->count(), 3);
    QCOMPARE(m_pl->itemIds(), QList<quint64>({ a.id, a.id, b.id }));
}

void CurrentPlaylistWidgetTest::dropWithUnknownIdsInsertsNothing()
{
    invokeDropped({ 99999 }, { 0 }, MediaListModel::libraryOrigin(), 0);

    QCOMPARE(m_pl->count(), 0);
    QCOMPARE(m_widget->listView()->rowCount(), 0);
}

void CurrentPlaylistWidgetTest::dropFilesImportsAtPosition()
{
    const MediaItem a = addLibraryFile("one.mp3");
    const MediaItem c = addLibraryFile("three.mp3");
    m_pl->addItems({ a.id, c.id });

    // A real file neither in the library nor in the store.
    const QUrl url = makeTaggedFile(m_dir->path(), "dropped.flac",
                                    "Dropped", "D", "D");
    if (url.isEmpty())
        QSKIP("ffmpeg not available");

    QMimeData md;
    md.setUrls({ url });
    QVERIFY(m_widget->listView()->listModel()->dropMimeData(
        &md, Qt::CopyAction, 1, 0, QModelIndex()));

    // importSources is asynchronous (the tag read runs on a worker
    // thread).
    test::waitUntil([this] { return !m_pl->importing(); });

    QCOMPARE(m_pl->count(), 3);
    QCOMPARE(m_pl->itemIds().at(0), a.id);
    QCOMPARE(m_pl->itemIds().at(1), storeIdFor(url));
    QCOMPARE(m_pl->itemIds().at(2), c.id);
    QCOMPARE(m_widget->listView()->rowCount(), 3);
}

void CurrentPlaylistWidgetTest::dropPlaylistFileImportsItsEntriesAtPosition()
{
    const MediaItem a = addLibraryFile("one.mp3");
    m_pl->addItems({ a.id });

    // Real files for the playlist's entries.
    const QUrl xu = makeTaggedFile(m_dir->path(), "px.flac", "PX", "P", "P");
    const QUrl yu = makeTaggedFile(m_dir->path(), "py.flac", "PY", "P", "P");
    if (xu.isEmpty() || yu.isEmpty())
        QSKIP("ffmpeg not available");
    const QString pl = m_dir->path() + "/mix.m3u";
    {
        QFile f(pl);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write((xu.toLocalFile() + "\n" + yu.toLocalFile() + "\n").toUtf8());
    }

    QMimeData md;
    md.setUrls({ QUrl::fromLocalFile(pl) });
    QVERIFY(m_widget->listView()->listModel()->dropMimeData(
        &md, Qt::CopyAction, 0, 0, QModelIndex()));

    test::waitUntil([this] { return !m_pl->importing(); });

    // The entries land at the drop position, in file order.
    QCOMPARE(m_pl->count(), 3);
    QCOMPARE(m_pl->itemIds().at(0), storeIdFor(xu));
    QCOMPARE(m_pl->itemIds().at(1), storeIdFor(yu));
    QCOMPARE(m_pl->itemIds().at(2), a.id);
}

void CurrentPlaylistWidgetTest::dropOfUnreadableFileAddsNothing()
{
    const QString txt = m_dir->path() + "/notes.txt";
    {
        QFile f(txt);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("hello");
    }

    QMimeData md;
    md.setUrls({ QUrl::fromLocalFile(txt) });
    // The model accepts any local file; the import skips what it cannot
    // read, so nothing lands.
    QVERIFY(m_widget->listView()->listModel()->dropMimeData(
        &md, Qt::CopyAction, 0, 0, QModelIndex()));

    test::waitUntil([this] { return !m_pl->importing(); });
    QCOMPARE(m_pl->count(), 0);
}

void CurrentPlaylistWidgetTest::contextMenuRemoveRemovesSelected()
{
    const MediaItem keep = addLibraryFile("keep.mp3");
    const MediaItem drop = addLibraryFile("drop.mp3");
    m_pl->addItems({ keep.id, drop.id });

    LayoutManager::getInstance()->setEditMode(false);
    MediaListView *list = m_widget->listView();
    list->selectRow(1);

    sendContextMenu(list);

    QMenu *menu = findOpenMenu();
    if (menu == nullptr) {
        QFAIL("context menu did not appear");
        return;
    }
    QAction *remove = findAction(menu, "Remove selection");
    if (remove == nullptr) {
        QFAIL("no \"Remove selection\" action");
        return;
    }
    QVERIFY(remove->isEnabled());
    remove->trigger();

    QCOMPARE(m_pl->count(), 1);
    QVERIFY(m_pl->indexOf(keep.id) >= 0);
    QCOMPARE(list->rowCount(), 1);
}

void CurrentPlaylistWidgetTest::contextMenuRemoveRemovesSecondCopyOfDuplicate()
{
    // The same item in two rows: removing the second copy takes that row,
    // not the first occurrence of the id.
    const MediaItem a = addLibraryFile("one.mp3");
    m_pl->addItems({ a.id, a.id });

    LayoutManager::getInstance()->setEditMode(false);
    MediaListView *list = m_widget->listView();
    list->selectRow(1);

    sendContextMenu(list);

    QMenu *menu = findOpenMenu();
    if (menu == nullptr) {
        QFAIL("context menu did not appear");
        return;
    }
    QAction *remove = findAction(menu, "Remove selection");
    if (remove == nullptr) {
        QFAIL("no \"Remove selection\" action");
        return;
    }
    remove->trigger();

    QCOMPARE(m_pl->count(), 1);
    QCOMPARE(m_pl->itemIds(), QList<quint64>({ a.id }));
    QCOMPARE(m_pl->currentIndex(), 0);
}

void CurrentPlaylistWidgetTest::contextMenuRemoveDisabledWithNoSelection()
{
    // No rows, no selection: "Remove selection" is offered but disabled.
    LayoutManager::getInstance()->setEditMode(false);
    MediaListView *list = m_widget->listView();

    sendContextMenu(list);

    QMenu *menu = findOpenMenu();
    if (menu == nullptr) {
        QFAIL("context menu did not appear");
        return;
    }
    QAction *remove = findAction(menu, "Remove selection");
    if (remove == nullptr) {
        QFAIL("no \"Remove selection\" action");
        return;
    }
    QVERIFY(!remove->isEnabled());
}

void CurrentPlaylistWidgetTest::deleteKeyRemovesSelectedRows()
{
    const MediaItem keep = addLibraryFile("keep.mp3");
    const MediaItem drop = addLibraryFile("drop.mp3");
    m_pl->addItems({ keep.id, drop.id });

    MediaListView *list = m_widget->listView();
    list->selectRow(1);
    list->setFocus();
    QTest::keyClick(list, Qt::Key_Delete);

    QCOMPARE(m_pl->count(), 1);
    QCOMPARE(m_pl->itemIds().at(0), keep.id);
}

void CurrentPlaylistWidgetTest::deleteKeyRemovesSecondCopyOfDuplicate()
{
    const MediaItem a = addLibraryFile("dup.mp3");
    m_pl->addItems({ a.id, a.id });

    MediaListView *list = m_widget->listView();
    list->selectRow(1);
    list->setFocus();
    QTest::keyClick(list, Qt::Key_Delete);

    QCOMPARE(m_pl->count(), 1);
    QCOMPARE(m_pl->itemIds().at(0), a.id);
}

void CurrentPlaylistWidgetTest::deleteKeyWithoutSelectionRemovesNothing()
{
    const MediaItem a = addLibraryFile("a.mp3");
    const MediaItem b = addLibraryFile("b.mp3");
    m_pl->addItems({ a.id, b.id });

    MediaListView *list = m_widget->listView();
    // refresh() selects the current row (the first one here): clear it,
    // the test is about a view with no selection.
    list->clearSelection();
    list->setFocus();
    QTest::keyClick(list, Qt::Key_Delete);

    QCOMPARE(m_pl->count(), 2);
}

void CurrentPlaylistWidgetTest::copyPasteInsertsRowsAtTheCursor()
{
    const MediaItem a = addLibraryFile("alpha.mp3");
    const MediaItem b = addLibraryFile("beta.flac");
    m_pl->addItems({ a.id, b.id });

    MediaListView *list = m_widget->listView();
    list->selectRow(0);
    list->setFocus();
    QTest::keyClick(list, Qt::Key_C, Qt::ControlModifier);
    // The cursor sits on row 1: the pasted row lands after it.
    list->selectRow(1);
    QTest::keyClick(list, Qt::Key_V, Qt::ControlModifier);

    QCOMPARE(m_pl->itemIds(), QList<quint64>({ a.id, b.id, a.id }));
}

void CurrentPlaylistWidgetTest::copyKeepsEveryCopiedRow()
{
    // The same item in two rows: copying both rows copies both copies.
    const MediaItem a = addLibraryFile("dup.mp3");
    m_pl->addItems({ a.id, a.id });

    MediaListView *list = m_widget->listView();
    selectRows(list, { 0, 1 });
    list->setFocus();
    QTest::keyClick(list, Qt::Key_C, Qt::ControlModifier);
    QTest::keyClick(list, Qt::Key_V, Qt::ControlModifier);

    // The cursor stayed on row 0: the two copies land after it.
    QCOMPARE(m_pl->itemIds(), QList<quint64>({ a.id, a.id, a.id, a.id }));
}

// The slot does not gate on edit mode (7d): edit-mode right-clicks are
// owned by the container's filter (the flip-flop), and its pass-through
// reaches this slot exactly as in normal mode — a widget without a
// container (no filter) offers its menu in edit mode too.
void CurrentPlaylistWidgetTest::contextMenuSlotShowsInEditMode()
{
    const MediaItem a = addLibraryFile("a.mp3");
    m_pl->addItems({ a.id });

    LayoutManager::getInstance()->setEditMode(true);
    MediaListView *list = m_widget->listView();
    list->selectRow(0);

    sendContextMenu(list);

    QMenu *menu = findOpenMenu();
    if (menu == nullptr) {
        QFAIL("context menu did not appear");
        return;
    }
    menu->hide();
}

void CurrentPlaylistWidgetTest::contextMenuBulkEditPassesSelectedIds()
{
    // The same item in two rows: the editor gets the item once (metadata
    // is an item property), and a second row's item joins the list. Two
    // distinct items is a bulk selection: the single-item editor is
    // disabled, the bulk editor passes the whole id set.
    const MediaItem a = addLibraryFile("one.mp3");
    const MediaItem b = addLibraryFile("two.mp3");
    m_pl->addItems({ a.id, a.id, b.id });

    TestCurrentPlaylistWidget widget;
    widget.show();

    LayoutManager::getInstance()->setEditMode(false);
    MediaListView *list = widget.listView();
    selectRows(list, { 0, 2 });

    sendContextMenu(list);

    QMenu *menu = findOpenMenuFor(&widget);
    if (menu == nullptr) {
        QFAIL("context menu did not appear");
        return;
    }
    QAction *edit = findAction(menu, "Edit metadata...");
    QAction *bulk = findAction(menu, "Bulk edit metadata...");
    if (edit == nullptr || bulk == nullptr) {
        QFAIL("no \"Edit metadata...\" / \"Bulk edit metadata...\" actions");
        return;
    }
    QVERIFY(!edit->isEnabled());
    QVERIFY(bulk->isEnabled());
    bulk->trigger();

    QCOMPARE(widget.m_editMetadataIds, QList<quint64>({ a.id, b.id }));
    QVERIFY(widget.m_editSingleMetadataId == 0);
}

void CurrentPlaylistWidgetTest::contextMenuEditMetadataSingleItem()
{
    // A single distinct item (one row): the single-item tag editor is
    // enabled and gets the item's id.
    const MediaItem a = addLibraryFile("one.mp3");
    m_pl->addItems({ a.id });

    TestCurrentPlaylistWidget widget;
    widget.show();

    LayoutManager::getInstance()->setEditMode(false);
    MediaListView *list = widget.listView();
    selectRows(list, { 0 });

    sendContextMenu(list);

    QMenu *menu = findOpenMenuFor(&widget);
    if (menu == nullptr) {
        QFAIL("context menu did not appear");
        return;
    }
    QAction *edit = findAction(menu, "Edit metadata...");
    if (edit == nullptr) {
        QFAIL("no \"Edit metadata...\" action");
        return;
    }
    QVERIFY(edit->isEnabled());
    edit->trigger();

    QCOMPARE(widget.m_editSingleMetadataId, a.id);
    QVERIFY(widget.m_editMetadataIds.isEmpty());
}

void CurrentPlaylistWidgetTest::contextMenuEditMetadataDisabledWithNoSelection()
{
    const MediaItem a = addLibraryFile("a.mp3");
    m_pl->addItems({ a.id });

    TestCurrentPlaylistWidget widget;
    widget.show();

    LayoutManager::getInstance()->setEditMode(false);
    MediaListView *list = widget.listView();
    // A freshly shown view selects its first row: clear the selection so
    // the menu really sees none.
    list->selectionModel()->clearSelection();

    sendContextMenu(list);

    QMenu *menu = findOpenMenuFor(&widget);
    if (menu == nullptr) {
        QFAIL("context menu did not appear");
        return;
    }
    QAction *edit = findAction(menu, "Edit metadata...");
    QAction *bulk = findAction(menu, "Bulk edit metadata...");
    if (edit == nullptr || bulk == nullptr) {
        QFAIL("no \"Edit metadata...\" / \"Bulk edit metadata...\" actions");
        return;
    }
    QVERIFY(!edit->isEnabled());
    QVERIFY(!bulk->isEnabled());
}

void CurrentPlaylistWidgetTest::contextMenuPlayNowPlaysTheSelectedRow()
{
    const MediaItem a = addLibraryFile("one.mp3");
    const MediaItem b = addLibraryFile("two.mp3");
    m_pl->addItems({ a.id, b.id });

    LayoutManager::getInstance()->setEditMode(false);
    MediaListView *list = m_widget->listView();
    selectRows(list, { 1 });

    sendContextMenu(list);

    QMenu *menu = findOpenMenu();
    if (menu == nullptr) {
        QFAIL("context menu did not appear");
        return;
    }
    QAction *playNow = findAction(menu, "Play now");
    if (playNow == nullptr) {
        QFAIL("no \"Play now\" action");
        return;
    }
    QVERIFY(playNow->isEnabled());
    playNow->trigger();

    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(m_pm->playingItem().id, b.id);
    QCOMPARE(m_pl->currentIndex(), 1);
}

void CurrentPlaylistWidgetTest::contextMenuPlayNextInsertsAfterCurrentRow()
{
    const MediaItem a = addLibraryFile("one.mp3");
    const MediaItem b = addLibraryFile("two.mp3");
    m_pl->addItems({ a.id, b.id });
    m_pl->setCurrentIndex(0);  // the current row is "one"

    LayoutManager::getInstance()->setEditMode(false);
    MediaListView *list = m_widget->listView();
    selectRows(list, { 1 });  // "two" is selected

    sendContextMenu(list);

    QMenu *menu = findOpenMenu();
    if (menu == nullptr) {
        QFAIL("context menu did not appear");
        return;
    }
    QAction *playNext = findAction(menu, "Play next");
    if (playNext == nullptr) {
        QFAIL("no \"Play next\" action");
        return;
    }
    QVERIFY(playNext->isEnabled());
    playNext->trigger();

    // "two" is inserted after the current row ("one"), before the old
    // row 1; the current row itself is untouched.
    const QList<quint64> items =
        m_pl->playlist(m_pl->currentPlaylistId()).items;
    QCOMPARE(items, QList<quint64>({ a.id, b.id, b.id }));
    QCOMPARE(m_pl->currentIndex(), 0);
    // Play next is a queueing verb: it does not start playback.
    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);
}

void CurrentPlaylistWidgetTest::contextMenuAddToLibraryFlagsItems()
{
    // A playlist item out of the library (the row keeps it in the
    // store): the verb flags it back in.
    const MediaItem a = addLibraryFile("one.mp3");
    m_pl->addItems({ a.id });
    QVERIFY(m_lib->removeItem(a.id));
    QVERIFY(!m_lib->contains(a.id));
    QVERIFY(MediaItemStore::resolve(a.id).isValid());

    LayoutManager::getInstance()->setEditMode(false);
    MediaListView *list = m_widget->listView();
    selectRows(list, { 0 });

    sendContextMenu(list);

    QMenu *menu = findOpenMenu();
    if (menu == nullptr) {
        QFAIL("context menu did not appear");
        return;
    }
    QAction *add = findAction(menu, "Add to library");
    if (add == nullptr) {
        QFAIL("no \"Add to library\" action");
        return;
    }
    QVERIFY(add->isEnabled());
    add->trigger();

    QVERIFY(m_lib->contains(a.id));
    QVERIFY(MediaItemStore::resolve(a.id).inLibrary);
}

void CurrentPlaylistWidgetTest::contextMenuCopyUrlCopiesSourceUrls()
{
    const MediaItem a = addLibraryFile("one.mp3");
    const MediaItem b = addLibraryFile("two.mp3");
    m_pl->addItems({ a.id, b.id });

    LayoutManager::getInstance()->setEditMode(false);
    MediaListView *list = m_widget->listView();
    selectRows(list, { 0, 1 });

    sendContextMenu(list);

    QMenu *menu = findOpenMenu();
    if (menu == nullptr) {
        QFAIL("context menu did not appear");
        return;
    }
    QAction *copyUrl = findAction(menu, "Copy URL");
    if (copyUrl == nullptr) {
        QFAIL("no \"Copy URL\" action");
        return;
    }
    QVERIFY(copyUrl->isEnabled());
    copyUrl->trigger();

    // Local items own no provider: the source URL, one line per row,
    // in row order.
    QCOMPARE(QApplication::clipboard()->text(),
             a.source.toString() + QLatin1Char('\n') + b.source.toString());
}

void CurrentPlaylistWidgetTest::doubleClickSetsCurrent()
{
    const MediaItem a = addLibraryFile("a.mp3");
    const MediaItem b = addLibraryFile("b.mp3");
    m_pl->addItems({ a.id, b.id });

    const QModelIndex row1 = m_widget->listView()->listModel()->index(1, 0);
    QMetaObject::invokeMethod(m_widget, "onItemDoubleClicked", Q_ARG(QModelIndex, row1));

    QCOMPARE(m_pl->currentIndex(), 1);
}

void CurrentPlaylistWidgetTest::doubleClickOfSecondCopyTargetsThatRow()
{
    // The same item in two rows: double-clicking the second copy makes
    // that row current (and plays from it), not the first occurrence of
    // the id.
    const MediaItem a = addLibraryFile("one.mp3");
    m_pl->addItems({ a.id, a.id });

    const QModelIndex row1 = m_widget->listView()->listModel()->index(1, 0);
    QMetaObject::invokeMethod(m_widget, "onItemDoubleClicked", Q_ARG(QModelIndex, row1));

    QCOMPARE(m_pl->currentIndex(), 1);
    QCOMPARE(m_pm->playingItem().id, a.id);
}

void CurrentPlaylistWidgetTest::currentIndexChangedSelectsRow()
{
    const MediaItem a = addLibraryFile("a.mp3");
    const MediaItem b = addLibraryFile("b.mp3");
    m_pl->addItems({ a.id, b.id });

    m_pl->setCurrentIndex(1);

    MediaListView *list = m_widget->listView();
    QCOMPARE(list->currentIndex().row(), 1);
}

void CurrentPlaylistWidgetTest::switchingCurrentPlaylistUpdatesList()
{
    const MediaItem a = addLibraryFile("a.mp3");
    m_pl->addItems({ a.id });
    QCOMPARE(m_widget->listView()->listModel()->data(
        m_widget->listView()->listModel()->index(0, MediaListModel::Title), Qt::DisplayRole).toString(), QString("a"));

    const quint64 other = m_pl->addPlaylist("Other");
    const MediaItem b = addLibraryFile("b.mp3");
    m_pl->setCurrentPlaylist(other);
    m_pl->addItems({ b.id });

    QCOMPARE(m_pl->currentPlaylistId(), other);
    QCOMPARE(m_widget->listView()->rowCount(), 1);
    QCOMPARE(m_widget->listView()->listModel()->data(
        m_widget->listView()->listModel()->index(0, MediaListModel::Title), Qt::DisplayRole).toString(), QString("b"));
}

void CurrentPlaylistWidgetTest::doubleClickPlaysTrack()
{
    const MediaItem a = addLibraryFile("a.mp3");
    const MediaItem b = addLibraryFile("b.mp3");
    m_pl->addItems({ a.id, b.id });

    const QModelIndex row1 = m_widget->listView()->listModel()->index(1, 0);
    QMetaObject::invokeMethod(m_widget, "onItemDoubleClicked", Q_ARG(QModelIndex, row1));

    QCOMPARE(m_pl->currentIndex(), 1);
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(m_pm->playingItem().id, b.id);
}

void CurrentPlaylistWidgetTest::doubleClickOfCurrentRowReplaysTrack()
{
    // Double-clicking the row that is already current must (re)start
    // playback: a double-click is a play request, not a selection request.
    const MediaItem a = addLibraryFile("a.mp3");
    m_pl->addItems({ a.id });

    const QModelIndex row0 = m_widget->listView()->listModel()->index(0, 0);
    QMetaObject::invokeMethod(m_widget, "onItemDoubleClicked", Q_ARG(QModelIndex, row0));
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);

    m_pm->stop();
    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);

    QMetaObject::invokeMethod(m_widget, "onItemDoubleClicked", Q_ARG(QModelIndex, row0));

    QCOMPARE(m_pl->currentIndex(), 0);
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(m_pm->playingItem().id, a.id);
}

void CurrentPlaylistWidgetTest::playingColumnMarksCurrentTrack()
{
    const MediaItem a = addLibraryFile("a.mp3");
    const MediaItem b = addLibraryFile("b.mp3");
    m_pl->addItems({ a.id, b.id });

    m_pm->playFromPlaylist(m_pl->currentPlaylistId(), 1);

    MediaListView *list = m_widget->listView();
    QCOMPARE(list->listModel()->data(list->listModel()->index(0, MediaListModel::Playing), Qt::DisplayRole).toString(), QString());
    QCOMPARE(list->listModel()->data(list->listModel()->index(1, MediaListModel::Playing), Qt::DisplayRole).toString(), QString(QChar(0x25B6)));

    // Pausing keeps the mark on the row, with the paused glyph.
    m_pm->pause();
    QCOMPARE(list->listModel()->data(list->listModel()->index(1, MediaListModel::Playing), Qt::DisplayRole).toString(), QString(QChar(0x23F8)));
    m_pm->togglePlayPause();
    QCOMPARE(list->listModel()->data(list->listModel()->index(1, MediaListModel::Playing), Qt::DisplayRole).toString(), QString(QChar(0x25B6)));

    m_pm->stop();
    QCOMPARE(list->listModel()->data(list->listModel()->index(1, MediaListModel::Playing), Qt::DisplayRole).toString(), QString());
}

void CurrentPlaylistWidgetTest::statusColumnShowsDownloadingWhileResolving()
{
    // A provider item: until its resolution (a download) lands, the row
    // carries the downloading mark instead of the playing one.
    const MediaItem a = addLibraryFile("a.mp3");
    MediaItem item = m_store->item(a.id);
    item.provider = m_deferred.id();
    QVERIFY(m_store->updateItem(item));
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_deferred));
    m_pl->addItems({ a.id });

    m_pm->playFromPlaylist(m_pl->currentPlaylistId(), 0);

    MediaListView *list = m_widget->listView();
    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);
    QCOMPARE(list->listModel()->data(list->listModel()->index(0, MediaListModel::Playing), Qt::DisplayRole).toString(), QString(QChar(0x2193)));

    // The resolution lands: the mark switches to the playing one.
    m_deferred.resolveNow(QUrl("https://stream.example.com/s.m3u8"));
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(list->listModel()->data(list->listModel()->index(0, MediaListModel::Playing), Qt::DisplayRole).toString(), QString(QChar(0x25B6)));

    m_pm->stop();
    QCOMPARE(list->listModel()->data(list->listModel()->index(0, MediaListModel::Playing), Qt::DisplayRole).toString(), QString());
}

void CurrentPlaylistWidgetTest::statusColumnShowsErrorWhenProviderFails()
{
    const MediaItem a = addLibraryFile("a.mp3");
    MediaItem item = m_store->item(a.id);
    item.provider = m_deferred.id();
    QVERIFY(m_store->updateItem(item));
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_deferred));
    m_pl->addItems({ a.id });

    m_pm->playFromPlaylist(m_pl->currentPlaylistId(), 0);
    m_deferred.resolveNow();  // no stream: a failure

    MediaListView *list = m_widget->listView();
    // The failed row carries the "cannot play" mark, and a stop keeps it.
    QCOMPARE(list->listModel()->data(list->listModel()->index(0, MediaListModel::Playing), Qt::DisplayRole).toString(), QString(QChar(0x26A0)));
    m_pm->stop();
    QCOMPARE(list->listModel()->data(list->listModel()->index(0, MediaListModel::Playing), Qt::DisplayRole).toString(), QString(QChar(0x26A0)));

    // A new attempt: the mark goes back to downloading, then playing.
    m_pm->playFromPlaylist(m_pl->currentPlaylistId(), 0);
    QCOMPARE(list->listModel()->data(list->listModel()->index(0, MediaListModel::Playing), Qt::DisplayRole).toString(), QString(QChar(0x2193)));
    m_deferred.resolveNow(QUrl("https://stream.example.com/s.m3u8"));
    QCOMPARE(list->listModel()->data(list->listModel()->index(0, MediaListModel::Playing), Qt::DisplayRole).toString(), QString(QChar(0x25B6)));
}

void CurrentPlaylistWidgetTest::statusColumnShowsErrorWhenLocalFileMissing()
{
    // A local file that is gone is unplayable before the player is
    // asked: the row keeps the "cannot play" mark.
    const MediaItem a = addLibraryFile("a.mp3");
    QFile::remove(m_dir->path() + "/a.mp3");

    m_pl->addItems({ a.id });
    m_pm->playFromPlaylist(m_pl->currentPlaylistId(), 0);

    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);
    QCOMPARE(m_pm->failedItem(), a);
    MediaListView *list = m_widget->listView();
    QCOMPARE(list->listModel()->data(list->listModel()->index(0, MediaListModel::Playing), Qt::DisplayRole).toString(), QString(QChar(0x26A0)));
}

void CurrentPlaylistWidgetTest::playingColumnMarksSecondCopyOfDuplicate()
{
    // The same item in two rows: the marker is on the row that is playing
    // (the current row), not the first copy of the item.
    const MediaItem a = addLibraryFile("one.mp3");
    m_pl->addItems({ a.id, a.id });

    m_pm->playFromPlaylist(m_pl->currentPlaylistId(), 1);

    MediaListView *list = m_widget->listView();
    QCOMPARE(list->listModel()->data(list->listModel()->index(0, MediaListModel::Playing), Qt::DisplayRole).toString(), QString());
    QCOMPARE(list->listModel()->data(list->listModel()->index(1, MediaListModel::Playing), Qt::DisplayRole).toString(), QString(QChar(0x25B6)));
}

void CurrentPlaylistWidgetTest::tableViewSupportsRowDragging()
{
    MediaListView *list = m_widget->listView();
    QVERIFY(list->dragEnabled());
    QVERIFY(list->acceptDrops());
    QCOMPARE(list->dragDropMode(), QAbstractItemView::DragDrop);
}

void CurrentPlaylistWidgetTest::columnStateStoredAndRestored()
{
    MediaListView *list = m_widget->listView();
    QHeaderView *header = list->header();
    header->moveSection(header->visualIndex(MediaListModel::Title), 0);
    header->setSectionHidden(MediaListModel::Artist, true);
    header->resizeSection(MediaListModel::Duration, 123);

    WidgetConfiguration cfg = m_widget->storeWidget();

    CurrentPlaylistWidget *other = new CurrentPlaylistWidget();
    other->restoreWidget(cfg);

    QHeaderView *restored = other->listView()->header();
    QCOMPARE(restored->visualIndex(MediaListModel::Title), 0);
    QCOMPARE(restored->visualIndex(MediaListModel::Playing), 1);
    QVERIFY(restored->isSectionHidden(MediaListModel::Artist));
    QCOMPARE(restored->sectionSize(MediaListModel::Duration), 123);

    delete other;
}

void CurrentPlaylistWidgetTest::toolbarVisibleWithAddAndRemoveButtons()
{
    QToolBar *toolbar = m_widget->findChild<QToolBar*>("toolbar");
    QVERIFY(toolbar != nullptr);
    QVERIFY(!toolbar->isHidden());
    QVERIFY(m_widget->toolbarVisible());
    QCOMPARE(m_widget->toolbarPosition(), BaseWidget::ToolbarPosition::Top);
    QVERIFY(toolbar->findChild<QToolButton*>("addButton") != nullptr);
    QVERIFY(toolbar->findChild<QToolButton*>("removeButton") != nullptr);
    // The toolbar is the strip at the top of the widget's box layout.
    QVBoxLayout *layout = qobject_cast<QVBoxLayout*>(m_widget->layout());
    QVERIFY(layout != nullptr);
    QCOMPARE(layout->indexOf(toolbar), 0);
}

void CurrentPlaylistWidgetTest::toolbarAddMenuOffersFilesAndUrl()
{
    QToolButton *add = m_widget->findChild<QToolButton*>("addButton");
    QVERIFY(add != nullptr);
    QCOMPARE(add->popupMode(), QToolButton::InstantPopup);
    QVERIFY(m_widget->findChild<QAction*>("addFilesAction") != nullptr);
    QVERIFY(m_widget->findChild<QAction*>("addUrlAction") != nullptr);
}

void CurrentPlaylistWidgetTest::toolbarAddFilesImportsChosenFiles()
{
    // A real tagged file (the playlist import reads tags as a drop does).
    const QUrl url = makeTaggedFile(m_dir->path(), "picked.flac",
                                    "Picked", "P", "P");
    if (url.isEmpty())
        QSKIP("ffmpeg not available");

    TestCurrentPlaylistWidget w;
    w.m_chosenFiles = { url.toLocalFile() };
    w.show();

    QAction *add = w.findChild<QAction*>("addFilesAction");
    QVERIFY(add != nullptr);
    add->trigger();

    // The import is asynchronous (the tag read runs on a worker thread).
    test::waitUntil([this] { return !m_pl->importing(); });

    QCOMPARE(m_pl->count(), 1);
    QCOMPARE(m_pl->itemIds().at(0), storeIdFor(url));
    QCOMPARE(w.listView()->rowCount(), 1);
}

void CurrentPlaylistWidgetTest::toolbarAddFilesEmptyChoiceImportsNothing()
{
    TestCurrentPlaylistWidget w;
    w.show();

    QAction *add = w.findChild<QAction*>("addFilesAction");
    QVERIFY(add != nullptr);
    add->trigger(); // the chooser bailed (empty selection)

    QCOMPARE(m_pl->count(), 0);
    QVERIFY(!m_pl->importing());
}

void CurrentPlaylistWidgetTest::toolbarAddFilesFlattensChosenPlaylistFile()
{
    // A playlist file picked in the chooser imports as its entries — the
    // same flattening a file drop does.
    const QUrl xu = makeTaggedFile(m_dir->path(), "px.flac", "PX", "P", "P");
    const QUrl yu = makeTaggedFile(m_dir->path(), "py.flac", "PY", "P", "P");
    if (xu.isEmpty() || yu.isEmpty())
        QSKIP("ffmpeg not available");
    const QString list = m_dir->path() + "/mix.m3u";
    {
        QFile f(list);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write((xu.toLocalFile() + "\n" + yu.toLocalFile() + "\n").toUtf8());
    }

    TestCurrentPlaylistWidget w;
    w.m_chosenFiles = { list };
    w.show();

    QAction *add = w.findChild<QAction*>("addFilesAction");
    QVERIFY(add != nullptr);
    add->trigger();

    test::waitUntil([this] { return !m_pl->importing(); });

    QCOMPARE(m_pl->count(), 2);
    QCOMPARE(m_pl->itemIds().at(0), storeIdFor(xu));
    QCOMPARE(m_pl->itemIds().at(1), storeIdFor(yu));
}

void CurrentPlaylistWidgetTest::removeButtonRemovesSelectedRows()
{
    const MediaItem keep = addLibraryFile("keep.mp3");
    const MediaItem drop = addLibraryFile("dropme.mp3");
    m_pl->addItems({ keep.id, drop.id });

    m_widget->listView()->selectRow(1);
    m_widget->findChild<QToolButton*>("removeButton")->click();

    QCOMPARE(m_pl->count(), 1);
    QCOMPARE(m_pl->itemIds().at(0), keep.id);
    QCOMPARE(m_widget->listView()->rowCount(), 1);
}

void CurrentPlaylistWidgetTest::removeButtonWithoutSelectionRemovesNothing()
{
    const MediaItem a = addLibraryFile("one.mp3");
    m_pl->addItems({ a.id });
    m_widget->listView()->clearSelection();

    m_widget->findChild<QToolButton*>("removeButton")->click();

    QCOMPARE(m_pl->count(), 1);
    QCOMPARE(m_pl->itemIds().at(0), a.id);
}

void CurrentPlaylistWidgetTest::removeButtonRemovesOneCopyOfDuplicate()
{
    // The rows, not their ids, name what is removed: one row of a
    // duplicated item goes, the other stays.
    const MediaItem a = addLibraryFile("one.mp3");
    m_pl->addItems({ a.id, a.id });

    m_widget->listView()->selectRow(1);
    m_widget->findChild<QToolButton*>("removeButton")->click();

    QCOMPARE(m_pl->count(), 1);
    QCOMPARE(m_pl->itemIds().at(0), a.id);
}

void CurrentPlaylistWidgetTest::addUrlOpensWizardOnWidget()
{
    TestCurrentPlaylistWidget w;
    w.show();

    w.findChild<QAction*>("addUrlAction")->trigger();
    UrlImportDialog *dialog = w.findChild<UrlImportDialog*>();
    QVERIFY(dialog != nullptr); // parented to the widget, findable under it

    dialog->deleteLater();
}

void CurrentPlaylistWidgetTest::addUrlSecondTriggerRaisesOpenWizard()
{
    // The wizard is non-modal, a single instance: a second trigger raises
    // the open one, no second dialog is created.
    TestCurrentPlaylistWidget w;
    w.show();

    w.findChild<QAction*>("addUrlAction")->trigger();
    UrlImportDialog *first = w.findChild<UrlImportDialog*>();
    QVERIFY(first != nullptr);
    w.findChild<QAction*>("addUrlAction")->trigger();
    QCOMPARE(w.findChild<UrlImportDialog*>(), first);
    QCOMPARE(w.findChildren<UrlImportDialog*>().size(), 1);

    first->deleteLater();
}

void CurrentPlaylistWidgetTest::addUrlConfirmedStreamUrlImportsIntoPlaylist()
{
    // A plain stream URL: the wizard fast-paths a batch with no claimed
    // url (confirmed immediately, no review), and the confirmed source
    // lands in the current playlist as an appended row.
    TestCurrentPlaylistWidget w;
    w.show();

    w.findChild<QAction*>("addUrlAction")->trigger();
    UrlImportDialog *dialog = w.findChild<UrlImportDialog*>();
    QVERIFY(dialog != nullptr);
    const QString stream = QStringLiteral("https://stream.example.com/picked.mp3");
    dialog->findChild<QPlainTextEdit*>("urlInput")->setPlainText(stream);
    dialog->findChild<QPushButton*>("okButton")->click();
    qApp->processEvents();

    QTRY_COMPARE_WITH_TIMEOUT(m_pl->count(), 1, 10000);
    QCOMPARE(m_pl->itemIds().at(0), storeIdFor(QUrl(stream)));
    // The fast path ends in QDialog::accept(): the wizard has closed and
    // queued itself for deletion, so it is not touched again.
}

void CurrentPlaylistWidgetTest::toolbarStateStoredOnlyWhenNonDefault()
{
    TestCurrentPlaylistWidget w;
    w.show();

    // The default state (visible, top) is not persisted.
    QVERIFY(!w.storeWidget().contains("toolbar"));

    w.showToolbar(false);
    WidgetConfiguration cfg = w.storeWidget();
    const QVariantMap toolbar = cfg.value("toolbar").toMap();
    QCOMPARE(toolbar.value("visible").toBool(), false);
    QCOMPARE(toolbar.value("position").toString(), QString("top"));

    // A restored widget applies it.
    CurrentPlaylistWidget *other = new CurrentPlaylistWidget();
    other->restoreWidget(cfg);
    QToolBar *restored = other->findChild<QToolBar*>("toolbar");
    QVERIFY(restored != nullptr);
    QVERIFY(restored->isHidden());

    delete other;
}

void CurrentPlaylistWidgetTest::registeredInLayoutManager()
{
    WidgetConfiguration cfg;
    BaseWidget* w = LayoutManager::getInstance()->createWidget(CurrentPlaylistWidget::id, cfg);

    QVERIFY(w != nullptr);
    QCOMPARE(w->widgetId(), CurrentPlaylistWidget::id);
    QCOMPARE(w->widgetTitle(), CurrentPlaylistWidget::title);
    delete w;
}

// True when the row's cells intersect the visible area (the list must
// be long enough that the row is scrollable out of it). A visible column:
// a hidden section's rect is empty.
bool CurrentPlaylistWidgetTest::rowVisible(MediaListView *view, int row) const
{
    const QModelIndex index =
        view->model()->index(row, MediaListModel::Title);
    return view->viewport()->rect().intersects(view->visualRect(index));
}

void CurrentPlaylistWidgetTest::followPlaybackScrollsToThePlayingItem()
{
    // A list long enough to scroll (the view is short). Row 35 carries a
    // unique item (the scroll resolves the item's first row, so a
    // repeated id would land on row 1).
    const MediaItem a = addLibraryFile("a.mp3");
    const MediaItem b = addLibraryFile("b.mp3");
    const MediaItem c = addLibraryFile("c.mp3");
    QList<quint64> ids;
    for (int i = 0; i < 40; ++i) {
        if (i == 35)
            ids.append(c.id);
        else
            ids.append((i % 2) != 0 ? b.id : a.id);
    }
    m_pl->addItems(ids);

    m_widget->resize(320, 160);
    qApp->processEvents();

    MediaListView *list = m_widget->listView();
    QVERIFY(!rowVisible(list, 35));
    list->setFollowPlayback(true);

    m_pm->playFromPlaylist(m_pl->currentPlaylistId(), 35);

    QVERIFY(rowVisible(list, 35));
}

void CurrentPlaylistWidgetTest::followPlaybackStoredOnlyWhenOn()
{
    // Off (the default): the key is absent, so a fresh layout document
    // cannot pin it.
    WidgetConfiguration off = m_widget->storeWidget();
    QVERIFY(!off.contains("followPlayback"));

    m_widget->listView()->setFollowPlayback(true);
    WidgetConfiguration on = m_widget->storeWidget();
    QVERIFY(on.value("followPlayback").toBool());

    // A restore applies it — and a missing key keeps the default off.
    m_widget->listView()->setFollowPlayback(false);
    m_widget->restoreWidget(on);
    QVERIFY(m_widget->listView()->followPlayback());
    m_widget->restoreWidget(off);
    QVERIFY(!m_widget->listView()->followPlayback());
}

void CurrentPlaylistWidgetTest::editMenuOffersFollowPlaybackToggle()
{
    QMenu *menu = m_widget->widgetContextMenu();
    QVERIFY(menu != nullptr);
    QAction *follow = findAction(menu, "Follow playback");
    QVERIFY(follow != nullptr);
    QVERIFY(follow->isCheckable());
    QVERIFY(!follow->isChecked());

    follow->trigger();
    QVERIFY(follow->isChecked());
    QVERIFY(m_widget->listView()->followPlayback());

    follow->trigger();
    QVERIFY(!follow->isChecked());
    QVERIFY(!m_widget->listView()->followPlayback());
}
}


QTEST_MAIN(CurrentPlaylistWidgetTest)
#include "currentplaylistwidget_test.moc"
