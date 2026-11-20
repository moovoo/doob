#include <QtTest>
#include <QTemporaryDir>
#include <QCoreApplication>

#include <QAction>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QIcon>
#include <QImage>
#include <QMimeData>
#include <QMenu>
#include <QHeaderView>
#include <QTreeWidget>

#include <array>
#include <memory>

#include "layoutmanager.h"
#include "medialistmodel.h"
#include "playlistfile.h"
#include "playlistlistview.h"
#include "playlistlistwidget.h"
#include "testsupport.h"

namespace {

// QIcon::operator== is unreliable for a freshly constructed icon (the
// pixmap cache is still empty), so compare the rendered images instead.
QImage iconImage(const QIcon &icon)
{
    return icon.pixmap(QSize(24, 24)).toImage();
}

class TestPlaylistListWidget : public PlaylistListWidget
{
public:
    using PlaylistListWidget::PlaylistListWidget;

    // The seams for the name/path inputs: the dialogs are untestable
    // offscreen.
    QString askPlaylistName() override { return m_name; }
    QString askRenameName(const QString &current) override
    {
        Q_UNUSED(current)
        return m_rename;
    }
    QString chooseExportPath(const QString &suggested) override
    {
        Q_UNUSED(suggested)
        return m_exportPath;
    }

    QString m_name;
    QString m_rename;
    QString m_exportPath;
};

class PlaylistListWidgetTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void hasPlayingNameTracksColumns();
    void showsOneRowPerPlaylist();
    void playlistsChangedRebuildsList();
    void playlistChangedUpdatesTrackCount();
    void trackCountUpdateKeepsSelection();
    void playingColumnMarksCurrentPlaylistOnlyWhilePlaying();
    void rebuildKeepsPlayingMarker();
    void newWidgetShowsIconWhilePlaying();
    void doubleClickSwitchesCurrentAndPlays();
    void doubleClickPlaysFromPlaylistCursor();
    void doubleClickEmptyPlaylistStops();
    void contextMenuNewPlaylistAsksNameAndAdds();
    void contextMenuNewPlaylistBlankNameAddsNothing();
    void contextMenuDeleteRemovesSelected();
    void contextMenuDeleteAbsentWithoutSelection();
    void contextMenuSlotShowsInEditMode();
    void rowNameCarriesDirtyMarker();
    void contextMenuRenameRenamesSelected();
    void contextMenuExportWritesFile();
    void contextMenuSaveDisabledForPlainPlaylist();
    void contextMenuSaveWritesFileAndClearsDirty();
    void dragRowReordersPlaylists();
    void dragRowToEmptyAreaAppends();
    void moveRowToIgnoresOutOfRangeIndex();
    void dropItemsOnRowAppendsToThatPlaylist();
    void dropFilesOnRowImportsIntoThatPlaylist();
    void dropOnEmptyAreaRefused();
    void registeredInLayoutManager();

private:
    MediaItem makeItem(quint64 id, const QString &title);
    QTreeWidgetItem* rowForPlaylist(quint64 playlistId) const;
    QString rowText(int row, int column) const;
    void doubleClickRow(QTreeWidgetItem *item);
    QMenu* findOpenMenu() const;
    QAction* findAction(const QMenu *menu, const QString &text) const;
    void sendContextMenu();
    // A position within a row of the list (viewport coordinates): the row's
    // upper half by default, its lower half when asked.
    QPoint rowPoint(int row, bool lowerHalf = false) const;
    int rowHeight(int row) const;
    // Sends a drag the way Qt delivers it: the enter must be accepted
    // (that registers the drop target), then the drop on the same viewport
    // position. Returns whether the enter was accepted.
    bool sendListDrop(const QPoint &pos, QMimeData *mime);

    FakePlayer m_fake{this};
    PlaylistManager* m_pl = nullptr;
    PlaybackManager* m_pm = nullptr;
    MediaItemStore* m_store = nullptr;
    PlaylistListWidget* m_widget = nullptr;
    std::unique_ptr<QTemporaryDir> m_dir;
};

