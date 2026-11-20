#include "shortcutspage.h"

#include "shortcutmanager.h"
#include "settingsmanager.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int ColumnCategory = 0;
constexpr int ColumnAction = 1;
constexpr int ColumnShortcut = 2;
constexpr int ColumnGlobal = 3;

} // namespace

ShortcutsPage::ShortcutsPage(QWidget *parent)
    : QWidget(parent)
    , m_table(nullptr)
{
    QVBoxLayout* layout = new QVBoxLayout(this);

    QLabel* title = new QLabel(
        "Global keyboard actions. Edit a sequence (or clear it to "
        "disable the action); global actions work in every window, the "
        "rest only in the main window.", this);
    title->setObjectName("intro");
    title->setWordWrap(true);
    layout->addWidget(title);

    m_table = new QTableWidget(this);
    m_table->setObjectName("actionsTable");
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels(
        { "Category", "Action", "Shortcut", "Global" });
    m_table->horizontalHeader()->setSectionResizeMode(ColumnCategory,
                                                      QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColumnAction,
                                                      QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColumnShortcut,
                                                      QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColumnGlobal,
                                                      QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_table, 1);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    QPushButton* reset = new QPushButton("Reset selection", this);
    reset->setObjectName("resetButton");
    buttonLayout->addWidget(reset);
    QPushButton* resetAll = new QPushButton("Reset all", this);
    resetAll->setObjectName("resetAllButton");
    buttonLayout->addWidget(resetAll);
    buttonLayout->addStretch(1);
    layout->addLayout(buttonLayout);

    connect(m_table, &QTableWidget::cellChanged, this,
            [this](int row, int column) {
        if (column == ColumnGlobal)
            onGlobalToggled(row);
    });
    connect(reset, &QPushButton::clicked, this, &ShortcutsPage::onResetClicked);
    connect(resetAll, &QPushButton::clicked, this, &ShortcutsPage::onResetAllClicked);

    // A value changed from elsewhere (the Settings browser) syncs the
    // matching row; the page's own edits loop back here too (a no-op:
    // the values already match).
    connect(SettingsManager::getInstance(), &SettingsManager::changed,
            this, [this](const QString &key, const QVariant &) {
        const QString id = ShortcutManager::actionIdForKey(key);
        if (id.isEmpty())
            return;
        const int row = rowForAction(id);
        if (row >= 0)
            syncRow(row);
    });

    rebuild();
}

void ShortcutsPage::rebuild()
{
    m_table->setRowCount(0);
    m_table->blockSignals(true);
    // Grouped by category (alphabetical), registration order within a
    // category.
    const QList<ShortcutAction> actions = ShortcutManager::getInstance()->actions();
    QStringList categories;
    for (const ShortcutAction &action : actions)
        if (!categories.contains(action.category))
            categories.append(action.category);
    std::sort(categories.begin(), categories.end());
    for (const QString &category : categories)
        for (const ShortcutAction &action : actions)
            if (action.category == category)
                addRow(action, category);
    m_table->blockSignals(false);
}

void ShortcutsPage::addRow(const ShortcutAction &action, const QString &category)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->setItem(row, ColumnCategory, new QTableWidgetItem(category));
    m_table->setItem(row, ColumnAction, new QTableWidgetItem(action.title));

    QKeySequenceEdit* edit = new QKeySequenceEdit;
    edit->setKeySequence(
        ShortcutManager::getInstance()->sequence(action.id));
    // The clear button is the edit's clearing UX (a Backspace over the
    // text is not, in this Qt); it empties the sequence with a
    // keySequenceChanged and no editingFinished, so the empty sequence
    // commits from there.
    edit->setClearButtonEnabled(true);
    m_table->setCellWidget(row, ColumnShortcut, edit);
    connect(edit, &QKeySequenceEdit::editingFinished, this,
            [this, action] {
        const int row = rowForAction(action.id);
        if (row >= 0)
            onSequenceEdited(row);
    });
    connect(edit, &QKeySequenceEdit::keySequenceChanged, this,
            [this, action](const QKeySequence &sequence) {
        if (!sequence.isEmpty())
            return; // a new recording also starts with a clear
        const int row = rowForAction(action.id);
        if (row >= 0)
            onSequenceEdited(row);
    });

    QTableWidgetItem* global = new QTableWidgetItem;
    global->setCheckState(Qt::Unchecked);
    m_table->setItem(row, ColumnGlobal, global);

    m_table->item(row, ColumnAction)->setData(Qt::UserRole, action.id);
    syncRow(row);
}

QString ShortcutsPage::actionIdAt(int row) const
{
    if (row < 0 || row >= m_table->rowCount())
        return {};
    return m_table->item(row, ColumnAction)->data(Qt::UserRole).toString();
}

int ShortcutsPage::rowForAction(const QString &id) const
{
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (actionIdAt(row) == id)
            return row;
    }
    return -1;
}

void ShortcutsPage::syncRow(int row)
{
    const QString id = actionIdAt(row);
    if (id.isEmpty())
        return;

    ShortcutManager* manager = ShortcutManager::getInstance();
    m_table->blockSignals(true);
    if (QKeySequenceEdit* edit =
            qobject_cast<QKeySequenceEdit*>(m_table->cellWidget(row, ColumnShortcut)))
        edit->setKeySequence(manager->sequence(id));
    if (QTableWidgetItem* item = m_table->item(row, ColumnGlobal))
        item->setCheckState(manager->isGlobal(id) ? Qt::Checked : Qt::Unchecked);
    m_table->blockSignals(false);
}

void ShortcutsPage::onSequenceEdited(int row)
{
    const QString id = actionIdAt(row);
    if (id.isEmpty())
        return;
    if (QKeySequenceEdit* edit =
            qobject_cast<QKeySequenceEdit*>(m_table->cellWidget(row, ColumnShortcut)))
        SettingsManager::getInstance()->setValue(
            ShortcutManager::sequenceKey(id), edit->keySequence().toString());
}

void ShortcutsPage::onGlobalToggled(int row)
{
    const QString id = actionIdAt(row);
    if (id.isEmpty())
        return;
    const Qt::CheckState state = m_table->item(row, ColumnGlobal)->checkState();
    SettingsManager::getInstance()->setValue(ShortcutManager::globalKey(id),
                                             state == Qt::Checked);
}

void ShortcutsPage::onResetClicked()
{
    const int row = m_table->currentRow();
    const QString id = actionIdAt(row);
    if (id.isEmpty())
        return;
    SettingsManager* settings = SettingsManager::getInstance();
    settings->resetToDefault(ShortcutManager::sequenceKey(id));
    settings->resetToDefault(ShortcutManager::globalKey(id));
}

void ShortcutsPage::onResetAllClicked()
{
    SettingsManager* settings = SettingsManager::getInstance();
    for (const ShortcutAction &action : ShortcutManager::getInstance()->actions()) {
        settings->resetToDefault(ShortcutManager::sequenceKey(action.id));
        settings->resetToDefault(ShortcutManager::globalKey(action.id));
    }
}
