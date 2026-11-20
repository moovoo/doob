#include "checkablelistdialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "dialogplumbing.h"

namespace {

// A stretcher row. This Qt's list view marks every row user-checkable
// (and draws a checkbox for the flag): the flag is cleared so the
// stretcher shows no checkbox — its row is its presence.
QListWidgetItem* makeStretcherItem()
{
    QListWidgetItem* item = new QListWidgetItem(QStringLiteral("Stretcher"));
    item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
    item->setData(Qt::UserRole, CheckableListDialog::stretcherKey);
    return item;
}

} // namespace

CheckableListDialog::CheckableListDialog(const QString& title,
                                         const QString& hint,
                                         const QList<QPair<QString, QString>>& catalog,
                                         const QStringList& order,
                                         QWidget* parent,
                                         bool stretchers,
                                         const QString& stateKey)
    : CheckableListDialog(title, hint, catalog, order, parent, stateKey,
                          stretchers, {}, false)
{
}

CheckableListDialog::CheckableListDialog(const QString& title,
                                         const QString& hint,
                                         const QList<QPair<QString, QString>>& catalog,
                                         const QStringList& order,
                                         const QHash<QString, Qt::CheckState>& states,
                                         QWidget* parent,
                                         const QString& stateKey)
    : CheckableListDialog(title, hint, catalog, order, parent, stateKey,
                          false, states, true)
{
}

CheckableListDialog::CheckableListDialog(const QString& title,
                                         const QString& hint,
                                         const QList<QPair<QString, QString>>& catalog,
                                         const QStringList& order,
                                         QWidget* parent,
                                         const QString& stateKey,
                                         bool stretchers,
                                         const QHash<QString, Qt::CheckState>& states,
                                         bool triState)
    : QDialog(parent)
    , m_triState(triState)
    , m_states(states)
    , m_catalog(catalog)
    , m_list(nullptr)
    , m_moveUp(nullptr)
    , m_moveDown(nullptr)
    , m_insertStretcher(nullptr)
    , m_remove(nullptr)
    , m_reset(nullptr)
    , m_populating(false)
{
    setWindowTitle(title);

    QVBoxLayout* layout = new QVBoxLayout(this);
    setLayout(layout);

    QLabel* hintLabel = new QLabel(hint, this);
    hintLabel->setObjectName("hintLabel");
    // The hints can be a full sentence (the lines' tri-state legend):
    // wrap, don't stretch the dialog to the text's width.
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);

    m_list = new QListWidget(this);
    m_list->setObjectName("itemList");
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_list, 1);

    QHBoxLayout* moveLayout = new QHBoxLayout();
    m_moveUp = new QPushButton("Move up", this);
    m_moveUp->setObjectName("moveUpButton");
    m_moveDown = new QPushButton("Move down", this);
    m_moveDown->setObjectName("moveDownButton");
    moveLayout->addWidget(m_moveUp);
    moveLayout->addWidget(m_moveDown);
    if (stretchers) {
        m_insertStretcher = new QPushButton("Insert stretcher", this);
        m_insertStretcher->setObjectName("insertStretcherButton");
        m_remove = new QPushButton("Remove", this);
        m_remove->setObjectName("removeButton");
        moveLayout->addStretch(1);
        moveLayout->addWidget(m_insertStretcher);
        moveLayout->addWidget(m_remove);
    }
    layout->addLayout(moveLayout);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    m_reset = new QPushButton("Reset to default", this);
    m_reset->setObjectName("resetButton");
    buttonLayout->addWidget(m_reset);
    buttonLayout->addStretch(1);

    QPushButton* cancel = new QPushButton("Cancel", this);
    cancel->setObjectName("cancelButton");
    buttonLayout->addWidget(cancel);

    QPushButton* ok = new QPushButton("OK", this);
    ok->setObjectName("okButton");
    ok->setDefault(true);
    buttonLayout->addWidget(ok);

    layout->addLayout(buttonLayout);

    connect(m_list, &QListWidget::itemChanged, this, &CheckableListDialog::onItemChanged);
    connect(m_list, &QListWidget::currentRowChanged, this, &CheckableListDialog::onSelectionChanged);
    connect(m_moveUp, &QPushButton::clicked, this, &CheckableListDialog::onMoveUpClicked);
    connect(m_moveDown, &QPushButton::clicked, this, &CheckableListDialog::onMoveDownClicked);
    if (m_insertStretcher != nullptr)
        connect(m_insertStretcher, &QPushButton::clicked, this,
                &CheckableListDialog::onInsertStretcherClicked);
    if (m_remove != nullptr)
        connect(m_remove, &QPushButton::clicked, this, &CheckableListDialog::onRemoveClicked);
    connect(m_reset, &QPushButton::clicked, this, &CheckableListDialog::onResetClicked);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);

    populate(order);
    updateMoveButtons();
    updateStretcherButtons();

    // The dialog's size and position are user preference, not layout:
    // the standard UiState wiring (tracked on every hide, restored now)
    // under the caller's key. A dialog without a key persists nothing.
    if (!stateKey.isEmpty())
        dialog::dialogUiState(this, stateKey);
}

