#include <QtTest>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QCoreApplication>

#include <QAction>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMenu>
#include <QMimeData>
#include <QTabBar>
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolBar>
#include <QTreeView>
#include <QToolButton>
#include <QIcon>
#include <QImage>
#include <QMouseEvent>

#include <array>
#include <memory>

#include "layoutmanager.h"
#include "librarymanager.h"
#include "medialistview.h"
#include "playlistfile.h"
#include "playlistswidget.h"
#include "rowclipboard.h"
#include "urlimportdialog.h"
#include "sourcemanager.h"
#include "taggedfile.h"
#include "testproviders.h"
#include "testsupport.h"

namespace {

// QIcon::operator== is unreliable for a freshly constructed icon (the
// pixmap cache is still empty), so compare the rendered images instead.
QImage iconImage(const QIcon &icon)
{
    return icon.pixmap(QSize(24, 24)).toImage();
}

// Records the ids handed to the metadata editor (the modal dialog is
// untestable offscreen).
class TestPlaylistsWidget : public PlaylistsWidget
{
public:
    using PlaylistsWidget::PlaylistsWidget;

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

    QString m_renameTo;
    QString m_exportPath;
    QString askPlaylistName(const QString &current) override
    {
        Q_UNUSED(current)
        return m_renameTo;
    }
    QString chooseExportPath(const QString &suggested) override
    {
        Q_UNUSED(suggested)
        return m_exportPath;
    }

    // The toolbar's chooser seam (QFileDialog is untestable offscreen).
    QStringList m_chosenFiles;
    QStringList chooseFiles() override { return m_chosenFiles; }

    // The toolbar visibility seam (the base's protected setter, reached
    // through the subclass).
    void showToolbar(bool visible) { BaseWidget::setToolbarVisible(visible); }
};

class PlaylistsWidgetTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void showsTabPerPlaylist();
    void tabShowsPlaylistItems();
    void playlistsChangedRebuildsTabs();
    void currentPlaylistItemsUpdateTab();
    void doubleClickSwitchesCurrentAndPlays();
    void playingColumnMarksCurrentRowOnly();
    void statusColumnShowsDownloadingWhileResolving();
    void playingTabShowsPlayIconOnlyWhilePlaying();
    void rebuildKeepsPlayingTabIcon();
    void newWidgetShowsIconWhilePlaying();
    void dropWithinTabReorderOfDuplicateMovesDraggedRow();
    void tabsAreNotClosable();
    void addButtonUsesPlusIcon();
    void addButtonCreatesPlaylist();
    void addButtonKeepsCurrentTab();
    void doubleClickOnEmptyStripCreatesPlaylist();
    void doubleClickOnEmptyStripKeepsCurrentTab();
    void doubleClickOnEmptyTabBarCreatesPlaylist();
    void middleClickClosesTab();
    void closingLastPlaylistCreatesNewEmptyOne();
    void dropFromLibraryInsertsIntoTabPlaylist();
    void dropOfExistingTrackFromLibraryAppendsCopy();
    void dropWithinTabReorders();
    void dropFilesIntoTabImportsAtPosition();
    void dropPlaylistFileIntoTabImportsItsEntries();
    void tabStripDropMediaFileOnTabAppends();
    void tabStripDropPlaylistFileOnTabAppendsEntries();
    void tabStripDropPlaylistFileOnStripCreatesStandalonePlaylist();
    void tabStripDropMediaFileOnStripRefused();
    void tabStripItemDragOnTabAppends();
    void tabStripItemDragOnStripRefused();
    void tabStripIsMovable();
    void tabReorderMovesPlaylist();
    void liveTabDragKeepsTheDraggedTab();
    void hoveringNonCurrentTabSwitchesAfterDelay();
    void leavingTheStripCancelsHoverSwitch();
    void hoverRestartsWhenTheTabChanges();
    void hoveringTheCurrentTabKeepsIt();
    void dirtyPlaylistTabCarriesStar();
    void tabContextMenuOffersRenameExportAndSave();
    void tabContextMenuSaveDisabledForPlainPlaylist();
    void tabContextMenuRenameRenamesPlaylist();
    void tabContextMenuSaveWritesFileAndClearsDirty();
    void tabContextMenuHiddenInEditMode();
    void tabViewSupportsRowDragging();
    void columnStateStoredAndRestoredPerTab();
    void currentTabStoredAndRestored();
    void currentTabOutOfRangeOnRestoreKeepsFirstTab();
    void rebuildKeepsTabColumnState();
    void toolbarCarriesAddAndRemoveButtons();
    void toolbarRemoveButtonRemovesShownTabSelection();
    void toolbarRemoveButtonOnlyAffectsShownTab();
    void toolbarAddFilesImportsIntoShownTab();
    void toolbarAddFilesFlattensChosenPlaylistFileIntoShownTab();
    void toolbarAddUrlOpensWizardAndImportsIntoShownTab();
    void toolbarStateStoredOnlyWhenNonDefault();
    void followPlaybackScrollsOnTabSwitch();
    void followPlaybackOffLeavesTheScrollUnchanged();
    void followPlaybackScrollsOnPlayingItemChanged();
    void followPlaybackKeepsTheSelection();
    void followPlaybackStoredOnlyWhenOn();
    void editMenuOffersFollowPlaybackToggle();
    void contextMenuRemoveRemovesSelected();
    void contextMenuRemoveOnlyAffectsThatTab();
    void contextMenuRemoveDisabledWithNoSelection();
    void deleteKeyRemovesSelectedRowsFromTab();
    void deleteKeyOnlyAffectsThatTab();
    void deleteKeyWithoutSelectionRemovesNothing();
    void copyPasteInsertsRowsAtTheCursor();
    void copyKeepsEveryCopiedRow();
    void contextMenuSlotShowsInEditMode();
    void contextMenuBulkEditPassesSelectedIds();
    void contextMenuEditMetadataSingleItem();
    void contextMenuEditMetadataDisabledWithNoSelection();
    void tabPositionMenuChangesPosition();
    void tabPositionStoredAndRestored();
    void tabPositionSeparatedFromToolbarEntries();
    void tabPositionSurvivesRebuild();
    void registeredInLayoutManager();

private:
    MediaItem makeItem(quint64 id, const QString &title);
    MediaItem addLibraryFile(const QString &name);
    MediaListView* tabView(int index) const;
    QString tabText(int tab, int row, int column) const;
    void doubleClickTab(int tab, int row);
    void emitDropped(MediaListView *view, QList<quint64> ids, QList<int> rows,
                     QString origin, int index);

    QMenu* findOpenMenuFor(QWidget *widget) const;
    QMenu* findOpenMenu() const { return findOpenMenuFor(m_widget); }
    QAction* findAction(const QMenu *menu, const QString &text) const;
    QMenu* findSubmenu(const QMenu *menu, const QString &text) const;
    void sendContextMenu(MediaListView *view);
    // True when the row's cells intersect the visible area.
    bool rowVisible(MediaListView *view, int row) const;
    // Two tabs (the current one and "Other"), the second with a list
    // long enough to scroll; returns the "Other" playlist's id.
    quint64 twoTabSetup();

    FakePlayer m_fake{this};
    DeferredProvider m_deferred{this};
    LibraryManager* m_lib = nullptr;
    PlaylistManager* m_pl = nullptr;
    PlaybackManager* m_pm = nullptr;
    MediaItemStore* m_store = nullptr;
    TestPlaylistsWidget* m_widget = nullptr;
    std::unique_ptr<QTemporaryDir> m_dir;
};

void PlaylistsWidgetTest::init()
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

    m_widget = new TestPlaylistsWidget();
    m_widget->show();
    m_widget->resize(600, 400);
    QCoreApplication::processEvents();
}

