#include <QtTest>

#include "settingsmanager.h"
#include "sourcemanager.h"
#include "trackmetadata.h"
#include "urlimportdialog.h"

#include <QContextMenuEvent>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <memory>

namespace {

using ListEntries = SourceProvider::ListEntries;
using ListVideo = SourceProvider::ListVideo;

// A provider that claims the URLs it is told to. Metadata and list
// answers come from its tables; with m_async the callbacks are queued
// (the GUI thread contract, an event loop spin needed to land them).
class UrlProvider : public SourceProvider
{
    Q_OBJECT
public:
    explicit UrlProvider(QObject *parent = nullptr) : SourceProvider(parent) {}

    QString id() const override { return QStringLiteral("urltest"); }
    QString title() const override { return QStringLiteral("Url Test"); }
    bool accepts(const QUrl &url) const override
    {
        return m_urls.contains(url.toString());
    }
    void fetchMetadata(const QUrl &url, MetadataCallback cb) override
    {
        const TrackMeta meta = m_meta.value(url.toString());
        if (m_async) {
            QMetaObject::invokeMethod(this, [cb, meta] { cb(meta); },
                                      Qt::QueuedConnection);
            return;
        }
        cb(meta);
    }
    void fetchList(const QUrl &url, const ListCallback &cb) override
    {
        const auto it = m_lists.constFind(url.toString());
        const ListEntries entries =
            it == m_lists.constEnd() ? ListEntries() : it.value();
        if (m_async) {
            QMetaObject::invokeMethod(this, [cb, entries] { cb(entries); },
                                      Qt::QueuedConnection);
            return;
        }
        cb(entries);
    }

    QStringList m_urls;
    QHash<QString, TrackMeta> m_meta;
    QHash<QString, ListEntries> m_lists;
    bool m_async = false;
};

class UrlImportDialogTest : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void inputTrimsAndDropsEmptyLines();
    void inputResolvesExistingRelativePaths();
    void emptyInputStaysOnInputStage();
    void fastPathWithoutProviders();
    void fastPathWhenNoListBehindClaimedUrls();
    void failedListShownInReviewNotImported();
    void emptyCollectionShownAsEmptySection();
    void failedListMixedWithGoodList();
    void listShownInReviewWithMetadata();
    void plainSourceShowsFetchedMetadata();
    void plainSourceFailedFetchShowsUrl();
    void localPathShowsFileName();
    void uncheckExcludesFromConfirmed();
    void sectionCheckboxTogglesWholeList();
    void dedupAcrossSources();
    void cancelOnReviewConfirmsNothing();
    void cancelOnWorkingStageDiscardsPendingCallbacks();
    void sourceMetaCarriesListMetadata();
    void workingStatusNamesTheExpansion();
    void filterHidesNonMatchingRows();
    void filterMatchesArtistCaseInsensitively();
    void filterKeepsHiddenRowsCheckState();
    void selectNoneThenFilterThenPick();
    void selectAllChecksOnlyVisibleRows();
    void rowContextMenuEditsMetadata();
    void cancelEditChangesNothing();
    void bulkEditAppliesToSelectedRows();
    void plainSourceEditOverridesFetchedMetadata();
    void headAndFailedRowsHaveNoEditMenu();
    void rejectSavesTheColumnWidths();

private:
    UrlImportDialog* openDialog();
    void submit(UrlImportDialog* dialog, const QStringList &lines);
    int stage(UrlImportDialog* dialog) const;
    QTreeWidget* tree(UrlImportDialog* dialog) const;
    QList<QUrl> confirmed(const QSignalSpy &spy) const;
    QHash<QString, TrackMeta> sourceMeta(const QSignalSpy &spy) const;
    // The row's context menu (nullptr when the row offers none).
    QMenu* requestRowMenu(UrlImportDialog* dialog, QTreeWidgetItem* item);
    // The open metadata editor (nullptr when none is open).
    QDialog* openEditor() const;

    UrlProvider m_provider{this};
    SettingsManager* m_settings = nullptr;
    std::unique_ptr<QTemporaryDir> m_stateDir;
};

void UrlImportDialogTest::init()
{
    SourceManager::getInstance()->clear();
    m_provider.m_urls.clear();
    m_provider.m_meta.clear();
    m_provider.m_lists.clear();
    m_provider.m_async = false;
    // A fresh settings file per test (the dialog persists its state
    // there).
    m_stateDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_stateDir->isValid());
    m_settings = SettingsManager::getInstance();
    m_settings->setSettingsPath(m_stateDir->path() + "/settings.json");
    m_settings->load();
}

void UrlImportDialogTest::cleanup()
{
    SourceManager::getInstance()->clear();
    SettingsManager::reset();
}

UrlImportDialog* UrlImportDialogTest::openDialog()
{
    UrlImportDialog* dialog = new UrlImportDialog();
    dialog->show();
    return dialog;
}

void UrlImportDialogTest::submit(UrlImportDialog* dialog,
                                 const QStringList &lines)
{
    dialog->findChild<QPlainTextEdit*>("urlInput")->setPlainText(
        lines.join('\n'));
    dialog->findChild<QPushButton*>("okButton")->click();
}

int UrlImportDialogTest::stage(UrlImportDialog* dialog) const
{
    return dialog->findChild<QStackedWidget*>("stageStack")->currentIndex();
}

QTreeWidget* UrlImportDialogTest::tree(UrlImportDialog* dialog) const
{
    return dialog->findChild<QTreeWidget*>("reviewTree");
}

QList<QUrl> UrlImportDialogTest::confirmed(const QSignalSpy &spy) const
{
    return spy[0][0].value<QList<QUrl>>();
}

QHash<QString, TrackMeta> UrlImportDialogTest::sourceMeta(const QSignalSpy &spy) const
{
    return spy[0][1].value<QHash<QString, TrackMeta>>();
}