void CheckableListDialog::populate(const QStringList& order)
{
    m_populating = true;
    m_list->clear();

    // Binary mode: the visible items first (in display order), then the
    // hidden ones (in catalog order). Tri-state mode: the list order is
    // the display sequence itself (every key, the states orthogonal).
    // Every catalog entry gets exactly one row; the stretcher rows ride
    // in the display order as given.
    QStringList keys = order;
    for (const auto& pair : m_catalog) {
        if (!keys.contains(pair.second))
            keys.append(pair.second);
    }

    for (const QString& key : keys) {
        if (key == stretcherKey) {
            m_list->addItem(makeStretcherItem());
            continue;
        }
        for (const auto& pair : m_catalog) {
            if (pair.second != key)
                continue;
            QListWidgetItem* item = new QListWidgetItem(pair.first, m_list);
            Qt::ItemFlags flags = item->flags() | Qt::ItemIsUserCheckable;
            if (m_triState)
                // The list's plain click toggle is binary (it would lose
                // the middle state); the tristate flag makes a click
                // cycle all three states.
                flags |= Qt::ItemIsUserTristate;
            item->setFlags(flags);
            Qt::CheckState state = Qt::Unchecked;
            if (m_triState)
                state = m_states.value(key, Qt::PartiallyChecked); // the default
            else if (order.contains(key))
                state = Qt::Checked;
            else
                state = Qt::Unchecked;
            item->setCheckState(state);
            item->setData(Qt::UserRole, key);
            break;
        }
    }

    m_populating = false;
}

QStringList CheckableListDialog::selectedOrder() const
{
    QStringList keys;
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* item = m_list->item(i);
        if (item->checkState() == Qt::Checked)
            keys.append(item->data(Qt::UserRole).toString());
    }
    return keys;
}

QStringList CheckableListDialog::sequence() const
{
    QStringList keys;
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* item = m_list->item(i);
        const bool inSequence = m_triState
            ? true // every row (the state is orthogonal to the order)
            : (isStretcher(item) || item->checkState() == Qt::Checked);
        if (inSequence)
            keys.append(item->data(Qt::UserRole).toString());
    }
    return keys;
}

QHash<QString, Qt::CheckState> CheckableListDialog::states() const
{
    // A tri-state notion: the binary dialogs keep none.
    if (!m_triState)
        return {};
    QHash<QString, Qt::CheckState> out;
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* item = m_list->item(i);
        if (!isStretcher(item))
            out.insert(item->data(Qt::UserRole).toString(), item->checkState());
    }
    return out;
}

