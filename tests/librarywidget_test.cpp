#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <QApplication>
#include <QAction>
#include <QContextMenuEvent>
#include <QDir>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSize>
#include <QStackedWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <array>

#include <memory>

#include "currentplaylistwidget.h"
#include "layoutmanager.h"
#include "librarywidget.h"
#include "mediafilterproxy.h"
#include "mediagroupview.h"
#include "medialistmodel.h"
#include "medialistview.h"
#include "mediaitemstore.h"
#include "sourcemanager.h"
#include "taggedfile.h"
#include "testsupport.h"
#include "urlimportdialog.h"

namespace {

class TestLibraryWidget : public LibraryWidget
{
public:
    using LibraryWidget::LibraryWidget;

    QStringList chooseFiles() override { return m_chosenFiles; }
    QString chooseDirectory() override { return m_chosenDir; }

    // Forward the protected base-class setter for the tests.
    void showToolbar(bool visible) { BaseWidget::setToolbarVisible(visible); }

    void editMetadata(const QList<quint64> &ids) override
    {
        m_editMetadataIds = ids;
    }

    void editSingleMetadata(quint64 id) override
    {
        m_editSingleMetadataId = id;
    }

    QStringList m_chosenFiles;
    QString m_chosenDir;
    QList<quint64> m_editMetadataIds;
    quint64 m_editSingleMetadataId = 0;
};

// A provider that claims the URLs it is told to and answers every
// metadata fetch synchronously with the configured TrackMeta.
class UrlTestProvider : public SourceProvider
{
    Q_OBJECT
public:
    explicit UrlTestProvider(QObject *parent = nullptr) : SourceProvider(parent) {}

    QString id() const override { return QStringLiteral("urltest"); }
    QString title() const override { return QStringLiteral("Url Test"); }
    bool accepts(const QUrl &url) const override
    {
        return m_urls.contains(url.toString());
    }
    void fetchMetadata(const QUrl &url, MetadataCallback cb) override
    {
        Q_UNUSED(url);
        cb(m_meta);
    }
    void fetchList(const QUrl &url, const ListCallback &cb) override
    {
        const auto it = m_lists.constFind(url.toString());
        cb(it == m_lists.constEnd() ? ListEntries() : it.value());
    }

    QStringList m_urls;
    TrackMeta m_meta;
    QHash<QString, ListEntries> m_lists;
};

class LibraryWidgetTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void showsLibraryItems();
    void playingColumnUnavailable();
    void updatesOnAddAndRemove();
    void addFilesAddsChosenFiles();
    void addFilesFlattensChosenPlaylistFile();
    void addFolderScansChosenDirectory();
    void addUrlAddsRemoteStreamAndLocalPath();
    void addUrlAddsProviderSource();
    void addUrlExpandsConfirmedList();
    void addUrlUncheckedListImportsNothing();
    void addUrlBailingOnReviewImportsNothing();
    void addUrlSecondTriggerRaisesOpenWizard();
    void removeButtonRemovesSelected();
    void deleteKeyRemovesSelected();
    void deleteKeyRemovesSelectedFromGroupView();
    void deleteKeyWithoutSelectionRemovesNothing();
    void toolbarVisibleByDefaultWithAddMenu();
    void widgetContextMenuTogglesToolbar();
    void widgetContextMenuMovesToolbarToBottom();
    void toolbarStateSerializedOnlyWhenNonDefault();
    void doubleClickAppendsToCurrentPlaylist();
    void doubleClickOfExistingTrackAppendsCopy();
    void columnStateStoredAndRestored();
    void searchBoxOnToolbar();
    void searchFiltersVisibleColumns();
    void searchMatchesArtistColumn();
    void searchMatchesHiddenColumns();
    void searchSurvivesLibraryUpdates();
    void noResyncButtonOnToolbar();
    void busyDisablesButtonsAndShowsSpinner();
    void registeredInLayoutManager();
    void contextMenuOffersEditMetadata();
    void contextMenuBulkEditPassesSelectedIds();
    void contextMenuEditMetadataDisabledWithNoSelection();
    void contextMenuSlotShowsInEditMode();
    void dropFilesImportsIntoLibrary();
    void dropPlaylistFileImportsItsEntries();
    void dropUrlOpensPrefilledWizard();
    void dropUrlWithListReviewsVideos();
    void dropUrlsWhileWizardOpenRaisesIt();

    void viewButtonsOnToolbar();
    void viewButtonsSwitchGrouping();
    void artistTreeShowsGroupRows();
    void albumTreeShowsGroupRows();
    void artistAlbumTreeShowsTupleGroups();
    void viewMenuInWidgetContextMenu();
    void viewModeStoredOnlyWhenNonDefault();
    void viewModeAndSortStoredAndRestored();
    void headerClickSortsRows();
    void headerClickSortNumericColumn();
    void groupDoubleClickAppendsAllTracks();
    void groupRemoveRemovesWholeGroup();
    void groupDragPayloadExpandsToTracks();
    void groupDropIntoPlaylistAddsAllTracks();
    void searchInArtistTree();

private:
    MediaItem addTempFile(const QString &name);
    MediaItem addFileAndWait(const QUrl &url);
    // A real tagged file in the library (QSKIPs when ffmpeg is missing).
    void addTaggedFile(const QString &name, const QString &title,
                       const QString &artist, const QString &album);
    // The title of the row a List-mode view row shows (resolved through
    // the proxy and the store).
    QString titleAt(MediaListView *list, int row);
    QMenu* openHeaderMenu(MediaListView *list);
    void triggerHeaderAction(MediaListView *list, const QString &label);
    void sendRowContextMenu(QAbstractItemView *view);
    QMenu* findRowMenu(QWidget *widget);
    void clickHeaderSection(MediaListView *list, int logical);
    // Selects the view row (a group view has no selectRow of its own).
    void selectViewRow(QWidget *view, const QModelIndex &index);
    // The "Add by URL" wizard the action opens (non-modal, a child of the
    // widget): open it, submit the pasted lines.
    UrlImportDialog* openUrlWizard(TestLibraryWidget &w);
    void submitWizard(UrlImportDialog *dialog, const QStringList &lines);

    LibraryManager* m_manager;
    LibraryWidget* m_widget;
    std::unique_ptr<QTemporaryDir> m_dir;
};

void LibraryWidgetTest::init()
{
    m_manager = LibraryManager::getInstance();
    m_dir = std::make_unique<QTemporaryDir>();
    // The library persists through the item store (write-through).
    MediaItemStore::getInstance()->setStoragePath(m_dir->path() + "/doob.db");
    MediaItemStore::getInstance()->clear();
    m_manager->clear();

    m_widget = new TestLibraryWidget();
    m_widget->show();
}

void LibraryWidgetTest::cleanup()
{
    delete m_widget;
    m_manager->clear();
    PlaylistManager::getInstance()->reset();
    MediaItemStore::getInstance()->clear();
}

UrlImportDialog* LibraryWidgetTest::openUrlWizard(TestLibraryWidget &w)
{
    w.findChild<QAction*>("addUrlAction")->trigger();
    return w.findChild<UrlImportDialog*>();
}

void LibraryWidgetTest::submitWizard(UrlImportDialog *dialog,
                                     const QStringList &lines)
{
    dialog->findChild<QPlainTextEdit*>("urlInput")->setPlainText(
        lines.join('\n'));
    dialog->findChild<QPushButton*>("okButton")->click();
}

MediaItem LibraryWidgetTest::addTempFile(const QString &name)
{
    const QString path = m_dir->path() + "/" + name;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("addTempFile: cannot open %s", qPrintable(path));
        return {};
    }
    f.write("\x00\x01\x02");
    f.close();
    return addFileAndWait(QUrl::fromLocalFile(path));
}

// addFile is asynchronous (the tag reading runs on a worker thread): the
// shared add + itemAdded + idle wait.
MediaItem LibraryWidgetTest::addFileAndWait(const QUrl &url)
{
    return test::addLibraryFileAndWait(m_manager, url);
}

void LibraryWidgetTest::showsLibraryItems()
{
    addTempFile("First Track.mp3");
    addTempFile("Second Track.flac");

    MediaListView *list = m_widget->listView();
    QCOMPARE(list->rowCount(), 2);
    QCOMPARE(titleAt(list, 0), QString("First Track"));
    QCOMPARE(titleAt(list, 1), QString("Second Track"));
}

void LibraryWidgetTest::playingColumnUnavailable()
{
    addTempFile("Some Track.mp3");

    MediaListView *list = m_widget->listView();
    // The model keeps all columns; the view makes the playlist-only one
    // unavailable, so it stays hidden and cannot be re-enabled from the
    // header menu.
    QCOMPARE(list->listModel()->columnCount(), MediaListModel::ColumnCount);
    QVERIFY(!list->columnAvailable(MediaListModel::Playing));
    QVERIFY(list->header()->isSectionHidden(MediaListModel::Playing));
    QVERIFY(!list->header()->isSectionHidden(MediaListModel::Title));

    // The header menu offers no entry for it.
    QHeaderView *header = list->header();
    QContextMenuEvent event(QContextMenuEvent::Mouse,
                            header->viewport()->rect().center(),
                            header->viewport()->rect().center());
    QCoreApplication::sendEvent(header->viewport(), &event);
    QMenu *menu = list->findChild<QMenu*>();
    if (menu == nullptr) {
        QFAIL("header context menu did not appear");
        return;
    }
    for (QAction *a : menu->actions())
        QVERIFY(a->text() != "Status");
    menu->hide();
    QCoreApplication::processEvents();
}

