#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <QComboBox>
#include <QFont>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QTableView>

#include <memory>

#include "librarymanager.h"
#include "mediaitemstore.h"
#include "metadatadialog.h"
#include "metatransformdialog.h"
#include "settingsmanager.h"
#include "taggedfile.h"
#include "testsupport.h"

Q_DECLARE_METATYPE(QList<quint64>)

namespace {

// Records the warnings instead of showing modal boxes (untestable
// offscreen), like the widgets' chooser seams.
class RecordingMetaDataDialog : public MetaDataDialog
{
public:
    using MetaDataDialog::MetaDataDialog;

    QStringList m_warnings;
    void showWarning(const QString &message) override
    {
        m_warnings.append(message);
    }
};

class MetaDataDialogTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void opensWithItemsSortedAndDeduped();
    void editMarksBoldAndTooltip();
    void editBackToOriginalClearsMark();
    void selectAllButtonsToggleRows();
    void transformPreviewsEveryCheckedRow();
    void transformShowsSyntaxError();
    void transformAppliesToCells();
    void transformAppliesBareLiteral();
    void transformSkipsNonNumericForNumberColumns();
    void transformColumnComboListsEditableColumns();
    void applyCommitsToStore();
    void applyCommitsOnlyChangedItems();
    void numericCellIsParsed();
    void invalidNumberBlocksCommit();
    void cancelDiscards();
    void readFilesButtonReflectsLocalItems();
    void readFromFilesPopulatesCells();
    void readFromFilesThenApplyWritesToStore();
    void readFromFilesLeavesMissingFileRowsUnchanged();
    void rejectSavesTheColumnWidths();

private:
    QStandardItemModel* model(MetaDataDialog &dialog) const;
    QStandardItem* cell(MetaDataDialog &dialog, int row, int column) const;

    MediaItemStore* m_store;
    std::unique_ptr<QTemporaryDir> m_dir;
    SettingsManager* m_settings = nullptr;
    std::unique_ptr<QTemporaryDir> m_stateDir;
    MediaItem m_a;
    MediaItem m_b;
};

void MetaDataDialogTest::init()
{
    m_store = MediaItemStore::getInstance();
    m_dir = std::make_unique<QTemporaryDir>();
    test::usePrivateDatabase(m_dir->path());
    m_store->clear();
    // A fresh settings file per test (the dialog persists its state
    // there).
    m_stateDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_stateDir->isValid());
    m_settings = SettingsManager::getInstance();
    m_settings->setSettingsPath(m_stateDir->path() + "/settings.json");
    m_settings->load();

    m_a = MediaItem();
    m_a.source = QUrl::fromLocalFile(m_dir->path() + "/01 - alpha.mp3");
    m_a.title = "T1";
    m_a.artist = "A1";
    m_a.trackNumber = 0;
    QVERIFY(m_store->insert(m_a));

    m_b = MediaItem();
    m_b.source = QUrl::fromLocalFile(m_dir->path() + "/02 - beta.mp3");
    m_b.title = "T2";
    m_b.artist = "A2";
    m_b.year = 2001;
    QVERIFY(m_store->insert(m_b));
}

void MetaDataDialogTest::cleanup()
{
    m_store->clear();
    SettingsManager::reset();
}

QStandardItemModel* MetaDataDialogTest::model(MetaDataDialog &dialog) const
{
    QTableView *view = dialog.findChild<QTableView*>("tableView");
    if (view == nullptr)
        return nullptr;
    return qobject_cast<QStandardItemModel*>(view->model());
}

QStandardItem* MetaDataDialogTest::cell(MetaDataDialog &dialog, int row, int column) const
{
    return model(dialog)->item(row, column);
}