void CheckableListDialog::onItemChanged(QListWidgetItem* item)
{
    if (m_populating)
        return;
    if (m_triState) {
        // Keep at least one visible line (visible = not never).
        if (item->checkState() == Qt::Unchecked && visibleCount() == 0)
            item->setCheckState(Qt::PartiallyChecked);
        return;
    }
    if (item->checkState() == Qt::Unchecked && checkedCount() == 0)
        item->setCheckState(Qt::Checked); // keep at least one item visible
}

void CheckableListDialog::onSelectionChanged()
{
    updateMoveButtons();
    updateStretcherButtons();
}

void CheckableListDialog::onMoveUpClicked()
{
    const int row = m_list->currentRow();
    if (row <= 0)
        return;
    QListWidgetItem* item = m_list->takeItem(row);
    m_list->insertItem(row - 1, item);
    m_list->setCurrentRow(row - 1);
}

void CheckableListDialog::onMoveDownClicked()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_list->count() - 1)
        return;
    QListWidgetItem* item = m_list->takeItem(row);
    m_list->insertItem(row + 1, item);
    m_list->setCurrentRow(row + 1);
}

void CheckableListDialog::onInsertStretcherClicked()
{
    // The cap is the number of gaps: one before the first button, one
    // after the last (the catalog size + 1).
    if (stretcherCount() >= m_catalog.size() + 1)
        return;
    const int row = m_list->currentRow();
    // The item is created unattached and inserted at its position: a
    // QListWidgetItem that is already in the list is not moved by
    // insertItem() in this Qt (it stays where it was added).
    QListWidgetItem* item = makeStretcherItem();
    if (row < 0)
        m_list->addItem(item);
    else
        m_list->insertItem(row + 1, item);
    m_list->setCurrentRow(row < 0 ? m_list->count() - 1 : row + 1);
    updateStretcherButtons();
}

void CheckableListDialog::onRemoveClicked()
{
    const int row = m_list->currentRow();
    if (row < 0)
        return;
    if (!isStretcher(m_list->item(row)))
        return; // the buttons are removed by unchecking, not here
    m_list->takeItem(row);
    updateStretcherButtons();
}

void CheckableListDialog::onResetClicked()
{
    // Every item visible, in catalog order (the stretchers are not part
    // of the default; the tri-state default is when-non-empty).
    QStringList all;
    for (const auto& pair : m_catalog)
        all.append(pair.second);
    if (m_triState) {
        m_states.clear();
        for (const auto& pair : m_catalog)
            m_states.insert(pair.second, Qt::PartiallyChecked);
    }
    populate(all);
    updateMoveButtons();
    updateStretcherButtons();
}

void CheckableListDialog::updateMoveButtons()
{
    const int row = m_list->currentRow();
    m_moveUp->setEnabled(row > 0);
    m_moveDown->setEnabled(row >= 0 && row < m_list->count() - 1);
}

void CheckableListDialog::updateStretcherButtons()
{
    if (m_insertStretcher == nullptr)
        return;
    m_insertStretcher->setEnabled(stretcherCount() < m_catalog.size() + 1);
    const int row = m_list->currentRow();
    m_remove->setEnabled(row >= 0 && isStretcher(m_list->item(row)));
}

int CheckableListDialog::checkedCount() const
{
    int count = 0;
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->checkState() == Qt::Checked)
            ++count;
    }
    return count;
}

int CheckableListDialog::visibleCount() const
{
    // The tri-state's "visible": any state but never (unchecked).
    int count = 0;
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->checkState() != Qt::Unchecked)
            ++count;
    }
    return count;
}

int CheckableListDialog::stretcherCount() const
{
    int count = 0;
    for (int i = 0; i < m_list->count(); ++i) {
        if (isStretcher(m_list->item(i)))
            ++count;
    }
    return count;
}

bool CheckableListDialog::isStretcher(QListWidgetItem* item) const
{
    return item->data(Qt::UserRole).toString() == stretcherKey;
}
