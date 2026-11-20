#ifndef METADATATABLE_H
#define METADATATABLE_H

#include <QFont>
#include <QHeaderView>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTableView>

// The metadata editors' shared table machinery (the bulk editor's, the
// single-item editor's): the editable table's setup, the diff mark (the
// bold font and the "original → current" tooltip) and the "Read from
// file" affordance (its button, its progress bar and a read's start).
namespace metadata {

// The editors' table: row selection, extended selection, no row
// numbers — the "tableView" the tests (and the column-width state) key
// on.
inline QTableView* editableTable(QWidget *parent, QStandardItemModel *model)
{
    QTableView* view = new QTableView(parent);
    view->setObjectName("tableView");
    view->setModel(model);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view->verticalHeader()->hide();
    return view;
}

// Marks (or unmarks) a cell's diff against its open-time value: bold
// font and a "original → current" tooltip when `changed`, the default
// font and no tooltip otherwise. `styling` is the caller's re-entrancy
// guard (the mark itself emits itemChanged).
inline void markDiff(QStandardItem *item, const QString &original,
                     bool changed, const QFont &bold, bool &styling)
{
    styling = true;
    if (changed) {
        item->setFont(bold);
        item->setToolTip(
            QStringLiteral("%1 \u2192 %2").arg(original, item->text()));
    } else {
        item->setFont(QFont());
        item->setToolTip(QString());
    }
    styling = false;
}

// The "Read from file" button and its progress bar (created idle and
// hidden until a read runs): the button is returned out, the bar back.
inline QProgressBar* readButton(QWidget *parent, QPushButton **button,
                                const QString &text, const QString &objectName,
                                const QString &tooltip)
{
    QPushButton* read = new QPushButton(text, parent);
    read->setObjectName(objectName);
    read->setToolTip(tooltip);
    *button = read;
    QProgressBar* bar = new QProgressBar(parent);
    bar->setObjectName("readProgressBar");
    bar->setRange(0, 1);
    bar->setFixedWidth(120);
    bar->setVisible(false);
    return bar;
}

// Starts a read (the button's enabled state is the gate): the button
// disables and the bar starts. False when the button is disabled.
inline bool beginRead(QPushButton *button, QProgressBar *bar)
{
    if (!button->isEnabled())
        return false;
    button->setEnabled(false);
    bar->setVisible(true);
    bar->setValue(0);
    return true;
}

} // namespace metadata

#endif // METADATATABLE_H