void PlaylistListWidgetTest::init()
{
    m_pl = PlaylistManager::getInstance();
    m_store = MediaItemStore::getInstance();
    m_dir = std::make_unique<QTemporaryDir>();
    test::usePrivateDatabase(m_dir->path());
    m_pl->reset();
    m_store->clear();

    m_pm = PlaybackManager::getInstance();
    test::installFakePlayer(m_pm, &m_fake);

    m_widget = new TestPlaylistListWidget();
    m_widget->show();
    m_widget->resize(400, 300);
    QCoreApplication::processEvents();
}

void PlaylistListWidgetTest::cleanup()
{
    delete m_widget;
    m_pm->stop();
    m_pl->reset();
    m_store->clear();
    // The context-menu tests toggle edit mode; restore the default so it
    // does not leak into the next test.
    LayoutManager::getInstance()->setEditMode(true);
}

// Builds the item and registers it in the standalone item store (items not
// in the library need a resolvable home); re-registering an id replaces
// the stored data.
MediaItem PlaylistListWidgetTest::makeItem(quint64 id, const QString &title)
{
    MediaItem item;
    item.id = id;
    item.title = title;
    item.source = QUrl::fromLocalFile("/tmp/" + title + ".mp3");
    m_store->remove(id);
    m_store->insert(item);
    return item;
}

QTreeWidgetItem* PlaylistListWidgetTest::rowForPlaylist(quint64 playlistId) const
{
    QTreeWidget *list = m_widget->list();
    for (int i = 0; i < list->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = list->topLevelItem(i);
        if (static_cast<quint64>(item->data(PlaylistListWidget::Playing, Qt::UserRole).toULongLong())
                == playlistId)
            return item;
    }
    return nullptr;
}

QString PlaylistListWidgetTest::rowText(int row, int column) const
{
    return m_widget->list()->topLevelItem(row)->text(column);
}

void PlaylistListWidgetTest::doubleClickRow(QTreeWidgetItem *item)
{
    const int signalIndex =
        m_widget->list()->metaObject()->indexOfSignal("itemDoubleClicked(QTreeWidgetItem*,int)");
    int column = 0;
    std::array<void *, 3> args = { nullptr, static_cast<void*>(&item),
                                   static_cast<void*>(&column), };
    QMetaObject::activate(m_widget->list(), signalIndex, args.data());
}

QMenu* PlaylistListWidgetTest::findOpenMenu() const
{
    const QList<QMenu*> menus = m_widget->findChildren<QMenu*>();
    for (QMenu *menu : menus)
        if (menu->isVisible())
            return menu;
    return nullptr;
}

QAction* PlaylistListWidgetTest::findAction(const QMenu *menu, const QString &text) const
{
    for (QAction *a : menu->actions())
        if (a->text() == text)
            return a;
    return nullptr;
}

// Right-clicks the list's rows: sends the context-menu event the way Qt
// does after a right-button release. The event targets the viewport (where
// the click lands); the scroll-area machinery routes it to the view's
// policy handling.
void PlaylistListWidgetTest::sendContextMenu()
{
    QWidget *viewport = m_widget->list()->viewport();
    const QPoint pos = viewport->rect().center();
    QContextMenuEvent cme(QContextMenuEvent::Mouse, pos,
                          viewport->mapToGlobal(pos), Qt::NoModifier);
    QCoreApplication::sendEvent(viewport, &cme);
    QCoreApplication::processEvents();
}

QPoint PlaylistListWidgetTest::rowPoint(int row, bool lowerHalf) const
{
    QTreeWidget *list = m_widget->list();
    // visualItemRect is in the viewport's coordinates — where the drag
    // events land.
    const QRect rect = list->visualItemRect(list->topLevelItem(row));
    const int y = rect.top() + (lowerHalf ? rect.height() - 2 : 2);
    return { rect.left() + 5, y };
}