void PlaylistsWidgetTest::cleanup()
{
    // A QTest::keyClick with a modifier sticks the app's keyboard modifier
    // state: the synthetic press is delivered through
    // qt_sendShortcutOverrideEvent (which sets QApplication's modifier
    // state to the event's), and no event ever releases the modifier key
    // itself. A later QAbstractItemView::setCurrentIndex() would read the
    // stuck Control as a Toggle command and toggle the selection off —
    // reset the state with a modifier-less key press before the widget
    // goes away.
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

// Builds the item and registers it in the standalone item store (items not
// in the library need a resolvable home); re-registering an id replaces
// the stored data.
MediaItem PlaylistsWidgetTest::makeItem(quint64 id, const QString &title)
{
    MediaItem item;
    item.id = id;
    item.title = title;
    // A real (dummy) file: playback is checked against the disk.
    const QString path = "/tmp/" + title + ".mp3";
    {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.write("\x00\x01\x02");
    }
    item.source = QUrl::fromLocalFile(path);
    m_store->remove(id);
    m_store->insert(item);
    return item;
}

MediaListView* PlaylistsWidgetTest::tabView(int index) const
{
    return qobject_cast<MediaListView*>(m_widget->tabWidget()->widget(index));
}

QString PlaylistsWidgetTest::tabText(int tab, int row, int column) const
{
    MediaListView *view = tabView(tab);
    return view->listModel()->data(view->listModel()->index(row, column), Qt::DisplayRole).toString();
}

void PlaylistsWidgetTest::doubleClickTab(int tab, int row)
{
    MediaListView *view = tabView(tab);
    const QModelIndex index = view->listModel()->index(row, 0);
    QMetaObject::invokeMethod(view, "doubleClicked", Q_ARG(QModelIndex, index));
}

void PlaylistsWidgetTest::emitDropped(MediaListView *view, QList<quint64> ids,
                                      QList<int> rows, QString origin, int index)
{
    const int signalIndex =
        view->metaObject()->indexOfSignal("itemsDropped(QList<quint64>,QList<int>,QString,int)");
    std::array<void *, 5> args = { nullptr, static_cast<void*>(&ids),
                                   static_cast<void*>(&rows),
                                   static_cast<void*>(&origin),
                                   static_cast<void*>(&index), };
    QMetaObject::activate(view, signalIndex, args.data());
}

QMenu* PlaylistsWidgetTest::findOpenMenuFor(QWidget *widget) const
{
    const QList<QMenu*> menus = widget->findChildren<QMenu*>();
    for (QMenu *menu : menus)
        if (menu->isVisible())
            return menu;
    return nullptr;
}

QAction* PlaylistsWidgetTest::findAction(const QMenu *menu, const QString &text) const
{
    for (QAction *a : menu->actions())
        if (a->text() == text)
            return a;
    return nullptr;
}

QMenu* PlaylistsWidgetTest::findSubmenu(const QMenu *menu, const QString &text) const
{
    const QAction *action = findAction(menu, text);
    return action != nullptr ? action->menu() : nullptr;
}

// Right-clicks a tab's rows: sends the context-menu event the way Qt does
// after a right-button release. The event targets the viewport (where the
// click lands); the view's scroll-area filter routes it to the view's policy.
void PlaylistsWidgetTest::sendContextMenu(MediaListView *view)
{
    QWidget *viewport = view->viewport();
    const QPoint pos = viewport->rect().center();
    QContextMenuEvent cme(QContextMenuEvent::Mouse, pos,
                          viewport->mapToGlobal(pos), Qt::NoModifier);
    QCoreApplication::sendEvent(viewport, &cme);
    QCoreApplication::processEvents();
}

// addFile is asynchronous (the tag reading runs on a worker thread):
// write the file, then the shared add + itemAdded + idle wait.
MediaItem PlaylistsWidgetTest::addLibraryFile(const QString &name)
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

void PlaylistsWidgetTest::showsTabPerPlaylist()
{
    QCOMPARE(m_widget->tabWidget()->count(), 1);
    QCOMPARE(m_widget->tabWidget()->tabText(0), QString("Playlist"));

    m_pl->addPlaylist("Road");
    m_pl->addPlaylist("Work");

    QCOMPARE(m_widget->tabWidget()->count(), 3);
    QCOMPARE(m_widget->tabWidget()->tabText(1), QString("Road"));
    QCOMPARE(m_widget->tabWidget()->tabText(2), QString("Work"));
}

void PlaylistsWidgetTest::tabShowsPlaylistItems()
{
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->setCurrentPlaylist(road);
    const MediaItem sun = makeItem(1, "Sun");
    const MediaItem moon = makeItem(2, "Moon");
    m_pl->addItems({ sun.id, moon.id });

    MediaListView *view = tabView(1);
    QCOMPARE(view->rowCount(), 2);
    QCOMPARE(tabText(1, 0, MediaListModel::Title), QString("Sun"));
    QCOMPARE(tabText(1, 1, MediaListModel::Title), QString("Moon"));
}

void PlaylistsWidgetTest::playlistsChangedRebuildsTabs()
{
    const quint64 road = m_pl->addPlaylist("Road");
    QCOMPARE(m_widget->tabWidget()->count(), 2);

    m_pl->removePlaylist(road);
    QCOMPARE(m_widget->tabWidget()->count(), 1);
    QCOMPARE(m_widget->tabWidget()->tabText(0), QString("Playlist"));
}

void PlaylistsWidgetTest::currentPlaylistItemsUpdateTab()
{
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->setCurrentPlaylist(road);
    m_pl->addItems({ makeItem(1, "Sun").id });

    MediaListView *view = tabView(1);
    QCOMPARE(view->rowCount(), 1);
    QCOMPARE(tabText(1, 0, MediaListModel::Title), QString("Sun"));

    m_pl->addItems({ makeItem(2, "Moon").id });
    QCOMPARE(view->rowCount(), 2);
    QCOMPARE(tabText(1, 1, MediaListModel::Title), QString("Moon"));
}

void PlaylistsWidgetTest::doubleClickSwitchesCurrentAndPlays()
{
    const quint64 defId = m_pl->currentPlaylistId();
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->setCurrentPlaylist(road);
    m_pl->addItems({ makeItem(1, "Sun").id, makeItem(2, "Moon").id });
    m_pl->setCurrentPlaylist(defId);

    doubleClickTab(1, 1);

    QCOMPARE(m_pl->currentPlaylistId(), road);
    QCOMPARE(m_pl->currentIndex(), 1);
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(m_pm->playingItem().id, 2ULL);
}

void PlaylistsWidgetTest::playingColumnMarksCurrentRowOnly()
{
    // The marker belongs to the row that is playing: the current
    // playlist's current row. The other tabs show no marker — even when
    // they hold the same item in their first row.
    const MediaItem sun = makeItem(1, "Sun");
    const MediaItem moon = makeItem(2, "Moon");
    m_pl->addItems({ sun.id, moon.id });

    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->setCurrentPlaylist(road);
    m_pl->addItems({ sun.id, makeItem(3, "Star").id });

    m_pm->playFromPlaylist(road, 0);
    QCOMPARE(m_pm->playingItem().id, sun.id);

    // The current playlist's tab marks its current row...
    QCOMPARE(tabText(1, 0, MediaListModel::Playing), QString(QChar(0x25B6)));
    QCOMPARE(tabText(1, 1, MediaListModel::Playing), QString());
    // ...the other tab shows no marker, even for the same item.
    QCOMPARE(tabText(0, 0, MediaListModel::Playing), QString());
    QCOMPARE(tabText(0, 1, MediaListModel::Playing), QString());

    m_pm->stop();

    QCOMPARE(tabText(0, 0, MediaListModel::Playing), QString());
    QCOMPARE(tabText(1, 0, MediaListModel::Playing), QString());
}

void PlaylistsWidgetTest::statusColumnShowsDownloadingWhileResolving()
{
    // A provider item: until its resolution (a download) lands, the
    // current tab's current row carries the downloading mark.
    const MediaItem sun = makeItem(1, "Sun");
    MediaItem item = m_store->item(sun.id);
    item.provider = m_deferred.id();
    QVERIFY(m_store->updateItem(item));
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_deferred));
    m_pl->addItems({ sun.id });

    m_pm->playFromPlaylist(m_pl->currentPlaylistId(), 0);
    QCOMPARE(tabText(0, 0, MediaListModel::Playing), QString(QChar(0x2193)));

    // The resolution lands: the mark switches to the playing one.
    m_deferred.resolveNow(QUrl("https://stream.example.com/s.m3u8"));
    QCOMPARE(tabText(0, 0, MediaListModel::Playing), QString(QChar(0x25B6)));

    m_pm->stop();
    QCOMPARE(tabText(0, 0, MediaListModel::Playing), QString());
}

void PlaylistsWidgetTest::playingTabShowsPlayIconOnlyWhilePlaying()
{
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->addPlaylist("Work");
    m_pl->setCurrentPlaylist(road);
    const MediaItem sun = makeItem(1, "Sun");
    m_pl->addItems({ sun.id });

    // Nothing is playing: no tab carries an icon.
    for (int i = 0; i < m_widget->tabWidget()->count(); ++i)
        QVERIFY(m_widget->tabWidget()->tabIcon(i).isNull());

    m_pm->playFromPlaylist(road, 0);

    // The tab of the playlist that is playing carries the play icon...
    QCOMPARE(iconImage(m_widget->tabWidget()->tabIcon(1)),
             iconImage(QIcon(QStringLiteral(":/icons/play.svg"))));
    // ...the other tabs show none.
    for (int i = 0; i < m_widget->tabWidget()->count(); ++i)
        QVERIFY(i == 1 || m_widget->tabWidget()->tabIcon(i).isNull());

    // Pausing clears the icon, resuming brings it back, stop clears it.
    m_pm->pause();
    QVERIFY(m_widget->tabWidget()->tabIcon(1).isNull());

    m_pm->togglePlayPause();
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(iconImage(m_widget->tabWidget()->tabIcon(1)),
             iconImage(QIcon(QStringLiteral(":/icons/play.svg"))));

    m_pm->stop();
    QVERIFY(m_widget->tabWidget()->tabIcon(1).isNull());
}

void PlaylistsWidgetTest::rebuildKeepsPlayingTabIcon()
{
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->setCurrentPlaylist(road);
    const MediaItem sun = makeItem(1, "Sun");
    m_pl->addItems({ sun.id });

    m_pm->playFromPlaylist(road, 0);

    // Adding a playlist rebuilds all the tabs.
    m_pl->addPlaylist("Work");

    QCOMPARE(iconImage(m_widget->tabWidget()->tabIcon(1)),
             iconImage(QIcon(QStringLiteral(":/icons/play.svg"))));
    for (int i = 0; i < m_widget->tabWidget()->count(); ++i)
        QVERIFY(i == 1 || m_widget->tabWidget()->tabIcon(i).isNull());
}

void PlaylistsWidgetTest::newWidgetShowsIconWhilePlaying()
{
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->setCurrentPlaylist(road);
    const MediaItem sun = makeItem(1, "Sun");
    m_pl->addItems({ sun.id });

    m_pm->playFromPlaylist(road, 0);

    // A widget created while a track is playing must start with the icon
    // on the right tab (the signals only fire for future changes).
    PlaylistsWidget other;
    QCOMPARE(other.tabWidget()->count(), 2);
    QCOMPARE(iconImage(other.tabWidget()->tabIcon(1)),
             iconImage(QIcon(QStringLiteral(":/icons/play.svg"))));
    QVERIFY(other.tabWidget()->tabIcon(0).isNull());
}

