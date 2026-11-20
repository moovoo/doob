#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <QFont>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTableView>

#include <memory>

#include "librarymanager.h"
#include "mediaitemstore.h"
#include "settingsmanager.h"
#include "singlemetadatadialog.h"
#include "taggedfile.h"
#include "testsupport.h"

namespace {

// Records the warnings instead of showing modal boxes (untestable
// offscreen), like the widgets' chooser seams.
class RecordingSingleMetaDataDialog : public SingleMetaDataDialog
{
public:
    using SingleMetaDataDialog::SingleMetaDataDialog;

    QStringList m_warnings;
    void showWarning(const QString &message) override
    {
        m_warnings.append(message);
    }
};

class SingleMetaDataDialogTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void tagRowsMapsStandardFieldsAndExtras();
    void opensWithSourceRowAndTags();
    void editMarksBoldAndClearsWhenReverted();
    void addRowAppendsEmptyEditableRow();
    void removeRowDeletesTag();
    void removeButtonIgnoresSourceRow() const;
    void saveWritesToStore();
    void saveWithoutChangesWritesNothing();
    void cancelDiscards();
    void invalidNumberBlocksSave();
    void duplicateKeyBlocksSave();
    void renamingStandardKeyMovesValueToExtra();
    void emptyValueClearsFieldAndDeletesExtra();
    void removeStandardRowClearsField();
    void unknownIdShowsSourceRowOnly();
    void readFileButtonReflectsItemKind();
    void readFromFileRebuildsRows();
    void readFromFileThenSaveWritesToStore();
    void rejectSavesTheColumnWidths() const;

private:
    QStandardItemModel* model(SingleMetaDataDialog &dialog) const;
    QStandardItem* cell(SingleMetaDataDialog &dialog, int row, int column) const;

    MediaItemStore* m_store;
    std::unique_ptr<QTemporaryDir> m_dir;
    SettingsManager* m_settings = nullptr;
    std::unique_ptr<QTemporaryDir> m_stateDir;
    MediaItem m_a;
};

void SingleMetaDataDialogTest::init()
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
    m_a.trackNumber = 3;
    m_a.trackTotal = 10;
    m_a.year = 2001;
    m_a.extra.insert(QStringLiteral("LYRICS"), QStringLiteral("la la"));
    m_a.extra.insert(QStringLiteral("MOOD"), QStringLiteral("dark"));
    QVERIFY(m_store->insert(m_a));
}

void SingleMetaDataDialogTest::cleanup()
{
    m_store->clear();
    SettingsManager::reset();
}

QStandardItemModel* SingleMetaDataDialogTest::model(SingleMetaDataDialog &dialog) const
{
    QTableView *view = dialog.findChild<QTableView*>("tableView");
    if (view == nullptr)
        return nullptr;
    return qobject_cast<QStandardItemModel*>(view->model());
}

QStandardItem* SingleMetaDataDialogTest::cell(SingleMetaDataDialog &dialog, int row,
                                              int column) const
{
    return model(dialog)->item(row, column);
}

void SingleMetaDataDialogTest::tagRowsMapsStandardFieldsAndExtras()
{
    const QList<SingleMetaDataDialog::TagRow> rows = SingleMetaDataDialog::tagRows(m_a);

    // The non-empty standard tags in canonical key order, then the extra
    // tags (the map's key order).
    QCOMPARE(rows.size(), 6);
    QCOMPARE(rows.at(0), (SingleMetaDataDialog::TagRow{ "TITLE", "T1" }));
    QCOMPARE(rows.at(1), (SingleMetaDataDialog::TagRow{ "ARTIST", "A1" }));
    QCOMPARE(rows.at(2), (SingleMetaDataDialog::TagRow{ "TRACKNUMBER", "3/10" }));
    QCOMPARE(rows.at(3), (SingleMetaDataDialog::TagRow{ "DATE", "2001" }));
    QCOMPARE(rows.at(4), (SingleMetaDataDialog::TagRow{ "LYRICS", "la la" }));
    QCOMPARE(rows.at(5), (SingleMetaDataDialog::TagRow{ "MOOD", "dark" }));
}

