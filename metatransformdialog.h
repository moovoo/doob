#ifndef METATRANSFORMDIALOG_H
#define METATRANSFORMDIALOG_H

#include <QDialog>
#include <QHideEvent>
#include <QList>

class QComboBox;
class QLineEdit;
class QStandardItemModel;
class QTableWidget;
class QLabel;
class QPushButton;
class UiState;

// The mass-transformation dialog: applies a MetaTransform expression to
// one column of a set of rows of the metadata editor's table. The
// expression is evaluated per row against the item's variables (the file
// parts, the open-time original metadata, the cell's present value); a
// live preview shows every row's result, and Apply writes the results
// into the table cells (in memory only — the editor's OK is what commits
// to the store).
class MetaTransformDialog : public QDialog
{
    Q_OBJECT
public:
    // model: the editor's table (the cells Apply writes into), ids: the
    // item id per table row, rows: the rows to transform (the checked
    // ones), initialColumn: the target column combo's start.
    explicit MetaTransformDialog(QStandardItemModel *model,
                                 const QList<quint64> &ids,
                                 const QList<int> &rows,
                                 QWidget *parent = nullptr);

    int column() const;
    QString expression() const;

protected:
    void hideEvent(QHideEvent *event) override;

private slots:
    void updatePreview();
    void onApply();
    void onHelp();

private:
    UiState* m_state = nullptr;
    // Copied, not referenced: the caller's checked-rows list (and the
    // ids accessor's return) are temporaries that must not dangle.
    QStandardItemModel* m_model;
    QList<quint64> m_ids;
    QList<int> m_rows;
    QComboBox* m_columnCombo;
    QLineEdit* m_expression;
    QTableWidget* m_preview;
    QLabel* m_status;
};

#endif // METATRANSFORMDIALOG_H