void MetaDataDialogTest::opensWithItemsSortedAndDeduped()
{
    MetaDataDialog dialog({ m_b.id, m_a.id, m_b.id, 99999 });
    dialog.show();
    QApplication::processEvents();

    QCOMPARE(dialog.itemIds(), QList<quint64>({ m_a.id, m_b.id }));
    QCOMPARE(model(dialog)->rowCount(), 2);
    QCOMPARE(cell(dialog, 0, MetaDataDialog::Title)->text(), QString("T1"));
    QCOMPARE(cell(dialog, 1, MetaDataDialog::Title)->text(), QString("T2"));
    QCOMPARE(cell(dialog, 0, MetaDataDialog::Source)->text(),
             m_a.source.toString());
    // The source column is not editable, the select column is checkable.
    QVERIFY(!cell(dialog, 0, MetaDataDialog::Source)->isEditable());
    QVERIFY(cell(dialog, 0, MetaDataDialog::Select)->isCheckable());
    QVERIFY(cell(dialog, 0, MetaDataDialog::Title)->isEditable());
}

void MetaDataDialogTest::editMarksBoldAndTooltip()
{
    MetaDataDialog dialog({ m_a.id });
    dialog.show();
    QApplication::processEvents();

    QStandardItem *item = cell(dialog, 0, MetaDataDialog::Title);
    item->setText("Edited");

    QVERIFY(item->font().bold());
    QCOMPARE(item->toolTip(), QString("T1 \u2192 Edited"));
    QCOMPARE(dialog.findChild<QLabel*>("statusLabel")->text(),
             QString("1 of 1 item(s) changed"));
}

void MetaDataDialogTest::editBackToOriginalClearsMark()
{
    MetaDataDialog dialog({ m_a.id });
    dialog.show();
    QApplication::processEvents();

    QStandardItem *item = cell(dialog, 0, MetaDataDialog::Title);
    item->setText("Edited");
    QVERIFY(item->font().bold());

    item->setText("T1");

    QVERIFY(!item->font().bold());
    QCOMPARE(item->toolTip(), QString());
    QCOMPARE(dialog.findChild<QLabel*>("statusLabel")->text(),
             QString("0 of 1 item(s) changed"));
}

void MetaDataDialogTest::selectAllButtonsToggleRows()
{
    MetaDataDialog dialog({ m_a.id, m_b.id });
    dialog.show();
    QApplication::processEvents();

    QPushButton *selectAll = dialog.findChild<QPushButton*>("selectAllButton");
    QPushButton *unselectAll = dialog.findChild<QPushButton*>("unselectAllButton");
    QPushButton *transform = dialog.findChild<QPushButton*>("transformButton");
    QVERIFY(selectAll != nullptr);
    QVERIFY(unselectAll != nullptr);
    QVERIFY(transform != nullptr);
    QVERIFY(!transform->isEnabled());

    selectAll->click();

    QCOMPARE(cell(dialog, 0, MetaDataDialog::Select)->checkState(), Qt::Checked);
    QCOMPARE(cell(dialog, 1, MetaDataDialog::Select)->checkState(), Qt::Checked);
    QVERIFY(transform->isEnabled());

    // The buttons work from any state: uncheck one row by hand, then
    // clear the rest with "Unselect all".
    cell(dialog, 0, MetaDataDialog::Select)->setCheckState(Qt::Unchecked);
    QCOMPARE(cell(dialog, 1, MetaDataDialog::Select)->checkState(), Qt::Checked);
    QVERIFY(transform->isEnabled());

    unselectAll->click();
    QCOMPARE(cell(dialog, 0, MetaDataDialog::Select)->checkState(), Qt::Unchecked);
    QCOMPARE(cell(dialog, 1, MetaDataDialog::Select)->checkState(), Qt::Unchecked);
    QVERIFY(!transform->isEnabled());

    selectAll->click();
    QCOMPARE(cell(dialog, 0, MetaDataDialog::Select)->checkState(), Qt::Checked);
    QCOMPARE(cell(dialog, 1, MetaDataDialog::Select)->checkState(), Qt::Checked);
    QVERIFY(transform->isEnabled());
}