void SingleMetaDataDialogTest::opensWithSourceRowAndTags()
{
    SingleMetaDataDialog dialog(m_a.id);
    dialog.show();
    QApplication::processEvents();

    QCOMPARE(dialog.itemId(), m_a.id);
    // The source row on top, then the item's six tags.
    QCOMPARE(model(dialog)->rowCount(), 7);
    QCOMPARE(cell(dialog, 0, SingleMetaDataDialog::Key)->text(), QString("Source"));
    QCOMPARE(cell(dialog, 0, SingleMetaDataDialog::Value)->text(),
             m_a.source.toString());
    QVERIFY(!cell(dialog, 0, SingleMetaDataDialog::Key)->isEditable());
    QVERIFY(!cell(dialog, 0, SingleMetaDataDialog::Value)->isEditable());
    // The tag rows are editable in both cells.
    QVERIFY(cell(dialog, 1, SingleMetaDataDialog::Key)->isEditable());
    QVERIFY(cell(dialog, 1, SingleMetaDataDialog::Value)->isEditable());
    QCOMPARE(cell(dialog, 1, SingleMetaDataDialog::Key)->text(), QString("TITLE"));
    QCOMPARE(cell(dialog, 1, SingleMetaDataDialog::Value)->text(), QString("T1"));
    QCOMPARE(dialog.findChild<QLabel*>("statusLabel")->text(),
             QString("0 tag(s) changed"));
}

void SingleMetaDataDialogTest::editMarksBoldAndClearsWhenReverted()
{
    SingleMetaDataDialog dialog(m_a.id);
    dialog.show();
    QApplication::processEvents();

    QStandardItem *item = cell(dialog, 1, SingleMetaDataDialog::Value);
    item->setText("Edited");

    QVERIFY(item->font().bold());
    QCOMPARE(item->toolTip(), QString("T1 \u2192 Edited"));
    QCOMPARE(dialog.findChild<QLabel*>("statusLabel")->text(),
             QString("1 tag(s) changed"));

    item->setText("T1");

    QVERIFY(!item->font().bold());
    QCOMPARE(item->toolTip(), QString());
    QCOMPARE(dialog.findChild<QLabel*>("statusLabel")->text(),
             QString("0 tag(s) changed"));
}

void SingleMetaDataDialogTest::addRowAppendsEmptyEditableRow()
{
    SingleMetaDataDialog dialog(m_a.id);
    dialog.show();
    QApplication::processEvents();

    QCOMPARE(model(dialog)->rowCount(), 7);
    dialog.findChild<QPushButton*>("addTagButton")->click();

    QCOMPARE(model(dialog)->rowCount(), 8);
    QStandardItem *key = cell(dialog, 7, SingleMetaDataDialog::Key);
    QStandardItem *value = cell(dialog, 7, SingleMetaDataDialog::Value);
    QVERIFY(key->isEditable());
    QVERIFY(value->isEditable());
    QCOMPARE(key->text(), QString());
    QCOMPARE(value->text(), QString());
    // An untouched empty row is not a change.
    QCOMPARE(dialog.findChild<QLabel*>("statusLabel")->text(),
             QString("0 tag(s) changed"));
}

void SingleMetaDataDialogTest::removeRowDeletesTag()
{
    SingleMetaDataDialog dialog(m_a.id);
    dialog.show();
    QApplication::processEvents();

    QTableView *view = dialog.findChild<QTableView*>("tableView");
    view->selectRow(1); // the TITLE row
    dialog.findChild<QPushButton*>("removeTagButton")->click();

    QCOMPARE(model(dialog)->rowCount(), 6);
    // The rows shifted up: ARTIST is row 1 now (it still reads unchanged).
    QCOMPARE(cell(dialog, 1, SingleMetaDataDialog::Key)->text(), QString("ARTIST"));
    QCOMPARE(cell(dialog, 1, SingleMetaDataDialog::Value)->text(), QString("A1"));
    // The removed tag is a change Save would make, so it counts.
    QCOMPARE(dialog.findChild<QLabel*>("statusLabel")->text(),
             QString("1 tag(s) changed"));
}

void SingleMetaDataDialogTest::removeButtonIgnoresSourceRow() const
{
    SingleMetaDataDialog dialog(m_a.id);
    dialog.show();
    QApplication::processEvents();

    QTableView *view = dialog.findChild<QTableView*>("tableView");
    QPushButton *remove = dialog.findChild<QPushButton*>("removeTagButton");

    view->selectRow(0);
    QVERIFY(!remove->isEnabled());

    view->selectRow(1);
    QVERIFY(remove->isEnabled());
}