void PlaylistsWidgetTest::tabsAreNotClosable()
{
    // Closing is the middle click on a tab, so the tabs carry no close
    // button of their own.
    m_pl->addPlaylist("Road");
    QVERIFY(!m_widget->tabWidget()->tabsClosable());
}

void PlaylistsWidgetTest::addButtonUsesPlusIcon()
{
    QToolButton *add = m_widget->findChild<QToolButton*>("addPlaylistButton");
    QVERIFY(add != nullptr);
    QCOMPARE(iconImage(add->icon()), iconImage(QIcon(":/icons/plus.svg")));
}

void PlaylistsWidgetTest::addButtonCreatesPlaylist()
{
    QToolButton *add = m_widget->findChild<QToolButton*>("addPlaylistButton");
    QVERIFY(add != nullptr);

    add->click();

    QCOMPARE(m_pl->playlistCount(), 2);
    QCOMPARE(m_widget->tabWidget()->count(), 2);
    QCOMPARE(m_widget->tabWidget()->tabText(1), QString("Playlist 2"));
}

void PlaylistsWidgetTest::addButtonKeepsCurrentTab()
{
    m_pl->addPlaylist("Road");
    m_pl->addPlaylist("Work");
    m_widget->tabWidget()->setCurrentIndex(1);

    m_widget->findChild<QToolButton*>("addPlaylistButton")->click();

    QCOMPARE(m_widget->tabWidget()->count(), 4);
    QCOMPARE(m_widget->tabWidget()->currentIndex(), 1);
    QCOMPARE(m_widget->tabWidget()->tabText(1), QString("Road"));
}