QMenu* UrlImportDialogTest::requestRowMenu(UrlImportDialog* dialog,
                                           QTreeWidgetItem* item)
{
    QTreeWidget* t = tree(dialog);
    t->resize(500, 300);
    QApplication::processEvents();
    const QRect rect = t->visualItemRect(item);
    if (rect.isEmpty())
        return nullptr;
    QContextMenuEvent event(QContextMenuEvent::Mouse, rect.center(),
                            rect.center());
    QApplication::sendEvent(t->viewport(), &event);
    return dialog->findChild<QMenu*>();
}

QDialog* UrlImportDialogTest::openEditor() const
{
    return qobject_cast<QDialog*>(qApp->activeModalWidget());
}

void UrlImportDialogTest::inputTrimsAndDropsEmptyLines()
{
    // No provider claims anything: the fast path confirms the batch as
    // pasted, the trimmed lines only.
    UrlImportDialog* dialog = openDialog();
    QSignalSpy spy(dialog, &UrlImportDialog::sourcesConfirmed);
    submit(dialog, { "  https://example.com/audio/a.mp3  ", "",
                     "https://example.com/audio/b.mp3", "   ", });

    QCOMPARE(spy.count(), 1);
    QCOMPARE(confirmed(spy),
             QList<QUrl>({ QUrl("https://example.com/audio/a.mp3"),
                           QUrl("https://example.com/audio/b.mp3") }));
}

