#include <QtTest>

#include <QDir>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include "librarysettingspage.h"
#include "preferencesdialog.h"
#include "pluginmanager.h"
#include "settings.h"
#include "settingsregistry.h"

namespace {

class PreferencesDialogTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void coreTopicsAreListedUnderMain();
    void pluginsGroupIsHiddenWithoutPlugins();
    void firstTopicIsSelectedByDefault();
    void topicTreeIsExpandedByDefault();
    void selectingATopicShowsItsPage();
    void pluginWithoutTopicsGetsANoSettingsNote();
    void pluginTopicsAreListedUnderPlugins();
    void okAcceptsTheDialog();

private:
    QTreeWidget* tree(PreferencesDialog& dialog) const
    {
        return dialog.findChild<QTreeWidget*>("topicTree");
    }
    QStackedWidget* pages(PreferencesDialog& dialog) const
    {
        return dialog.findChild<QStackedWidget*>("pages");
    }
    QTreeWidgetItem* findChild(QTreeWidgetItem* parent, const QString& title) const
    {
        for (int i = 0; i < parent->childCount(); ++i)
            if (parent->child(i)->text(0) == title)
                return parent->child(i);
        return nullptr;
    }

    // The data dir the fake plugin's settings file lands in.
    QTemporaryDir m_dataDir{QDir::tempPath() + "/doob_preferencesdialog_test"};
    SettingsRegistry* m_registry = nullptr;
};

void PreferencesDialogTest::initTestCase()
{
    qputenv("DOOB_DATA_DIR", m_dataDir.path().toUtf8());
    Settings::reset();
}

void PreferencesDialogTest::cleanupTestCase()
{
    qunsetenv("DOOB_DATA_DIR");
    Settings::reset();
}

void PreferencesDialogTest::init()
{
    // A clean plugin and topic state before every test.
    PluginManager::getInstance()->shutdownPlugins();
    m_registry = SettingsRegistry::getInstance();
    m_registry->clear();
}

void PreferencesDialogTest::cleanup()
{
    PluginManager::getInstance()->shutdownPlugins();
    m_registry->clear();
}

void PreferencesDialogTest::coreTopicsAreListedUnderMain()
{
    PreferencesDialog dialog;
    dialog.show();

    const QTreeWidget* topics = tree(dialog);
    QCOMPARE(topics->topLevelItem(0)->text(0), QString("Main"));
    QCOMPARE(topics->topLevelItem(0)->childCount(), 4);
    QCOMPARE(topics->topLevelItem(0)->child(0)->text(0), QString("Library"));
    QCOMPARE(topics->topLevelItem(0)->child(1)->text(0), QString("Settings"));
    QCOMPARE(topics->topLevelItem(0)->child(2)->text(0), QString("DSP"));
    QCOMPARE(topics->topLevelItem(0)->child(3)->text(0), QString("Shortcuts"));
}

void PreferencesDialogTest::pluginsGroupIsHiddenWithoutPlugins()
{
    PreferencesDialog dialog;
    dialog.show();

    QCOMPARE(tree(dialog)->topLevelItemCount(), 1);
}

void PreferencesDialogTest::firstTopicIsSelectedByDefault()
{
    PreferencesDialog dialog;
    dialog.show();

    QCOMPARE(dialog.selectedTopicId(), QString("library"));
    QCOMPARE(pages(dialog)->count(), 4);
    QVERIFY(pages(dialog)->currentWidget()->findChild<QListWidget*>("watchDirs"));
}

void PreferencesDialogTest::topicTreeIsExpandedByDefault()
{
    // A plugin owner (no topics) keeps the "Plugins" group present, so
    // both groups must be expanded.
    m_registry->registerOwner("owneronly", "Owner only");

    PreferencesDialog dialog;
    dialog.show();

    QTreeWidget* topics = tree(dialog);
    QCOMPARE(topics->topLevelItemCount(), 2);
    for (int i = 0; i < topics->topLevelItemCount(); ++i)
        QVERIFY(topics->topLevelItem(i)->isExpanded());
}

void PreferencesDialogTest::selectingATopicShowsItsPage()
{
    QLabel* page = new QLabel("Test page");
    page->setObjectName("testPage");
    QVERIFY(m_registry->registerTopic("test", "Test topic", QString(),
                                      [page]() { return page; }));

    PreferencesDialog dialog;
    dialog.show();

    QTreeWidget* topics = tree(dialog);
    QTreeWidgetItem* main = topics->topLevelItem(0);
    QVERIFY(findChild(main, "Test topic"));
    topics->setCurrentItem(findChild(main, "Test topic"));

    QCOMPARE(dialog.selectedTopicId(), QString("test"));
    QCOMPARE(pages(dialog)->currentWidget(), static_cast<QWidget*>(page));
}

void PreferencesDialogTest::pluginWithoutTopicsGetsANoSettingsNote()
{
    // The fake plugin registers no topics, only its owner.
    PluginManager::getInstance()->loadFromDir(FAKE_PLUGIN_DIR);

    PreferencesDialog dialog;
    dialog.show();

    QTreeWidget* topics = tree(dialog);
    QCOMPARE(topics->topLevelItemCount(), 2);
    QTreeWidgetItem* plugins = topics->topLevelItem(1);
    QCOMPARE(plugins->text(0), QString("Plugins"));
    QCOMPARE(plugins->childCount(), 1);
    QTreeWidgetItem* entry = plugins->child(0);
    QCOMPARE(entry->text(0), QString("fakeplugin"));

    topics->setCurrentItem(entry);
    QCOMPARE(dialog.selectedTopicId(), QString("fakeplugin"));

    QWidget* page = pages(dialog)->currentWidget();
    QCOMPARE(page->objectName(), QString("noSettingsPage"));
    QLabel* label = page->findChild<QLabel*>("noSettingsLabel");
    QVERIFY(label);
    QVERIFY(label->text().contains("does not provide any settings"));
}

void PreferencesDialogTest::pluginTopicsAreListedUnderPlugins()
{
    QLabel* page = new QLabel("Plugin page");
    page->setObjectName("pluginPage");
    QVERIFY(m_registry->registerTopic("pluginx:topic", "Plugin topic", "pluginx",
                                      [page]() { return page; }));

    PreferencesDialog dialog;
    dialog.show();

    QTreeWidget* topics = tree(dialog);
    QTreeWidgetItem* plugins = topics->topLevelItem(1);
    QCOMPARE(plugins->text(0), QString("Plugins"));
    QTreeWidgetItem* entry = findChild(plugins, "Plugin topic");
    QVERIFY(entry);

    topics->setCurrentItem(entry);
    QCOMPARE(dialog.selectedTopicId(), QString("pluginx:topic"));
    QCOMPARE(pages(dialog)->currentWidget(), static_cast<QWidget*>(page));
}

void PreferencesDialogTest::okAcceptsTheDialog()
{
    PreferencesDialog dialog;
    dialog.show();

    dialog.findChild<QPushButton*>("okButton")->click();

    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
}

} // namespace

QTEST_MAIN(PreferencesDialogTest)
#include "preferencesdialog_test.moc"