int PlaylistListWidgetTest::rowHeight(int row) const
{
    QTreeWidget *list = m_widget->list();
    return list->visualItemRect(list->topLevelItem(row)).height();
}

bool PlaylistListWidgetTest::sendListDrop(const QPoint &pos, QMimeData *mime)
{
    QTreeWidget *list = m_widget->list();
    QDragEnterEvent enter(pos, Qt::CopyAction, mime, Qt::NoButton,
                          Qt::NoModifier);
    QCoreApplication::sendEvent(list->viewport(), &enter);
    if (!enter.isAccepted())
        return false;
    QDropEvent drop(pos, Qt::CopyAction, mime, Qt::NoButton,
                    Qt::NoModifier);
    QCoreApplication::sendEvent(list->viewport(), &drop);
    return true;
}


void PlaylistListWidgetTest::hasPlayingNameTracksColumns()
{
    QTreeWidget *list = m_widget->list();
    QCOMPARE(list->columnCount(), 3);
    QCOMPARE(list->headerItem()->text(0), QString("Playing"));
    QCOMPARE(list->headerItem()->text(1), QString("Name"));
    QCOMPARE(list->headerItem()->text(2), QString("Tracks"));
}

void PlaylistListWidgetTest::showsOneRowPerPlaylist()
{
    // The manager's default playlist, empty.
    QCOMPARE(m_widget->list()->topLevelItemCount(), 1);
    QCOMPARE(rowText(0, PlaylistListWidget::Name), QString("Playlist"));
    QCOMPARE(rowText(0, PlaylistListWidget::Tracks), QString("0"));

    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->setCurrentPlaylist(road);
    m_pl->addItems({ makeItem(1, "Sun").id, makeItem(2, "Moon").id });

    QCOMPARE(m_widget->list()->topLevelItemCount(), 2);
    QCOMPARE(rowText(1, PlaylistListWidget::Name), QString("Road"));
    QCOMPARE(rowText(1, PlaylistListWidget::Tracks), QString("2"));
}

void PlaylistListWidgetTest::playlistsChangedRebuildsList()
{
    const quint64 road = m_pl->addPlaylist("Road");
    QCOMPARE(m_widget->list()->topLevelItemCount(), 2);

    m_pl->removePlaylist(road);
    QCOMPARE(m_widget->list()->topLevelItemCount(), 1);
    QCOMPARE(rowText(0, PlaylistListWidget::Name), QString("Playlist"));
}

void PlaylistListWidgetTest::playlistChangedUpdatesTrackCount()
{
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->setCurrentPlaylist(road);
    m_pl->addItems({ makeItem(1, "A").id, makeItem(2, "B").id });
    QCOMPARE(rowText(1, PlaylistListWidget::Tracks), QString("2"));

    m_pl->addItems({ makeItem(3, "C").id });
    QCOMPARE(rowText(1, PlaylistListWidget::Tracks), QString("3"));

    m_pl->removeAt(0);
    QCOMPARE(rowText(1, PlaylistListWidget::Tracks), QString("2"));
}

void PlaylistListWidgetTest::trackCountUpdateKeepsSelection()
{
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->setCurrentPlaylist(road);
    m_pl->addItems({ makeItem(1, "A").id });

    m_widget->list()->setCurrentItem(rowForPlaylist(road));

    // A content update refreshes the row in place: the selection survives.
    m_pl->addItems({ makeItem(2, "B").id });

    QCOMPARE(m_widget->list()->currentItem(), rowForPlaylist(road));
    QCOMPARE(rowText(1, PlaylistListWidget::Tracks), QString("2"));
}