void LibraryWidgetTest::updatesOnAddAndRemove()
{
    const MediaItem a = addTempFile("a.mp3");
    QCOMPARE(m_widget->listView()->rowCount(), 1);

    m_manager->removeItem(a.id);
    QCOMPARE(m_widget->listView()->rowCount(), 0);
}

void LibraryWidgetTest::addFilesAddsChosenFiles()
{
    const QString f1 = m_dir->path() + "/picked1.mp3";
    const QString f2 = m_dir->path() + "/picked2.flac";
    {
        QFile x(f1);
        QVERIFY(x.open(QIODevice::WriteOnly));
        x.write("x");
    }
    {
        QFile x(f2);
        QVERIFY(x.open(QIODevice::WriteOnly));
        x.write("x");
    }

    TestLibraryWidget w;
    w.m_chosenFiles = { f1, f2 };
    w.show();

    w.findChild<QAction*>("addFilesAction")->trigger();

    // The import runs on a worker thread; wait for both items to arrive.
    QTRY_COMPARE_WITH_TIMEOUT(m_manager->count(), 2, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(w.listView()->rowCount(), 2, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!m_manager->busy(), 10000);
}

void LibraryWidgetTest::addFilesFlattensChosenPlaylistFile()
{
    // A playlist file picked in the chooser imports as its entries —
    // the same flattening a file drop does (the file itself never
    // becomes an item).
    const QString f1 = m_dir->path() + "/picked1.mp3";
    const QString f2 = m_dir->path() + "/picked2.flac";
    {
        QFile x(f1);
        QVERIFY(x.open(QIODevice::WriteOnly));
        x.write("x");
    }
    {
        QFile x(f2);
        QVERIFY(x.open(QIODevice::WriteOnly));
        x.write("x");
    }
    const QString list = m_dir->path() + "/mix.m3u";
    {
        QFile x(list);
        QVERIFY(x.open(QIODevice::WriteOnly));
        x.write((f1 + "\n" + f2 + "\n").toUtf8());
    }

    TestLibraryWidget w;
    w.m_chosenFiles = { list };
    w.show();

    w.findChild<QAction*>("addFilesAction")->trigger();

    QTRY_COMPARE_WITH_TIMEOUT(m_manager->count(), 2, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(w.listView()->rowCount(), 2, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!m_manager->busy(), 10000);
}

void LibraryWidgetTest::addFolderScansChosenDirectory()
{
    QVERIFY(QDir(m_dir->path()).mkdir("music"));
    QVERIFY(QDir(m_dir->path() + "/music").mkdir("sub"));
    {
        QFile x(m_dir->path() + "/music/song1.mp3");
        QVERIFY(x.open(QIODevice::WriteOnly));
        x.write("x");
    }
    {
        QFile x(m_dir->path() + "/music/sub/song2.ogg");
        QVERIFY(x.open(QIODevice::WriteOnly));
        x.write("x");
    }

    TestLibraryWidget w;
    w.m_chosenDir = m_dir->path() + "/music";
    w.show();

    w.findChild<QAction*>("addFolderAction")->trigger();

    // The scan runs on a worker thread; wait for both items to arrive.
    QTRY_COMPARE_WITH_TIMEOUT(m_manager->count(), 2, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(w.listView()->rowCount(), 2, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!m_manager->busy(), 10000);
}

void LibraryWidgetTest::addUrlAddsRemoteStreamAndLocalPath()
{
    // A plain http(s) stream needs no metadata read (added synchronously,
    // kind Remote); a local file path pasted as a URL goes through the
    // worker like a chosen file. No provider claims anything: the wizard
    // takes the fast path (no review).
    const QString path = m_dir->path() + "/pasted.mp3";
    {
        QFile x(path);
        QVERIFY(x.open(QIODevice::WriteOnly));
        x.write("x");
    }

    TestLibraryWidget w;
    w.show();

    UrlImportDialog* dialog = openUrlWizard(w);
    QVERIFY(dialog != nullptr);
    submitWizard(dialog, { "https://example.com/audio/stream.mp3", path });

    QTRY_COMPARE_WITH_TIMEOUT(m_manager->count(), 2, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!m_manager->busy(), 10000);

    const MediaItem remote = m_manager->findBySource(QUrl("https://example.com/audio/stream.mp3"));
    QVERIFY(remote.id != 0);
    QCOMPARE(remote.kind, MediaItem::Remote);
    const MediaItem local = m_manager->findBySource(QUrl::fromLocalFile(path));
    QVERIFY(local.id != 0);
    QCOMPARE(local.kind, MediaItem::Local);
}

void LibraryWidgetTest::addUrlAddsProviderSource()
{
    // A source a registered provider claims is fetched from the provider
    // and added as a Plugin-kind item with the claimant recorded. No list
    // behind it: the wizard takes the fast path.
    UrlTestProvider provider(this);
    const QUrl source("https://music.example.com/watch?v=1");
    provider.m_urls = { source.toString() };
    provider.m_meta.title = "Provider Track";
    QVERIFY(SourceManager::getInstance()->registerProvider(&provider));

    TestLibraryWidget w;
    w.show();

    UrlImportDialog* dialog = openUrlWizard(w);
    QVERIFY(dialog != nullptr);
    submitWizard(dialog, { source.toString() });

    QTRY_COMPARE_WITH_TIMEOUT(m_manager->count(), 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!m_manager->busy(), 10000);

    const MediaItem item = m_manager->findBySource(source);
    QCOMPARE(item.kind, MediaItem::Plugin);
    QCOMPARE(item.provider, QString("urltest"));
    QCOMPARE(item.title, QString("Provider Track"));

    SourceManager::getInstance()->unregisterProvider(&provider);
}

void LibraryWidgetTest::addUrlExpandsConfirmedList()
{
    // A pasted link with a list= argument: the list is discovered (its
    // videos show in the review with the list's own metadata), and the
    // checked videos are imported, in list order.
    UrlTestProvider provider(this);
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    // The provider claims the list link and the list's videos (the
    // expansion imports the videos, not the list link).
    provider.m_urls = { listUrl.toString(),
                        "https://music.example.com/watch?v=1",
                        "https://music.example.com/watch?v=2", };
    provider.m_meta.title = "Provider Track";
    SourceProvider::ListEntries list;
    list.title = "The List";
    SourceProvider::ListVideo first(QUrl("https://music.example.com/watch?v=1"));
    first.title = "First";
    SourceProvider::ListVideo second(QUrl("https://music.example.com/watch?v=2"));
    second.title = "Second";
    list.videos = { first, second };
    provider.m_lists[listUrl.toString()] = list;
    QVERIFY(SourceManager::getInstance()->registerProvider(&provider));

    TestLibraryWidget w;
    w.show();

    UrlImportDialog* dialog = openUrlWizard(w);
    QVERIFY(dialog != nullptr);
    submitWizard(dialog, { listUrl.toString() });

    // The review stage: the list's section, its two videos checked.
    QTreeWidget* tree = dialog->findChild<QTreeWidget*>("reviewTree");
    QCOMPARE(tree->topLevelItemCount(), 1);
    QCOMPARE(tree->topLevelItem(0)->text(0), QString("The List  (2)"));
    QCOMPARE(tree->topLevelItem(0)->childCount(), 2);
    QCOMPARE(tree->topLevelItem(0)->child(0)->text(0), QString("First"));

    dialog->findChild<QPushButton*>("addButton")->click();

    QTRY_COMPARE_WITH_TIMEOUT(m_manager->count(), 2, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!m_manager->busy(), 10000);

    const MediaItem firstItem = m_manager->findBySource(QUrl("https://music.example.com/watch?v=1"));
    QVERIFY(firstItem.id != 0);
    QCOMPARE(firstItem.kind, MediaItem::Plugin);
    const MediaItem secondItem = m_manager->findBySource(QUrl("https://music.example.com/watch?v=2"));
    QVERIFY(secondItem.id != 0);
    QCOMPARE(secondItem.kind, MediaItem::Plugin);

    SourceManager::getInstance()->unregisterProvider(&provider);
}

void LibraryWidgetTest::addUrlUncheckedListImportsNothing()
{
    // The list is discovered, the user unchecks all of its videos: nothing
    // is imported (the list link is not a source of its own).
    UrlTestProvider provider(this);
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    provider.m_urls = { listUrl.toString() };
    provider.m_meta.title = "Provider Track";
    SourceProvider::ListEntries list;
    list.videos = { SourceProvider::ListVideo(QUrl("https://music.example.com/watch?v=1")),
                    SourceProvider::ListVideo(QUrl("https://music.example.com/watch?v=2")), };
    provider.m_lists[listUrl.toString()] = list;
    QVERIFY(SourceManager::getInstance()->registerProvider(&provider));

    TestLibraryWidget w;
    w.show();

    UrlImportDialog* dialog = openUrlWizard(w);
    QVERIFY(dialog != nullptr);
    submitWizard(dialog, { listUrl.toString() });

    QTreeWidget* tree = dialog->findChild<QTreeWidget*>("reviewTree");
    tree->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
    QPushButton* add = dialog->findChild<QPushButton*>("addButton");
    QVERIFY(!add->isEnabled());

    dialog->findChild<QPushButton*>("reviewCancelButton")->click();

    QCOMPARE(m_manager->count(), 0);
    QVERIFY(!m_manager->busy());

    SourceManager::getInstance()->unregisterProvider(&provider);
}

void LibraryWidgetTest::addUrlBailingOnReviewImportsNothing()
{
    // The user cancels the review: nothing is imported.
    UrlTestProvider provider(this);
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    provider.m_urls = { listUrl.toString() };
    provider.m_meta.title = "Provider Track";
    SourceProvider::ListEntries list;
    list.videos = { SourceProvider::ListVideo(QUrl("https://music.example.com/watch?v=1")) };
    provider.m_lists[listUrl.toString()] = list;
    QVERIFY(SourceManager::getInstance()->registerProvider(&provider));

    TestLibraryWidget w;
    w.show();

    UrlImportDialog* dialog = openUrlWizard(w);
    QVERIFY(dialog != nullptr);
    submitWizard(dialog, { listUrl.toString() });
    QCOMPARE(dialog->findChild<QStackedWidget*>("stageStack")->currentIndex(), 2);

    dialog->findChild<QPushButton*>("reviewCancelButton")->click();

    QCOMPARE(m_manager->count(), 0);
    QVERIFY(!m_manager->busy());

    SourceManager::getInstance()->unregisterProvider(&provider);
}

void LibraryWidgetTest::addUrlSecondTriggerRaisesOpenWizard()
{
    // The wizard is non-modal, single instance: a second trigger while it
    // is open raises the same dialog, no second one is created.
    TestLibraryWidget w;
    w.show();

    UrlImportDialog* first = openUrlWizard(w);
    QVERIFY(first != nullptr);
    w.findChild<QAction*>("addUrlAction")->trigger();
    QCOMPARE(w.findChild<UrlImportDialog*>(), first);
    QCOMPARE(w.findChildren<UrlImportDialog*>().size(), 1);

    first->deleteLater();
}

void LibraryWidgetTest::removeButtonRemovesSelected()
{
    const MediaItem keep = addTempFile("keep.mp3");
    addTempFile("drop-me.mp3");

    MediaListView *list = m_widget->listView();
    list->selectRow(1);

    m_widget->findChild<QToolButton*>("removeButton")->click();

    QCOMPARE(m_manager->count(), 1);
    QVERIFY(m_manager->contains(keep.id));
    QCOMPARE(list->rowCount(), 1);
    QCOMPARE(titleAt(list, 0), QString("keep"));
}

void LibraryWidgetTest::deleteKeyRemovesSelected()
{
    const MediaItem keep = addTempFile("keep.mp3");
    addTempFile("drop-me.mp3");

    MediaListView *list = m_widget->listView();
    list->selectRow(1);
    list->setFocus();
    QTest::keyClick(list, Qt::Key_Delete);

    QCOMPARE(m_manager->count(), 1);
    QVERIFY(m_manager->contains(keep.id));
}

void LibraryWidgetTest::deleteKeyRemovesSelectedFromGroupView()
{
    addTempFile("one.mp3");
    addTempFile("two.flac");

    // Artists mode: both untagged files group under "(Unknown)".
    QTest::mouseClick(m_widget->findChild<QToolButton *>("viewArtistButton"),
                      Qt::LeftButton);
    MediaGroupModel *model = m_widget->groupView()->groupModel();
    QCOMPARE(model->rowCount(), 1);
    selectViewRow(m_widget->groupView(), model->index(0, 0));

    m_widget->groupView()->setFocus();
    QTest::keyClick(m_widget->groupView(), Qt::Key_Delete);

    QCOMPARE(m_manager->count(), 0);
}

void LibraryWidgetTest::deleteKeyWithoutSelectionRemovesNothing()
{
    addTempFile("a.mp3");
    addTempFile("b.mp3");

    MediaListView *list = m_widget->listView();
    list->setFocus();
    QTest::keyClick(list, Qt::Key_Delete);

    QCOMPARE(m_manager->count(), 2);
}

void LibraryWidgetTest::toolbarVisibleByDefaultWithAddMenu()
{
    addTempFile("Some Track.mp3");

    QToolBar* toolbar = m_widget->findChild<QToolBar*>("toolbar");
    if (toolbar == nullptr) {
        QFAIL("library has no toolbar");
        return;
    }
    QVERIFY(!toolbar->isHidden());
    QVERIFY(m_widget->toolbarVisible());
    QCOMPARE(m_widget->toolbarPosition(), BaseWidget::ToolbarPosition::Top);
    // The toolbar is the strip above the views.
    QLayout* layout = m_widget->layout();
    QCOMPARE(layout->indexOf(toolbar), 0);

    QToolButton* add = toolbar->findChild<QToolButton*>("addButton");
    if (add == nullptr) {
        QFAIL("no add button on the toolbar");
        return;
    }
    QVERIFY(add->menu() != nullptr);
    QStringList labels;
    for (QAction* a : add->menu()->actions())
        labels << a->text();
    QCOMPARE(labels,
             QStringList({ "Add files...", "Add folder...", "Add by URL..." }));

    QVERIFY(toolbar->findChild<QToolButton*>("removeButton") != nullptr);
}

void LibraryWidgetTest::widgetContextMenuTogglesToolbar()
{
    QToolBar* toolbar = m_widget->findChild<QToolBar*>("toolbar");
    if (toolbar == nullptr) {
        QFAIL("library has no toolbar");
        return;
    }

    QMenu* menu = m_widget->widgetContextMenu();
    if (menu == nullptr) {
        QFAIL("no context menu");
        return;
    }
    QAction* show = nullptr;
    for (QAction* a : menu->actions()) {
        if (a->text() == "Show toolbar")
            show = a;
    }
    if (show == nullptr) {
        QFAIL("no \"Show toolbar\" action");
        return;
    }
    QVERIFY(show->isChecked());

    show->trigger(); // uncheck -> hide
    QVERIFY(toolbar->isHidden());

    show->trigger(); // re-check -> show
    QVERIFY(!toolbar->isHidden());
}

void LibraryWidgetTest::widgetContextMenuMovesToolbarToBottom()
{
    QToolBar* toolbar = m_widget->findChild<QToolBar*>("toolbar");
    if (toolbar == nullptr) {
        QFAIL("library has no toolbar");
        return;
    }

    QMenu* menu = m_widget->widgetContextMenu();
    if (menu == nullptr) {
        QFAIL("no context menu");
        return;
    }
    QAction* bottom = nullptr;
    for (QAction* a : menu->actions()) {
        if (a->text() == "Toolbar at bottom")
            bottom = a;
    }
    if (bottom == nullptr) {
        QFAIL("no \"Toolbar at bottom\" action");
        return;
    }

    bottom->trigger();
    QLayout* layout = m_widget->layout();
    QCOMPARE(layout->indexOf(toolbar), layout->count() - 1);

    // A fresh menu reflects the new position; moving back works too.
    menu->deleteLater();
    QMenu* menu2 = m_widget->widgetContextMenu();
    if (menu2 == nullptr) {
        QFAIL("no context menu");
        return;
    }
    QAction* top = nullptr;
    QAction* bottom2 = nullptr;
    for (QAction* a : menu2->actions()) {
        if (a->text() == "Toolbar at top")
            top = a;
        else if (a->text() == "Toolbar at bottom")
            bottom2 = a;
    }
    if (top == nullptr || bottom2 == nullptr) {
        QFAIL("no position actions in the fresh menu");
        return;
    }
    QVERIFY(!top->isChecked());
    QVERIFY(bottom2->isChecked());
    top->trigger();
    QCOMPARE(layout->indexOf(toolbar), 0);
    menu2->deleteLater();
}

void LibraryWidgetTest::toolbarStateSerializedOnlyWhenNonDefault()
{
    TestLibraryWidget w;
    w.show();

    // The default state (visible, top) is not persisted.
    QVERIFY(!w.storeWidget().contains("toolbar"));

    w.showToolbar(false);
    WidgetConfiguration cfg = w.storeWidget();
    const QVariantMap toolbar = cfg.value("toolbar").toMap();
    QCOMPARE(toolbar.value("visible").toBool(), false);
    QCOMPARE(toolbar.value("position").toString(), QString("top"));

    // A restored widget applies it.
    LibraryWidget* other = new LibraryWidget();
    other->restoreWidget(cfg);
    QToolBar* restored = other->findChild<QToolBar*>("toolbar");
    if (restored == nullptr) {
        QFAIL("restored library has no toolbar");
        delete other;
        return;
    }
    QVERIFY(restored->isHidden());

    delete other;
}

void LibraryWidgetTest::doubleClickAppendsToCurrentPlaylist()
{
    PlaylistManager* pl = PlaylistManager::getInstance();
    pl->reset();
    QCOMPARE(pl->count(), 0);

    const MediaItem a = addTempFile("one.mp3");
    const MediaItem b = addTempFile("two.flac");

    MediaListView* list = m_widget->listView();

    // Double-click the second row: it is appended to the current playlist.
    QMetaObject::invokeMethod(m_widget, "onItemDoubleClicked",
                              Q_ARG(QModelIndex, list->model()->index(1, 0)));
    QCOMPARE(pl->count(), 1);
    QCOMPARE(pl->itemIds().at(0), b.id);

    // Double-click the first row: it is appended after the existing one.
    QMetaObject::invokeMethod(m_widget, "onItemDoubleClicked",
                              Q_ARG(QModelIndex, list->model()->index(0, 0)));
    QCOMPARE(pl->count(), 2);
    QCOMPARE(pl->itemIds().at(0), b.id);
    QCOMPARE(pl->itemIds().at(1), a.id);
}

void LibraryWidgetTest::doubleClickOfExistingTrackAppendsCopy()
{
    PlaylistManager* pl = PlaylistManager::getInstance();
    pl->reset();

    const MediaItem a = addTempFile("one.mp3");
    pl->addItems({ a.id });

    // Double-clicking the same library item again appends another row:
    // a playlist may hold the same item any number of times.
    MediaListView* list = m_widget->listView();
    QMetaObject::invokeMethod(m_widget, "onItemDoubleClicked",
                              Q_ARG(QModelIndex, list->model()->index(0, 0)));

    QCOMPARE(pl->count(), 2);
    QCOMPARE(pl->itemIds(), QList<quint64>({ a.id, a.id }));
}

void LibraryWidgetTest::columnStateStoredAndRestored()
{
    MediaListView *list = m_widget->listView();
    QHeaderView *header = list->header();
    header->moveSection(header->visualIndex(MediaListModel::Duration), 0);
    header->setSectionHidden(MediaListModel::Artist, true);

    WidgetConfiguration cfg = m_widget->storeWidget();

    LibraryWidget *other = new LibraryWidget();
    other->restoreWidget(cfg);

    QHeaderView *restored = other->listView()->header();
    QCOMPARE(restored->visualIndex(MediaListModel::Duration), 0);
    QVERIFY(restored->isSectionHidden(MediaListModel::Artist));
    // The Playing column is unavailable in the library: it never enters the
    // stored state and stays hidden on the restored widget.
    QVERIFY(!cfg.value("columns").toMap().value("order").toStringList().contains("playing"));
    QVERIFY(!other->listView()->columnAvailable(MediaListModel::Playing));
    QVERIFY(restored->isSectionHidden(MediaListModel::Playing));
    QVERIFY(!restored->isSectionHidden(MediaListModel::Title));

    delete other;
}

QMenu* LibraryWidgetTest::openHeaderMenu(MediaListView *list)
{
    QHeaderView *header = list->header();
    QContextMenuEvent event(QContextMenuEvent::Mouse,
                            header->viewport()->rect().center(),
                            header->viewport()->rect().center());
    QCoreApplication::sendEvent(header->viewport(), &event);
    QMenu *menu = list->findChild<QMenu*>();
    if (menu == nullptr)
        return nullptr;
    QCoreApplication::processEvents();
    return menu;
}

void LibraryWidgetTest::triggerHeaderAction(MediaListView *list, const QString &label)
{
    QMenu *menu = openHeaderMenu(list);
    if (menu == nullptr) {
        QFAIL("header context menu did not appear");
        return;
    }
    for (QAction *a : menu->actions()) {
        if (a->text() == label)
            a->trigger();
    }
    menu->hide();
    QCoreApplication::processEvents();
}

void LibraryWidgetTest::searchBoxOnToolbar()
{
    QToolBar* toolbar = m_widget->findChild<QToolBar*>("toolbar");
    if (toolbar == nullptr) {
        QFAIL("library has no toolbar");
        return;
    }

    QLineEdit* search = toolbar->findChild<QLineEdit*>("searchBox");
    if (search == nullptr) {
        QFAIL("no search box on the toolbar");
        return;
    }
    QVERIFY(search->text().isEmpty());
    QVERIFY(search->isClearButtonEnabled());

    // The magnifying glass is a leading icon action that renders.
    QAction* icon = search->findChild<QAction*>("searchIconAction");
    if (icon == nullptr) {
        QFAIL("no search icon action on the search box");
        return;
    }
    QVERIFY(!icon->icon().isNull());
    QVERIFY(!icon->icon().pixmap(QSize(16, 16)).isNull());
}

void LibraryWidgetTest::searchFiltersVisibleColumns()
{
    const MediaItem alpha = addTempFile("alpha.mp3");
    addTempFile("beta.mp3");
    addTempFile("gamma.mp3");

    QLineEdit* search = m_widget->findChild<QLineEdit*>("searchBox");
    QVERIFY(search != nullptr);
    MediaListView* list = m_widget->listView();

    search->setText("ALPHA"); // case-insensitive
    QCOMPARE(list->rowCount(), 1);
    QCOMPARE(titleAt(list, 0), QString("alpha"));

    search->setText("be"); // partial match
    QCOMPARE(list->rowCount(), 1);

    search->setText("zzz-no-match");
    QCOMPARE(list->rowCount(), 0);

    search->clear();
    QCOMPARE(list->rowCount(), 3);
}

void LibraryWidgetTest::searchMatchesArtistColumn()
{
    addTempFile("alpha.mp3");
    const QUrl url = makeTaggedFile(m_dir->path(), "tagged.flac",
                                    "Gamma Tune", "Gamma Artist", "Gamma Album");
    if (url.isEmpty())
        QSKIP("ffmpeg not available to generate a tagged file");
    addFileAndWait(url);

    QLineEdit* search = m_widget->findChild<QLineEdit*>("searchBox");
    QVERIFY(search != nullptr);
    MediaListView* list = m_widget->listView();

    // Only the Artist column holds this text.
    search->setText("gamma artist");
    QCOMPARE(list->rowCount(), 1);
    QCOMPARE(titleAt(list, 0), QString("Gamma Tune"));

    // The Album column is visible: it matches too.
    search->setText("gamma album");
    QCOMPARE(list->rowCount(), 1);

    search->clear();
    QCOMPARE(list->rowCount(), 2);
}

void LibraryWidgetTest::searchMatchesHiddenColumns()
{
    addTempFile("unique-title.mp3");
    addTempFile("other.mp3");

    MediaListView *list = m_widget->listView();
    QLineEdit* search = m_widget->findChild<QLineEdit*>("searchBox");
    QVERIFY(search != nullptr);

    // The search is over the item's data, not the visible columns: hide
    // the Title column and the match in it survives.
    triggerHeaderAction(list, "Title");
    QVERIFY(list->header()->isSectionHidden(MediaListModel::Title));

    search->setText("unique");
    QCOMPARE(list->rowCount(), 1);

    triggerHeaderAction(list, "Title");
    QVERIFY(!list->header()->isSectionHidden(MediaListModel::Title));

    search->clear();
    QCOMPARE(list->rowCount(), 2);
}

void LibraryWidgetTest::searchSurvivesLibraryUpdates()
{
    addTempFile("alpha.mp3");
    addTempFile("beta.mp3");

    MediaListView* list = m_widget->listView();
    QLineEdit* search = m_widget->findChild<QLineEdit*>("searchBox");
    QVERIFY(search != nullptr);

    search->setText("alpha");
    QCOMPARE(list->rowCount(), 1);

    // A new matching item appears, a non-matching one stays out.
    addTempFile("alpha two.mp3");
    addTempFile("gamma.mp3");
    QCOMPARE(list->rowCount(), 2);

    // Removing the first match leaves the other one.
    QVERIFY(m_manager->removeItem(list->idAt(list->model()->index(0, 0))));
    QCOMPARE(list->rowCount(), 1);
    QCOMPARE(titleAt(list, 0), QString("alpha two"));

    // Clearing the filter shows the whole library again.
    search->clear();
    QCOMPARE(list->rowCount(), 3);
}

void LibraryWidgetTest::noResyncButtonOnToolbar()
{
    QToolBar* toolbar = m_widget->findChild<QToolBar*>("toolbar");
    if (toolbar == nullptr) {
        QFAIL("library has no toolbar");
        return;
    }

    // Re-reading tags moved into the metadata editor dialogs ("Read from
    // files"): the toolbar carries no resync action.
    QVERIFY(toolbar->findChild<QToolButton*>("resyncButton") == nullptr);

    // The busy indicator is on the toolbar too; idle it is zero-width
    // and (deliberately) not hidden: a hide() here would fight the
    // toolbar's re-show of its children and loop the main thread.
    QWidget* spinner = toolbar->findChild<QWidget*>("busyIndicator");
    if (spinner == nullptr) {
        QFAIL("no busy indicator on the toolbar");
        return;
    }
    QCOMPARE(spinner->width(), 0);
    QVERIFY(!spinner->isHidden());
}

void LibraryWidgetTest::busyDisablesButtonsAndShowsSpinner()
{
    const QString path = m_dir->path() + "/spinner.mp3";
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("\x00\x01\x02");
    f.close();

    QToolButton* add = m_widget->findChild<QToolButton*>("addButton");
    QToolButton* remove = m_widget->findChild<QToolButton*>("removeButton");
    QWidget* spinner = m_widget->findChild<QWidget*>("busyIndicator");
    if (add == nullptr || remove == nullptr || spinner == nullptr) {
        QFAIL("missing toolbar controls");
        return;
    }
    QVERIFY(add->isEnabled());
    QCOMPARE(spinner->width(), 0);

    m_manager->addFiles({ QUrl::fromLocalFile(path) });

    // The busy state is set synchronously: the buttons are disabled and the
    // spinner takes its 16px slot while the worker thread reads the file.
    QVERIFY(!add->isEnabled());
    QVERIFY(!remove->isEnabled());
    QCOMPARE(spinner->width(), 16);

    // When the operation finishes, the buttons come back and the spinner
    // collapses back to zero width.
    QTRY_VERIFY_WITH_TIMEOUT(!m_manager->busy(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(add->isEnabled(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(remove->isEnabled(), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(spinner->width(), 0, 10000);
}


void LibraryWidgetTest::registeredInLayoutManager()
{
    WidgetConfiguration cfg;
    BaseWidget* w = LayoutManager::getInstance()->createWidget(LibraryWidget::id, cfg);

    QVERIFY(w != nullptr);
    QCOMPARE(w->widgetId(), LibraryWidget::id);
    QCOMPARE(w->widgetTitle(), LibraryWidget::title);
    delete w;
}

void LibraryWidgetTest::sendRowContextMenu(QAbstractItemView *view)
{
    QWidget *viewport = view->viewport();
    const QPoint pos = viewport->rect().center();
    QContextMenuEvent cme(QContextMenuEvent::Mouse, pos,
                          viewport->mapToGlobal(pos), Qt::NoModifier);
    QCoreApplication::sendEvent(viewport, &cme);
    QCoreApplication::processEvents();
}

QMenu* LibraryWidgetTest::findRowMenu(QWidget *widget)
{
    const QList<QMenu*> menus = widget->findChildren<QMenu*>();
    for (QMenu *menu : menus)
        if (menu->isVisible())
            return menu;
    return nullptr;
}

// Both edit actions with a single-item selection: "Edit metadata..."
// (the single-item tag editor) is enabled, and "Bulk edit metadata..."
// (the table) is available too.
void LibraryWidgetTest::contextMenuOffersEditMetadata()
{
    addTempFile("a.mp3");
    const MediaItem b = addTempFile("b.mp3");

    TestLibraryWidget w;
    w.show();
    MediaListView *list = w.listView();
    QCOMPARE(list->rowCount(), 2);
    list->selectRow(1);

    LayoutManager::getInstance()->setEditMode(false);
    sendRowContextMenu(list);

    QMenu *menu = findRowMenu(&w);
    if (menu == nullptr) {
        QFAIL("row context menu did not appear");
        return;
    }
    QAction *edit = nullptr;
    QAction *bulk = nullptr;
    for (QAction *a : menu->actions()) {
        if (a->text() == "Edit metadata...")
            edit = a;
        else if (a->text() == "Bulk edit metadata...")
            bulk = a;
    }
    if (edit == nullptr || bulk == nullptr) {
        QFAIL("no \"Edit metadata...\" / \"Bulk edit metadata...\" actions");
        return;
    }
    QVERIFY(edit->isEnabled());
    QVERIFY(bulk->isEnabled());
    edit->trigger();

    QCOMPARE(w.m_editSingleMetadataId, b.id);
    QVERIFY(w.m_editMetadataIds.isEmpty());
}

// A multi-item selection disables the single-item editor and passes the
// whole id set to the bulk editor.
void LibraryWidgetTest::contextMenuBulkEditPassesSelectedIds()
{
    const MediaItem a = addTempFile("a.mp3");
    const MediaItem b = addTempFile("b.mp3");

    TestLibraryWidget w;
    w.show();
    MediaListView *list = w.listView();
    selectViewRow(list, list->model()->index(0, 0));
    selectViewRow(list, list->model()->index(1, 0));

    LayoutManager::getInstance()->setEditMode(false);
    sendRowContextMenu(list);

    QMenu *menu = findRowMenu(&w);
    if (menu == nullptr) {
        QFAIL("row context menu did not appear");
        return;
    }
    QAction *edit = nullptr;
    QAction *bulk = nullptr;
    for (QAction *a : menu->actions()) {
        if (a->text() == "Edit metadata...")
            edit = a;
        else if (a->text() == "Bulk edit metadata...")
            bulk = a;
    }
    if (edit == nullptr || bulk == nullptr) {
        QFAIL("no \"Edit metadata...\" / \"Bulk edit metadata...\" actions");
        return;
    }
    QVERIFY(!edit->isEnabled());
    QVERIFY(bulk->isEnabled());
    bulk->trigger();

    QCOMPARE(w.m_editMetadataIds, QList<quint64>({ a.id, b.id }));
    QVERIFY(w.m_editSingleMetadataId == 0);
}

void LibraryWidgetTest::contextMenuEditMetadataDisabledWithNoSelection()
{
    addTempFile("a.mp3");

    TestLibraryWidget w;
    w.show();
    MediaListView *list = w.listView();

    LayoutManager::getInstance()->setEditMode(false);
    sendRowContextMenu(list);

    QMenu *menu = findRowMenu(&w);
    if (menu == nullptr) {
        QFAIL("row context menu did not appear");
        return;
    }
    QAction *edit = nullptr;
    QAction *bulk = nullptr;
    for (QAction *a : menu->actions()) {
        if (a->text() == "Edit metadata...")
            edit = a;
        else if (a->text() == "Bulk edit metadata...")
            bulk = a;
    }
    if (edit == nullptr || bulk == nullptr) {
        QFAIL("no \"Edit metadata...\" / \"Bulk edit metadata...\" actions");
        return;
    }
    QVERIFY(!edit->isEnabled());
    QVERIFY(!bulk->isEnabled());
}

// The slot does not gate on edit mode (7d): edit-mode right-clicks are
// owned by the container's filter (the flip-flop), and its pass-through
// reaches this slot exactly as in normal mode — a widget without a
// container (no filter) offers its menu in edit mode too.
void LibraryWidgetTest::contextMenuSlotShowsInEditMode()
{
    addTempFile("a.mp3");

    LayoutManager::getInstance()->setEditMode(true);
    MediaListView *list = m_widget->listView();
    list->selectRow(0);

    sendRowContextMenu(list);

    QMenu *menu = findRowMenu(m_widget);
    if (menu == nullptr) {
        QFAIL("row menu did not appear");
        return;
    }
    menu->hide();
    LayoutManager::getInstance()->setEditMode(false);
}

void LibraryWidgetTest::dropFilesImportsIntoLibrary()
{
    // Files not in the library yet, written straight to disk.
    const QString a = m_dir->path() + "/drop-a.mp3";
    const QString b = m_dir->path() + "/drop-b.flac";
    for (const QString &path : { a, b }) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("\x00\x01\x02");
    }

    QMimeData md;
    md.setUrls({ QUrl::fromLocalFile(a), QUrl::fromLocalFile(b) });
    // The List view's model is the filter proxy: it maps the drop to the
    // source and forwards it.
    QVERIFY(m_widget->listView()->model()->dropMimeData(
        &md, Qt::CopyAction, 0, 0, QModelIndex()));

    test::waitForIdle(m_manager);
    QCOMPARE(m_manager->items().size(), 2);
    QCOMPARE(m_widget->listView()->rowCount(), 2);
}

void LibraryWidgetTest::dropPlaylistFileImportsItsEntries()
{
    const QString a = m_dir->path() + "/pl-a.mp3";
    const QString b = m_dir->path() + "/pl-b.flac";
    for (const QString &path : { a, b }) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("\x00\x01\x02");
    }
    const QString pl = m_dir->path() + "/mix.m3u";
    {
        QFile f(pl);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write((a + "\n" + b + "\n").toUtf8());
    }

    QMimeData md;
    md.setUrls({ QUrl::fromLocalFile(pl) });
    QVERIFY(m_widget->listView()->model()->dropMimeData(
        &md, Qt::CopyAction, 0, 0, QModelIndex()));

    // The playlist file is flattened into its entries; the file itself is
    // not a library item.
    test::waitForIdle(m_manager);
    QCOMPARE(m_manager->items().size(), 2);
    QCOMPARE(m_widget->listView()->rowCount(), 2);
}

void LibraryWidgetTest::dropUrlOpensPrefilledWizard()
{
    // A non-local URL from outside the app (a browser) goes through the
    // import wizard, opened pre-filled with the dropped URLs. No provider
    // claims the url: the wizard takes the fast path (no review) and the
    // stream is added (kind Remote).
    TestLibraryWidget w;
    w.show();

    QMimeData md;
    md.setUrls({ QUrl("https://example.com/audio/stream.mp3") });
    QVERIFY(w.listView()->model()->dropMimeData(
        &md, Qt::CopyAction, 0, 0, QModelIndex()));

    QTRY_COMPARE_WITH_TIMEOUT(m_manager->count(), 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!m_manager->busy(), 10000);

    const MediaItem remote =
        m_manager->findBySource(QUrl("https://example.com/audio/stream.mp3"));
    QVERIFY(remote.id != 0);
    QCOMPARE(remote.kind, MediaItem::Remote);
    // The wizard closed itself after the fast path (WA_DeleteOnClose).
    QTest::qWait(50);
    QCOMPARE(w.findChild<UrlImportDialog*>(), nullptr);
}

void LibraryWidgetTest::dropUrlWithListReviewsVideos()
{
    // A dropped URL a provider claims (a list link): the wizard expands
    // the list and the review shows its videos — the same flow as the
    // pasted input, no re-fetch at import time.
    UrlTestProvider provider(this);
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    provider.m_urls = { listUrl.toString(),
                        "https://music.example.com/watch?v=1",
                        "https://music.example.com/watch?v=2", };
    SourceProvider::ListEntries list;
    list.title = "The List";
    SourceProvider::ListVideo first(QUrl("https://music.example.com/watch?v=1"));
    first.title = "First";
    SourceProvider::ListVideo second(QUrl("https://music.example.com/watch?v=2"));
    second.title = "Second";
    list.videos = { first, second };
    provider.m_lists[listUrl.toString()] = list;
    QVERIFY(SourceManager::getInstance()->registerProvider(&provider));

    TestLibraryWidget w;
    w.show();

    QMimeData md;
    md.setUrls({ listUrl });
    QVERIFY(w.listView()->model()->dropMimeData(
        &md, Qt::CopyAction, 0, 0, QModelIndex()));

    UrlImportDialog* dialog = w.findChild<UrlImportDialog*>();
    QVERIFY(dialog != nullptr);
    QTreeWidget* tree = dialog->findChild<QTreeWidget*>("reviewTree");
    QCOMPARE(tree->topLevelItemCount(), 1);
    QCOMPARE(tree->topLevelItem(0)->text(0), QString("The List  (2)"));
    QCOMPARE(tree->topLevelItem(0)->childCount(), 2);
    QCOMPARE(tree->topLevelItem(0)->child(0)->text(0), QString("First"));

    dialog->findChild<QPushButton*>("addButton")->click();

    QTRY_COMPARE_WITH_TIMEOUT(m_manager->count(), 2, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!m_manager->busy(), 10000);
    QCOMPARE(m_manager->findBySource(QUrl("https://music.example.com/watch?v=1")).title,
             QString("First"));

    SourceManager::getInstance()->unregisterProvider(&provider);
}

void LibraryWidgetTest::dropUrlsWhileWizardOpenRaisesIt()
{
    // An already-open wizard is left alone by a URL drop (a second start
    // would replace its in-progress flow): the drop only raises it.
    UrlTestProvider provider(this);
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    provider.m_urls = { listUrl.toString(),
                        "https://music.example.com/watch?v=9", };
    SourceProvider::ListEntries list;
    list.title = "The List";
    list.videos = { SourceProvider::ListVideo(QUrl("https://music.example.com/watch?v=1")),
                    SourceProvider::ListVideo(QUrl("https://music.example.com/watch?v=2")), };
    provider.m_lists[listUrl.toString()] = list;
    QVERIFY(SourceManager::getInstance()->registerProvider(&provider));

    TestLibraryWidget w;
    w.show();

    UrlImportDialog* dialog = openUrlWizard(w);
    QVERIFY(dialog != nullptr);
    submitWizard(dialog, { listUrl.toString() });
    QTreeWidget* tree = dialog->findChild<QTreeWidget*>("reviewTree");
    QCOMPARE(tree->topLevelItemCount(), 1);

    QMimeData md;
    md.setUrls({ QUrl("https://music.example.com/watch?v=9") });
    QVERIFY(w.listView()->model()->dropMimeData(
        &md, Qt::CopyAction, 0, 0, QModelIndex()));

    QCOMPARE(w.findChild<UrlImportDialog*>(), dialog);
    QCOMPARE(w.findChildren<UrlImportDialog*>().size(), 1);
    // The first flow's review is intact: the dropped url never entered it.
    QCOMPARE(tree->topLevelItem(0)->text(0), QString("The List  (2)"));
    QCOMPARE(m_manager->count(), 0);

    SourceManager::getInstance()->unregisterProvider(&provider);
}

void LibraryWidgetTest::addTaggedFile(const QString &name, const QString &title,
                                      const QString &artist, const QString &album)
{
    // QSKIP in the helper: a value-returning function cannot use the
    // bare-return macros, so the tests call this void helper.
    const QUrl url = makeTaggedFile(m_dir->path(), name, title, artist, album);
    if (url.isEmpty())
        QSKIP("ffmpeg not available to generate a tagged file");
    addFileAndWait(url);
}

void LibraryWidgetTest::selectViewRow(QWidget *view, const QModelIndex &index)
{
    QItemSelectionModel *sm =
        static_cast<QAbstractItemView*>(view)->selectionModel();
    QVERIFY(sm != nullptr);
    sm->select(QItemSelection(index, index),
               QItemSelectionModel::Select | QItemSelectionModel::Rows);
}

QString LibraryWidgetTest::titleAt(MediaListView *list, int row)
{
    const quint64 id = list->idAt(list->model()->index(row, 0));
    return MediaItemStore::resolve(id).title;
}

void LibraryWidgetTest::clickHeaderSection(MediaListView *list, int logical)
{
    // A synthetic mouse click offscreen does not reach the QHeaderView's
    // section handling (verified against a bare header), so the
    // sectionClicked signal — Qt's own header machinery — is emitted
    // directly, the way the view's connect() will receive it.
    QHeaderView *header = list->header();
    QVERIFY(header);
    const int signalIndex =
        header->metaObject()->indexOfSignal("sectionClicked(int)");
    QVERIFY(signalIndex >= 0);
    std::array<void *, 2> args = { nullptr, &logical };
    QMetaObject::activate(header, signalIndex, args.data());
}

void LibraryWidgetTest::viewButtonsOnToolbar()
{
    QToolButton *listButton = m_widget->findChild<QToolButton *>("viewListButton");
    QToolButton *artistButton = m_widget->findChild<QToolButton *>("viewArtistButton");
    QToolButton *albumButton = m_widget->findChild<QToolButton *>("viewAlbumButton");
    QToolButton *tupleButton = m_widget->findChild<QToolButton *>("viewArtistAlbumButton");
    QVERIFY(listButton);
    QVERIFY(artistButton);
    QVERIFY(albumButton);
    QVERIFY(tupleButton);
    // The view buttons live in the widget's own toolbar strip.
    QToolBar *toolbar = m_widget->findChild<QToolBar *>();
    QVERIFY(toolbar);
    QVERIFY(toolbar->isVisibleTo(listButton));
    // List is the default: its button is checked, the others are not.
    QVERIFY(listButton->isChecked());
    QVERIFY(!artistButton->isChecked());
    QVERIFY(!albumButton->isChecked());
    QVERIFY(!tupleButton->isChecked());
    QCOMPARE(m_widget->viewMode(), LibraryWidget::ViewMode::List);
}

void LibraryWidgetTest::viewButtonsSwitchGrouping()
{
    addTaggedFile("one.mp3", "One", "Ade", "Al 1");
    QStackedWidget *stack = m_widget->findChild<QStackedWidget *>("viewStack");
    QVERIFY(stack);
    QCOMPARE(stack->currentIndex(), 0); // the list view

    QToolButton *artistButton = m_widget->findChild<QToolButton *>("viewArtistButton");
    QToolButton *albumButton = m_widget->findChild<QToolButton *>("viewAlbumButton");
    QToolButton *tupleButton = m_widget->findChild<QToolButton *>("viewArtistAlbumButton");
    QToolButton *listButton = m_widget->findChild<QToolButton *>("viewListButton");

    QTest::mouseClick(artistButton, Qt::LeftButton);
    QCOMPARE(m_widget->viewMode(), LibraryWidget::ViewMode::ByArtist);
    QCOMPARE(m_widget->groupView()->groupModel()->grouping(),
             MediaGroupModel::Grouping::ByArtist);
    QCOMPARE(stack->currentIndex(), 1); // the group view
    QVERIFY(artistButton->isChecked());
    QVERIFY(!listButton->isChecked());
    // The tree switches to decorated, expandable rows; the top-level
    // groups come up collapsed.
    QVERIFY(m_widget->groupView()->rootIsDecorated());
    QCOMPARE(m_widget->groupView()->groupModel()->rowCount(), 1);
    QVERIFY(!m_widget->groupView()->isExpanded(
        m_widget->groupView()->groupModel()->index(0, 0)));

    QTest::mouseClick(albumButton, Qt::LeftButton);
    QCOMPARE(m_widget->viewMode(), LibraryWidget::ViewMode::ByAlbum);
    QCOMPARE(m_widget->groupView()->groupModel()->grouping(),
             MediaGroupModel::Grouping::ByAlbum);
    QVERIFY(!artistButton->isChecked());
    QVERIFY(albumButton->isChecked());
    QCOMPARE(m_widget->groupView()->groupModel()->rowCount(), 1); // one album

    QTest::mouseClick(tupleButton, Qt::LeftButton);
    QCOMPARE(m_widget->viewMode(), LibraryWidget::ViewMode::ByArtistAlbum);
    QCOMPARE(m_widget->groupView()->groupModel()->grouping(),
             MediaGroupModel::Grouping::ByArtistAlbum);
    QVERIFY(albumButton->isChecked() == false);
    QVERIFY(tupleButton->isChecked());
    QCOMPARE(m_widget->groupView()->groupModel()->rowCount(), 1); // one pair

    QTest::mouseClick(listButton, Qt::LeftButton);
    QCOMPARE(m_widget->viewMode(), LibraryWidget::ViewMode::List);
    QVERIFY(listButton->isChecked());
    QCOMPARE(stack->currentIndex(), 0);
    QCOMPARE(m_widget->listView()->rowCount(), 1);
}

void LibraryWidgetTest::artistTreeShowsGroupRows()
{
    addTaggedFile("a1.mp3", "One", "Ade", "Al 1");
    addTaggedFile("a2.mp3", "Two", "Bee", "Al 1");
    addTaggedFile("a3.mp3", "Three", "Ade", "Al 2");

    QToolButton *artistButton = m_widget->findChild<QToolButton *>("viewArtistButton");
    QTest::mouseClick(artistButton, Qt::LeftButton);

    MediaGroupModel *model = m_widget->groupView()->groupModel();
    QCOMPARE(model->rowCount(), 2);
    // A group row shows its name with its track count at the end of the
    // line; the groups are name-sorted.
    QCOMPARE(model->rowText(model->index(0, 0)), QString("Ade (2)"));
    QCOMPARE(model->rowText(model->index(1, 0)), QString("Bee (1)"));
    QVERIFY(m_widget->groupView()->rootIsDecorated());
    QVERIFY(!m_widget->groupView()->isExpanded(model->index(0, 0)));
    // Expanding "Ade" shows its albums, each carrying its tracks.
    const QModelIndex ade = model->index(0, 0);
    QCOMPARE(model->rowCount(ade), 2);
    QCOMPARE(model->rowText(model->index(0, 0, ade)), QString("Al 1 (1)"));
    QCOMPARE(model->rowText(model->index(1, 0, ade)), QString("Al 2 (1)"));
    const QModelIndex al2 = model->index(1, 0, ade);
    QCOMPARE(model->rowCount(al2), 1);
    // A track row shows its title (no number: the files are untagged).
    QCOMPARE(model->rowText(model->index(0, 0, al2)), QString("Three"));
    QVERIFY(!model->isGroupRow(model->index(0, 0, al2)));
}

void LibraryWidgetTest::albumTreeShowsGroupRows()
{
    addTaggedFile("a1.mp3", "One", "Ade", "Al 1");
    addTaggedFile("a2.mp3", "Two", "Bee", "Al 1");
    addTaggedFile("a3.mp3", "Three", "Ade", "Al 2");

    QToolButton *albumButton = m_widget->findChild<QToolButton *>("viewAlbumButton");
    QTest::mouseClick(albumButton, Qt::LeftButton);

    MediaGroupModel *model = m_widget->groupView()->groupModel();
    QCOMPARE(model->rowCount(), 2);
    // The album names, name-sorted, each with its count.
    QCOMPARE(model->rowText(model->index(0, 0)), QString("Al 1 (2)"));
    QCOMPARE(model->rowText(model->index(1, 0)), QString("Al 2 (1)"));
    // Tracks under "Al 1" span both artists.
    QCOMPARE(model->rowCount(model->index(0, 0)), 2);
    QCOMPARE(model->rowCount(model->index(1, 0)), 1);
}

void LibraryWidgetTest::artistAlbumTreeShowsTupleGroups()
{
    addTaggedFile("a1.mp3", "One", "Ade", "Al 1");
    addTaggedFile("a2.mp3", "Two", "Bee", "Al 1");
    addTaggedFile("a3.mp3", "Three", "Ade", "Al 2");

    QToolButton *tupleButton = m_widget->findChild<QToolButton *>("viewArtistAlbumButton");
    QTest::mouseClick(tupleButton, Qt::LeftButton);

    MediaGroupModel *model = m_widget->groupView()->groupModel();
    QCOMPARE(model->rowCount(), 3);
    // The (artist, album) pairs, one level: the album alone does not
    // merge across artists ("Al 1" appears under both Ade and Bee).
    QCOMPARE(model->rowText(model->index(0, 0)),
             QStringLiteral("Ade — Al 1 (1)"));
    QCOMPARE(model->rowText(model->index(1, 0)),
             QStringLiteral("Ade — Al 2 (1)"));
    QCOMPARE(model->rowText(model->index(2, 0)),
             QStringLiteral("Bee — Al 1 (1)"));
    QCOMPARE(model->rowCount(model->index(0, 0)), 1);
    QCOMPARE(model->rowText(model->index(0, 0, model->index(0, 0))),
             QString("One"));
}

void LibraryWidgetTest::viewMenuInWidgetContextMenu()
{
    QMenu *menu = m_widget->widgetContextMenu();
    QVERIFY(menu);
    QAction *viewAction = nullptr;
    for (QAction *a : menu->actions())
        if (a->text() == "View")
            viewAction = a;
    if (viewAction == nullptr || viewAction->menu() == nullptr) {
        QFAIL("no \"View\" submenu in the context menu");
        return;
    }
    const QList<QAction *> actions = viewAction->menu()->actions();
    QCOMPARE(actions.size(), 4);
    QCOMPARE(actions.at(0)->text(), QString("List"));
    QCOMPARE(actions.at(1)->text(), QString("Artists"));
    QCOMPARE(actions.at(2)->text(), QString("Albums"));
    QCOMPARE(actions.at(3)->text(), QString("Artist + Album"));
    for (QAction *action : actions) {
        QVERIFY(action->isCheckable());
        QVERIFY(action->isChecked() == (action->text() == "List"));
    }
    // Triggering an action switches the view mode (and the toolbar buttons).
    actions.at(1)->trigger();
    QCOMPARE(m_widget->viewMode(), LibraryWidget::ViewMode::ByArtist);
    QVERIFY(m_widget->findChild<QToolButton *>("viewArtistButton")->isChecked());
}

void LibraryWidgetTest::viewModeStoredOnlyWhenNonDefault()
{
    addTaggedFile("a1.mp3", "One", "Ade", "Al 1");

    struct Case
    {
        const char *button;
        QString stored;
    };
    const QList<Case> cases = {
        { "viewArtistButton", "artists" },
        { "viewAlbumButton", "albums" },
        { "viewArtistAlbumButton", "artistalbum" },
    };
    for (const Case &c : cases) {
        QTest::mouseClick(m_widget->findChild<QToolButton *>(c.button),
                          Qt::LeftButton);
        WidgetConfiguration config = m_widget->storeWidget();
        QCOMPARE(config.value("view").toString(), c.stored);
    }

    // Back to the default view: nothing is stored.
    QTest::mouseClick(m_widget->findChild<QToolButton *>("viewListButton"),
                      Qt::LeftButton);
    WidgetConfiguration config = m_widget->storeWidget();
    QVERIFY(!config.contains("view"));
}

void LibraryWidgetTest::viewModeAndSortStoredAndRestored()
{
    addTaggedFile("a1.mp3", "One", "Ade", "Al 1");
    addTaggedFile("a2.mp3", "Two", "Bee", "Al 1");

    // Artist view + title-ascending sort.
    QTest::mouseClick(m_widget->findChild<QToolButton *>("viewArtistButton"),
                      Qt::LeftButton);
    MediaFilterProxyModel *proxy =
        qobject_cast<MediaFilterProxyModel *>(m_widget->listView()->model());
    QVERIFY(proxy);
    proxy->setSort(MediaListModel::Title, Qt::AscendingOrder);

    WidgetConfiguration config = m_widget->storeWidget();
    QCOMPARE(config.value("view").toString(), QString("artists"));
    // The sort is a single {column, ascending} map.
    const QVariantMap sort = config.value("sort").toMap();
    QCOMPARE(sort.value("column").toString(),
             MediaListModel::columnKey(MediaListModel::Title));
    QVERIFY(sort.value("ascending").toBool());

    // A fresh widget restores both.
    delete m_widget;
    m_widget = new TestLibraryWidget;
    m_widget->show();
    m_widget->restoreWidget(config);
    QCOMPARE(m_widget->viewMode(), LibraryWidget::ViewMode::ByArtist);
    MediaFilterProxyModel *restored =
        qobject_cast<MediaFilterProxyModel *>(m_widget->listView()->model());
    QVERIFY(restored);
    QCOMPARE(restored->sortColumn(), MediaListModel::Title);
    QCOMPARE(restored->sortOrder(), Qt::AscendingOrder);
}

void LibraryWidgetTest::headerClickSortsRows()
{
    addTaggedFile("a1.mp3", "Ade", "Ade", "Al 1");
    addTaggedFile("a2.mp3", "Bee", "Bee", "Al 1");
    addTaggedFile("a3.mp3", "Zed", "Zed", "Al 1");

    MediaListView *list = m_widget->listView();
    MediaFilterProxyModel *proxy =
        qobject_cast<MediaFilterProxyModel *>(list->model());
    QVERIFY(proxy);
    QCOMPARE(proxy->sortColumn(), -1); // the given (ingestion) order

    // Ascending by title.
    clickHeaderSection(list, MediaListModel::Title);
    QCOMPARE(proxy->sortColumn(), MediaListModel::Title);
    QCOMPARE(proxy->sortOrder(), Qt::AscendingOrder);
    QCOMPARE(titleAt(list, 0), QString("Ade"));
    QCOMPARE(titleAt(list, 2), QString("Zed"));

    // Descending.
    clickHeaderSection(list, MediaListModel::Title);
    QCOMPARE(proxy->sortOrder(), Qt::DescendingOrder);
    QCOMPARE(titleAt(list, 0), QString("Zed"));

    // Off (back to ingestion order).
    clickHeaderSection(list, MediaListModel::Title);
    QCOMPARE(proxy->sortColumn(), -1);
    QCOMPARE(titleAt(list, 0), QString("Ade"));
    QCOMPARE(titleAt(list, 2), QString("Zed"));
}

void LibraryWidgetTest::headerClickSortNumericColumn()
{
    // Two tracks of 2009 and 2010: sorted numerically, not lexicographically
    // (as strings "2010" would come before "2009").
    const QUrl url1 = makeTaggedFileWith(m_dir->path(), "n1.mp3",
        { { "TITLE", "N" }, { "ARTIST", "N" }, { "ALBUM", "N" },
          { "TRACKNUMBER", "1" }, { "DATE", "2009" }, });
    if (url1.isEmpty())
        QSKIP("ffmpeg not available to generate a tagged file");
    const QUrl url2 = makeTaggedFileWith(m_dir->path(), "n2.mp3",
        { { "TITLE", "N" }, { "ARTIST", "N" }, { "ALBUM", "N" },
          { "TRACKNUMBER", "2" }, { "DATE", "2010" }, });
    if (url2.isEmpty())
        QSKIP("ffmpeg not available to generate a tagged file");
    addFileAndWait(url1);
    addFileAndWait(url2);

    MediaListView *list = m_widget->listView();
    clickHeaderSection(list, MediaListModel::Year);
    QCOMPARE(list->listModel()->data(
        list->listModel()->index(0, MediaListModel::Year), Qt::DisplayRole)
        .toString(),
        QString("2009"));
    QCOMPARE(list->listModel()->data(
        list->listModel()->index(1, MediaListModel::Year), Qt::DisplayRole)
        .toString(),
        QString("2010"));
}

void LibraryWidgetTest::groupDoubleClickAppendsAllTracks()
{
    PlaylistManager *pl = PlaylistManager::getInstance();
    pl->reset();
    QCOMPARE(pl->count(), 0);

    addTaggedFile("a1.mp3", "One", "Ade", "Al 1");
    addTaggedFile("a2.mp3", "Three", "Ade", "Al 2");
    addTaggedFile("a3.mp3", "Two", "Bee", "Al 1");

    QTest::mouseClick(m_widget->findChild<QToolButton *>("viewArtistButton"),
                      Qt::LeftButton);
    MediaGroupModel *model = m_widget->groupView()->groupModel();
    const QModelIndex ade = model->index(0, 0);
    QCOMPARE(model->rowText(ade), QString("Ade (2)"));

    // Double-clicking the artist row appends every track beneath it.
    QMetaObject::invokeMethod(m_widget, "onItemDoubleClicked",
                              Q_ARG(QModelIndex, ade));
    QCOMPARE(pl->count(), 2); // both of Ade's tracks
}

void LibraryWidgetTest::groupRemoveRemovesWholeGroup()
{
    addTaggedFile("a1.mp3", "One", "Ade", "Al 1");
    addTaggedFile("a2.mp3", "Three", "Ade", "Al 2");
    addTaggedFile("a3.mp3", "Two", "Bee", "Al 1");

    QTest::mouseClick(m_widget->findChild<QToolButton *>("viewArtistButton"),
                      Qt::LeftButton);
    MediaGroupModel *model = m_widget->groupView()->groupModel();
    selectViewRow(m_widget->groupView(), model->index(0, 0)); // the "Ade" group row

    m_widget->findChild<QToolButton *>("removeButton")->click();

    QCOMPARE(m_manager->count(), 1); // only Bee's track left
    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(model->rowText(model->index(0, 0)), QString("Bee (1)"));
}

void LibraryWidgetTest::groupDragPayloadExpandsToTracks()
{
    addTaggedFile("a1.mp3", "One", "Ade", "Al 1");
    addTaggedFile("a2.mp3", "Three", "Ade", "Al 2");
    addTaggedFile("a3.mp3", "Two", "Bee", "Al 1");

    QTest::mouseClick(m_widget->findChild<QToolButton *>("viewAlbumButton"),
                      Qt::LeftButton);
    MediaGroupModel *model = m_widget->groupView()->groupModel();
    // "Al 2" is the second top-level group (name sort).
    const QModelIndex al2 = model->index(1, 0);
    QCOMPARE(model->rowText(al2), QString("Al 2 (1)"));

    QMimeData *md = model->mimeData({ al2 });
    QVERIFY(md);
    const MediaListModel::DragData drag = MediaListModel::dragDataFromMimeData(md);
    QCOMPARE(drag.ids.size(), 1); // the album's single track
    QCOMPARE(drag.origin, MediaListModel::libraryOrigin());
    QVERIFY(drag.rows.isEmpty()); // a group drag is an insertion, not a reorder
    md->deleteLater();
}

void LibraryWidgetTest::groupDropIntoPlaylistAddsAllTracks()
{
    PlaylistManager *pl = PlaylistManager::getInstance();
    pl->reset();
    QCOMPARE(pl->count(), 0);

    addTaggedFile("a1.mp3", "One", "Ade", "Al 1");
    addTaggedFile("a2.mp3", "Three", "Ade", "Al 2");
    addTaggedFile("a3.mp3", "Two", "Bee", "Al 1");

    QTest::mouseClick(m_widget->findChild<QToolButton *>("viewArtistButton"),
                      Qt::LeftButton);
    MediaGroupModel *model = m_widget->groupView()->groupModel();
    const QModelIndex ade = model->index(0, 0);

    // Drop the artist row onto the playlist view: every track beneath it
    // lands in the playlist.
    QMimeData *md = model->mimeData({ ade });
    QVERIFY(md);
    CurrentPlaylistWidget *cpl = new CurrentPlaylistWidget;
    cpl->show();
    QVERIFY(cpl->listView()->listModel()->dropMimeData(
        md, Qt::CopyAction, 0, 0, QModelIndex()));
    md->deleteLater();
    QCOMPARE(pl->count(), 2); // all of Ade's tracks
    delete cpl;
}

void LibraryWidgetTest::searchInArtistTree()
{
    addTaggedFile("a1.mp3", "One", "Ade", "Al 1");
    addTaggedFile("a2.mp3", "Two", "Bee", "Al 1");

    QTest::mouseClick(m_widget->findChild<QToolButton *>("viewArtistButton"),
                      Qt::LeftButton);
    MediaGroupModel *model = m_widget->groupView()->groupModel();
    QCOMPARE(model->rowCount(), 2);

    // The search filters the items before the tree is built: Bee's group
    // has no matching track and falls away.
    QLineEdit *search = m_widget->findChild<QLineEdit *>("searchBox");
    QVERIFY(search);
    search->setText("Ade");
    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(model->rowText(model->index(0, 0)), QString("Ade (1)"));
}
}


QTEST_MAIN(LibraryWidgetTest)
#include "librarywidget_test.moc"
