#ifndef DIALOGPLUMBING_H
#define DIALOGPLUMBING_H

#include <QDialog>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "uistate.h"

// The small dialogs' shared plumbing: the standard UiState wiring
// (the geometry tracked and restored), the button row (the cancel that
// rejects and the default primary button — the "cancelButton" and
// "okButton" names the tests key on), the reference help dialog (rich
// text in a scrollable browser and a close button) and the two-level
// tree scan (QTreeWidget::findItems does not match on a data role on
// this Qt (6.12) — it finds nothing with a role argument, so the scan
// is manual).
namespace dialog {

// The dialog's standard UiState wiring: created under `key` (the state
// scope the dialog's pages share), tracking the dialog's geometry
// (saved on every hide) and restoring it (the stored maximized flag on
// the first show).
inline UiState *dialogUiState(QDialog *dlg, const QString &key)
{
    UiState *state = new UiState(key, dlg);
    state->track(dlg);
    state->restoreGeometry(dlg);
    return state;
}

// The dialog's button row: a stretch (unless `stretch` is false), the
// cancel button (rejects) and the default primary button (fires
// `primarySlot`), parented to `dlg` and laid out in `row`.
template <typename PrimarySlot>
void dialogButtons(QDialog *dlg, QHBoxLayout *row,
                   const QString &cancelText, const QString &cancelName,
                   const QString &primaryText, const QString &primaryName,
                   PrimarySlot primarySlot, bool stretch = true)
{
    QPushButton *cancel = new QPushButton(cancelText, dlg);
    cancel->setObjectName(cancelName);
    QPushButton *primary = new QPushButton(primaryText, dlg);
    primary->setObjectName(primaryName);
    primary->setDefault(true);
    if (stretch)
        row->addStretch();
    row->addWidget(cancel);
    row->addWidget(primary);
    QObject::connect(cancel, &QPushButton::clicked, dlg, &QDialog::reject);
    QObject::connect(primary, &QPushButton::clicked, dlg, primarySlot);
}

// The OK-only row (no cancel): a stretch and the default OK button
// (accepts).
inline void dialogOkButton(QDialog *dlg, QHBoxLayout *row)
{
    QPushButton *ok = new QPushButton("OK", dlg);
    ok->setObjectName("okButton");
    ok->setDefault(true);
    row->addStretch(1);
    row->addWidget(ok);
    QObject::connect(ok, &QPushButton::clicked, dlg, &QDialog::accept);
}

// The help dialog's shape: rich text in a scrollable browser (the
// reference outgrows a message box) and a close button — the metadata
// transform's and the style sheet's help share it.
inline void showReferenceDialog(QWidget *parent, const QString &title,
                                const QString &html, bool externalLinks = false)
{
    QDialog help(parent);
    help.setWindowTitle(title);
    QTextBrowser *browser = new QTextBrowser(&help);
    browser->setHtml(html);
    if (externalLinks)
        browser->setOpenExternalLinks(true);
    QPushButton *close = new QPushButton("Close", &help);
    close->setDefault(true);
    QObject::connect(close, &QPushButton::clicked, &help, &QDialog::accept);
    QVBoxLayout *layout = new QVBoxLayout(&help);
    layout->addWidget(browser);
    layout->addWidget(close, 0, Qt::AlignRight);
    help.resize(700, 560);
    help.exec();
}

// The entry of a two-level tree (the ungrouped default at the top,
// group → entries below it) carrying `name` in column 0's `role` (the
// top-level items are checked too).
inline QTreeWidgetItem *treeItemByData(QTreeWidget *tree, const QString &name,
                                       int role = Qt::UserRole)
{
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = tree->topLevelItem(i);
        if (item->data(0, role).toString() == name)
            return item;
        for (int j = 0; j < item->childCount(); ++j) {
            if (item->child(j)->data(0, role).toString() == name)
                return item->child(j);
        }
    }
    return nullptr;
}

} // namespace dialog

#endif // DIALOGPLUMBING_H
