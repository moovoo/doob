#include "configpage.h"

#include "dialogplumbing.h"
#include "settingsmanager.h"
#include "settingsregistry.h"
#include "uistate.h"
#include "valueeditdialog.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QHash>
#include <QHideEvent>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMetaType>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace {
// The tree's stable column keys (one per logical column, in order):
// the names its widths are stored under.
const QStringList kTreeColumns = { "key", "type", "value", "default" };
}

ConfigPage::ConfigPage(QWidget *parent)
    : QWidget(parent)
{
    QVBoxLayout* layout = new QVBoxLayout(this);

    QHBoxLayout* filterLayout = new QHBoxLayout();
    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("searchBox"));
    m_search->setPlaceholderText(QStringLiteral("Search key, title or "
                                                "description..."));
    m_modifiedOnly = new QCheckBox(QStringLiteral("Modified only"), this);
    m_modifiedOnly->setObjectName(QStringLiteral("modifiedOnly"));
    m_showOrphaned = new QCheckBox(QStringLiteral("Show orphaned"), this);
    m_showOrphaned->setObjectName(QStringLiteral("showOrphaned"));
    m_showOrphaned->setChecked(true);
    filterLayout->addWidget(m_search);
    filterLayout->addWidget(m_modifiedOnly);
    filterLayout->addWidget(m_showOrphaned);
    layout->addLayout(filterLayout);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("settingsTree"));
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({ QStringLiteral("Key"), QStringLiteral("Type"),
                              QStringLiteral("Value"),
                              QStringLiteral("Default"), });
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(m_tree, 1);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch(1);
    m_reset = new QPushButton(QStringLiteral("Reset to default"), this);
    m_reset->setObjectName(QStringLiteral("resetButton"));
    m_reset->setEnabled(false);
    buttonLayout->addWidget(m_reset);
    layout->addLayout(buttonLayout);

    connect(m_search, &QLineEdit::textChanged, this, &ConfigPage::rebuild);
    connect(m_modifiedOnly, &QCheckBox::toggled, this, &ConfigPage::rebuild);
    connect(m_showOrphaned, &QCheckBox::toggled, this, &ConfigPage::rebuild);
    connect(m_reset, &QPushButton::clicked, this, &ConfigPage::onResetClicked);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            &ConfigPage::onRowDoubleClicked);
    connect(m_tree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *) {
        m_reset->setEnabled(
            isSettingRow(item)
            && m_settings != nullptr
            && m_settings->isModified(item->data(0, Qt::UserRole).toString()));
    });

    rebuild();

    // The tree's column widths, in the dialog's state scope (the page
    // is part of the dialog's arrangement — the dialog's geometry and
    // the page's columns share the "dialogs.preferences" map).
    m_state = new UiState(QStringLiteral("dialogs.preferences"), this);
    m_state->restoreColumns(m_tree->header(), kTreeColumns);
}

void ConfigPage::hideEvent(QHideEvent *event)
{
    // The page is not a window (it has no closeEvent): a hide is its
    // close — the dialog's close cascades one to it, and the stacked
    // widget hides it on a topic switch (an unchanged save is a no-op).
    m_state->saveColumns(m_tree->header(), kTreeColumns);
    QWidget::hideEvent(event);
}

void ConfigPage::rebuild()
{
    // The SettingsManager is a process singleton; a test may reset it
    // between the page's construction and a later rebuild, so re-point
    // the change connections at the current instance.
    SettingsManager* settings = SettingsManager::getInstance();
    if (settings != m_settings) {
        disconnect(m_changedConnection);
        m_changedConnection =
            connect(settings, &SettingsManager::changed, this,
                    &ConfigPage::rebuild);
        m_settings = settings;
    }

    const QString selectedKey = [this] {
        const QTreeWidgetItem* item = m_tree->currentItem();
        return item == nullptr ? QString()
                               : item->data(0, Qt::UserRole).toString();
    }();
    const QString search = m_search->text().trimmed().toLower();

    m_tree->clear();
    addSettingRows(settings, search);
    addOrphanedRows(settings, search);

    // A group with no visible child is dropped.
    for (int g = m_tree->topLevelItemCount() - 1; g >= 0; --g)
        if (m_tree->topLevelItem(g)->childCount() == 0)
            delete m_tree->topLevelItem(g);

    if (QTreeWidgetItem* row = rowByKey(selectedKey))
        m_tree->setCurrentItem(row);

    // The groups stay open across every rebuild (construction, a search
    // keystroke, a filter toggle, a SettingsManager change).
    m_tree->expandAll();
}