void SingleMetaDataDialogTest::saveWritesToStore()
{
    RecordingSingleMetaDataDialog dialog(m_a.id);
    dialog.show();
    QApplication::processEvents();

    cell(dialog, 1, SingleMetaDataDialog::Value)->setText("New title"); // TITLE
    // An edited tag in place (MOOD is row 6: source, TITLE, ARTIST,
    // TRACKNUMBER, DATE, LYRICS, MOOD).
    cell(dialog, 6, SingleMetaDataDialog::Value)->setText("bright");
    // A new custom tag.
    dialog.findChild<QPushButton*>("addTagButton")->click();
    cell(dialog, 7, SingleMetaDataDialog::Key)->setText("lyricist");
    cell(dialog, 7, SingleMetaDataDialog::Value)->setText("Sam");
    // A deleted tag: the LYRICS row.
    QTableView *view = dialog.findChild<QTableView*>("tableView");
    view->selectRow(5);
    dialog.findChild<QPushButton*>("removeTagButton")->click();

    QSignalSpy spy(m_store, &MediaItemStore::itemChanged);
    dialog.findChild<QPushButton*>("saveButton")->click();

    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
    QCOMPARE(spy.count(), 1);
    const MediaItem a = m_store->item(m_a.id);
    QCOMPARE(a.title, QString("New title"));
    QCOMPARE(a.extra.value("MOOD"), QString("bright"));
    QCOMPARE(a.extra.value("LYRICIST"), QString("Sam"));
    QVERIFY(!a.extra.contains("LYRICS"));
    // The source is untouched (the store keeps it as stored).
    QCOMPARE(a.source, m_a.source);
    QVERIFY(dialog.m_warnings.isEmpty());
}

void SingleMetaDataDialogTest::saveWithoutChangesWritesNothing()
{
    RecordingSingleMetaDataDialog dialog(m_a.id);
    dialog.show();
    QApplication::processEvents();

    QSignalSpy spy(m_store, &MediaItemStore::itemChanged);
    dialog.findChild<QPushButton*>("saveButton")->click();

    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
    QCOMPARE(spy.count(), 0);
    QVERIFY(dialog.m_warnings.isEmpty());
}

void SingleMetaDataDialogTest::cancelDiscards()
{
    SingleMetaDataDialog dialog(m_a.id);
    dialog.show();
    QApplication::processEvents();

    cell(dialog, 1, SingleMetaDataDialog::Value)->setText("Never saved");
    dialog.findChild<QPushButton*>("cancelButton")->click();

    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Rejected));
    QCOMPARE(m_store->item(m_a.id).title, QString("T1"));
}

void SingleMetaDataDialogTest::invalidNumberBlocksSave()
{
    RecordingSingleMetaDataDialog dialog(m_a.id);
    dialog.show();
    QApplication::processEvents();

    cell(dialog, 3, SingleMetaDataDialog::Value)->setText("abc"); // TRACKNUMBER

    dialog.findChild<QPushButton*>("saveButton")->click();

    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Rejected));
    QCOMPARE(dialog.m_warnings.size(), 1);
    QVERIFY(dialog.m_warnings.at(0).contains("not a number"));
    // The store is untouched.
    QCOMPARE(m_store->item(m_a.id).trackNumber, 3);
}

void SingleMetaDataDialogTest::duplicateKeyBlocksSave()
{
    RecordingSingleMetaDataDialog dialog(m_a.id);
    dialog.show();
    QApplication::processEvents();

    // A second TITLE row.
    dialog.findChild<QPushButton*>("addTagButton")->click();
    cell(dialog, 7, SingleMetaDataDialog::Key)->setText("title");
    cell(dialog, 7, SingleMetaDataDialog::Value)->setText("dup");

    dialog.findChild<QPushButton*>("saveButton")->click();

    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Rejected));
    QCOMPARE(dialog.m_warnings.size(), 1);
    QVERIFY(dialog.m_warnings.at(0).contains("duplicate"));
    QCOMPARE(m_store->item(m_a.id).title, QString("T1"));
}

void SingleMetaDataDialogTest::renamingStandardKeyMovesValueToExtra()
{
    RecordingSingleMetaDataDialog dialog(m_a.id);
    dialog.show();
    QApplication::processEvents();

    // Renaming the TITLE row's key re-targets its value: the row no
    // longer names the field (which is cleared), the value lands as a
    // custom tag.
    cell(dialog, 1, SingleMetaDataDialog::Key)->setText("FOO");
    cell(dialog, 1, SingleMetaDataDialog::Value)->setText("bar");

    dialog.findChild<QPushButton*>("saveButton")->click();

    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
    const MediaItem a = m_store->item(m_a.id);
    QVERIFY(a.title.isEmpty());
    QCOMPARE(a.extra.value("FOO"), QString("bar"));
}