void PlaylistListWidgetTest::playingColumnMarksCurrentPlaylistOnlyWhilePlaying()
{
    const quint64 def = m_pl->currentPlaylistId();
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->setCurrentPlaylist(road);
    m_pl->addItems({ makeItem(1, "Sun").id });
    m_pl->setCurrentPlaylist(def);

    // Nothing is playing: no row carries an icon.
    for (int i = 0; i < m_widget->list()->topLevelItemCount(); ++i)
        QVERIFY(m_widget->list()->topLevelItem(i)->icon(PlaylistListWidget::Playing).isNull());

    m_pm->playFromPlaylist(road, 0);

    // The row of the playlist that is playing carries the play icon...
    QCOMPARE(iconImage(rowForPlaylist(road)->icon(PlaylistListWidget::Playing)),
             iconImage(QIcon(QStringLiteral(":/icons/play.svg"))));
    // ...the other rows show none.
    QVERIFY(rowForPlaylist(def)->icon(PlaylistListWidget::Playing).isNull());

    // Pausing clears the icon, resuming brings it back, stop clears it.
    m_pm->pause();
    QVERIFY(rowForPlaylist(road)->icon(PlaylistListWidget::Playing).isNull());

    m_pm->togglePlayPause();
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(iconImage(rowForPlaylist(road)->icon(PlaylistListWidget::Playing)),
             iconImage(QIcon(QStringLiteral(":/icons/play.svg"))));

    m_pm->stop();
    QVERIFY(rowForPlaylist(road)->icon(PlaylistListWidget::Playing).isNull());
}

void PlaylistListWidgetTest::rebuildKeepsPlayingMarker()
{
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->setCurrentPlaylist(road);
    m_pl->addItems({ makeItem(1, "Sun").id });

    m_pm->playFromPlaylist(road, 0);

    // Adding a playlist rebuilds all the rows.
    m_pl->addPlaylist("Work");

    QCOMPARE(iconImage(rowForPlaylist(road)->icon(PlaylistListWidget::Playing)),
             iconImage(QIcon(QStringLiteral(":/icons/play.svg"))));
    for (int i = 0; i < m_widget->list()->topLevelItemCount(); ++i) {
        if (m_widget->list()->topLevelItem(i) != rowForPlaylist(road))
            QVERIFY(m_widget->list()->topLevelItem(i)->icon(PlaylistListWidget::Playing).isNull());
    }
}

void PlaylistListWidgetTest::newWidgetShowsIconWhilePlaying()
{
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->setCurrentPlaylist(road);
    m_pl->addItems({ makeItem(1, "Sun").id });

    m_pm->playFromPlaylist(road, 0);

    // A widget created while a track is playing must start with the icon
    // on the right row (the signals only fire for future changes).
    PlaylistListWidget other;
    QCOMPARE(other.list()->topLevelItemCount(), 2);
    QCOMPARE(iconImage(other.list()->topLevelItem(1)->icon(PlaylistListWidget::Playing)),
             iconImage(QIcon(QStringLiteral(":/icons/play.svg"))));
    QVERIFY(other.list()->topLevelItem(0)->icon(PlaylistListWidget::Playing).isNull());
}

void PlaylistListWidgetTest::doubleClickSwitchesCurrentAndPlays()
{
    const quint64 def = m_pl->currentPlaylistId();
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->setCurrentPlaylist(road);
    m_pl->addItems({ makeItem(1, "Sun").id, makeItem(2, "Moon").id });
    m_pl->setCurrentPlaylist(def);

    doubleClickRow(rowForPlaylist(road));

    QCOMPARE(m_pl->currentPlaylistId(), road);
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(m_pm->playingItem().id, 1ULL);
}