void MetaDataDialogTest::transformPreviewsEveryCheckedRow()
{
    MetaDataDialog dialog({ m_a.id, m_b.id });
    dialog.show();
    QApplication::processEvents();

    QStandardItemModel *m = model(dialog);
    cell(dialog, 0, MetaDataDialog::Select)->setCheckState(Qt::Checked);
    cell(dialog, 1, MetaDataDialog::Select)->setCheckState(Qt::Checked);

    MetaTransformDialog transform(m, dialog.itemIds(), { 0, 1 }, &dialog);
    transform.show();
    QApplication::processEvents();

    QLineEdit *expr = transform.findChild<QLineEdit*>("expressionEdit");
    QVERIFY(expr != nullptr);
    expr->setText(R"(%filename% match("^(\\d+) - (.+)$", 2))");

    QTableWidget *preview = transform.findChild<QTableWidget*>("previewTable");
    QVERIFY(preview != nullptr);
    QCOMPARE(preview->rowCount(), 2);
    QCOMPARE(preview->item(0, 0)->text(), QString("1. T1"));
    QCOMPARE(preview->item(0, 1)->text(), QString("alpha.mp3"));
    QCOMPARE(preview->item(1, 1)->text(), QString("beta.mp3"));
}

void MetaDataDialogTest::transformShowsSyntaxError()
{
    MetaDataDialog dialog({ m_a.id });
    dialog.show();
    QApplication::processEvents();

    MetaTransformDialog transform(model(dialog), dialog.itemIds(), { 0 }, &dialog);
    transform.show();
    QApplication::processEvents();

    transform.findChild<QLineEdit*>("expressionEdit")->setText("%nope%");

    QTableWidget *preview = transform.findChild<QTableWidget*>("previewTable");
    QCOMPARE(preview->rowCount(), 0);
    QVERIFY(transform.findChild<QLabel*>("statusLabel")->text().contains("nope"));
}

void MetaDataDialogTest::transformAppliesToCells()
{
    MetaDataDialog dialog({ m_a.id, m_b.id });
    dialog.show();
    QApplication::processEvents();

    QStandardItemModel *m = model(dialog);
    MetaTransformDialog transform(m, dialog.itemIds(), { 0, 1 }, &dialog);
    transform.show();
    QApplication::processEvents();

    // Track # from the file name's leading number.
    QComboBox *combo = transform.findChild<QComboBox*>("columnCombo");
    QVERIFY(combo != nullptr);
    QCOMPARE(combo->currentData().toInt(), MetaDataDialog::Title);
    combo->setCurrentIndex(
        combo->findData(MetaDataDialog::TrackNumber));

    transform.findChild<QLineEdit*>("expressionEdit")
        ->setText(R"(%filename% match("^(\\d+) - .+$", 1))");

    transform.findChild<QPushButton*>("applyButton")->click();

    QCOMPARE(cell(dialog, 0, MetaDataDialog::TrackNumber)->text(), QString("01"));
    QCOMPARE(cell(dialog, 1, MetaDataDialog::TrackNumber)->text(), QString("02"));
    // The applied cells carry the diff mark (the editor's cell slot ran).
    QVERIFY(cell(dialog, 0, MetaDataDialog::TrackNumber)->font().bold());
    QVERIFY(transform.findChild<QLabel*>("statusLabel")->text()
                .contains("2"));
}

