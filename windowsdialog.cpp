#include "windowsdialog.h"

#include "dialogplumbing.h"
#include "windowmanager.h"
#include "uistate.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QVBoxLayout>

WindowsDialog::WindowsDialog(Mode mode, QWidget *parent, const QString &selectedId)
    : QDialog(parent), m_mode(mode)
{
    setWindowTitle(mode == Mode::Editor ? "Windows" : "Select window");

    m_table = new QTableWidget(this);
    m_table->setObjectName("table");
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({QStringLiteral("Name"),
                                        QStringLiteral("Type"),
                                        QStringLiteral("Visible"),
                                       });
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(mode == Mode::Editor
        ? QAbstractItemView::DoubleClicked
        : QAbstractItemView::NoEditTriggers);
    connect(m_table, &QTableWidget::itemChanged, this, &WindowsDialog::onItemChanged);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(m_table);

    QDialogButtonBox* box = nullptr;
    if (mode == Mode::Editor) {
        QHBoxLayout* buttons = new QHBoxLayout();
        QPushButton* add = new QPushButton(QStringLiteral("Add"), this);
        add->setObjectName("addButton");
        QPushButton* remove = new QPushButton(QStringLiteral("Remove"), this);
        remove->setObjectName("removeButton");
        QPushButton* toggle = new QPushButton(QStringLiteral("Show/Hide"), this);
        toggle->setObjectName("toggleButton");
        connect(add, &QPushButton::clicked, this, &WindowsDialog::onAddClicked);
        connect(remove, &QPushButton::clicked, this, &WindowsDialog::onRemoveClicked);
        connect(toggle, &QPushButton::clicked, this, &WindowsDialog::onToggleVisibilityClicked);
        buttons->addWidget(add);
        buttons->addWidget(remove);
        buttons->addWidget(toggle);
        buttons->addStretch(1);
        layout->addLayout(buttons);
        box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    } else {
        box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        // The box's standard buttons carry no objectName of their own
        // (only a "which" property): name them for test access.
        box->button(QDialogButtonBox::Ok)->setObjectName("okButton");
        box->button(QDialogButtonBox::Cancel)->setObjectName("cancelButton");
        connect(m_table, &QTableWidget::itemDoubleClicked, this,
                [this](QTableWidgetItem* item) {
            // A real double-click makes the item the current row first
            // (the mouse press); a synthetic one does not — select from
            // the item itself so both paths pick the same row.
            m_table->setCurrentCell(item->row(), 0);
            accept();
        });
    }
    box->setObjectName("buttonBox");
    layout->addWidget(box);

    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // The table is a live view: a change from anywhere (the toggle
    // widget, a window's own close button) refreshes the rows.
    connect(WindowManager::getInstance(), &WindowManager::windowsChanged,
            this, &WindowsDialog::onWindowsChanged);

    populate(selectedId);
    resize(480, 320);

    // The dialog's own state: its size and position (saved on every
    // hide) and the table's column widths (the name column stretches).
    m_state =
        dialog::dialogUiState(this, QStringLiteral("dialogs.windows"));
    m_state->restoreColumns(m_table->horizontalHeader(),
                             { "name", "type", "visible" });
}

void WindowsDialog::hideEvent(QHideEvent *event)
{
    // The buttons close through reject/accept, which hide the dialog
    // without a closeEvent — the columns are saved on every hide, like
    // the geometry (an unchanged save is a no-op).
    m_state->saveColumns(m_table->horizontalHeader(),
                             { "name", "type", "visible" });
    QDialog::hideEvent(event);
}

QString WindowsDialog::selectedId() const
{
    return selectedWindowId();
}

QPair<QString, WindowManager::Kind> WindowsDialog::promptNewWindow()
{
    QDialog dialog(this);
    dialog.setWindowTitle("New window");

    QFormLayout* form = new QFormLayout(&dialog);
    QLineEdit* name = new QLineEdit(QStringLiteral("Window"), &dialog);
    name->setObjectName("nameEdit");
    form->addRow(QStringLiteral("Name:"), name);
    QRadioButton* standalone = new QRadioButton(QStringLiteral("Standalone"), &dialog);
    QRadioButton* docked = new QRadioButton(QStringLiteral("Docked"), &dialog);
    standalone->setChecked(true);
    form->addRow(QStringLiteral("Type:"), standalone);
    form->addRow(QString(), docked);
    QDialogButtonBox* box = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(box);

    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
        return {QString(), WindowManager::Kind::Standalone};
    return {name->text().trimmed(),
            docked->isChecked() ? WindowManager::Kind::Docked
                                : WindowManager::Kind::Standalone};
}

QString WindowsDialog::removeWindowText(const QString &title)
{
    return QStringLiteral("Remove the window \"%1\"? Its widgets are lost "
                          "with it.").arg(title);
}