void PlaylistListWidgetTest::doubleClickPlaysFromPlaylistCursor()
{
    // The playlist's own cursor is valid: the double-click resumes from it
    // instead of starting over.
    const quint64 def = m_pl->currentPlaylistId();
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->setCurrentPlaylist(road);
    m_pl->addItems({ makeItem(1, "Sun").id, makeItem(2, "Moon").id, makeItem(3, "Star").id });
    m_pl->setCurrentIndex(2);
    m_pl->setCurrentPlaylist(def);

    doubleClickRow(rowForPlaylist(road));

    QCOMPARE(m_pl->currentPlaylistId(), road);
    QCOMPARE(m_pl->currentIndex(), 2);
    QCOMPARE(m_pm->playingItem().id, 3ULL);
}

void PlaylistListWidgetTest::doubleClickEmptyPlaylistStops()
{
    m_pl->addItems({ makeItem(1, "Sun").id });
    m_pm->playFromPlaylist(m_pl->currentPlaylistId(), 0);
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);

    const quint64 empty = m_pl->addPlaylist("Empty");
    doubleClickRow(rowForPlaylist(empty));

    QCOMPARE(m_pl->currentPlaylistId(), empty);
    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);
}

void PlaylistListWidgetTest::contextMenuNewPlaylistAsksNameAndAdds()
{
    static_cast<TestPlaylistListWidget*>(m_widget)->m_name = "My List";

    LayoutManager::getInstance()->setEditMode(false);

    sendContextMenu();

    QMenu *menu = findOpenMenu();
    if (menu == nullptr) {
        QFAIL("context menu did not appear");
        return;
    }
    QAction *add = findAction(menu, "New playlist");
    if (add == nullptr) {
        QFAIL("no \"New playlist\" action");
        return;
    }
    QVERIFY(add->isEnabled());
    add->trigger();
    menu->hide();
    QCoreApplication::processEvents();

    QCOMPARE(m_pl->playlistCount(), 2);
    QCOMPARE(m_pl->playlists().at(1).name, QString("My List"));
    QCOMPARE(m_widget->list()->topLevelItemCount(), 2);
    QCOMPARE(rowText(1, PlaylistListWidget::Name), QString("My List"));
}

void PlaylistListWidgetTest::contextMenuNewPlaylistBlankNameAddsNothing()
{
    static_cast<TestPlaylistListWidget*>(m_widget)->m_name = "   ";

    LayoutManager::getInstance()->setEditMode(false);

    sendContextMenu();

    QMenu *menu = findOpenMenu();
    if (menu == nullptr) {
        QFAIL("context menu did not appear");
        return;
    }
    QAction *add = findAction(menu, "New playlist");
    if (add == nullptr) {
        QFAIL("no \"New playlist\" action");
        return;
    }
    add->trigger();
    menu->hide();
    QCoreApplication::processEvents();

    QCOMPARE(m_pl->playlistCount(), 1);
    QCOMPARE(m_widget->list()->topLevelItemCount(), 1);
}

void PlaylistListWidgetTest::contextMenuDeleteRemovesSelected()
{
    const quint64 def = m_pl->currentPlaylistId();
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->addPlaylist("Work");

    LayoutManager::getInstance()->setEditMode(false);
    m_widget->list()->setCurrentItem(rowForPlaylist(road));

    sendContextMenu();

    QMenu *menu = findOpenMenu();
    if (menu == nullptr) {
        QFAIL("context menu did not appear");
        return;
    }
    QAction *remove = findAction(menu, "Delete playlist");
    if (remove == nullptr) {
        QFAIL("no \"Delete playlist\" action");
        return;
    }
    QVERIFY(remove->isEnabled());
    remove->trigger();
    menu->hide();
    QCoreApplication::processEvents();

    QCOMPARE(m_pl->playlistIndexOf(road), -1);
    QCOMPARE(m_pl->playlistCount(), 2);
    QCOMPARE(m_widget->list()->topLevelItemCount(), 2);
    // The other playlists survive...
    QVERIFY(rowForPlaylist(def) != nullptr);
    // ...and removing the non-current playlist does not touch playback's
    // current playlist.
    QCOMPARE(m_pl->currentPlaylistId(), def);
}