void ConfigPage::addSettingRows(SettingsManager *settings,
                                const QString &search)
{
    // The registered settings, grouped by owner (the core under "Core",
    // a plugin under its declared title).
    QHash<QString, QTreeWidgetItem*> groups;
    const auto groupFor = [this, &groups](const QString &owner)
            -> QTreeWidgetItem* {
        const auto it = groups.find(owner);
        if (it != groups.end())
            return it.value();
        const QString title = owner.isEmpty()
            ? QStringLiteral("Core")
            : SettingsRegistry::getInstance()->ownerTitle(owner);
        QTreeWidgetItem* group = new QTreeWidgetItem(m_tree, { title });
        groups.insert(owner, group);
        return group;
    };

    for (const Setting &s : settings->settings()) {
        if (m_modifiedOnly->isChecked() && !settings->isModified(s.key))
            continue;
        if (!matches(search, s.key, s.title, s.description))
            continue;
        addRow(groupFor(s.owner), s.key, settings->value(s.key), s.defaultValue,
               SettingRow);
    }
}

void ConfigPage::addOrphanedRows(SettingsManager *settings,
                                 const QString &search)
{
    // The orphaned keys (stored values of unregistered settings — a
    // removed plugin's leftovers): shown at the user's request.
    if (!m_showOrphaned->isChecked())
        return;
    QStringList keys;
    for (const QString &key : settings->storedKeys()) {
        if (!matches(search, key, QString(), QString()))
            continue;
        if (settings->setting(key).isValid())
            continue; // a registered key, already listed
        keys.append(key);
    }
    if (keys.isEmpty())
        return;
    new QTreeWidgetItem(m_tree, { QStringLiteral("Orphaned") });
    QTreeWidgetItem* group =
        m_tree->topLevelItem(m_tree->topLevelItemCount() - 1);
    for (const QString &key : keys)
        addRow(group, key, settings->value(key), QVariant(), OrphanedRow);
}

bool ConfigPage::matches(const QString &search, const QString &key,
                         const QString &title, const QString &description)
{
    return search.isEmpty()
        || key.toLower().contains(search)
        || title.toLower().contains(search)
        || description.toLower().contains(search);
}

void ConfigPage::addRow(QTreeWidgetItem *group, const QString &key,
                        const QVariant &value, const QVariant &defaultValue,
                        int kind)
{
    // The type column shows the registered default's type (stable) for a
    // setting, the stored value's type otherwise.
    const QVariant typedValue = kind == SettingRow ? defaultValue : value;
    QTreeWidgetItem* row = new QTreeWidgetItem(
        group, { key,
                 typedValue.isValid() ? typeName(typedValue) : QString(),
                 displayValue(value),
                 defaultValue.isValid() ? displayValue(defaultValue)
                                        : QStringLiteral("-"), });
    row->setData(0, Qt::UserRole, key);
    row->setData(0, Qt::UserRole + 1, kind);
}

QTreeWidgetItem* ConfigPage::rowByKey(const QString &key) const
{
    return dialog::treeItemByData(m_tree, key);
}

bool ConfigPage::isSettingRow(const QTreeWidgetItem *item) const
{
    // A group item carries no kind data; an unset itemData reads back as
    // 0, which is SettingRow, so the kind check alone admits groups.
    return item != nullptr
        && item->parent() != nullptr
        && item->data(0, Qt::UserRole + 1).toInt() == SettingRow;
}

void ConfigPage::onRowDoubleClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);
    if (!isSettingRow(item))
        return; // groups and orphaned rows are read-only

    const QString key = item->data(0, Qt::UserRole).toString();
    const Setting s = m_settings->setting(key);
    ValueEditDialog* dialog = new ValueEditDialog(
        s.title.isEmpty() ? key : s.title, m_settings->value(key), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::accepted, this, [this, dialog, key] {
        m_settings->setValue(key, dialog->editedValue());
    });
    dialog->show();
}

void ConfigPage::onResetClicked()
{
    const QTreeWidgetItem* item = m_tree->currentItem();
    if (!isSettingRow(item))
        return;
    // resetToDefault emits changed(), which rebuilds the view.
    m_settings->resetToDefault(item->data(0, Qt::UserRole).toString());
}

QString ConfigPage::displayValue(const QVariant &value)
{
    if (!value.isValid())
        return QStringLiteral("(none)");
    switch (value.type()) {
    case QMetaType::Bool:
        return value.toBool() ? QStringLiteral("true")
                              : QStringLiteral("false");
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
        return QString::number(value.toDouble());
    case QMetaType::QStringList:
        return value.toStringList().join(QStringLiteral(", "));
    case QMetaType::QVariantList:
    case QMetaType::QVariantMap:
        return QString::fromUtf8(QJsonDocument::fromVariant(value)
                                     .toJson(QJsonDocument::Compact));
    default:
        return value.toString();
    }
}

QString ConfigPage::typeName(const QVariant &value)
{
    return QString::fromLatin1(value.typeName());
}