bool WindowsDialog::confirmRemoveWindow(const QString &id, const QString &title)
{
    return QMessageBox::question(this, "Remove window", removeWindowText(title))
           == QMessageBox::Yes;
}

void WindowsDialog::onAddClicked()
{
    const QPair<QString, WindowManager::Kind> result = promptNewWindow();
    if (result.first.isEmpty())
        return;
    const QString id = WindowManager::getInstance()->addWindow(result.second,
                                                               result.first);
    if (!id.isEmpty())
        m_table->selectRow(rowOf(id));
}

void WindowsDialog::onRemoveClicked()
{
    const QString id = selectedWindowId();
    if (id.isEmpty())
        return;
    if (!confirmRemoveWindow(id, WindowManager::getInstance()->windowTitle(id)))
        return;
    WindowManager::getInstance()->removeWindow(id);
}

void WindowsDialog::onToggleVisibilityClicked()
{
    const QString id = selectedWindowId();
    if (id.isEmpty())
        return;
    WindowManager* wm = WindowManager::getInstance();
    wm->setWindowVisible(id, !wm->windowVisible(id));
}

void WindowsDialog::onItemChanged(QTableWidgetItem* item)
{
    if (m_populating || item == nullptr)
        return;
    // The id rides on the row's name item (the other columns carry
    // none of their own).
    const int row = m_table->row(item);
    const QTableWidgetItem* name = m_table->item(row, 0);
    if (name == nullptr)
        return;
    const QString id = name->data(Qt::UserRole).toString();
    if (id.isEmpty())
        return;
    if (m_table->column(item) == 0) {
        const QString title = item->text().trimmed();
        if (!title.isEmpty())
            WindowManager::getInstance()->setWindowTitle(id, title);
    } else if (m_table->column(item) == 2) {
        WindowManager::getInstance()->setWindowVisible(
            id, item->checkState() == Qt::Checked);
    }
}

void WindowsDialog::onWindowsChanged()
{
    populate(selectedWindowId());
}

void WindowsDialog::populate(const QString &selectId)
{
    m_populating = true;
    m_table->setRowCount(0);

    const QList<WindowManager::Info> windows = WindowManager::getInstance()->windows();
    for (const WindowManager::Info &info : windows)
        addRow(info);
    m_populating = false;

    const int row = rowOf(selectId);
    if (m_table->rowCount() == 0)
        m_table->clearSelection();
    else
        m_table->selectRow(row >= 0 ? row : 0);
}

void WindowsDialog::addRow(const WindowManager::Info &info)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);

    QTableWidgetItem* name = new QTableWidgetItem(info.title);
    name->setData(Qt::UserRole, info.id);
    if (m_mode == Mode::Editor)
        name->setFlags(name->flags() | Qt::ItemIsEditable);
    m_table->setItem(row, 0, name);

    if (m_mode == Mode::Editor)
        m_table->setCellWidget(row, 1, typeCombo(info));
    else
        m_table->setItem(row, 1, kindItem(info.kind));

    QTableWidgetItem* vis = new QTableWidgetItem();
    vis->setCheckState(info.visible ? Qt::Checked : Qt::Unchecked);
    if (m_mode == Mode::Editor)
        vis->setFlags(vis->flags() | Qt::ItemIsUserCheckable);
    m_table->setItem(row, 2, vis);
}

QComboBox* WindowsDialog::typeCombo(const WindowManager::Info &info)
{
    QComboBox* combo = new QComboBox();
    combo->addItem(QStringLiteral("Standalone"));
    combo->addItem(QStringLiteral("Docked"));
    combo->setCurrentIndex(info.kind == WindowManager::Kind::Docked ? 1 : 0);
    connect(combo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this, id = info.id](int index) {
        if (m_populating)
            return;
        WindowManager::getInstance()->setWindowKind(
            id, index == 1 ? WindowManager::Kind::Docked
                           : WindowManager::Kind::Standalone);
    });
    return combo;
}

QTableWidgetItem* WindowsDialog::kindItem(WindowManager::Kind kind)
{
    return new QTableWidgetItem(
        kind == WindowManager::Kind::Docked ? QStringLiteral("Docked")
                                            : QStringLiteral("Standalone"));
}

QString WindowsDialog::selectedWindowId() const
{
    const int row = m_table->currentRow();
    if (row < 0)
        return {};
    if (QTableWidgetItem* item = m_table->item(row, 0))
        return item->data(Qt::UserRole).toString();
    return {};
}

int WindowsDialog::rowOf(const QString &id) const
{
    for (int row = 0; row < m_table->rowCount(); ++row)
        if (QTableWidgetItem* item = m_table->item(row, 0);
            item != nullptr && item->data(Qt::UserRole).toString() == id)
            return row;
    return -1;
}