void SingleMetaDataDialogTest::emptyValueClearsFieldAndDeletesExtra()
{
    RecordingSingleMetaDataDialog dialog(m_a.id);
    dialog.show();
    QApplication::processEvents();

    cell(dialog, 1, SingleMetaDataDialog::Value)->setText("");   // TITLE
    cell(dialog, 5, SingleMetaDataDialog::Value)->setText("");   // LYRICS

    dialog.findChild<QPushButton*>("saveButton")->click();

    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
    const MediaItem a = m_store->item(m_a.id);
    QVERIFY(a.title.isEmpty());
    QVERIFY(!a.extra.contains("LYRICS"));
    QCOMPARE(a.extra.value("MOOD"), QString("dark"));
}

void SingleMetaDataDialogTest::removeStandardRowClearsField()
{
    RecordingSingleMetaDataDialog dialog(m_a.id);
    dialog.show();
    QApplication::processEvents();

    // Removing the DATE row (row 4) clears the year on save.
    QTableView *view = dialog.findChild<QTableView*>("tableView");
    view->selectRow(4);
    dialog.findChild<QPushButton*>("removeTagButton")->click();

    dialog.findChild<QPushButton*>("saveButton")->click();

    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
    const MediaItem a = m_store->item(m_a.id);
    QCOMPARE(a.year, 0);
    QCOMPARE(a.title, QString("T1"));
}

void SingleMetaDataDialogTest::unknownIdShowsSourceRowOnly()
{
    RecordingSingleMetaDataDialog dialog(99999);
    dialog.show();
    QApplication::processEvents();

    QCOMPARE(model(dialog)->rowCount(), 1);
    QCOMPARE(cell(dialog, 0, SingleMetaDataDialog::Key)->text(), QString("Source"));

    // Nothing to write: the save is an accepted no-op.
    QSignalSpy spy(m_store, &MediaItemStore::itemChanged);
    dialog.findChild<QPushButton*>("saveButton")->click();
    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
    QCOMPARE(spy.count(), 0);
}

void SingleMetaDataDialogTest::readFileButtonReflectsItemKind()
{
    SingleMetaDataDialog dialog(m_a.id);
    dialog.show();
    QApplication::processEvents();

    QPushButton *read = dialog.findChild<QPushButton*>("readFileButton");
    QVERIFY(read != nullptr);
    // m_a has a local file source: the read is available.
    QVERIFY(read->isEnabled());

    MediaItem remote;
    remote.kind = MediaItem::Remote;
    remote.source = QUrl("https://stream.example.com/live");
    remote.title = "Stream";
    QVERIFY(m_store->insert(remote));

    SingleMetaDataDialog nonLocal(remote.id);
    nonLocal.show();
    QApplication::processEvents();

    QPushButton *read2 = nonLocal.findChild<QPushButton*>("readFileButton");
    QVERIFY(read2 != nullptr);
    // No local file to read: the button stays disabled.
    QVERIFY(!read2->isEnabled());
}

