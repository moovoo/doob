#include "preferencesdialog.h"

#include "configpage.h"
#include "dialogplumbing.h"
#include "dspsettingspage.h"
#include "librarysettingspage.h"
#include "settingsregistry.h"
#include "shortcutspage.h"
#include "uistate.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
// The built-in settings topics, registered like any plugin's would be.
// The registry is idempotent (a taken id is skipped), so a second dialog
// that opens finds the first registration.
void registerBuiltinSettingsTopics()
{
    SettingsRegistry* registry = SettingsRegistry::getInstance();
    if (!registry->contains("library"))
        registry->registerTopic("library", "Library", QString(),
                                []{ return new LibrarySettingsPage; });
    if (!registry->contains("settings"))
        registry->registerTopic("settings", "Settings", QString(),
                                []{ return new ConfigPage; });
    if (!registry->contains("dsp"))
        registry->registerTopic("dsp", "DSP", QString(),
                                []{ return new DspSettingsPage; });
    if (!registry->contains("shortcuts"))
        registry->registerTopic("shortcuts", "Shortcuts", QString(),
                                []{ return new ShortcutsPage; });
}

// The page a plugin that registered no topics gets: a note, not a form.
QWidget* noSettingsPage(const QString &owner)
{
    QWidget* page = new QWidget;
    page->setObjectName("noSettingsPage");
    QVBoxLayout* layout = new QVBoxLayout(page);
    QLabel* label = new QLabel(
        QStringLiteral("%1 does not provide any settings.").arg(owner), page);
    label->setObjectName("noSettingsLabel");
    layout->addWidget(label);
    layout->addStretch(1);
    return page;
}
}

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
    , m_tree(nullptr)
    , m_pages(nullptr)
{
    setWindowTitle("Preferences");

    registerBuiltinSettingsTopics();

    QVBoxLayout* outer = new QVBoxLayout(this);

    QHBoxLayout* middle = new QHBoxLayout();

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName("topicTree");
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);
    m_tree->setFixedWidth(200);
    // The two groups, owned by the tree.
    new QTreeWidgetItem(m_tree, QStringList{ "Main" });
    new QTreeWidgetItem(m_tree, QStringList{ "Plugins" });

    m_pages = new QStackedWidget(this);
    m_pages->setObjectName("pages");

    middle->addWidget(m_tree);
    middle->addWidget(m_pages, 1);
    outer->addLayout(middle, 1);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    dialog::dialogOkButton(this, buttonLayout);
    outer->addLayout(buttonLayout);

    connect(m_tree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *) {
        if (item != nullptr)
            m_pages->setCurrentIndex(item->data(0, Qt::UserRole + 1).toInt());
    });
    buildTopics();
    // The groups open on dialog show, so the topics are visible without
    // expanding anything by hand.
    m_tree->expandAll();

    // Select the first topic, so the dialog opens on something.
    if (QTreeWidgetItem* main = m_tree->topLevelItem(0))
        if (QTreeWidgetItem* first = main->child(0))
            m_tree->setCurrentItem(first);

    resize(600, 400);

    // The dialog's own state (its size and position) — the topics the
    // user picked are the pages' business, not the dialog's.
    m_state =
        dialog::dialogUiState(this, QStringLiteral("dialogs.preferences"));
}

void PreferencesDialog::buildTopics()
{
    QTreeWidgetItem* mainGroup = m_tree->topLevelItem(0);
    QTreeWidgetItem* pluginsGroup = m_tree->topLevelItem(1);

    SettingsRegistry* registry = SettingsRegistry::getInstance();

    for (const SettingsTopic &topic : registry->topics())
        if (topic.owner.isEmpty())
            addEntry(mainGroup, topic.title, topic.id, topic.creator());

    for (const QString &owner : registry->owners()) {
        const QList<SettingsTopic> topics = registry->topicsForOwner(owner);
        if (topics.isEmpty()) {
            const QString title = registry->ownerTitle(owner);
            addEntry(pluginsGroup, title, owner, noSettingsPage(title));
            continue;
        }
        for (const SettingsTopic &topic : topics)
            addEntry(pluginsGroup, topic.title, topic.id, topic.creator());
    }

    // An empty group is dropped (no plugins loaded => no "Plugins").
    if (pluginsGroup->childCount() == 0)
        delete pluginsGroup;
}

QTreeWidgetItem* PreferencesDialog::addEntry(QTreeWidgetItem *group,
                                             const QString &title,
                                             const QString &topicId,
                                             QWidget *page)
{
    const int index = m_pages->addWidget(page);

    QTreeWidgetItem* item = new QTreeWidgetItem(group, QStringList{ title });
    item->setData(0, Qt::UserRole, topicId);
    item->setData(0, Qt::UserRole + 1, index);
    return item;
}

QString PreferencesDialog::selectedTopicId() const
{
    const QTreeWidgetItem* item = m_tree->currentItem();
    return item == nullptr ? QString() : item->data(0, Qt::UserRole).toString();
}