void PlaylistListWidgetTest::contextMenuDeleteAbsentWithoutSelection()
{
    // Nothing is selected, so "Delete playlist" is not offered.
    LayoutManager::getInstance()->setEditMode(false);
    m_widget->list()->clearSelection();
    m_widget->list()->setCurrentItem(nullptr);

    sendContextMenu();

    QMenu *menu = findOpenMenu();
    if (menu == nullptr) {
        QFAIL("context menu did not appear");
        return;
    }
    QVERIFY(findAction(menu, "New playlist") != nullptr);
    QVERIFY(findAction(menu, "Delete playlist") == nullptr);
}

// The slot does not gate on edit mode (7d): edit-mode right-clicks are
// owned by the container's filter (the flip-flop), and its pass-through
// reaches this slot exactly as in normal mode — a widget without a
// container (no filter) offers its menu in edit mode too.
void PlaylistListWidgetTest::contextMenuSlotShowsInEditMode()
{
    LayoutManager::getInstance()->setEditMode(true);
    m_widget->list()->setCurrentItem(rowForPlaylist(m_pl->currentPlaylistId()));

    sendContextMenu();

    QMenu *menu = findOpenMenu();
    if (menu == nullptr) {
        QFAIL("context menu did not appear");
        return;
    }
    menu->hide();
}

void PlaylistListWidgetTest::rowNameCarriesDirtyMarker()
{
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->markImported(road, QStringLiteral("file:///tmp/road.m3u"));
    QCOMPARE(rowText(1, PlaylistListWidget::Name), QString("Road"));

    // A content mutation marks it dirty: the * appears.
    m_pl->setCurrentPlaylist(road);
    m_pl->addItems({ makeItem(1, "A").id });
    QCOMPARE(rowText(1, PlaylistListWidget::Name), QString("Road*"));

    // A save clears the marker.
    m_pl->savePlaylist(road, m_dir->path() + "/road.m3u");
    QCOMPARE(rowText(1, PlaylistListWidget::Name), QString("Road"));
}

void PlaylistListWidgetTest::contextMenuRenameRenamesSelected()
{
    const quint64 road = m_pl->addPlaylist("Road");
    static_cast<TestPlaylistListWidget*>(m_widget)->m_rename = "New Name";

    LayoutManager::getInstance()->setEditMode(false);
    m_widget->list()->setCurrentItem(rowForPlaylist(road));

    sendContextMenu();

    QMenu *menu = findOpenMenu();
    if (menu == nullptr) {
        QFAIL("context menu did not appear");
        return;
    }
    QAction *rename = findAction(menu, "Rename...");
    if (rename == nullptr) {
        QFAIL("no \"Rename...\" action");
        return;
    }
    rename->trigger();
    menu->hide();
    QCoreApplication::processEvents();

    QCOMPARE(m_pl->playlist(road).name, QString("New Name"));
    QCOMPARE(rowText(1, PlaylistListWidget::Name), QString("New Name"));
}

void PlaylistListWidgetTest::contextMenuExportWritesFile()
{
    const quint64 road = m_pl->addPlaylist("Road");
    m_pl->setCurrentPlaylist(road);
    const MediaItem a = makeItem(1, "A");
    const MediaItem b = makeItem(2, "B");
    m_pl->addItems({ a.id, b.id });

    const QString out = m_dir->path() + "/export.m3u";
    static_cast<TestPlaylistListWidget*>(m_widget)->m_exportPath = out;

    LayoutManager::getInstance()->setEditMode(false);
    m_widget->list()->setCurrentItem(rowForPlaylist(road));

    sendContextMenu();

    QMenu *menu = findOpenMenu();
    if (menu == nullptr) {
        QFAIL("context menu did not appear");
        return;
    }
    QAction *exportAction = findAction(menu, "Export to file...");
    if (exportAction == nullptr) {
        QFAIL("no \"Export to file...\" action");
        return;
    }
    exportAction->trigger();
    menu->hide();
    QCoreApplication::processEvents();

    PlaylistFile file;
    QVERIFY(parsePlaylistFile(out, file));
    QCOMPARE(file.entries, QList<QUrl>({ a.source, b.source }));
}