void MetaDataDialogTest::transformAppliesBareLiteral()
{
    // The "type the value and hit Apply" case: a bare string is a literal
    // constant, no macro needed.
    MetaDataDialog dialog({ m_a.id, m_b.id });
    dialog.show();
    QApplication::processEvents();

    QStandardItemModel *m = model(dialog);
    MetaTransformDialog transform(m, dialog.itemIds(), { 0, 1 }, &dialog);
    transform.show();
    QApplication::processEvents();

    transform.findChild<QComboBox*>("columnCombo")
        ->setCurrentIndex(transform.findChild<QComboBox*>("columnCombo")
                              ->findData(MetaDataDialog::Album));
    transform.findChild<QLineEdit*>("expressionEdit")->setText("My album");
    transform.findChild<QPushButton*>("applyButton")->click();

    QCOMPARE(cell(dialog, 0, MetaDataDialog::Album)->text(), QString("My album"));
    QCOMPARE(cell(dialog, 1, MetaDataDialog::Album)->text(), QString("My album"));
    QVERIFY(transform.findChild<QLabel*>("statusLabel")->text().contains("2"));
}

void MetaDataDialogTest::transformSkipsNonNumericForNumberColumns()
{
    MetaDataDialog dialog({ m_a.id, m_b.id });
    dialog.show();
    QApplication::processEvents();

    QStandardItemModel *m = model(dialog);
    MetaTransformDialog transform(m, dialog.itemIds(), { 0, 1 }, &dialog);
    transform.show();
    QApplication::processEvents();

    transform.findChild<QComboBox*>("columnCombo")
        ->setCurrentIndex(transform.findChild<QComboBox*>("columnCombo")
                              ->findData(MetaDataDialog::TrackNumber));
    // "mp3" is not a number: both rows are skipped, nothing is written.
    transform.findChild<QLineEdit*>("expressionEdit")->setText("%ext%");
    transform.findChild<QPushButton*>("applyButton")->click();

    QCOMPARE(cell(dialog, 0, MetaDataDialog::TrackNumber)->text(), QString());
    QCOMPARE(cell(dialog, 1, MetaDataDialog::TrackNumber)->text(), QString());
    QVERIFY(transform.findChild<QLabel*>("statusLabel")->text().contains("skipped"));
}

void MetaDataDialogTest::transformColumnComboListsEditableColumns()
{
    MetaDataDialog dialog({ m_a.id });
    dialog.show();
    QApplication::processEvents();

    MetaTransformDialog transform(model(dialog), dialog.itemIds(), { 0 }, &dialog);
    transform.show();
    QApplication::processEvents();

    QComboBox *combo = transform.findChild<QComboBox*>("columnCombo");
    QCOMPARE(combo->count(), MetaDataDialog::editableColumns().size());
    for (int i = 0; i < combo->count(); ++i)
        QCOMPARE(combo->itemData(i).toInt(),
                 MetaDataDialog::editableColumns().at(i));
}

void MetaDataDialogTest::applyCommitsToStore()
{
    RecordingMetaDataDialog dialog({ m_a.id, m_b.id });
    dialog.show();
    QApplication::processEvents();

    cell(dialog, 0, MetaDataDialog::Title)->setText("New title");
    cell(dialog, 0, MetaDataDialog::Artist)->setText("New artist");

    QSignalSpy spy(m_store, &MediaItemStore::itemChanged);
    dialog.findChild<QPushButton*>("okButton")->click();

    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
    QCOMPARE(spy.count(), 1);
    const MediaItem a = m_store->item(m_a.id);
    QCOMPARE(a.title, QString("New title"));
    QCOMPARE(a.artist, QString("New artist"));
    // Only the changed item is written.
    QCOMPARE(m_store->item(m_b.id).title, QString("T2"));
    QVERIFY(dialog.m_warnings.isEmpty());
}

void MetaDataDialogTest::applyCommitsOnlyChangedItems()
{
    RecordingMetaDataDialog dialog({ m_a.id, m_b.id });
    dialog.show();
    QApplication::processEvents();

    // No cell changed: nothing is written.
    QSignalSpy spy(m_store, &MediaItemStore::itemChanged);
    dialog.findChild<QPushButton*>("okButton")->click();

    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
    QCOMPARE(spy.count(), 0);
}

