#include "dspsettingspage.h"

#include <QDialog>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "dspregistry.h"
#include "settingsregistry.h"

DspSettingsPage::DspSettingsPage(QWidget *parent)
    : QWidget(parent)
{
    m_registry = DspRegistry::getInstance();

    QVBoxLayout* layout = new QVBoxLayout(this);

    QLabel* title = new QLabel(
        QStringLiteral("The processors run in this order, top to bottom, "
                       "over every chunk the audio output plays; a "
                       "processor is in the chain while its box is "
                       "checked. A driver without an open audio path "
                       "(the built-in null driver) plays without "
                       "them."),
        this);
    title->setObjectName("dspNote");
    title->setWordWrap(true);
    layout->addWidget(title);

    m_list = new QListWidget(this);
    m_list->setObjectName("dspList");
    layout->addWidget(m_list, 1);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    m_up = new QPushButton("Up", this);
    m_up->setObjectName("moveUpButton");
    buttonLayout->addWidget(m_up);
    m_down = new QPushButton("Down", this);
    m_down->setObjectName("moveDownButton");
    buttonLayout->addWidget(m_down);
    buttonLayout->addStretch(1);
    m_configure = new QPushButton("Configure...", this);
    m_configure->setObjectName("configureButton");
    buttonLayout->addWidget(m_configure);
    layout->addLayout(buttonLayout);

    connect(m_list, &QListWidget::itemChanged, this,
            [this](QListWidgetItem* item) { onItemChanged(item); });
    connect(m_list, &QListWidget::currentRowChanged, this,
            &DspSettingsPage::onCurrentRowChanged);
    connect(m_up, &QPushButton::clicked, this, &DspSettingsPage::onMoveUp);
    connect(m_down, &QPushButton::clicked, this, &DspSettingsPage::onMoveDown);
    connect(m_configure, &QPushButton::clicked, this,
            &DspSettingsPage::onConfigure);
    connect(m_registry, &DspRegistry::chainChanged, this,
            &DspSettingsPage::onChainChanged);

    // Initialize from the current registry state (the signal only
    // covers future changes).
    refresh();
}

void DspSettingsPage::refresh()
{
    m_updating = true;
    const int current = selectedId().isEmpty() ? -1 : m_list->currentRow();
    m_list->clear();
    const QList<QPair<QString, QString>> entries = m_registry->options();
    for (const QPair<QString, QString> &entry : entries) {
        const QString owner = m_registry->owner(entry.first);
        QString text = entry.second;
        if (!owner.isEmpty()) {
            const QString ownerTitle =
                SettingsRegistry::getInstance()->ownerTitle(owner);
            text += QStringLiteral(" — %1").arg(
                ownerTitle.isEmpty() ? owner : ownerTitle);
        }
        auto* item = new QListWidgetItem(text, m_list);
        item->setData(Qt::UserRole, entry.first);
        item->setCheckState(m_registry->isEnabled(entry.first)
                                ? Qt::Checked
                                : Qt::Unchecked);
    }
    m_updating = false;

    // Keep the selection on its entry across the rebuild.
    if (current >= 0 && current < m_list->count())
        m_list->setCurrentRow(current);
    onCurrentRowChanged(m_list->currentRow());
}

QString DspSettingsPage::selectedId() const
{
    const QListWidgetItem* item = m_list->currentItem();
    return item == nullptr ? QString()
                           : item->data(Qt::UserRole).toString();
}

void DspSettingsPage::onItemChanged(QListWidgetItem* item)
{
    if (m_updating)
        return;
    m_registry->setEnabled(item->data(Qt::UserRole).toString(),
                           item->checkState() == Qt::Checked);
}

void DspSettingsPage::onCurrentRowChanged(int row)
{
    Q_UNUSED(row);
    const bool selected = !selectedId().isEmpty();
    m_up->setEnabled(selected);
    m_down->setEnabled(selected);
    m_configure->setEnabled(
        selected && m_registry->page(selectedId()) != nullptr);
}

void DspSettingsPage::onMoveUp()
{
    const QString id = selectedId();
    if (!id.isEmpty())
        m_registry->moveUp(id);
}

void DspSettingsPage::onMoveDown()
{
    const QString id = selectedId();
    if (!id.isEmpty())
        m_registry->moveDown(id);
}

void DspSettingsPage::onConfigure()
{
    const DspRegistry::PageCreator creator = m_registry->page(selectedId());
    if (creator == nullptr)
        return;
    QDialog* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("DSP: %1")
                               .arg(m_list->currentItem()->text()));
    QVBoxLayout* layout = new QVBoxLayout(dialog);
    QWidget* page = creator();
    layout->addWidget(page);
    dialog->resize(420, 320);
    dialog->show();
}

void DspSettingsPage::onChainChanged()
{
    refresh();
}