void SingleMetaDataDialogTest::readFromFileRebuildsRows()
{
    const QUrl url = makeTaggedFileWith(m_dir->path(), "song.flac",
        { { "TITLE", "New Title" }, { "ARTIST", "A1" },
          { "ALBUM", "New Album" }, { "MOOD", "bright" }, });
    if (url.isEmpty())
        QSKIP("ffmpeg not available to generate a tagged file");

    // A stale database item for the real file.
    MediaItem song;
    song.source = url;
    song.title = "Stale Title";
    song.artist = "A1";
    song.extra.insert(QStringLiteral("LYRICS"), QStringLiteral("la la"));
    QVERIFY(m_store->insert(song));

    SingleMetaDataDialog dialog(song.id);
    dialog.show();
    QApplication::processEvents();

    QPushButton *read = dialog.findChild<QPushButton*>("readFileButton");
    QProgressBar *bar = dialog.findChild<QProgressBar*>("readProgressBar");
    QVERIFY(read != nullptr);
    QVERIFY(read->isEnabled());
    QVERIFY(!bar->isVisible());

    read->click();
    LibraryManager *lib = LibraryManager::getInstance();
    QVERIFY(test::waitUntil([&lib] { return !lib->busy(); }));
    QApplication::processEvents();

    // The table is the file's tags (the ffmpeg file carries an ENCODER
    // tag along): the stale LYRICS row is gone, the ALBUM row is new.
    auto rowOf = [this, &dialog](const QString &key) {
        for (int row = 1; row < model(dialog)->rowCount(); ++row)
            if (cell(dialog, row, SingleMetaDataDialog::Key)->text() == key)
                return row;
        return -1;
    };
    const int titleRow = rowOf("TITLE");
    QCOMPARE(titleRow, 1);
    QCOMPARE(cell(dialog, titleRow, SingleMetaDataDialog::Value)->text(),
             QString("New Title"));
    QCOMPARE(rowOf("LYRICS"), -1);
    QCOMPARE(cell(dialog, rowOf("ARTIST"), SingleMetaDataDialog::Value)->text(),
             QString("A1"));
    QCOMPARE(cell(dialog, rowOf("ALBUM"), SingleMetaDataDialog::Value)->text(),
             QString("New Album"));
    QCOMPARE(cell(dialog, rowOf("MOOD"), SingleMetaDataDialog::Value)->text(),
             QString("bright"));
    // The changed values are marked against the database...
    QVERIFY(cell(dialog, titleRow, SingleMetaDataDialog::Value)->font().bold());
    QCOMPARE(cell(dialog, titleRow, SingleMetaDataDialog::Value)->toolTip(),
             QString("Stale Title \u2192 New Title"));
    QVERIFY(!cell(dialog, rowOf("ARTIST"),
                  SingleMetaDataDialog::Value)->font().bold());
    // ...the store is untouched until Save.
    QCOMPARE(m_store->item(song.id).title, QString("Stale Title"));
    QCOMPARE(m_store->item(song.id).extra.value("LYRICS"), QString("la la"));
    QVERIFY(!m_store->item(song.id).extra.contains("MOOD"));
    // The read is done: the button is back, the progress bar is gone.
    QVERIFY(read->isEnabled());
    QVERIFY(!bar->isVisible());
}

void SingleMetaDataDialogTest::readFromFileThenSaveWritesToStore()
{
    const QUrl url = makeTaggedFileWith(m_dir->path(), "song.flac",
        { { "TITLE", "New Title" }, { "ARTIST", "A1" },
          { "ALBUM", "New Album" }, });
    if (url.isEmpty())
        QSKIP("ffmpeg not available to generate a tagged file");

    MediaItem song;
    song.source = url;
    song.title = "Stale Title";
    song.artist = "A1";
    song.year = 2001;
    QVERIFY(m_store->insert(song));

    RecordingSingleMetaDataDialog dialog(song.id);
    dialog.show();
    QApplication::processEvents();

    dialog.findChild<QPushButton*>("readFileButton")->click();
    LibraryManager *lib = LibraryManager::getInstance();
    QVERIFY(test::waitUntil([&lib] { return !lib->busy(); }));
    QApplication::processEvents();

    // Save writes the read values through the store: the file's tags,
    // nothing else (the stale year is dropped).
    QSignalSpy spy(m_store, &MediaItemStore::itemChanged);
    dialog.findChild<QPushButton*>("saveButton")->click();

    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
    QCOMPARE(spy.count(), 1);
    const MediaItem saved = m_store->item(song.id);
    QCOMPARE(saved.title, QString("New Title"));
    QCOMPARE(saved.album, QString("New Album"));
    QCOMPARE(saved.year, 0);
    QVERIFY(dialog.m_warnings.isEmpty());
}

void SingleMetaDataDialogTest::rejectSavesTheColumnWidths() const
{
    // The button box closes through reject/accept, which hide the
    // dialog without a closeEvent — the table's column widths are
    // saved on the hide and a fresh instance restores them.
    SingleMetaDataDialog dialog(m_a.id);
    dialog.show();
    QApplication::processEvents();
    QTableView* view = dialog.findChild<QTableView*>("tableView");
    view->horizontalHeader()->resizeSection(0, 321);
    dialog.reject();

    SingleMetaDataDialog second(m_a.id);
    second.show();
    QApplication::processEvents();
    QCOMPARE(second.findChild<QTableView*>("tableView")
                 ->horizontalHeader()->sectionSize(0), 321);
}

} // namespace

QTEST_MAIN(SingleMetaDataDialogTest)
#include "singlemetadatadialog_test.moc"