void MetaDataDialogTest::numericCellIsParsed()
{
    RecordingMetaDataDialog dialog({ m_a.id });
    dialog.show();
    QApplication::processEvents();

    cell(dialog, 0, MetaDataDialog::TrackNumber)->setText("07");
    cell(dialog, 0, MetaDataDialog::Year)->setText("1994");

    dialog.findChild<QPushButton*>("okButton")->click();

    const MediaItem a = m_store->item(m_a.id);
    QCOMPARE(a.trackNumber, 7);
    QCOMPARE(a.year, 1994);
}

void MetaDataDialogTest::invalidNumberBlocksCommit()
{
    RecordingMetaDataDialog dialog({ m_a.id });
    dialog.show();
    QApplication::processEvents();

    cell(dialog, 0, MetaDataDialog::TrackNumber)->setText("abc");

    dialog.findChild<QPushButton*>("okButton")->click();

    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Rejected));
    QCOMPARE(dialog.m_warnings.size(), 1);
    QVERIFY(dialog.m_warnings.at(0).contains("not a number"));
    // The store is untouched.
    QCOMPARE(m_store->item(m_a.id).trackNumber, 0);
}

void MetaDataDialogTest::cancelDiscards()
{
    MetaDataDialog dialog({ m_a.id });
    dialog.show();
    QApplication::processEvents();

    cell(dialog, 0, MetaDataDialog::Title)->setText("Never saved");
    dialog.findChild<QPushButton*>("cancelButton")->click();

    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Rejected));
    QCOMPARE(m_store->item(m_a.id).title, QString("T1"));
}

void MetaDataDialogTest::readFilesButtonReflectsLocalItems()
{
    MetaDataDialog local({ m_a.id });
    local.show();
    QApplication::processEvents();

    QPushButton *read = local.findChild<QPushButton*>("readFilesButton");
    QVERIFY(read != nullptr);
    // m_a has a local file source: the read is available.
    QVERIFY(read->isEnabled());

    MediaItem remote;
    remote.kind = MediaItem::Remote;
    remote.source = QUrl("https://stream.example.com/live");
    remote.title = "Stream";
    QVERIFY(m_store->insert(remote));

    MetaDataDialog nonLocal({ remote.id });
    nonLocal.show();
    QApplication::processEvents();

    QPushButton *read2 = nonLocal.findChild<QPushButton*>("readFilesButton");
    QVERIFY(read2 != nullptr);
    // No local file to read: the button stays disabled.
    QVERIFY(!read2->isEnabled());
}

void MetaDataDialogTest::readFromFilesPopulatesCells()
{
    const QUrl url = makeTaggedFileWith(m_dir->path(), "song.flac",
        { { "TITLE", "File Title" }, { "ARTIST", "File Artist" },
          { "ALBUM", "File Album" }, });
    if (url.isEmpty())
        QSKIP("ffmpeg not available to generate a tagged file");

    MediaItem song;
    song.source = url;
    song.title = "Old Title";
    song.artist = "Old Artist";
    QVERIFY(m_store->insert(song));

    MetaDataDialog dialog({ song.id });
    dialog.show();
    QApplication::processEvents();

    QPushButton *read = dialog.findChild<QPushButton*>("readFilesButton");
    QProgressBar *bar = dialog.findChild<QProgressBar*>("readProgressBar");
    QVERIFY(read != nullptr);
    QVERIFY(read->isEnabled());
    QVERIFY(!bar->isVisible());

    read->click();
    LibraryManager *lib = LibraryManager::getInstance();
    QVERIFY(test::waitUntil([&lib] { return !lib->busy(); }));
    QApplication::processEvents();

    // The file's tags are in the cells, marked against the open-time
    // snapshot...
    QCOMPARE(cell(dialog, 0, MetaDataDialog::Title)->text(), QString("File Title"));
    QCOMPARE(cell(dialog, 0, MetaDataDialog::Artist)->text(), QString("File Artist"));
    QCOMPARE(cell(dialog, 0, MetaDataDialog::Album)->text(), QString("File Album"));
    QVERIFY(cell(dialog, 0, MetaDataDialog::Title)->font().bold());
    QCOMPARE(cell(dialog, 0, MetaDataDialog::Title)->toolTip(),
             QString("Old Title \u2192 File Title"));
    QCOMPARE(dialog.findChild<QLabel*>("statusLabel")->text(),
             QString("1 of 1 item(s) changed"));
    // ...and the store is untouched until Apply.
    QCOMPARE(m_store->item(song.id).title, QString("Old Title"));
    // The read is done: the button is back, the progress bar is gone.
    QVERIFY(read->isEnabled());
    QVERIFY(!bar->isVisible());
}

