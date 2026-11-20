#ifndef METADATADIALOG_H
#define METADATADIALOG_H

#include <QDialog>
#include <QHideEvent>
#include <QFont>
#include <QHash>
#include <QList>

#include "mediaitem.h"
#include "trackmetadata.h"

class QStandardItem;
class QStandardItemModel;
class QTableView;
class UiState;
class QProgressBar;
class QPushButton;
class QLabel;

// The metadata editors as the media views open them: a modal dialog
// over the given items (the bulk table) or the single-item tag view,
// parented to the view's window — the widgets' editMetadata seams
// share the open-and-exec here.
void openMetadataEditor(QWidget *parent, const QList<quint64> &ids);
void openSingleMetadataEditor(QWidget *parent, quint64 id);

// The metadata editor: a table over the selected items' standard metadata
// fields, editable in place. It edits the database data only (the store's
// items) — the real files are never touched. A changed cell is marked bold
// with a tooltip showing the diff against the open-time snapshot; OK writes
// the changes through the store (write-through), Cancel discards.
// "Read from files" re-reads the rows' tags from their local files
// (through the library manager's worker) and shows the read values in the
// table — a preview like a manual edit, written only on Apply.
class MetaDataDialog : public QDialog
{
    Q_OBJECT
public:
    // The editor's table columns (shared with the transform dialog, which
    // offers the editable ones as targets).
    enum Column
    {
        Select = 0,  // the checkbox column (the mass-edit selection)
        Source,      // read-only: identifies the row
        Title,
        Artist,
        Album,
        AlbumArtist,
        Composer,
        Genre,
        Comment,
        TrackNumber,
        TrackTotal,
        DiscNumber,
        DiscTotal,
        Year,
        ColumnCount,
    };

    static QStringList columnLabels();
    // The stable column keys (one per logical column, in order): the
    // names the column widths are stored under ("dialogs.metadata").
    static QStringList columnKeys();
    static bool isNumericColumn(int column);
    // The editable columns (everything except the select and source ones).
    static QList<int> editableColumns();

    // Edits the metadata of the items with these ids (deduplicated,
    // ascending; ids the store does not hold are skipped).
    explicit MetaDataDialog(const QList<quint64> &ids, QWidget *parent = nullptr);

    // The ids shown, ascending (one row each).
    QList<quint64> itemIds() const { return m_ids; }

protected:
    // Opens the transform sub-dialog for the checked rows. A test seam
    // (the sub-dialog is modal, untestable offscreen) — the production
    // implementation execs it.
    virtual void openTransformDialog();
    // A non-fatal problem (an unparseable number, a failed write): a
    // message box in production, a record in the tests.
    virtual void showWarning(const QString &message);
    void hideEvent(QHideEvent *event) override;

private slots:
    void onCellChanged(QStandardItem *item);
    void onTransformClicked();
    void onReadFilesClicked();
    void onAccept();

private:
    void buildModel();
    // Sets every row's select box (the select-all / unselect-all buttons).
    void setAllChecked(bool checked);
    // The "N of M items changed" line under the table.
    void updateStatus();
    // The read button's state: enabled for a local-file selection while
    // the library manager is idle.
    void updateReadButton();
    // Fills the rows whose files were read: the read tags become cell text
    // (the diff marks follow from the cell-changed slot). Nothing is
    // written until Apply.
    void applyReadResults(const QHash<quint64, TrackMeta> &results);
    static QString displayValue(const MediaItem &item, int column);
    static void applyValue(MediaItem &item, int column, const QString &text);
    // The rows whose select box is checked.
    QList<int> checkedRows() const;
    QStandardItem* cell(int row, int column) const;

    UiState* m_state = nullptr;
    QStandardItemModel* m_model;
    QTableView* m_view;
    QPushButton* m_selectAllButton;
    QPushButton* m_unselectAllButton;
    QPushButton* m_transformButton;
    QPushButton* m_readFilesButton;
    QProgressBar* m_readProgressBar;
    QLabel* m_status;
    QList<quint64> m_ids;
    QHash<quint64, int> m_idToRow;
    bool m_hasLocalFiles = false;
    // The snapshot at open time: the item (what OK rebuilds from) and the
    // per-cell text (the diff baseline).
    QList<MediaItem> m_originals;
    QHash<int, QStringList> m_originalCells;
    // The read-from-file results (the successful reads, per id — a failed
    // read is absent): their art is stored on Accept (the tags fill the
    // cells, the art has no cell).
    QHash<quint64, TrackMeta> m_readResults;
    QFont m_bold;
    bool m_styling = false;
};

#endif // METADATADIALOG_H