void PlaylistsWidgetTest::doubleClickOnEmptyStripCreatesPlaylist()
{
    // The tab bar is only as wide as its tabs; the empty strip to their right
    // is the QTabWidget's own area, so the event arrives at the QTabWidget.
    QTabBar *bar = m_widget->tabWidget()->tabBar();
    const QPoint pos(bar->tabRect(0).right() + 20, bar->height() / 2);
    QMouseEvent ev(QEvent::MouseButtonDblClick, QPointF(pos), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(m_widget->tabWidget(), &ev);

    QCOMPARE(m_pl->playlistCount(), 2);
    QCOMPARE(m_widget->tabWidget()->count(), 2);
}

void PlaylistsWidgetTest::doubleClickOnEmptyStripKeepsCurrentTab()
{
    m_pl->addPlaylist("Road");
    m_pl->addPlaylist("Work");
    m_widget->tabWidget()->setCurrentIndex(2);

    QTabBar *bar = m_widget->tabWidget()->tabBar();
    const QPoint pos(bar->tabRect(bar->count() - 1).right() + 20, bar->height() / 2);
    QMouseEvent ev(QEvent::MouseButtonDblClick, QPointF(pos), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(m_widget->tabWidget(), &ev);

    QCOMPARE(m_pl->playlistCount(), 4);
    QCOMPARE(m_widget->tabWidget()->count(), 4);
    QCOMPARE(m_widget->tabWidget()->currentIndex(), 2);
    QCOMPARE(m_widget->tabWidget()->tabText(2), QString("Work"));
}

void PlaylistsWidgetTest::doubleClickOnEmptyTabBarCreatesPlaylist()
{
    QTabBar *bar = m_widget->tabWidget()->tabBar();
    const QPoint pos(bar->tabRect(0).right() + 5, bar->height() / 2);
    QMouseEvent ev(QEvent::MouseButtonDblClick, QPointF(pos), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(bar, &ev);

    QCOMPARE(m_pl->playlistCount(), 2);
    QCOMPARE(m_widget->tabWidget()->count(), 2);
}

void PlaylistsWidgetTest::middleClickClosesTab()
{
    const quint64 road = m_pl->addPlaylist("Road");

    QTabBar *bar = m_widget->tabWidget()->tabBar();
    const QPoint pos = bar->tabRect(1).center();
    QMouseEvent ev(QEvent::MouseButtonPress, QPointF(pos), Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
    QCoreApplication::sendEvent(bar, &ev);

    QCOMPARE(m_pl->playlistIndexOf(road), -1);
    QCOMPARE(m_widget->tabWidget()->count(), 1);
}

void PlaylistsWidgetTest::closingLastPlaylistCreatesNewEmptyOne()
{
    const quint64 def = m_pl->currentPlaylistId();

    QTabBar *bar = m_widget->tabWidget()->tabBar();
    const QPoint pos = bar->tabRect(0).center();
    QMouseEvent ev(QEvent::MouseButtonPress, QPointF(pos), Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
    QCoreApplication::sendEvent(bar, &ev);

    QCOMPARE(m_pl->playlistCount(), 1);
    QVERIFY(m_pl->currentPlaylistId() != def);
    QCOMPARE(m_pl->count(), 0);
    QCOMPARE(m_widget->tabWidget()->count(), 1);
}

void PlaylistsWidgetTest::dropFromLibraryInsertsIntoTabPlaylist()
{
    const quint64 road = m_pl->addPlaylist("Road");
    const MediaItem a = addLibraryFile("alpha.mp3");
    const MediaItem b = addLibraryFile("beta.mp3");

    emitDropped(tabView(1), { a.id }, { 0 }, MediaListModel::libraryOrigin(), 0);
    emitDropped(tabView(1), { b.id }, { 0 }, MediaListModel::libraryOrigin(), 1);

    QCOMPARE(m_pl->count(), 0);
    const QList<quint64> items = m_pl->playlist(road).items;
    QCOMPARE(items, QList<quint64>({ a.id, b.id }));
    QCOMPARE(tabView(1)->rowCount(), 2);
}

void PlaylistsWidgetTest::dropOfExistingTrackFromLibraryAppendsCopy()
{
    // The track is already in the tab's playlist: a library drag of it is
    // an insertion — a playlist may hold the same item any number of
    // times, so the drag appends another row instead of moving the
    // existing one.
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->setCurrentPlaylist(road);
    const MediaItem a = addLibraryFile("one.mp3");
    const MediaItem b = addLibraryFile("two.mp3");
    m_pl->addItems({ a.id, b.id });

    emitDropped(tabView(1), { a.id }, { 0 }, MediaListModel::libraryOrigin(), 0);

    QCOMPARE(m_pl->playlist(road).items, QList<quint64>({ a.id, a.id, b.id }));
}

void PlaylistsWidgetTest::dropWithinTabReorderOfDuplicateMovesDraggedRow()
{
    // The same item in two rows of the tab: a reorder names the dragged
    // rows, so dragging the second copy to the front moves that row.
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->setCurrentPlaylist(road);
    const MediaItem sun = makeItem(1, "Sun");
    const MediaItem star = makeItem(2, "Star");
    m_pl->addItems({ sun.id, star.id, sun.id });

    emitDropped(tabView(1), { sun.id }, { 2 }, MediaListModel::playlistOrigin(road), 0);

    const QList<quint64> items = m_pl->playlist(road).items;
    QCOMPARE(items, QList<quint64>({ sun.id, sun.id, star.id }));
}

void PlaylistsWidgetTest::dropWithinTabReorders()
{
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->setCurrentPlaylist(road);
    const MediaItem a = addLibraryFile("one.mp3");
    const MediaItem b = addLibraryFile("two.mp3");
    const MediaItem c = addLibraryFile("three.mp3");
    m_pl->addItems({ a.id, b.id, c.id });

    emitDropped(tabView(1), { a.id }, { 0 }, MediaListModel::playlistOrigin(road), 3);

    const QList<quint64> items = m_pl->playlist(road).items;
    QCOMPARE(items, QList<quint64>({ b.id, c.id, a.id }));
}

void PlaylistsWidgetTest::dropFilesIntoTabImportsAtPosition()
{
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->setCurrentPlaylist(road);
    const MediaItem a = addLibraryFile("one.mp3");
    m_pl->addItems({ a.id });

    // A real file neither in the library nor in the store.
    const QUrl url = makeTaggedFile(m_dir->path(), "dropped.flac",
                                    "Dropped", "D", "D");
    if (url.isEmpty())
        QSKIP("ffmpeg not available");

    QMimeData md;
    md.setUrls({ url });
    QVERIFY(tabView(1)->listModel()->dropMimeData(
        &md, Qt::CopyAction, 0, 0, QModelIndex()));

    test::waitUntil([this] { return !m_pl->importing(); });

    // The block lands at the drop position (the head of the tab's list).
    const QList<quint64> items = m_pl->playlist(road).items;
    QCOMPARE(items, QList<quint64>({ m_store->ids().last(), a.id }));
    QCOMPARE(tabView(1)->rowCount(), 2);
}

void PlaylistsWidgetTest::dropPlaylistFileIntoTabImportsItsEntries()
{
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->setCurrentPlaylist(road);
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
    QVERIFY(tabView(1)->listModel()->dropMimeData(
        &md, Qt::CopyAction, 0, 0, QModelIndex()));

    test::waitUntil([this] { return !m_pl->importing(); });

    // The entries land at the drop position, in file order.
    auto idFor = [this](const QUrl &source) -> quint64 {
        for (const MediaItem &item : m_store->items())
            if (item.source == source)
                return item.id;
        return 0;
    };
    const QList<quint64> items = m_pl->playlist(road).items;
    QCOMPARE(items, QList<quint64>({ idFor(xu), idFor(yu), a.id }));
}

void PlaylistsWidgetTest::tabStripDropMediaFileOnTabAppends()
{
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->setCurrentPlaylist(road);
    const MediaItem a = addLibraryFile("one.mp3");
    m_pl->addItems({ a.id });

    const QUrl url = makeTaggedFile(m_dir->path(), "tab.flac", "T", "T", "T");
    if (url.isEmpty())
        QSKIP("ffmpeg not available");

    QTabBar *bar = m_widget->tabWidget()->tabBar();
    const QPoint pos = bar->tabRect(1).center();
    QMimeData md;
    md.setUrls({ url });
    QDragEnterEvent enter(pos, Qt::CopyAction, &md, Qt::NoButton, Qt::NoModifier);
    QVERIFY(QCoreApplication::sendEvent(bar, &enter));
    QVERIFY(enter.isAccepted());
    QDropEvent drop(pos, Qt::CopyAction, &md, Qt::NoButton, Qt::NoModifier);
    QVERIFY(QCoreApplication::sendEvent(bar, &drop));

    test::waitUntil([this] { return !m_pl->importing(); });

    // Appended to the tab's playlist.
    const QList<quint64> items = m_pl->playlist(road).items;
    QCOMPARE(items, QList<quint64>({ a.id, m_store->ids().last() }));
}

void PlaylistsWidgetTest::tabStripDropPlaylistFileOnTabAppendsEntries()
{
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->setCurrentPlaylist(road);
    const MediaItem a = addLibraryFile("one.mp3");
    m_pl->addItems({ a.id });

    const QUrl xu = makeTaggedFile(m_dir->path(), "tx.flac", "TX", "T", "T");
    if (xu.isEmpty())
        QSKIP("ffmpeg not available");
    const QString pl = m_dir->path() + "/tab.m3u";
    {
        QFile f(pl);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(xu.toLocalFile().toUtf8());
    }

    QTabBar *bar = m_widget->tabWidget()->tabBar();
    const QPoint pos = bar->tabRect(1).center();
    QMimeData md;
    md.setUrls({ QUrl::fromLocalFile(pl) });
    QDragEnterEvent enter(pos, Qt::CopyAction, &md, Qt::NoButton, Qt::NoModifier);
    QVERIFY(QCoreApplication::sendEvent(bar, &enter));
    QVERIFY(enter.isAccepted());
    QDropEvent drop(pos, Qt::CopyAction, &md, Qt::NoButton, Qt::NoModifier);
    QVERIFY(QCoreApplication::sendEvent(bar, &drop));

    test::waitUntil([this] { return !m_pl->importing(); });

    // The entries are appended to the tab's playlist (no new playlist).
    QCOMPARE(m_pl->playlistCount(), 2);
    const QList<quint64> items = m_pl->playlist(road).items;
    QCOMPARE(items, QList<quint64>({ a.id, m_store->ids().last() }));
}

void PlaylistsWidgetTest::tabStripDropPlaylistFileOnStripCreatesStandalonePlaylist()
{
    const QUrl url = makeTaggedFile(m_dir->path(), "strip.flac", "S", "S", "S");
    if (url.isEmpty())
        QSKIP("ffmpeg not available");
    const QString pl = m_dir->path() + "/strip.m3u";
    {
        QFile f(pl);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(url.toLocalFile().toUtf8());
    }

    const int before = m_pl->playlistCount();
    QTabWidget *tabs = m_widget->tabWidget();
    QTabBar *bar = tabs->tabBar();
    // The empty strip: right of the tabs, on the QTabWidget itself.
    const QPoint pos(bar->x() + bar->width() + 10, bar->height() / 2);
    QMimeData md;
    md.setUrls({ QUrl::fromLocalFile(pl) });
    QDragEnterEvent enter(pos, Qt::CopyAction, &md, Qt::NoButton, Qt::NoModifier);
    QVERIFY(QCoreApplication::sendEvent(tabs, &enter));
    QVERIFY(enter.isAccepted());
    QDropEvent drop(pos, Qt::CopyAction, &md, Qt::NoButton, Qt::NoModifier);
    QVERIFY(QCoreApplication::sendEvent(tabs, &drop));

    test::waitUntil([this] { return !m_pl->importing(); });

    QCOMPARE(m_pl->playlistCount(), before + 1);
    const Playlist created = m_pl->playlists().last();
    QCOMPARE(created.name, QString("strip"));
    QVERIFY(created.imported);
    QCOMPARE(created.items, QList<quint64>({ m_store->ids().last() }));
}

void PlaylistsWidgetTest::tabStripDropMediaFileOnStripRefused()
{
    const QUrl url = makeTaggedFile(m_dir->path(), "refused.flac", "R", "R", "R");
    if (url.isEmpty())
        QSKIP("ffmpeg not available");

    QTabWidget *tabs = m_widget->tabWidget();
    QTabBar *bar = tabs->tabBar();
    const QPoint pos(bar->x() + bar->width() + 10, bar->height() / 2);
    QMimeData md;
    md.setUrls({ url });
    QDragEnterEvent enter(pos, Qt::CopyAction, &md, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(tabs, &enter);

    // Media files over the empty strip are refused: no new playlist.
    QVERIFY(!enter.isAccepted());
    QCOMPARE(m_pl->playlistCount(), 1);
}

void PlaylistsWidgetTest::tabStripItemDragOnTabAppends()
{
    const quint64 road = m_pl->addPlaylist("Road");
    const MediaItem a = addLibraryFile("one.mp3");
    const MediaItem b = addLibraryFile("two.mp3");

    // The item drag's payload, built the way a media view builds it.
    MediaListModel model;
    model.setItemIds({ a.id, b.id });
    model.setOriginId(MediaListModel::libraryOrigin());
    QModelIndexList indexes;
    indexes << model.index(0, 0) << model.index(1, 0);
    QMimeData *md = model.mimeData(indexes);
    QVERIFY(md != nullptr);

    QTabBar *bar = m_widget->tabWidget()->tabBar();
    const QPoint pos = bar->tabRect(1).center();
    QDragEnterEvent enter(pos, Qt::CopyAction, md, Qt::NoButton,
                          Qt::NoModifier);
    QVERIFY(QCoreApplication::sendEvent(bar, &enter));
    QVERIFY(enter.isAccepted());
    QDropEvent drop(pos, Qt::CopyAction, md, Qt::NoButton,
                    Qt::NoModifier);
    QVERIFY(QCoreApplication::sendEvent(bar, &drop));
    delete md;

    // Appended to the tab's playlist (one row per dragged row).
    QCOMPARE(m_pl->playlist(road).items, QList<quint64>({ a.id, b.id }));
}

void PlaylistsWidgetTest::tabStripItemDragOnStripRefused()
{
    const MediaItem a = addLibraryFile("one.mp3");

    MediaListModel model;
    model.setItemIds({ a.id });
    QModelIndexList indexes;
    indexes << model.index(0, 0);
    QMimeData *md = model.mimeData(indexes);
    QVERIFY(md != nullptr);

    QTabWidget *tabs = m_widget->tabWidget();
    QTabBar *bar = tabs->tabBar();
    const QPoint pos(bar->x() + bar->width() + 10, bar->height() / 2);
    QDragEnterEvent enter(pos, Qt::CopyAction, md, Qt::NoButton,
                          Qt::NoModifier);
    QCoreApplication::sendEvent(tabs, &enter);
    delete md;

    // Item drags over the empty strip name no target playlist: refused.
    QVERIFY(!enter.isAccepted());
    QCOMPARE(m_pl->playlistCount(), 1);
}

void PlaylistsWidgetTest::tabStripIsMovable()
{
    QVERIFY(m_widget->tabWidget()->tabBar()->isMovable());
}

void PlaylistsWidgetTest::tabReorderMovesPlaylist()
{
    const quint64 road = m_pl->addPlaylist("Road");
    const quint64 sea = m_pl->addPlaylist("Sea");
    Q_UNUSED(road)
    Q_UNUSED(sea)

    QTabBar *bar = m_widget->tabWidget()->tabBar();
    // Drag the first tab to the end.
    bar->moveTab(0, 2);

    QCOMPARE(m_pl->playlists().at(0).name, QString("Road"));
    QCOMPARE(m_pl->playlists().at(1).name, QString("Sea"));
    QCOMPARE(m_pl->playlists().at(2).name, QString("Playlist"));
}

void PlaylistsWidgetTest::liveTabDragKeepsTheDraggedTab()
{
    m_pl->addPlaylist("Road");
    m_pl->addPlaylist("Sea");

    QTabWidget *tabs = m_widget->tabWidget();
    QTabBar *bar = tabs->tabBar();
    QWidget *w0 = tabs->widget(0);
    QWidget *w1 = tabs->widget(1);
    QWidget *w2 = tabs->widget(2);

    // A live tab drag: the bar moves the dragged tab across its
    // neighbours as the pointer travels (each crossing emits tabMoved,
    // which persists the move).
    bar->moveTab(0, 1);
    bar->moveTab(1, 2);

    // The manager follows the moves...
    QCOMPARE(m_pl->playlists().at(0).name, QString("Road"));
    QCOMPARE(m_pl->playlists().at(1).name, QString("Sea"));
    QCOMPARE(m_pl->playlists().at(2).name, QString("Playlist"));

    // ...and the tab widgets themselves were not rebuilt: a rebuild would
    // destroy the tab being dragged and abort the drag. The same widgets
    // are in the bar, in the new order.
    QCOMPARE(tabs->widget(0), w1);
    QCOMPARE(tabs->widget(1), w2);
    QCOMPARE(tabs->widget(2), w0);

    // The suppression is one-shot per move: the next playlistsChanged
    // (a real set change) rebuilds as usual.
    m_pl->addPlaylist("Hill");
    QCOMPARE(tabs->count(), 4);
    QVERIFY(tabs->widget(0) != w1); // the tabs were recreated
    QCOMPARE(tabs->widget(3)->property("playlistId").toULongLong(),
             m_pl->playlists().at(3).id);
}

void PlaylistsWidgetTest::hoveringNonCurrentTabSwitchesAfterDelay()
{
    m_pl->addPlaylist("Road");
    QCOMPARE(m_widget->tabWidget()->currentIndex(), 0);

    m_widget->hoverTab(1);

    QTest::qWait(400);
    QCOMPARE(m_widget->tabWidget()->currentIndex(), 1);
}

void PlaylistsWidgetTest::leavingTheStripCancelsHoverSwitch()
{
    m_pl->addPlaylist("Road");

    m_widget->hoverTab(1);
    m_widget->hoverTab(-1); // the pointer left the strip before the delay

    QTest::qWait(400);
    QCOMPARE(m_widget->tabWidget()->currentIndex(), 0);
}

void PlaylistsWidgetTest::hoverRestartsWhenTheTabChanges()
{
    m_pl->addPlaylist("Road");
    m_pl->addPlaylist("Sea");

    // A quick pass over two tabs (less than the delay in total) must not
    // switch: each hover restarts the window.
    m_widget->hoverTab(1);
    QTest::qWait(150);
    m_widget->hoverTab(2);
    QTest::qWait(150);
    QCOMPARE(m_widget->tabWidget()->currentIndex(), 0);

    // Staying on the tab for the full delay switches.
    QTest::qWait(300);
    QCOMPARE(m_widget->tabWidget()->currentIndex(), 2);
}

void PlaylistsWidgetTest::hoveringTheCurrentTabKeepsIt()
{
    m_pl->addPlaylist("Road");
    m_widget->tabWidget()->setCurrentIndex(1);

    m_widget->hoverTab(1);
    QTest::qWait(400);
    QCOMPARE(m_widget->tabWidget()->currentIndex(), 1);
}

void PlaylistsWidgetTest::dirtyPlaylistTabCarriesStar()
{
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->markImported(road, QStringLiteral("file:///tmp/road.m3u"));
    QTabBar *bar = m_widget->tabWidget()->tabBar();
    QCOMPARE(bar->tabText(1), QString("Road"));

    // A content mutation marks it dirty: the tab carries the *.
    m_pl->setCurrentPlaylist(road);
    m_pl->addItems({ makeItem(1, "A").id });
    QCOMPARE(bar->tabText(1), QString("Road*"));

    // A save clears the marker.
    m_pl->savePlaylist(road, m_dir->path() + "/saved.m3u");
    QCOMPARE(bar->tabText(1), QString("Road"));
}

void PlaylistsWidgetTest::tabContextMenuOffersRenameExportAndSave()
{
    m_pl->addPlaylist("Road");
    LayoutManager::getInstance()->setEditMode(false);
    QTabBar *bar = m_widget->tabWidget()->tabBar();
    const QPoint pos = bar->tabRect(1).center();
    QContextMenuEvent cme(QContextMenuEvent::Mouse, pos, bar->mapToGlobal(pos));
    QCoreApplication::sendEvent(bar, &cme);

    QMenu *menu = findOpenMenuFor(m_widget);
    if (menu == nullptr) {
        QFAIL("tab context menu did not appear");
        return;
    }
    QStringList labels;
    for (QAction *a : menu->actions())
        labels << a->text();
    QCOMPARE(labels, QStringList({ "Rename...", "Export to file...", "Save" }));
    menu->hide();
    QCoreApplication::processEvents();
}

void PlaylistsWidgetTest::tabContextMenuSaveDisabledForPlainPlaylist()
{
    m_pl->addPlaylist("Road");
    LayoutManager::getInstance()->setEditMode(false);
    QTabBar *bar = m_widget->tabWidget()->tabBar();
    const QPoint pos = bar->tabRect(1).center();
    QContextMenuEvent cme(QContextMenuEvent::Mouse, pos, bar->mapToGlobal(pos));
    QCoreApplication::sendEvent(bar, &cme);

    QMenu *menu = findOpenMenuFor(m_widget);
    if (menu == nullptr) {
        QFAIL("tab context menu did not appear");
        return;
    }
    // Not imported (no source file): a save has no target.
    QAction *save = findAction(menu, "Save");
    if (save == nullptr) {
        QFAIL("no \"Save\" action");
        return;
    }
    QVERIFY(!save->isEnabled());
    menu->hide();
    QCoreApplication::processEvents();
}

void PlaylistsWidgetTest::tabContextMenuRenameRenamesPlaylist()
{
    const quint64 road = m_pl->addPlaylist("Road");
    LayoutManager::getInstance()->setEditMode(false);
    m_widget->m_renameTo = "New Name";

    QTabBar *bar = m_widget->tabWidget()->tabBar();
    const QPoint pos = bar->tabRect(1).center();
    QContextMenuEvent cme(QContextMenuEvent::Mouse, pos, bar->mapToGlobal(pos));
    QCoreApplication::sendEvent(bar, &cme);

    QMenu *menu = findOpenMenuFor(m_widget);
    if (menu == nullptr) {
        QFAIL("tab context menu did not appear");
        return;
    }
    findAction(menu, "Rename...")->trigger();

    QCOMPARE(m_pl->playlist(road).name, QString("New Name"));
    QCOMPARE(bar->tabText(1), QString("New Name"));
    menu->hide();
    QCoreApplication::processEvents();
}

void PlaylistsWidgetTest::tabContextMenuSaveWritesFileAndClearsDirty()
{
    const quint64 road = m_pl->addPlaylist("Road");
    const QString source = m_dir->path() + "/road.m3u";
    m_pl->markImported(road, QUrl::fromLocalFile(source).toString());

    m_pl->setCurrentPlaylist(road);
    const MediaItem a = makeItem(1, "A");
    const MediaItem b = makeItem(2, "B");
    m_pl->addItems({ a.id, b.id });
    QVERIFY(m_pl->playlist(road).dirty);

    LayoutManager::getInstance()->setEditMode(false);
    QTabBar *bar = m_widget->tabWidget()->tabBar();
    const QPoint pos = bar->tabRect(1).center();
    QContextMenuEvent cme(QContextMenuEvent::Mouse, pos, bar->mapToGlobal(pos));
    QCoreApplication::sendEvent(bar, &cme);

    QMenu *menu = findOpenMenuFor(m_widget);
    if (menu == nullptr) {
        QFAIL("tab context menu did not appear");
        return;
    }
    QAction *save = findAction(menu, "Save");
    if (save == nullptr) {
        QFAIL("no \"Save\" action");
        return;
    }
    QVERIFY(save->isEnabled());
    save->trigger();

    QVERIFY(!m_pl->playlist(road).dirty);
    PlaylistFile file;
    QVERIFY(parsePlaylistFile(source, file));
    QCOMPARE(file.entries, QList<QUrl>({ a.source, b.source }));
    menu->hide();
    QCoreApplication::processEvents();
}

void PlaylistsWidgetTest::tabContextMenuHiddenInEditMode()
{
    m_pl->addPlaylist("Road");
    LayoutManager::getInstance()->setEditMode(true);
    QTabBar *bar = m_widget->tabWidget()->tabBar();
    const QPoint pos = bar->tabRect(1).center();
    QContextMenuEvent cme(QContextMenuEvent::Mouse, pos, bar->mapToGlobal(pos));
    QCoreApplication::sendEvent(bar, &cme);

    QVERIFY(findOpenMenuFor(m_widget) == nullptr);
}

void PlaylistsWidgetTest::tabViewSupportsRowDragging()
{
    MediaListView *view = tabView(0);
    QVERIFY(view->dragEnabled());
    QVERIFY(view->acceptDrops());
    QCOMPARE(view->dragDropMode(), QAbstractItemView::DragDrop);
}

void PlaylistsWidgetTest::columnStateStoredAndRestoredPerTab()
{
    m_pl->addPlaylist("Second");

    MediaListView *first = tabView(0);
    first->header()->moveSection(first->header()->visualIndex(MediaListModel::Title), 0);
    first->header()->setSectionHidden(MediaListModel::Artist, true);

    WidgetConfiguration cfg = m_widget->storeWidget();

    PlaylistsWidget *other = new PlaylistsWidget();
    other->restoreWidget(cfg);

    MediaListView *restoredFirst = qobject_cast<MediaListView*>(other->tabWidget()->widget(0));
    MediaListView *restoredSecond = qobject_cast<MediaListView*>(other->tabWidget()->widget(1));
    QVERIFY(restoredFirst != nullptr);
    QVERIFY(restoredSecond != nullptr);

    QCOMPARE(restoredFirst->header()->visualIndex(MediaListModel::Title), 0);
    QVERIFY(restoredFirst->header()->isSectionHidden(MediaListModel::Artist));
    // The untouched tab keeps its defaults.
    QCOMPARE(restoredSecond->header()->visualIndex(MediaListModel::Playing), 0);
    QVERIFY(!restoredSecond->header()->isSectionHidden(MediaListModel::Artist));

    delete other;
}

void PlaylistsWidgetTest::currentTabStoredAndRestored()
{
    m_pl->addPlaylist("Road");
    m_pl->addPlaylist("Work");
    m_widget->tabWidget()->setCurrentIndex(2);

    WidgetConfiguration cfg = m_widget->storeWidget();
    QCOMPARE(cfg.value("current").toInt(), 2);

    PlaylistsWidget *other = new PlaylistsWidget();
    other->restoreWidget(cfg);

    QCOMPARE(other->tabWidget()->count(), 3);
    QCOMPARE(other->tabWidget()->currentIndex(), 2);
    QCOMPARE(other->tabWidget()->tabText(2), QString("Work"));

    delete other;
}

void PlaylistsWidgetTest::currentTabOutOfRangeOnRestoreKeepsFirstTab()
{
    m_pl->addPlaylist("Road");
    m_pl->addPlaylist("Work");
    m_widget->tabWidget()->setCurrentIndex(2);

    WidgetConfiguration cfg = m_widget->storeWidget();

    // The playlist of the saved tab is gone by the time the layout is
    // restored: the stored index is out of range, so the first tab stays.
    m_pl->removePlaylist(m_pl->playlists().at(2).id);

    PlaylistsWidget *other = new PlaylistsWidget();
    other->restoreWidget(cfg);

    QCOMPARE(other->tabWidget()->count(), 2);
    QCOMPARE(other->tabWidget()->currentIndex(), 0);
    QCOMPARE(other->tabWidget()->tabText(0), QString("Playlist"));

    delete other;
}

void PlaylistsWidgetTest::rebuildKeepsTabColumnState()
{
    MediaListView *first = tabView(0);
    first->header()->moveSection(first->header()->visualIndex(MediaListModel::Duration), 0);
    first->header()->setSectionHidden(MediaListModel::Playing, true);

    // Adding a playlist rebuilds all tabs.
    m_pl->addPlaylist("Second");

    MediaListView *rebuilt = tabView(0);
    QCOMPARE(rebuilt->header()->visualIndex(MediaListModel::Duration), 0);
    QVERIFY(rebuilt->header()->isSectionHidden(MediaListModel::Playing));
    // The new tab starts with the defaults.
    QCOMPARE(tabView(1)->header()->visualIndex(MediaListModel::Playing), 0);
    QVERIFY(!tabView(1)->header()->isSectionHidden(MediaListModel::Playing));
}

void PlaylistsWidgetTest::contextMenuRemoveRemovesSelected()
{
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->setCurrentPlaylist(road);
    const MediaItem keep = makeItem(1, "Keep");
    const MediaItem drop = makeItem(2, "Drop");
    m_pl->addItems({ keep.id, drop.id });

    LayoutManager::getInstance()->setEditMode(false);
    m_widget->tabWidget()->setCurrentIndex(1);
    MediaListView *view = tabView(1);
    view->selectRow(1);

    sendContextMenu(view);

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
    menu->hide();
    QCoreApplication::processEvents();

    QCOMPARE(m_pl->playlist(road).items, QList<quint64>({ keep.id }));
    QCOMPARE(tabView(1)->rowCount(), 1);
}

void PlaylistsWidgetTest::contextMenuRemoveOnlyAffectsThatTab()
{
    const quint64 def = m_pl->currentPlaylistId();
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->addItems({ makeItem(1, "Def").id });
    m_pl->setCurrentPlaylist(road);
    m_pl->addItems({ makeItem(2, "R1").id, makeItem(3, "R2").id });
    m_pl->setCurrentPlaylist(def);

    LayoutManager::getInstance()->setEditMode(false);
    m_widget->tabWidget()->setCurrentIndex(1);
    MediaListView *view = tabView(1);
    view->selectRow(0);

    sendContextMenu(view);

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
    menu->hide();
    QCoreApplication::processEvents();

    // Tab 1 lost its first row; tab 0 is untouched.
    QCOMPARE(m_pl->playlist(road).items, QList<quint64>({ 3ULL }));
    QCOMPARE(m_pl->playlist(def).items, QList<quint64>({ 1ULL }));
    QCOMPARE(tabView(1)->rowCount(), 1);
    QCOMPARE(tabView(0)->rowCount(), 1);
}

void PlaylistsWidgetTest::contextMenuRemoveDisabledWithNoSelection()
{
    // refreshTab populates the rows but selects none, so with no manual
    // selection "Remove selection" is disabled.
    m_pl->addItems({ makeItem(1, "A").id });

    LayoutManager::getInstance()->setEditMode(false);
    MediaListView *view = tabView(0);

    sendContextMenu(view);

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

void PlaylistsWidgetTest::deleteKeyRemovesSelectedRowsFromTab()
{
    const quint64 a = makeItem(1, "Keep").id;
    const quint64 b = makeItem(2, "Drop").id;
    m_pl->addItems({ a, b });

    MediaListView *view = tabView(0);
    view->selectRow(1);
    view->setFocus();
    QTest::keyClick(view, Qt::Key_Delete);

    QCOMPARE(m_pl->itemIds(), QList<quint64>({ a }));
    QCOMPARE(view->rowCount(), 1);
}

void PlaylistsWidgetTest::deleteKeyOnlyAffectsThatTab()
{
    const quint64 def = m_pl->currentPlaylistId();
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->addItems({ makeItem(1, "Def").id, makeItem(2, "Def2").id });
    m_pl->insertItemsInto(road, 0, { makeItem(3, "R1").id, makeItem(4, "R2").id });

    // The shortcut fires only for the focused view, and a hidden tab page
    // cannot take focus: show the tab first.
    m_widget->tabWidget()->setCurrentIndex(1);
    MediaListView *view = tabView(1);
    view->selectRow(0);
    view->setFocus();
    QTest::keyClick(view, Qt::Key_Delete);

    // Tab 1 lost its first row; tab 0 is untouched.
    QCOMPARE(m_pl->playlist(road).items, QList<quint64>({ 4ULL }));
    QCOMPARE(m_pl->playlist(def).items, QList<quint64>({ 1ULL, 2ULL }));
}

void PlaylistsWidgetTest::deleteKeyWithoutSelectionRemovesNothing()
{
    const quint64 a = makeItem(1, "A").id;
    const quint64 b = makeItem(2, "B").id;
    m_pl->addItems({ a, b });

    MediaListView *view = tabView(0);
    view->setFocus();
    QTest::keyClick(view, Qt::Key_Delete);

    QCOMPARE(m_pl->itemIds(), QList<quint64>({ a, b }));
}

void PlaylistsWidgetTest::copyPasteInsertsRowsAtTheCursor()
{
    const quint64 a = makeItem(1, "Alpha").id;
    const quint64 b = makeItem(2, "Beta").id;
    m_pl->addItems({ a, b });

    MediaListView *view = tabView(0);
    view->selectRow(0);
    view->setFocus();
    QTest::keyClick(view, Qt::Key_C, Qt::ControlModifier);
    // The cursor sits on row 1: the pasted row lands after it.
    view->selectRow(1);
    QTest::keyClick(view, Qt::Key_V, Qt::ControlModifier);

    QCOMPARE(m_pl->itemIds(), QList<quint64>({ a, b, a }));
}

void PlaylistsWidgetTest::copyKeepsEveryCopiedRow()
{
    // The same item in two rows: copying both rows copies both copies.
    const quint64 a = makeItem(1, "Alpha").id;
    m_pl->addItems({ a, a });

    MediaListView *view = tabView(0);
    QItemSelectionModel *sm = view->selectionModel();
    sm->select(QItemSelection(view->model()->index(0, 0),
                              view->model()->index(0, 0)),
               QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    sm->select(QItemSelection(view->model()->index(1, 0),
                              view->model()->index(1, 0)),
               QItemSelectionModel::Select | QItemSelectionModel::Rows);
    view->setFocus();
    QTest::keyClick(view, Qt::Key_C, Qt::ControlModifier);
    QTest::keyClick(view, Qt::Key_V, Qt::ControlModifier);

    // The cursor stayed on row 0: the two copies land after it.
    QCOMPARE(m_pl->itemIds(), QList<quint64>({ a, a, a, a }));
}

// The slot does not gate on edit mode (7d): edit-mode right-clicks are
// owned by the container's filter (the flip-flop), and its pass-through
// reaches this slot exactly as in normal mode — a widget without a
// container (no filter) offers its menu in edit mode too.
void PlaylistsWidgetTest::contextMenuSlotShowsInEditMode()
{
    m_pl->addItems({ makeItem(1, "A").id });

    LayoutManager::getInstance()->setEditMode(true);
    MediaListView *view = tabView(0);
    view->selectRow(0);

    sendContextMenu(view);

    QMenu *menu = findOpenMenu();
    if (menu == nullptr) {
        QFAIL("context menu did not appear");
        return;
    }
    menu->hide();
}

void PlaylistsWidgetTest::contextMenuBulkEditPassesSelectedIds()
{
    // The same item in two rows: the editor gets the item once (metadata
    // is an item property), and the second row's item joins the list. Two
    // distinct items is a bulk selection: the single-item editor is
    // disabled, the bulk editor passes the whole id set.
    const MediaItem a = makeItem(1, "A");
    const MediaItem b = makeItem(2, "B");
    m_pl->addItems({ a.id, a.id, b.id });

    TestPlaylistsWidget widget;
    widget.show();
    widget.resize(600, 400);
    QCoreApplication::processEvents();

    LayoutManager::getInstance()->setEditMode(false);
    MediaListView *view = qobject_cast<MediaListView*>(
        widget.tabWidget()->widget(0));
    view->selectRow(0);
    QItemSelectionModel *sm = view->selectionModel();
    const QModelIndex row2 = view->model()->index(2, 0);
    sm->select(QItemSelection(row2, row2),
               QItemSelectionModel::Select | QItemSelectionModel::Rows);

    sendContextMenu(view);

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

void PlaylistsWidgetTest::contextMenuEditMetadataSingleItem()
{
    // A single distinct item (one row): the single-item tag editor is
    // enabled and gets the item's id.
    const MediaItem a = makeItem(1, "A");
    m_pl->addItems({ a.id });

    TestPlaylistsWidget widget;
    widget.show();
    widget.resize(600, 400);
    QCoreApplication::processEvents();

    LayoutManager::getInstance()->setEditMode(false);
    MediaListView *view = qobject_cast<MediaListView*>(
        widget.tabWidget()->widget(0));
    view->selectRow(0);

    sendContextMenu(view);

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

void PlaylistsWidgetTest::contextMenuEditMetadataDisabledWithNoSelection()
{
    m_pl->addItems({ makeItem(1, "A").id });

    TestPlaylistsWidget widget;
    widget.show();
    widget.resize(600, 400);
    QCoreApplication::processEvents();

    LayoutManager::getInstance()->setEditMode(false);
    MediaListView *view = qobject_cast<MediaListView*>(
        widget.tabWidget()->widget(0));

    sendContextMenu(view);

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

void PlaylistsWidgetTest::tabPositionMenuChangesPosition()
{
    QCOMPARE(m_widget->tabWidget()->tabPosition(), QTabWidget::North);

    QMenu *menu = m_widget->widgetContextMenu();
    QVERIFY(menu != nullptr);

    // This widget has no tab-moving option — position only.
    QVERIFY(findAction(menu, "Movable tabs") == nullptr);

    QMenu *position = findSubmenu(menu, "Tab position");
    QVERIFY(position != nullptr);

    QAction *top = findAction(position, "Top");
    QAction *bottom = findAction(position, "Bottom");
    QAction *left = findAction(position, "Left");
    QAction *right = findAction(position, "Right");
    QVERIFY(top != nullptr && bottom != nullptr && left != nullptr && right != nullptr);
    if (top == nullptr || bottom == nullptr || left == nullptr || right == nullptr)
        QFAIL("tab-position actions missing");

    QVERIFY(top->isChecked());
    QVERIFY(!bottom->isChecked());
    QVERIFY(!left->isChecked());
    QVERIFY(!right->isChecked());

    bottom->trigger();
    QCOMPARE(m_widget->tabWidget()->tabPosition(), QTabWidget::South);
    left->trigger();
    QCOMPARE(m_widget->tabWidget()->tabPosition(), QTabWidget::West);
    right->trigger();
    QCOMPARE(m_widget->tabWidget()->tabPosition(), QTabWidget::East);

    // A freshly opened menu reflects the current state.
    QMenu *menu2 = m_widget->widgetContextMenu();
    QVERIFY(menu2 != nullptr);
    QMenu *position2 = findSubmenu(menu2, "Tab position");
    QVERIFY(position2 != nullptr);
    QVERIFY(!findAction(position2, "Top")->isChecked());
    QVERIFY(!findAction(position2, "Bottom")->isChecked());
    QVERIFY(!findAction(position2, "Left")->isChecked());
    QVERIFY(findAction(position2, "Right")->isChecked());

    delete menu;
    delete menu2;
}

void PlaylistsWidgetTest::tabPositionSeparatedFromToolbarEntries()
{
    QMenu *menu = m_widget->widgetContextMenu();
    QVERIFY(menu != nullptr);

    // The menu's entries with its separators: Style... / | / Show toolbar
    // / | / Toolbar at top / Toolbar at bottom / | / Tab position / | /
    // Follow playback. The separator before "Tab position" (the one this
    // widget adds) splits the base's toolbar section off the tab position
    // submenu.
    const QList<QAction *> actions = menu->actions();
    int positionIndex = -1;
    int separators = 0;
    for (int i = 0; i < actions.size(); ++i) {
        if (actions.at(i)->text().isEmpty())
            ++separators;
        else if (actions.at(i)->text() == QStringLiteral("Tab position"))
            positionIndex = i;
    }
    QVERIFY(positionIndex >= 2);
    QCOMPARE(separators, 4);
    // Directly before "Tab position": the separator; before it, the base's
    // last toolbar entry. After it: the separator, then the follow
    // playback toggle (the widget's last entry).
    QVERIFY(actions.at(positionIndex - 1)->text().isEmpty());
    QCOMPARE(actions.at(positionIndex - 2)->text(), QStringLiteral("Toolbar at bottom"));
    QVERIFY(actions.at(positionIndex + 1)->text().isEmpty());
    QCOMPARE(actions.at(positionIndex + 2)->text(),
             QStringLiteral("Follow playback"));

    delete menu;
}

void PlaylistsWidgetTest::tabPositionStoredAndRestored()
{
    // The default (top) is not stored.
    WidgetConfiguration fresh = m_widget->storeWidget();
    QVERIFY(!fresh.contains("tabPosition"));

    QMenu *menu = m_widget->widgetContextMenu();
    findAction(findSubmenu(menu, "Tab position"), "Bottom")->trigger();
    delete menu;
    WidgetConfiguration cfg = m_widget->storeWidget();
    QCOMPARE(cfg.value("tabPosition").toString(), QString("bottom"));

    PlaylistsWidget *other = new PlaylistsWidget();
    other->restoreWidget(cfg);
    QCOMPARE(other->tabWidget()->tabPosition(), QTabWidget::South);
    delete other;

    QMenu *menuRight = m_widget->widgetContextMenu();
    findAction(findSubmenu(menuRight, "Tab position"), "Right")->trigger();
    delete menuRight;
    WidgetConfiguration cfgRight = m_widget->storeWidget();
    QCOMPARE(cfgRight.value("tabPosition").toString(), QString("right"));

    PlaylistsWidget *other2 = new PlaylistsWidget();
    other2->restoreWidget(cfgRight);
    QCOMPARE(other2->tabWidget()->tabPosition(), QTabWidget::East);
    delete other2;

    // A layout without the key keeps the default.
    PlaylistsWidget *other3 = new PlaylistsWidget();
    other3->restoreWidget(fresh);
    QCOMPARE(other3->tabWidget()->tabPosition(), QTabWidget::North);
    delete other3;
}

void PlaylistsWidgetTest::tabPositionSurvivesRebuild()
{
    QMenu *menu = m_widget->widgetContextMenu();
    findAction(findSubmenu(menu, "Tab position"), "Left")->trigger();
    delete menu;
    QCOMPARE(m_widget->tabWidget()->tabPosition(), QTabWidget::West);

    // Adding a playlist rebuilds all the tabs.
    m_pl->addPlaylist("Road");
    QCOMPARE(m_widget->tabWidget()->count(), 2);
    QCOMPARE(m_widget->tabWidget()->tabPosition(), QTabWidget::West);
}

void PlaylistsWidgetTest::toolbarCarriesAddAndRemoveButtons()
{
    QToolBar *toolbar = m_widget->findChild<QToolBar*>("toolbar");
    QVERIFY(toolbar != nullptr);
    QVERIFY(!toolbar->isHidden());
    QVERIFY(m_widget->toolbarVisible());
    QVERIFY(toolbar->findChild<QToolButton*>("addButton") != nullptr);
    QVERIFY(toolbar->findChild<QToolButton*>("removeButton") != nullptr);
    // The add menu's actions (named under the widget, findable as
    // descendants of the toolbar's button).
    QVERIFY(m_widget->findChild<QAction*>("addFilesAction") != nullptr);
    QVERIFY(m_widget->findChild<QAction*>("addUrlAction") != nullptr);
}

void PlaylistsWidgetTest::toolbarRemoveButtonRemovesShownTabSelection()
{
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->setCurrentPlaylist(road);
    const MediaItem keep = makeItem(1, "Keep");
    const MediaItem drop = makeItem(2, "Drop");
    m_pl->addItems({ keep.id, drop.id });

    m_widget->tabWidget()->setCurrentIndex(1);
    tabView(1)->selectRow(1);

    m_widget->findChild<QToolButton*>("removeButton")->click();

    QCOMPARE(m_pl->playlist(road).items, QList<quint64>({ keep.id }));
    QCOMPARE(tabView(1)->rowCount(), 1);
}

void PlaylistsWidgetTest::toolbarRemoveButtonOnlyAffectsShownTab()
{
    // The displayed tab names the playlist, not the current one: with
    // Road current but the default tab shown, the selection is removed
    // from the default playlist and Road keeps its rows.
    const quint64 def = m_pl->currentPlaylistId();
    const quint64 road = m_pl->addPlaylist("Road");
    const MediaItem a = makeItem(1, "A");
    const MediaItem b = makeItem(2, "B");
    const MediaItem c = makeItem(3, "C");
    m_pl->addItems({ a.id, b.id }); // to the current (default) playlist
    m_pl->setCurrentPlaylist(road);
    m_pl->addItems({ c.id }); // to Road

    m_widget->tabWidget()->setCurrentIndex(0);
    tabView(0)->selectRow(1);

    m_widget->findChild<QToolButton*>("removeButton")->click();

    QCOMPARE(m_pl->playlist(def).items, QList<quint64>({ a.id }));
    QCOMPARE(m_pl->playlist(road).items, QList<quint64>({ c.id }));
}

void PlaylistsWidgetTest::toolbarAddFilesImportsIntoShownTab()
{
    const QUrl url = makeTaggedFile(m_dir->path(), "picked.flac",
                                    "Picked", "P", "P");
    if (url.isEmpty())
        QSKIP("ffmpeg not available");

    const quint64 road = m_pl->addPlaylist("Road");
    m_widget->tabWidget()->setCurrentIndex(1); // Road shown
    m_widget->m_chosenFiles = { url.toLocalFile() };
    m_widget->findChild<QAction*>("addFilesAction")->trigger();

    // The import is asynchronous (the tag read runs on a worker thread).
    test::waitUntil([this] { return !m_pl->importing(); });

    // The row lands in the shown tab's playlist, not the current one.
    QCOMPARE(m_pl->playlist(road).items, QList<quint64>({ m_store->ids().last() }));
    QCOMPARE(tabView(1)->rowCount(), 1);
    QCOMPARE(m_pl->playlist(m_pl->playlists().first().id).items, QList<quint64>());
}

void PlaylistsWidgetTest::toolbarAddFilesFlattensChosenPlaylistFileIntoShownTab()
{
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

    const quint64 road = m_pl->addPlaylist("Road");
    m_widget->tabWidget()->setCurrentIndex(1); // Road shown
    m_widget->m_chosenFiles = { list };
    m_widget->findChild<QAction*>("addFilesAction")->trigger();

    test::waitUntil([this] { return !m_pl->importing(); });

    auto idFor = [this](const QUrl &source) -> quint64 {
        for (const MediaItem &item : m_store->items())
            if (item.source == source)
                return item.id;
        return 0;
    };
    // The entries land in file order.
    QCOMPARE(m_pl->playlist(road).items,
             QList<quint64>({ idFor(xu), idFor(yu) }));
}

void PlaylistsWidgetTest::toolbarAddUrlOpensWizardAndImportsIntoShownTab()
{
    // A plain stream URL: the wizard fast-paths a batch with no claimed
    // url (confirmed immediately, no review); the confirmed source lands
    // in the tab displayed at confirmation time.
    const quint64 road = m_pl->addPlaylist("Road");
    m_widget->tabWidget()->setCurrentIndex(1); // Road shown

    m_widget->findChild<QAction*>("addUrlAction")->trigger();
    UrlImportDialog *dialog = m_widget->findChild<UrlImportDialog*>();
    QVERIFY(dialog != nullptr);
    const QString stream = QStringLiteral("https://stream.example.com/picked.mp3");
    dialog->findChild<QPlainTextEdit*>("urlInput")->setPlainText(stream);
    dialog->findChild<QPushButton*>("okButton")->click();
    QCoreApplication::processEvents();

    QTRY_COMPARE_WITH_TIMEOUT(m_pl->playlist(road).items.size(), 1, 10000);
    QCOMPARE(m_pl->playlist(road).items,
             QList<quint64>({ m_store->ids().last() }));
    // The fast path ends in QDialog::accept(): the wizard has closed and
    // queued itself for deletion, so it is not touched again.
}

void PlaylistsWidgetTest::toolbarStateStoredOnlyWhenNonDefault()
{
    m_widget->showToolbar(false);
    WidgetConfiguration cfg = m_widget->storeWidget();

    // The default state (visible, top) is not persisted; the hidden one is.
    QVERIFY(cfg.contains("toolbar"));
    const QVariantMap toolbar = cfg.value("toolbar").toMap();
    QCOMPARE(toolbar.value("visible").toBool(), false);
    QCOMPARE(toolbar.value("position").toString(), QString("top"));

    // A restored widget applies it.
    PlaylistsWidget *other = new PlaylistsWidget();
    other->restoreWidget(cfg);
    QToolBar *restored = other->findChild<QToolBar*>("toolbar");
    QVERIFY(restored != nullptr);
    QVERIFY(restored->isHidden());
    delete other;

    // A default state stores nothing.
    m_widget->showToolbar(true);
    WidgetConfiguration def = m_widget->storeWidget();
    QVERIFY(!def.contains("toolbar"));
}

void PlaylistsWidgetTest::registeredInLayoutManager()
{
    WidgetConfiguration cfg;
    BaseWidget* w = LayoutManager::getInstance()->createWidget(PlaylistsWidget::id, cfg);

    QVERIFY(w != nullptr);
    QCOMPARE(w->widgetId(), PlaylistsWidget::id);
    QCOMPARE(w->widgetTitle(), PlaylistsWidget::title);
    delete w;
}

// A visible column: a hidden section's rect is empty.
bool PlaylistsWidgetTest::rowVisible(MediaListView *view, int row) const
{
    const QModelIndex index =
        view->model()->index(row, MediaListModel::Title);
    return view->viewport()->rect().intersects(view->visualRect(index));
}

// Two tabs (the current one and "Other"), the second with a list long
// enough to scroll; returns the "Other" playlist's id. The current
// playlist is left as it was. Row 35 carries a unique item (the follow
// playback's scroll resolves the item's first row, so a repeated id
// would land on row 1).
quint64 PlaylistsWidgetTest::twoTabSetup()
{
    const quint64 defId = m_pl->currentPlaylistId();
    const MediaItem a = makeItem(1, "Alpha");
    const MediaItem b = makeItem(2, "Beta");
    const MediaItem c = makeItem(3, "Gamma");
    const quint64 other = m_pl->addPlaylist("Other");
    m_pl->setCurrentPlaylist(other);
    QList<quint64> ids;
    for (int i = 0; i < 40; ++i) {
        if (i == 35)
            ids.append(c.id);
        else
            ids.append((i % 2) != 0 ? b.id : a.id);
    }
    m_pl->addItems(ids);
    m_pl->setCurrentPlaylist(defId);
    return other;
}

void PlaylistsWidgetTest::followPlaybackScrollsOnTabSwitch()
{
    const quint64 other = twoTabSetup();
    m_widget->tabWidget()->setCurrentIndex(1);
    MediaListView *view = tabView(1);
    view->setFollowPlayback(true);
    tabView(0)->setFollowPlayback(true);

    // A track deep in the list: the shown view scrolls to it.
    m_pm->playFromPlaylist(other, 35);
    QVERIFY(rowVisible(view, 35));

    // The user scrolls away: switching tabs and back must bring the row
    // into view again (the switch is the trigger, the scroll the effect).
    view->scrollToTop();
    QCoreApplication::processEvents();
    QVERIFY(!rowVisible(view, 35));

    m_widget->tabWidget()->setCurrentIndex(0);
    m_widget->tabWidget()->setCurrentIndex(1);

    QVERIFY(rowVisible(view, 35));
}

void PlaylistsWidgetTest::followPlaybackOffLeavesTheScrollUnchanged()
{
    const quint64 other = twoTabSetup();
    m_widget->tabWidget()->setCurrentIndex(1);
    MediaListView *view = tabView(1);
    QVERIFY(!view->followPlayback()); // the default

    m_pm->playFromPlaylist(other, 35);
    QVERIFY(!rowVisible(view, 35));

    // The tab switch triggers the scroll too: off, it stays put.
    m_widget->tabWidget()->setCurrentIndex(0);
    m_widget->tabWidget()->setCurrentIndex(1);
    QVERIFY(!rowVisible(view, 35));
}

void PlaylistsWidgetTest::followPlaybackScrollsOnPlayingItemChanged()
{
    const quint64 other = twoTabSetup();
    m_widget->tabWidget()->setCurrentIndex(1);
    MediaListView *view = tabView(1);
    view->setFollowPlayback(true);

    m_pm->playFromPlaylist(other, 35);
    QVERIFY(rowVisible(view, 35));

    // The next track: the view follows the new row.
    m_pm->playFromPlaylist(other, 0);
    QVERIFY(rowVisible(view, 0));
    QVERIFY(!rowVisible(view, 35));
}

void PlaylistsWidgetTest::followPlaybackKeepsTheSelection()
{
    const quint64 other = twoTabSetup();
    m_widget->tabWidget()->setCurrentIndex(1);
    MediaListView *view = tabView(1);
    view->setFollowPlayback(true);

    // The user's selection stays where they put it: the scroll never
    // selects the playing row.
    view->selectRow(0);
    m_pm->playFromPlaylist(other, 35);
    QVERIFY(rowVisible(view, 35));

    const QList<QModelIndex> selected = view->selectionModel()->selectedRows();
    QCOMPARE(selected.size(), 1);
    QCOMPARE(selected.first().row(), 0);

    // The Delete acts on the selection (row 0), not the playing row.
    view->setFocus();
    QTest::keyClick(view, Qt::Key_Delete);
    QCOMPARE(m_pl->playlist(other).items.size(), 39);
    QCOMPARE(m_pl->playlist(other).items.first(), 2);
}

void PlaylistsWidgetTest::followPlaybackStoredOnlyWhenOn()
{
    // Off (the default): the key is absent, so a fresh layout document
    // cannot pin it.
    WidgetConfiguration off = m_widget->storeWidget();
    QVERIFY(!off.contains("followPlayback"));

    QMenu *menu = m_widget->widgetContextMenu();
    QAction *follow = findAction(menu, "Follow playback");
    QVERIFY(follow != nullptr);
    follow->trigger();
    WidgetConfiguration on = m_widget->storeWidget();
    QVERIFY(on.value("followPlayback").toBool());

    // A restore applies it to the tab views — and a missing key keeps
    // the default off.
    follow->trigger();
    m_widget->restoreWidget(on);
    QVERIFY(tabView(0)->followPlayback());

    m_widget->restoreWidget(off);
    QVERIFY(!tabView(0)->followPlayback());
}

void PlaylistsWidgetTest::editMenuOffersFollowPlaybackToggle()
{
    twoTabSetup(); // two tabs: the toggle must reach every tab view

    QMenu *menu = m_widget->widgetContextMenu();
    QVERIFY(menu != nullptr);
    QAction *follow = findAction(menu, "Follow playback");
    QVERIFY(follow != nullptr);
    QVERIFY(follow->isCheckable());
    QVERIFY(!follow->isChecked());

    follow->trigger();
    QVERIFY(follow->isChecked());
    QVERIFY(tabView(0)->followPlayback());
    QVERIFY(tabView(1)->followPlayback());

    follow->trigger();
    QVERIFY(!follow->isChecked());
    QVERIFY(!tabView(0)->followPlayback());
    QVERIFY(!tabView(1)->followPlayback());
}
}


QTEST_MAIN(PlaylistsWidgetTest)
#include "playlistswidget_test.moc"