void MetaDataDialogTest::readFromFilesThenApplyWritesToStore()
{
    const QUrl url = makeTaggedFileWith(m_dir->path(), "song.flac",
        { { "TITLE", "File Title" }, { "ARTIST", "File Artist" } });
    if (url.isEmpty())
        QSKIP("ffmpeg not available to generate a tagged file");

    MediaItem song;
    song.source = url;
    song.title = "Old Title";
    song.artist = "Old Artist";
    QVERIFY(m_store->insert(song));

    RecordingMetaDataDialog dialog({ song.id });
    dialog.show();
    QApplication::processEvents();

    dialog.findChild<QPushButton*>("readFilesButton")->click();
    LibraryManager *lib = LibraryManager::getInstance();
    QVERIFY(test::waitUntil([&lib] { return !lib->busy(); }));
    QApplication::processEvents();

    // Apply writes the read values through the store.
    QSignalSpy spy(m_store, &MediaItemStore::itemChanged);
    dialog.findChild<QPushButton*>("okButton")->click();

    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
    QCOMPARE(spy.count(), 1);
    const MediaItem saved = m_store->item(song.id);
    QCOMPARE(saved.title, QString("File Title"));
    QCOMPARE(saved.artist, QString("File Artist"));
    QVERIFY(dialog.m_warnings.isEmpty());
}

void MetaDataDialogTest::readFromFilesLeavesMissingFileRowsUnchanged()
{
    // m_a's file does not exist on disk: the read has nothing for it.
    MetaDataDialog dialog({ m_a.id });
    dialog.show();
    QApplication::processEvents();

    dialog.findChild<QPushButton*>("readFilesButton")->click();
    LibraryManager *lib = LibraryManager::getInstance();
    QVERIFY(test::waitUntil([&lib] { return !lib->busy(); }));
    QApplication::processEvents();

    QCOMPARE(cell(dialog, 0, MetaDataDialog::Title)->text(), QString("T1"));
    QCOMPARE(cell(dialog, 0, MetaDataDialog::Artist)->text(), QString("A1"));
    QCOMPARE(dialog.findChild<QLabel*>("statusLabel")->text(),
             QString("0 of 1 item(s) changed"));
    QVERIFY(dialog.findChild<QPushButton*>("readFilesButton")->isEnabled());
}

void MetaDataDialogTest::rejectSavesTheColumnWidths()
{
    // The button box closes through reject/accept, which hide the
    // dialog without a closeEvent — the table's column widths are
    // saved on the hide and a fresh instance restores them.
    MetaDataDialog dialog({ m_a.id });
    dialog.show();
    QApplication::processEvents();
    QTableView* view = dialog.findChild<QTableView*>("tableView");
    view->horizontalHeader()->resizeSection(0, 321);
    dialog.reject();

    MetaDataDialog second({ m_a.id });
    second.show();
    QApplication::processEvents();
    QCOMPARE(second.findChild<QTableView*>("tableView")
                 ->horizontalHeader()->sectionSize(0), 321);
}

} // namespace

QTEST_MAIN(MetaDataDialogTest)
#include "metadatadialog_test.moc"