void PlaylistListWidgetTest::contextMenuSaveDisabledForPlainPlaylist()
{
    const quint64 road = m_pl->addPlaylist("Road");

    LayoutManager::getInstance()->setEditMode(false);
    m_widget->list()->setCurrentItem(rowForPlaylist(road));

    sendContextMenu();

    QMenu *menu = findOpenMenu();
    if (menu == nullptr) {
        QFAIL("context menu did not appear");
        return;
    }
    // Not imported (no source file): a save has no target.
    QAction *save = findAction(menu, "Save");
    QVERIFY(save != nullptr);
    QVERIFY(!save->isEnabled()); // NOLINT(clang-analyzer-core.CallAndMessage)
    menu->hide();
    QCoreApplication::processEvents();
}

void PlaylistListWidgetTest::contextMenuSaveWritesFileAndClearsDirty()
{
    const quint64 road = m_pl->addPlaylist("Road");
    const QString source = m_dir->path() + "/road.m3u";
    m_pl->markImported(road, QUrl::fromLocalFile(source).toString());
    m_pl->setCurrentPlaylist(road);
    const MediaItem a = makeItem(1, "A");
    m_pl->addItems({ a.id });
    QVERIFY(m_pl->playlist(road).dirty);

    LayoutManager::getInstance()->setEditMode(false);
    m_widget->list()->setCurrentItem(rowForPlaylist(road));

    sendContextMenu();

    QMenu *menu = findOpenMenu();
    if (menu == nullptr) {
        QFAIL("context menu did not appear");
        return;
    }
    QAction *save = findAction(menu, "Save");
    if (save == nullptr) {
        QFAIL("no \"Save\" action");
        return;
    }
    QVERIFY(save->isEnabled());
    save->trigger();
    menu->hide();
    QCoreApplication::processEvents();

    QVERIFY(!m_pl->playlist(road).dirty);
    PlaylistFile file;
    QVERIFY(parsePlaylistFile(source, file));
    QCOMPARE(file.entries, QList<QUrl>({ a.source }));
}

void PlaylistListWidgetTest::dragRowReordersPlaylists()
{
    const quint64 def = m_pl->currentPlaylistId();
    const quint64 road = m_pl->addPlaylist("Road");
    const quint64 sea = m_pl->addPlaylist("Sea");
    Q_UNUSED(sea)
    // Rows: 0=Playlist, 1=Road, 2=Sea

    PlaylistListView *list = m_widget->list();

    // The drag payload carries the playlist's id.
    QMimeData *md = list->dragMimeData(rowForPlaylist(road));
    QVERIFY(md != nullptr);
    QCOMPARE(md->data(PlaylistListView::playlistMimeType()), QByteArray::number(road));

    // Dropping Road on row 0's upper half moves it to the front.
    QVERIFY(sendListDrop(rowPoint(0), md));
    delete md;

    // The manager reorders (its order drives the rows).
    QCOMPARE(m_pl->playlists().at(0).id, road);
    QCOMPARE(m_pl->playlists().at(1).id, def);
    QCOMPARE(m_pl->playlists().at(2).id, sea);
}

void PlaylistListWidgetTest::dragRowToEmptyAreaAppends()
{
    const quint64 def = m_pl->currentPlaylistId();
    m_pl->addPlaylist("Road");

    PlaylistListView *list = m_widget->list();

    // A drop on the empty area below the last row appends to the end.
    const int last = list->topLevelItemCount() - 1;
    const QPoint pos = rowPoint(last) + QPoint(0, rowHeight(last) + 10);
    QMimeData *md = list->dragMimeData(rowForPlaylist(def));
    QVERIFY(sendListDrop(pos, md));
    delete md;

    QCOMPARE(m_pl->playlists().last().id, def);
}

