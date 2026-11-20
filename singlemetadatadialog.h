#ifndef SINGLEMETADATADIALOG_H
#define SINGLEMETADATADIALOG_H

#include <QDialog>
#include <QHideEvent>
#include <QFont>
#include <QHash>
#include <QList>
#include <QString>

#include "mediaitem.h"
#include "trackmetadata.h"

class QStandardItem;
class QStandardItemModel;
class QTableView;
class QProgressBar;
class QPushButton;
class QLabel;
class UiState;

// The single-item metadata editor: a key/value table of one item's tags —
// a fixed, read-only source row on top, then every non-empty standard tag
// (its canonical key) and every extra tag. Cells are editable in place; a
// changed cell is marked bold with a tooltip showing the diff against the
// open-time snapshot. Rows can be added (a new tag) and removed (a tag
// deleted, the source row excepted). Save writes the item through the
// store (write-through), Cancel discards — nothing is persisted until
// Save. "Read from file" re-reads the item's tags from its local file
// (through the library manager's worker) and rebuilds the table from them
// — a preview like a manual edit, written only on Save.
class SingleMetaDataDialog : public QDialog
{
    Q_OBJECT
public:
    // The table's columns.
    enum Column
    {
        Key = 0,
        Value,
        ColumnCount,
    };

    // One tag row: its canonical (uppercase) key and its value.
    struct TagRow
    {
        QString key;
        QString value;

        bool operator==(const TagRow &other) const
        {
            return key == other.key && value == other.value;
        }
    };

    // The tag rows shown for an item: its non-empty standard tags in
    // canonical key order, then its extra tags (the map's key order).
    // The source row is not a tag and is not listed here.
    static QList<TagRow> tagRows(const MediaItem &item);

    explicit SingleMetaDataDialog(quint64 id, QWidget *parent = nullptr);

    // The item id edited (0 when the store does not hold it — the dialog
    // then shows only the source-less table).
    quint64 itemId() const { return m_id; }

protected:
    // A non-fatal problem (a duplicate tag key, an unparseable number, a
    // failed write): a message box in production, a record in the tests.
    virtual void showWarning(const QString &message);
    void hideEvent(QHideEvent *event) override;

private slots:
    void onCellChanged(QStandardItem *item);
    void onAddRow();
    void onRemoveRow();
    void onReadClicked();
    void onSave();

private:
    void buildModel();
    // The remove button follows the selection (a source-row selection
    // leaves it disabled).
    void updateButtons();
    // The read button's state: enabled for a local-file item while the
    // library manager is idle.
    void updateReadButton();
    // Rebuilds the tag rows from the read file (the source row stays): the
    // rows are the file's tags, marked against the database's value per
    // key. Nothing is written until Save.
    void applyReadResult(const TrackMeta &meta);
    // The "N tag(s) changed" line under the table.
    void updateStatus();
    // The item Save would write: the open-time snapshot's identity
    // (id, source, kind, statistics) with the metadata rebuilt from the
    // rows (standard keys onto their field, the rest as extra tags — a
    // removed row deletes its tag). Problems (duplicate keys,
    // unparseable numbers) are appended.
    MediaItem buildUpdatedItem(QStringList &problems) const;
    // Applies one row to the item (the key decides the destination: a
    // standard key onto its field, anything else as an extra tag).
    static void applyRow(MediaItem &item, const QString &key, const QString &value,
                         QStringList &problems);
    // "3/10" → (3, 10), a bare "3" → (3, 0); a non-numeric part appends a
    // problem and leaves the fields at (0, 0).
    static bool parseNumberTotal(const QString &text, int &number, int &total,
                                 QStringList &problems, const QString &key);
    // The open-time metadata and the edited one, field by field (the
    // items share the id, so MediaItem's operator== cannot tell).
    static bool sameMetadata(const MediaItem &a, const MediaItem &b);
    QStandardItem* cell(int row, int column) const;

    QStandardItemModel* m_model;
    UiState* m_state = nullptr;
    QTableView* m_view;
    QPushButton* m_addButton;
    QPushButton* m_removeButton;
    QPushButton* m_readButton;
    QProgressBar* m_readProgressBar;
    QLabel* m_status;
    quint64 m_id = 0;
    MediaItem m_original;
    // The last read-from-file result (empty when none ran): its art is
    // stored on Save (the read's tags fill the table, the art has no cell).
    TrackMeta m_readMeta;
    // The open-time values per canonical key (the source row is not a tag
    // and not listed), the diff source for the bold mark and the status:
    // a row compares against the database's value for its key, so a
    // read-from-file rebuild needs no baseline update.
    QHash<QString, QString> m_originalValues;
    QFont m_bold;
    bool m_styling = false;
};

#endif // SINGLEMETADATADIALOG_H