void UrlImportDialogTest::inputResolvesExistingRelativePaths()
{
    // A line naming a file that exists relative to the current directory
    // is its file: URL (the user-input fallback would have guessed
    // http://).
    const QString cwd = QDir::currentPath();
    QDir::setCurrent(m_stateDir->path());
    {
        QFile f(QStringLiteral("rel.mp3"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
    }
    UrlImportDialog* dialog = openDialog();
    QSignalSpy spy(dialog, &UrlImportDialog::sourcesConfirmed);
    submit(dialog, { QStringLiteral("rel.mp3"),
                     QStringLiteral("https://example.com/audio/c.mp3"), });

    QCOMPARE(spy.count(), 1);
    QCOMPARE(confirmed(spy),
             QList<QUrl>({ QUrl::fromLocalFile(m_stateDir->path() + "/rel.mp3"),
                           QUrl("https://example.com/audio/c.mp3") }));
    QDir::setCurrent(cwd);
}

void UrlImportDialogTest::emptyInputStaysOnInputStage()
{
    UrlImportDialog* dialog = openDialog();
    QSignalSpy spy(dialog, &UrlImportDialog::sourcesConfirmed);
    submit(dialog, { "", "   " });

    QCOMPARE(stage(dialog), 0);
    QCOMPARE(spy.count(), 0);
}

void UrlImportDialogTest::fastPathWithoutProviders()
{
    // A plain stream needs no read: the batch imports as pasted, no
    // review (the dialog accepts itself).
    UrlImportDialog* dialog = openDialog();
    QSignalSpy spy(dialog, &UrlImportDialog::sourcesConfirmed);
    submit(dialog, { "https://example.com/audio/stream.mp3" });

    QCOMPARE(spy.count(), 1);
    QCOMPARE(confirmed(spy),
             QList<QUrl>({ QUrl("https://example.com/audio/stream.mp3") }));
}

void UrlImportDialogTest::fastPathWhenNoListBehindClaimedUrls()
{
    // A claimed url whose fetchList comes back empty references no
    // collection: it imports as pasted, no review.
    const QUrl source("https://music.example.com/watch?v=1");
    m_provider.m_urls = { source.toString() };
    m_provider.m_meta[source.toString()].title = "A Track";
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    UrlImportDialog* dialog = openDialog();
    QSignalSpy spy(dialog, &UrlImportDialog::sourcesConfirmed);
    submit(dialog, { source.toString() });

    QCOMPARE(spy.count(), 1);
    QCOMPARE(confirmed(spy), QList<QUrl>({ source }));
}

void UrlImportDialogTest::failedListShownInReviewNotImported()
{
    // A claimed url whose collection read failed: no fast path (a
    // failed expansion is not "no list behind it"), the review lists
    // the link under "Failed sources" — not checkable, never imported.
    const QUrl source("https://open.spotify.com/album/abc");
    m_provider.m_urls = { source.toString() };
    ListEntries failed;
    failed.isCollection = true;
    failed.error = QStringLiteral("HTTP 404: Resource not found");
    m_provider.m_lists[source.toString()] = failed;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    UrlImportDialog* dialog = openDialog();
    QSignalSpy spy(dialog, &UrlImportDialog::sourcesConfirmed);
    submit(dialog, { source.toString() });

    QCOMPARE(stage(dialog), 2);
    QCOMPARE(spy.count(), 0);
    QTreeWidget* t = tree(dialog);
    QCOMPARE(t->topLevelItemCount(), 1);
    QCOMPARE(t->topLevelItem(0)->text(0), QString("Failed sources  (1)"));
    QTreeWidgetItem* row = t->topLevelItem(0)->child(0);
    QCOMPARE(row->text(0), source.toString());
    QCOMPARE(row->text(1),
             QString("failed: HTTP 404: Resource not found"));
    QCOMPARE(row->checkState(0), Qt::Unchecked);
    QCOMPARE(row->flags() & Qt::ItemIsUserCheckable, Qt::ItemFlags());

    // Nothing importable: the Add button is disabled and stays so.
    QPushButton* add = dialog->findChild<QPushButton*>("addButton");
    QCOMPARE(add->text(), QString("Add"));
    QVERIFY(!add->isEnabled());

    // Even a select-all (and a section-head toggle) must not make the
    // failed row importable.
    dialog->findChild<QPushButton*>("selectAllButton")->click();
    QCOMPARE(row->checkState(0), Qt::Unchecked);
    t->topLevelItem(0)->setCheckState(0, Qt::Checked);
    QCOMPARE(row->checkState(0), Qt::Unchecked);
    QCOMPARE(add->isEnabled(), false);
}

void UrlImportDialogTest::emptyCollectionShownAsEmptySection()
{
    // A claimed url whose collection is legitimately empty: a section
    // with no rows — nothing to import, no failed row.
    const QUrl source("https://open.spotify.com/playlist/empty");
    m_provider.m_urls = { source.toString() };
    ListEntries empty;
    empty.title = QStringLiteral("An Empty Playlist");
    empty.isCollection = true;
    m_provider.m_lists[source.toString()] = empty;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    UrlImportDialog* dialog = openDialog();
    QSignalSpy spy(dialog, &UrlImportDialog::sourcesConfirmed);
    submit(dialog, { source.toString() });

    QCOMPARE(stage(dialog), 2);
    QTreeWidget* t = tree(dialog);
    QCOMPARE(t->topLevelItemCount(), 1);
    QCOMPARE(t->topLevelItem(0)->text(0),
             QString("An Empty Playlist  (0)"));
    QCOMPARE(t->topLevelItem(0)->childCount(), 0);

    QPushButton* add = dialog->findChild<QPushButton*>("addButton");
    QCOMPARE(add->text(), QString("Add"));
    QVERIFY(!add->isEnabled());
}

void UrlImportDialogTest::failedListMixedWithGoodList()
{
    // A good list and a failed one: the failed link is excluded from
    // the confirmed import, the list's rows are not.
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    const QUrl failedUrl("https://open.spotify.com/artist/xyz");
    m_provider.m_urls = { listUrl.toString(), failedUrl.toString() };
    ListEntries list;
    list.title = QStringLiteral("The List");
    list.videos = { ListVideo(QUrl("https://music.example.com/watch?v=1")) };
    m_provider.m_lists[listUrl.toString()] = list;
    ListEntries failed;
    failed.isCollection = true;
    failed.error = QStringLiteral("HTTP 400: Invalid limit");
    m_provider.m_lists[failedUrl.toString()] = failed;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    UrlImportDialog* dialog = openDialog();
    QSignalSpy spy(dialog, &UrlImportDialog::sourcesConfirmed);
    submit(dialog, { listUrl.toString(), failedUrl.toString() });

    QCOMPARE(stage(dialog), 2);
    QTreeWidget* t = tree(dialog);
    QCOMPARE(t->topLevelItemCount(), 2);
    QCOMPARE(t->topLevelItem(0)->text(0), QString("The List  (1)"));
    QCOMPARE(t->topLevelItem(1)->text(0), QString("Failed sources  (1)"));
    QTreeWidgetItem* failedRow = t->topLevelItem(1)->child(0);
    QCOMPARE(failedRow->checkState(0), Qt::Unchecked);

    // One importable item.
    QPushButton* add = dialog->findChild<QPushButton*>("addButton");
    QCOMPARE(add->text(), QString("Add 1 item"));
    add->click();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(confirmed(spy),
             QList<QUrl>({ QUrl("https://music.example.com/watch?v=1") }));
}

void UrlImportDialogTest::listShownInReviewWithMetadata()
{
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    m_provider.m_urls = { listUrl.toString() };
    ListEntries list;
    list.title = "The List";
    ListVideo first(QUrl("https://music.example.com/watch?v=1"));
    first.title = "First";
    first.artist = "The Band";
    first.durationMs = 133000;
    ListVideo second(QUrl("https://music.example.com/watch?v=2"));
    second.title = "Second";
    list.videos = { first, second };
    m_provider.m_lists[listUrl.toString()] = list;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    UrlImportDialog* dialog = openDialog();
    QSignalSpy spy(dialog, &UrlImportDialog::sourcesConfirmed);
    submit(dialog, { listUrl.toString() });

    // The review stage, the list as its only section.
    QCOMPARE(stage(dialog), 2);
    QCOMPARE(spy.count(), 0);
    QTreeWidget* t = tree(dialog);
    QCOMPARE(t->topLevelItemCount(), 1);
    QCOMPARE(t->topLevelItem(0)->text(0), QString("The List  (2)"));
    QCOMPARE(t->topLevelItem(0)->childCount(), 2);
    QTreeWidgetItem* row0 = t->topLevelItem(0)->child(0);
    QCOMPARE(row0->text(0), QString("First"));
    QCOMPARE(row0->text(1), QString("The Band"));
    QCOMPARE(row0->text(2), QString(""));
    QCOMPARE(row0->text(3), QString("2:13"));
    QCOMPARE(row0->checkState(0), Qt::Checked);
    QCOMPARE(t->topLevelItem(0)->child(1)->text(0), QString("Second"));

    // Everything checked: the Add button counts the deduped batch.
    QPushButton* add = dialog->findChild<QPushButton*>("addButton");
    QCOMPARE(add->text(), QString("Add 2 items"));
    QVERIFY(add->isEnabled());

    add->click();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(confirmed(spy),
             QList<QUrl>({ QUrl("https://music.example.com/watch?v=1"),
                           QUrl("https://music.example.com/watch?v=2") }));
}

void UrlImportDialogTest::plainSourceShowsFetchedMetadata()
{
    // A list and a plain claimed link: the plain one's fetched metadata
    // fills its review row (under "Other sources").
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    const QUrl plain("https://music.example.com/watch?v=9");
    m_provider.m_urls = { listUrl.toString(), plain.toString() };
    m_provider.m_meta[plain.toString()].title = "Plain Track";
    m_provider.m_meta[plain.toString()].artist = "Solo";
    ListEntries list;
    list.title = "The List";
    list.videos = { ListVideo(QUrl("https://music.example.com/watch?v=1")) };
    m_provider.m_lists[listUrl.toString()] = list;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    UrlImportDialog* dialog = openDialog();
    QSignalSpy spy(dialog, &UrlImportDialog::sourcesConfirmed);
    submit(dialog, { listUrl.toString(), plain.toString() });

    QCOMPARE(stage(dialog), 2);
    QTreeWidget* t = tree(dialog);
    QCOMPARE(t->topLevelItemCount(), 2);
    QCOMPARE(t->topLevelItem(1)->text(0), QString("Other sources  (1)"));
    QCOMPARE(t->topLevelItem(1)->child(0)->text(0), QString("Plain Track"));
    QCOMPARE(t->topLevelItem(1)->child(0)->text(1), QString("Solo"));

    dialog->findChild<QPushButton*>("addButton")->click();
    QCOMPARE(confirmed(spy),
             QList<QUrl>({ QUrl("https://music.example.com/watch?v=1"), plain }));
}

void UrlImportDialogTest::plainSourceFailedFetchShowsUrl()
{
    // The plain source's metadata fetch fails (an empty answer): the row
    // shows the url and stays checkable (the import's own fetch retries
    // it, no metadata is provided for it).
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    const QUrl plain("https://music.example.com/watch?v=9");
    m_provider.m_urls = { listUrl.toString(), plain.toString() };
    // No meta table entry for the plain url: the fetch answers empty.
    ListEntries list;
    list.videos = { ListVideo(QUrl("https://music.example.com/watch?v=1")) };
    m_provider.m_lists[listUrl.toString()] = list;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    UrlImportDialog* dialog = openDialog();
    QSignalSpy spy(dialog, &UrlImportDialog::sourcesConfirmed);
    submit(dialog, { listUrl.toString(), plain.toString() });

    QCOMPARE(stage(dialog), 2);
    QTreeWidget* t = tree(dialog);
    QCOMPARE(t->topLevelItem(1)->child(0)->text(0), plain.toString());
    QCOMPARE(t->topLevelItem(1)->child(0)->checkState(0), Qt::Checked);

    dialog->findChild<QPushButton*>("addButton")->click();
    QCOMPARE(confirmed(spy),
             QList<QUrl>({ QUrl("https://music.example.com/watch?v=1"), plain }));
    // No provided metadata for the failed fetch.
    QVERIFY(!sourceMeta(spy).contains(plain.toString()));
}

void UrlImportDialogTest::localPathShowsFileName()
{
    // A pasted local path shows its file name (underscores as spaces).
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    const QString path = "/tmp/some_pasted_file.mp3";
    m_provider.m_urls = { listUrl.toString() };
    ListEntries list;
    list.videos = { ListVideo(QUrl("https://music.example.com/watch?v=1")) };
    m_provider.m_lists[listUrl.toString()] = list;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    UrlImportDialog* dialog = openDialog();
    submit(dialog, { listUrl.toString(), path });

    QCOMPARE(stage(dialog), 2);
    QTreeWidget* t = tree(dialog);
    QCOMPARE(t->topLevelItemCount(), 2);
    QCOMPARE(t->topLevelItem(1)->child(0)->text(0), QString("some pasted file"));
}

void UrlImportDialogTest::uncheckExcludesFromConfirmed()
{
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    m_provider.m_urls = { listUrl.toString() };
    ListEntries list;
    list.videos = { ListVideo(QUrl("https://music.example.com/watch?v=1")),
                    ListVideo(QUrl("https://music.example.com/watch?v=2")), };
    m_provider.m_lists[listUrl.toString()] = list;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    UrlImportDialog* dialog = openDialog();
    QSignalSpy spy(dialog, &UrlImportDialog::sourcesConfirmed);
    submit(dialog, { listUrl.toString() });

    QTreeWidget* t = tree(dialog);
    t->topLevelItem(0)->child(0)->setCheckState(0, Qt::Unchecked);
    QPushButton* add = dialog->findChild<QPushButton*>("addButton");
    QCOMPARE(add->text(), QString("Add 1 item"));

    add->click();
    QCOMPARE(confirmed(spy),
             QList<QUrl>({ QUrl("https://music.example.com/watch?v=2") }));
}

void UrlImportDialogTest::sectionCheckboxTogglesWholeList()
{
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    m_provider.m_urls = { listUrl.toString() };
    ListEntries list;
    list.videos = { ListVideo(QUrl("https://music.example.com/watch?v=1")),
                    ListVideo(QUrl("https://music.example.com/watch?v=2")), };
    m_provider.m_lists[listUrl.toString()] = list;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    UrlImportDialog* dialog = openDialog();
    QSignalSpy spy(dialog, &UrlImportDialog::sourcesConfirmed);
    submit(dialog, { listUrl.toString() });

    QTreeWidget* t = tree(dialog);
    QPushButton* add = dialog->findChild<QPushButton*>("addButton");
    // Unchecking the section head takes the whole list with it.
    t->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
    QCOMPARE(t->topLevelItem(0)->child(0)->checkState(0), Qt::Unchecked);
    QCOMPARE(t->topLevelItem(0)->child(1)->checkState(0), Qt::Unchecked);
    QVERIFY(!add->isEnabled());
    QCOMPARE(add->text(), QString("Add"));
    // ...and checking it back confirms it again.
    t->topLevelItem(0)->setCheckState(0, Qt::Checked);
    QCOMPARE(t->topLevelItem(0)->child(0)->checkState(0), Qt::Checked);
    QVERIFY(add->isEnabled());

    add->click();
    QCOMPARE(confirmed(spy).size(), 2);
}

void UrlImportDialogTest::dedupAcrossSources()
{
    // A video the batch carries twice (a list and a plain paste of the
    // same url) imports once, at its first position.
    const QUrl v1("https://music.example.com/watch?v=1");
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    m_provider.m_urls = { listUrl.toString(), v1.toString() };
    ListEntries list;
    list.videos = { ListVideo(v1),
                    ListVideo(QUrl("https://music.example.com/watch?v=2")), };
    m_provider.m_lists[listUrl.toString()] = list;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    UrlImportDialog* dialog = openDialog();
    QSignalSpy spy(dialog, &UrlImportDialog::sourcesConfirmed);
    submit(dialog, { listUrl.toString(), v1.toString() });

    QCOMPARE(stage(dialog), 2);
    dialog->findChild<QPushButton*>("addButton")->click();
    QCOMPARE(confirmed(spy),
             QList<QUrl>({ v1, QUrl("https://music.example.com/watch?v=2") }));
}

void UrlImportDialogTest::cancelOnReviewConfirmsNothing()
{
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    m_provider.m_urls = { listUrl.toString() };
    ListEntries list;
    list.videos = { ListVideo(QUrl("https://music.example.com/watch?v=1")) };
    m_provider.m_lists[listUrl.toString()] = list;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    UrlImportDialog* dialog = openDialog();
    QSignalSpy spy(dialog, &UrlImportDialog::sourcesConfirmed);
    submit(dialog, { listUrl.toString() });

    QCOMPARE(stage(dialog), 2);
    dialog->findChild<QPushButton*>("reviewCancelButton")->click();
    QCOMPARE(spy.count(), 0);
}

void UrlImportDialogTest::cancelOnWorkingStageDiscardsPendingCallbacks()
{
    // An async provider (the queued callbacks land after the cancel):
    // the cancel discards them — no signal, no crash (the QPointer
    // guards in the provider callbacks).
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    m_provider.m_urls = { listUrl.toString() };
    m_provider.m_async = true;
    ListEntries list;
    list.videos = { ListVideo(QUrl("https://music.example.com/watch?v=1")) };
    m_provider.m_lists[listUrl.toString()] = list;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    UrlImportDialog* dialog = openDialog();
    QSignalSpy spy(dialog, &UrlImportDialog::sourcesConfirmed);
    submit(dialog, { listUrl.toString() });

    // The fetches are in flight: the working stage, the status names the
    // expansion.
    QCOMPARE(stage(dialog), 1);
    QCOMPARE(dialog->findChild<QLabel*>("statusLabel")->text().isEmpty(), false);

    dialog->findChild<QPushButton*>("workingCancelButton")->click();
    // The queued callbacks land into the destroyed dialog: the guards
    // keep them no-ops.
    QTest::qWait(200);
    QCOMPARE(spy.count(), 0);
}

void UrlImportDialogTest::sourceMetaCarriesListMetadata()
{
    // The confirmed sources carry the list's display metadata as-is:
    // the import uses it directly (no re-fetch).
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    m_provider.m_urls = { listUrl.toString() };
    ListEntries list;
    ListVideo video(QUrl("https://music.example.com/watch?v=1"));
    video.title = "First";
    video.artist = "The Band";
    video.album = "The Album";
    video.durationMs = 133000;
    list.videos = { video };
    m_provider.m_lists[listUrl.toString()] = list;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    UrlImportDialog* dialog = openDialog();
    QSignalSpy spy(dialog, &UrlImportDialog::sourcesConfirmed);
    submit(dialog, { listUrl.toString() });

    dialog->findChild<QPushButton*>("addButton")->click();
    QCOMPARE(spy.count(), 1);
    const QHash<QString, TrackMeta> meta = sourceMeta(spy);
    QVERIFY(meta.contains(video.url.toString()));
    QCOMPARE(meta.value(video.url.toString()).title, QString("First"));
    QCOMPARE(meta.value(video.url.toString()).artist, QString("The Band"));
    QCOMPARE(meta.value(video.url.toString()).album, QString("The Album"));
    QCOMPARE(meta.value(video.url.toString()).durationMs, 133000);
}

void UrlImportDialogTest::workingStatusNamesTheExpansion()
{
    // An async list fetch: the working status names the sources left.
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    m_provider.m_urls = { listUrl.toString() };
    m_provider.m_async = true;
    ListEntries list;
    list.title = "The List";
    list.videos = { ListVideo(QUrl("https://music.example.com/watch?v=1")) };
    m_provider.m_lists[listUrl.toString()] = list;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    UrlImportDialog* dialog = openDialog();
    submit(dialog, { listUrl.toString() });

    QCOMPARE(stage(dialog), 1);
    QCOMPARE(dialog->findChild<QLabel*>("statusLabel")->text(),
             QString("Expanding 1 source(s)…"));

    // The fetch lands: the status reports the found list.
    QTRY_COMPARE_WITH_TIMEOUT(stage(dialog), 2, 5000);
    QCOMPARE(dialog->findChild<QLabel*>("statusLabel")->text(),
             QString("Found 'The List' (1 videos)"));
}

void UrlImportDialogTest::filterHidesNonMatchingRows()
{
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    m_provider.m_urls = { listUrl.toString() };
    ListEntries list;
    ListVideo a(QUrl("https://music.example.com/watch?v=1"));
    a.title = "Alpha One";
    ListVideo b(QUrl("https://music.example.com/watch?v=2"));
    b.title = "Beta Two";
    ListVideo c(QUrl("https://music.example.com/watch?v=3"));
    c.title = "Gamma Three";
    list.videos = { a, b, c };
    m_provider.m_lists[listUrl.toString()] = list;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    UrlImportDialog* dialog = openDialog();
    submit(dialog, { listUrl.toString() });
    QCOMPARE(stage(dialog), 2);
    QTreeWidget* t = tree(dialog);
    QLineEdit* filter = dialog->findChild<QLineEdit*>("filterInput");

    // A case-insensitive substring of the title.
    filter->setText("ALPHA");
    QVERIFY(!t->topLevelItem(0)->isHidden());
    QVERIFY(!t->topLevelItem(0)->child(0)->isHidden());
    QVERIFY(t->topLevelItem(0)->child(1)->isHidden());
    QVERIFY(t->topLevelItem(0)->child(2)->isHidden());

    // No match: the section itself goes away.
    filter->setText("zzz");
    QVERIFY(t->topLevelItem(0)->isHidden());

    // Clearing restores the view.
    filter->clear();
    QVERIFY(!t->topLevelItem(0)->isHidden());
    QVERIFY(!t->topLevelItem(0)->child(2)->isHidden());
}

void UrlImportDialogTest::filterMatchesArtistCaseInsensitively()
{
    // The filter matches the artist column as well.
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    m_provider.m_urls = { listUrl.toString() };
    ListEntries list;
    ListVideo a(QUrl("https://music.example.com/watch?v=1"));
    a.title = "Track One";
    a.artist = "The Band";
    ListVideo b(QUrl("https://music.example.com/watch?v=2"));
    b.title = "Track Two";
    b.artist = "The Other";
    list.videos = { a, b };
    m_provider.m_lists[listUrl.toString()] = list;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    UrlImportDialog* dialog = openDialog();
    submit(dialog, { listUrl.toString() });

    QTreeWidget* t = tree(dialog);
    dialog->findChild<QLineEdit*>("filterInput")->setText("the band");
    QVERIFY(!t->topLevelItem(0)->child(0)->isHidden());
    QVERIFY(t->topLevelItem(0)->child(1)->isHidden());
}

void UrlImportDialogTest::filterKeepsHiddenRowsCheckState()
{
    // The filter is display-only: a hidden row keeps its check state, so
    // a hidden checked row is still part of the confirmation.
    const QUrl v1("https://music.example.com/watch?v=1");
    const QUrl v2("https://music.example.com/watch?v=2");
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    m_provider.m_urls = { listUrl.toString() };
    ListEntries list;
    ListVideo a(v1);
    a.title = "First";
    ListVideo b(v2);
    b.title = "Second";
    list.videos = { a, b };
    m_provider.m_lists[listUrl.toString()] = list;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    UrlImportDialog* dialog = openDialog();
    QSignalSpy spy(dialog, &UrlImportDialog::sourcesConfirmed);
    submit(dialog, { listUrl.toString() });

    QTreeWidget* t = tree(dialog);
    dialog->findChild<QLineEdit*>("filterInput")->setText("first");
    QVERIFY(t->topLevelItem(0)->child(0)->isHidden() == false);
    QVERIFY(t->topLevelItem(0)->child(1)->isHidden());
    // Uncheck the only visible row: the hidden row stays checked and
    // counts for the Add button.
    t->topLevelItem(0)->child(0)->setCheckState(0, Qt::Unchecked);
    QPushButton* add = dialog->findChild<QPushButton*>("addButton");
    QCOMPARE(add->text(), QString("Add 1 item"));

    add->click();
    QCOMPARE(confirmed(spy), QList<QUrl>({ v2 }));
}

void UrlImportDialogTest::selectNoneThenFilterThenPick()
{
    // The big-list workflow: clear the selection, filter, pick the wanted
    // rows, confirm.
    const QUrl v2("https://music.example.com/watch?v=2");
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    m_provider.m_urls = { listUrl.toString() };
    ListEntries list;
    ListVideo a(QUrl("https://music.example.com/watch?v=1"));
    a.title = "Alpha One";
    ListVideo b(v2);
    b.title = "Beta Two";
    ListVideo c(QUrl("https://music.example.com/watch?v=3"));
    c.title = "Gamma Three";
    list.videos = { a, b, c };
    m_provider.m_lists[listUrl.toString()] = list;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    UrlImportDialog* dialog = openDialog();
    QSignalSpy spy(dialog, &UrlImportDialog::sourcesConfirmed);
    submit(dialog, { listUrl.toString() });

    QTreeWidget* t = tree(dialog);
    QPushButton* add = dialog->findChild<QPushButton*>("addButton");
    dialog->findChild<QPushButton*>("selectNoneButton")->click();
    for (int j = 0; j < 3; ++j)
        QCOMPARE(t->topLevelItem(0)->child(j)->checkState(0),
                 Qt::Unchecked);
    QCOMPARE(add->text(), QString("Add"));
    QVERIFY(!add->isEnabled());

    dialog->findChild<QLineEdit*>("filterInput")->setText("beta");
    t->topLevelItem(0)->child(1)->setCheckState(0, Qt::Checked);
    QCOMPARE(add->text(), QString("Add 1 item"));

    add->click();
    QCOMPARE(confirmed(spy), QList<QUrl>({ v2 }));
}

void UrlImportDialogTest::selectAllChecksOnlyVisibleRows()
{
    // Select all only touches the rows visible under the filter; the head
    // reflects all of its rows (the hidden unchecked ones keep it partial).
    const QUrl v1("https://music.example.com/watch?v=1");
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    m_provider.m_urls = { listUrl.toString() };
    ListEntries list;
    ListVideo a(v1);
    a.title = "Alpha One";
    ListVideo b(QUrl("https://music.example.com/watch?v=2"));
    b.title = "Beta Two";
    list.videos = { a, b };
    m_provider.m_lists[listUrl.toString()] = list;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    UrlImportDialog* dialog = openDialog();
    QSignalSpy spy(dialog, &UrlImportDialog::sourcesConfirmed);
    submit(dialog, { listUrl.toString() });

    QTreeWidget* t = tree(dialog);
    QPushButton* add = dialog->findChild<QPushButton*>("addButton");
    dialog->findChild<QPushButton*>("selectNoneButton")->click();
    dialog->findChild<QLineEdit*>("filterInput")->setText("alpha");
    dialog->findChild<QPushButton*>("selectAllButton")->click();

    QCOMPARE(t->topLevelItem(0)->child(0)->checkState(0), Qt::Checked);
    QCOMPARE(t->topLevelItem(0)->child(1)->checkState(0), Qt::Unchecked);
    QCOMPARE(t->topLevelItem(0)->checkState(0), Qt::PartiallyChecked);
    QCOMPARE(add->text(), QString("Add 1 item"));

    add->click();
    QCOMPARE(confirmed(spy), QList<QUrl>({ v1 }));
}

void UrlImportDialogTest::rowContextMenuEditsMetadata()
{
    // A row's context menu edits the row's metadata: the editor is
    // prefilled with the gathered values, the set fields win (trimmed),
    // the unset ones keep, and the edit rides the confirmed metadata.
    const QUrl v1("https://music.example.com/watch?v=1");
    const QUrl v2("https://music.example.com/watch?v=2");
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    m_provider.m_urls = { listUrl.toString() };
    ListEntries list;
    ListVideo a(v1);
    a.title = "First";
    a.artist = "The Band";
    a.album = "Old Album";
    a.durationMs = 1000;
    ListVideo b(v2);
    b.title = "Second";
    b.artist = "The Band";
    b.durationMs = 2000;
    list.videos = { a, b };
    m_provider.m_lists[listUrl.toString()] = list;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    UrlImportDialog* dialog = openDialog();
    QSignalSpy spy(dialog, &UrlImportDialog::sourcesConfirmed);
    submit(dialog, { listUrl.toString() });
    QTreeWidget* t = tree(dialog);
    QTreeWidgetItem* row1 = t->topLevelItem(0)->child(0);
    QTreeWidgetItem* row2 = t->topLevelItem(0)->child(1);
    // The review shows the gathered album.
    QCOMPARE(row1->text(2), QString("Old Album"));

    QMenu* menu = requestRowMenu(dialog, row1);
    QVERIFY(menu != nullptr);
    QCOMPARE(menu->actions().size(), 1);
    QCOMPARE(menu->actions().first()->text(), QString("Edit metadata…"));
    menu->actions().first()->trigger();
    QDialog* edit = openEditor();
    QVERIFY(edit != nullptr);
    // Prefilled with the row's gathered metadata.
    QCOMPARE(edit->findChild<QLineEdit*>("titleEdit")->text(), QString("First"));
    QCOMPARE(edit->findChild<QLineEdit*>("artistEdit")->text(), QString("The Band"));
    QCOMPARE(edit->findChild<QLineEdit*>("albumEdit")->text(), QString("Old Album"));
    edit->findChild<QLineEdit*>("titleEdit")->setText("  Renamed  ");
    edit->findChild<QLineEdit*>("albumEdit")->clear(); // keep: "Old Album"
    edit->findChild<QPushButton*>("okButton")->click();

    // The row follows; the sibling is untouched.
    QCOMPARE(row1->text(0), QString("Renamed"));
    QCOMPARE(row1->text(1), QString("The Band"));
    QCOMPARE(row1->text(2), QString("Old Album"));
    QCOMPARE(row2->text(0), QString("Second"));
    QCOMPARE(row2->text(2), QString(""));

    dialog->findChild<QPushButton*>("addButton")->click();
    QCOMPARE(spy.count(), 1);
    const QHash<QString, TrackMeta> meta = sourceMeta(spy);
    QCOMPARE(meta.value(v1.toString()).title, QString("Renamed"));
    QCOMPARE(meta.value(v1.toString()).artist, QString("The Band"));
    QCOMPARE(meta.value(v1.toString()).album, QString("Old Album"));
    QCOMPARE(meta.value(v2.toString()).title, QString("Second"));
    QCOMPARE(meta.value(v2.toString()).album, QString(""));
}

void UrlImportDialogTest::cancelEditChangesNothing()
{
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    m_provider.m_urls = { listUrl.toString() };
    ListEntries list;
    ListVideo a(QUrl("https://music.example.com/watch?v=1"));
    a.title = "First";
    list.videos = { a };
    m_provider.m_lists[listUrl.toString()] = list;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    UrlImportDialog* dialog = openDialog();
    QSignalSpy spy(dialog, &UrlImportDialog::sourcesConfirmed);
    submit(dialog, { listUrl.toString() });
    QTreeWidgetItem* row = tree(dialog)->topLevelItem(0)->child(0);

    QMenu* menu = requestRowMenu(dialog, row);
    QVERIFY(menu != nullptr);
    menu->actions().first()->trigger();
    QDialog* edit = openEditor();
    QVERIFY(edit != nullptr);
    edit->findChild<QLineEdit*>("titleEdit")->setText("Should Not Stick");
    edit->findChild<QPushButton*>("cancelButton")->click();

    QCOMPARE(row->text(0), QString("First"));
    dialog->findChild<QPushButton*>("addButton")->click();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(sourceMeta(spy).value(a.url.toString()).title, QString("First"));
}

void UrlImportDialogTest::bulkEditAppliesToSelectedRows()
{
    // The selected rows (a section head in the selection is dropped) are
    // edited in bulk: the set fields apply to all, the empty ones keep.
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    m_provider.m_urls = { listUrl.toString() };
    ListEntries list;
    ListVideo a(QUrl("https://music.example.com/watch?v=1"));
    a.title = "First";
    ListVideo b(QUrl("https://music.example.com/watch?v=2"));
    b.title = "Second";
    b.album = "Kept Album";
    list.videos = { a, b };
    m_provider.m_lists[listUrl.toString()] = list;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    UrlImportDialog* dialog = openDialog();
    QSignalSpy spy(dialog, &UrlImportDialog::sourcesConfirmed);
    submit(dialog, { listUrl.toString() });
    QTreeWidget* t = tree(dialog);
    QTreeWidgetItem* head = t->topLevelItem(0);
    QTreeWidgetItem* row1 = head->child(0);
    QTreeWidgetItem* row2 = head->child(1);
    row1->setSelected(true);
    row2->setSelected(true);
    head->setSelected(true); // a head in the selection must not count

    QMenu* menu = requestRowMenu(dialog, row1);
    QVERIFY(menu != nullptr);
    QCOMPARE(menu->actions().first()->text(),
             QString("Edit metadata of 2 selected…"));
    menu->actions().first()->trigger();
    QDialog* edit = openEditor();
    QVERIFY(edit != nullptr);
    // A bulk edit starts empty: nothing to keep, nothing prefilled.
    QCOMPARE(edit->findChild<QLineEdit*>("titleEdit")->text(), QString(""));
    QCOMPARE(edit->findChild<QLineEdit*>("albumEdit")->text(), QString(""));
    edit->findChild<QLineEdit*>("albumEdit")->setText("Bulk Album");
    edit->findChild<QPushButton*>("okButton")->click();

    // Both rows take the album; the titles and the kept album stand.
    QCOMPARE(row1->text(0), QString("First"));
    QCOMPARE(row1->text(2), QString("Bulk Album"));
    QCOMPARE(row2->text(0), QString("Second"));
    QCOMPARE(row2->text(2), QString("Bulk Album"));

    dialog->findChild<QPushButton*>("addButton")->click();
    QCOMPARE(spy.count(), 1);
    const QHash<QString, TrackMeta> meta = sourceMeta(spy);
    QCOMPARE(meta.value(a.url.toString()).title, QString("First"));
    QCOMPARE(meta.value(a.url.toString()).album, QString("Bulk Album"));
    QCOMPARE(meta.value(b.url.toString()).title, QString("Second"));
    QCOMPARE(meta.value(b.url.toString()).album, QString("Bulk Album"));
}

void UrlImportDialogTest::plainSourceEditOverridesFetchedMetadata()
{
    // The edit of a plain source's row overrides the metadata the wizard
    // fetched for it. A list rides along, because a batch of plain
    // sources only fast-paths (no review to edit in).
    const QUrl source("https://music.example.com/watch?v=9");
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    m_provider.m_urls = { source.toString(), listUrl.toString() };
    TrackMeta meta;
    meta.title = "Fetched";
    meta.artist = "The Artist";
    meta.album = "The Source Album";
    meta.durationMs = 5000;
    m_provider.m_meta[source.toString()] = meta;
    ListEntries list;
    list.videos = { ListVideo(QUrl("https://music.example.com/watch?v=1")) };
    m_provider.m_lists[listUrl.toString()] = list;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    UrlImportDialog* dialog = openDialog();
    QSignalSpy spy(dialog, &UrlImportDialog::sourcesConfirmed);
    submit(dialog, { source.toString(), listUrl.toString() });
    QCOMPARE(stage(dialog), 2);
    QTreeWidget* t = tree(dialog);
    QCOMPARE(t->topLevelItemCount(), 2);
    QTreeWidgetItem* row = t->topLevelItem(1)->child(0); // "Other sources"
    QCOMPARE(row->text(0), QString("Fetched"));
    QCOMPARE(row->text(2), QString("The Source Album"));

    QMenu* menu = requestRowMenu(dialog, row);
    QVERIFY(menu != nullptr);
    menu->actions().first()->trigger();
    QDialog* edit = openEditor();
    QVERIFY(edit != nullptr);
    edit->findChild<QLineEdit*>("titleEdit")->setText("Edited Title");
    edit->findChild<QPushButton*>("okButton")->click();

    QCOMPARE(row->text(0), QString("Edited Title"));
    QCOMPARE(row->text(1), QString("The Artist"));
    dialog->findChild<QPushButton*>("addButton")->click();
    QCOMPARE(spy.count(), 1);
    const QHash<QString, TrackMeta> confirmed = sourceMeta(spy);
    QCOMPARE(confirmed.value(source.toString()).title, QString("Edited Title"));
    QCOMPARE(confirmed.value(source.toString()).artist, QString("The Artist"));
    QCOMPARE(confirmed.value(source.toString()).album, QString("The Source Album"));
}

void UrlImportDialogTest::headAndFailedRowsHaveNoEditMenu()
{
    // A section head and a failed row are not editable: no menu.
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    const QUrl badUrl("https://music.example.com/watch?v=8&list=L8");
    m_provider.m_urls = { listUrl.toString(), badUrl.toString() };
    ListEntries list;
    ListVideo a(QUrl("https://music.example.com/watch?v=1"));
    a.title = "First";
    list.videos = { a };
    m_provider.m_lists[listUrl.toString()] = list;
    ListEntries failed;
    failed.error = "boom";
    m_provider.m_lists[badUrl.toString()] = failed;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    UrlImportDialog* dialog = openDialog();
    submit(dialog, { listUrl.toString(), badUrl.toString() });
    QTreeWidget* t = tree(dialog);
    QCOMPARE(t->topLevelItemCount(), 2);
    QTreeWidgetItem* head = t->topLevelItem(0);
    QTreeWidgetItem* failedRow = t->topLevelItem(1)->child(0);

    QVERIFY(requestRowMenu(dialog, head) == nullptr);
    QVERIFY(requestRowMenu(dialog, failedRow) == nullptr);
}

void UrlImportDialogTest::rejectSavesTheColumnWidths()
{
    // The buttons close through reject/accept, which hide the dialog
    // without a closeEvent — the review tree's column widths are saved
    // on the hide and a fresh instance restores them.
    UrlImportDialog* dialog = openDialog();
    QTreeWidget* t = tree(dialog);
    t->header()->resizeSection(0, 321);
    dialog->reject();  // a hide (WA_DeleteOnClose deletes on close only)
    delete dialog;

    UrlImportDialog* second = openDialog();
    QCOMPARE(tree(second)->header()->sectionSize(0), 321);
    second->reject();
    delete second;
}

} // namespace

QTEST_MAIN(UrlImportDialogTest)
#include "urlimportdialog_test.moc"