void PlaylistListWidgetTest::dropItemsOnRowAppendsToThatPlaylist()
{
    const quint64 road = m_pl->addPlaylist("Road");
    const MediaItem a = makeItem(1, "Sun");
    const MediaItem b = makeItem(2, "Moon");
    // Rows: 0=Playlist, 1=Road

    // The item drag's payload, built the way a media view builds it.
    MediaListModel model;
    model.setItemIds({ a.id, b.id });
    model.setOriginId(MediaListModel::libraryOrigin());
    QModelIndexList indexes;
    indexes << model.index(0, 0) << model.index(1, 0);
    QMimeData *md = model.mimeData(indexes);
    QVERIFY(md != nullptr);

    // Dropping on the Road row appends to the Road playlist — a drop on
    // the row's lower half still names the row under the pointer.
    QVERIFY(sendListDrop(rowPoint(1, true), md));
    delete md;

    QCOMPARE(m_pl->playlist(road).items, QList<quint64>({ a.id, b.id }));
    // The other playlist is untouched.
    QCOMPARE(m_pl->currentPlaylistId(), m_pl->playlists().at(0).id);
    QCOMPARE(m_pl->playlist(m_pl->playlists().at(0).id).items.size(), 0);
}

void PlaylistListWidgetTest::dropFilesOnRowImportsIntoThatPlaylist()
{
    const quint64 road = m_pl->addPlaylist("Road");
    const MediaItem a = makeItem(1, "Sun");
    // Rows: 0=Playlist, 1=Road

    QMimeData md;
    md.setUrls({ a.source });
    QVERIFY(sendListDrop(rowPoint(1), &md));

    test::waitUntil([this] { return !m_pl->importing(); });

    // A source the store already holds is reused without a file read.
    QCOMPARE(m_pl->playlist(road).items, QList<quint64>({ a.id }));
}

void PlaylistListWidgetTest::dropOnEmptyAreaRefused()
{
    const quint64 road = m_pl->addPlaylist("Road");
    const MediaItem a = makeItem(1, "Sun");
    // Rows: 0=Playlist, 1=Road

    QTreeWidget *list = m_widget->list();
    const int last = list->topLevelItemCount() - 1;
    const QPoint pos = rowPoint(last) + QPoint(0, rowHeight(last) + 10);

    // An item drag over the empty area names no target row: refused.
    MediaListModel model;
    model.setItemIds({ a.id });
    QModelIndexList indexes;
    indexes << model.index(0, 0);
    QMimeData *md = model.mimeData(indexes);
    QVERIFY(!sendListDrop(pos, md));
    delete md;

    // A file drop over the empty area is refused as well.
    QMimeData files;
    files.setUrls({ a.source });
    QVERIFY(!sendListDrop(pos, &files));

    // The playlists are untouched.
    QCOMPARE(m_pl->playlist(road).items.size(), 0);
}

void PlaylistListWidgetTest::moveRowToIgnoresOutOfRangeIndex()
{
    const quint64 def = m_pl->currentPlaylistId();

    m_widget->moveRowTo(def, 5);
    m_widget->moveRowTo(def, -1);

    QCOMPARE(m_pl->playlists().at(0).id, def);
}

void PlaylistListWidgetTest::registeredInLayoutManager()
{
    WidgetConfiguration cfg;
    BaseWidget* w = LayoutManager::getInstance()->createWidget(PlaylistListWidget::id, cfg);

    QVERIFY(w != nullptr);
    QCOMPARE(w->widgetId(), PlaylistListWidget::id);
    QCOMPARE(w->widgetTitle(), PlaylistListWidget::title);
    delete w;
}
}


QTEST_MAIN(PlaylistListWidgetTest)
#include "playlistlistwidget_test.moc"
