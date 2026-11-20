#include <QtTest>

#include <QCheckBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <memory>
#include <array>

#include "configpage.h"
#include "settingsmanager.h"
#include "settingsregistry.h"
#include "valueeditdialog.h"

namespace {

class ConfigPageTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void settingsAreGroupedByOwner();
    void treeIsExpandedByDefaultAndAfterSearch();
    void valueAndDefaultColumnsShow();
    void orphanedKeysToggle();
    void modifiedOnlyFilter();
    void searchFilter();
    void doubleClickOpensTypedEditor();
    void doubleClickOnGroupOpensNoEditor();
    void editStringValue();
    void editBoolValue();
    void editListValue();
    void resetToDefault();
    void resetButtonDisabledForUnmodifiedAndOrphanedRows();
    void settingsChangesRefreshTheView();
    void columnWidthsPersistAcrossInstances();

private:
    QTreeWidget* tree() const;
    QTreeWidgetItem* row(const QString &key) const;
    void doubleClick(QTreeWidgetItem *item);

    ConfigPage* m_page = nullptr;
    SettingsManager* m_settings = nullptr;
    std::unique_ptr<QTemporaryDir> m_dir;
};

void ConfigPageTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    m_settings = SettingsManager::getInstance();
    m_settings->setSettingsPath(m_dir->path() + "/settings.json");
    m_settings->load();
    m_settings->registerSetting(
        { "core.string", "String setting", "A string.", "def", QString() });
    m_settings->registerSetting(
        { "core.bool", "Bool setting", "A bool.", false, QString() });
    m_settings->registerSetting(
        { "core.list", "List setting", "A list.", QStringList(), QString() });
    m_settings->registerSetting(
        { "plugin.key", "Plugin setting", "A plugin one.", 1, "fakeplugin" });
    SettingsRegistry::getInstance()->registerOwner("fakeplugin",
                                                   "Fake Plugin");
    m_page = new ConfigPage;
}

void ConfigPageTest::cleanup()
{
    delete m_page;
    m_page = nullptr;
    SettingsRegistry::getInstance()->clear();
    SettingsManager::reset();
}

QTreeWidget* ConfigPageTest::tree() const
{
    return m_page->findChild<QTreeWidget*>("settingsTree");
}

QTreeWidgetItem* ConfigPageTest::row(const QString &key) const
{
    QTreeWidget* t = tree();
    for (int g = 0; g < t->topLevelItemCount(); ++g) {
        QTreeWidgetItem* group = t->topLevelItem(g);
        for (int c = 0; c < group->childCount(); ++c) {
            QTreeWidgetItem* child = group->child(c);
            if (child->text(0) == key)
                return child;
        }
    }
    return nullptr;
}

void ConfigPageTest::doubleClick(QTreeWidgetItem *item)
{
    m_page->show();
    // A synthetic itemDoubleClicked (this Qt's QMetaObject::activate
    // exposes only the void** overload): the tree's double-click handling
    // is not needed to exercise the page's handler.
    const int idx = tree()->metaObject()
        ->indexOfSignal("itemDoubleClicked(QTreeWidgetItem*,int)");
    int column = 0;
    std::array<void*, 3> args = { nullptr, static_cast<void*>(&item),
                                  static_cast<void*>(&column), };
    QMetaObject::activate(tree(), idx, args.data());
}

void ConfigPageTest::settingsAreGroupedByOwner()
{
    QTreeWidget* t = tree();
    QCOMPARE(t->topLevelItemCount(), 2);  // no orphaned yet
    QCOMPARE(t->topLevelItem(0)->text(0), QString("Core"));
    QCOMPARE(t->topLevelItem(0)->childCount(), 3);
    QCOMPARE(t->topLevelItem(1)->text(0), QString("Fake Plugin"));
    QCOMPARE(t->topLevelItem(1)->childCount(), 1);
}

void ConfigPageTest::treeIsExpandedByDefaultAndAfterSearch()
{
    QTreeWidget* t = tree();
    for (int g = 0; g < t->topLevelItemCount(); ++g)
        QVERIFY(t->topLevelItem(g)->isExpanded());

    // A search keystroke rebuilds the tree; the remaining groups come
    // back expanded.
    QLineEdit* search = m_page->findChild<QLineEdit*>("searchBox");
    QVERIFY(search != nullptr);
    search->setText("list");  // only core.list matches

    t = tree();
    QCOMPARE(t->topLevelItemCount(), 1);
    for (int g = 0; g < t->topLevelItemCount(); ++g)
        QVERIFY(t->topLevelItem(g)->isExpanded());
    QVERIFY(row("core.list") != nullptr);
}

void ConfigPageTest::valueAndDefaultColumnsShow()
{
    QTreeWidgetItem* r = row("core.string");
    QVERIFY(r != nullptr);
    QCOMPARE(r->text(1), QString("QString"));
    QCOMPARE(r->text(2), QString("def"));
    QCOMPARE(r->text(3), QString("def"));

    QTreeWidgetItem* l = row("core.list");
    QVERIFY(l != nullptr);
    QCOMPARE(l->text(1), QString("QStringList"));
    QCOMPARE(l->text(2), QString());  // the empty list
    QCOMPARE(l->text(3), QString());
}

void ConfigPageTest::orphanedKeysToggle()
{
    m_settings->setValue("loose.key", "leftover");
    QVERIFY(row("loose.key") != nullptr);

    QCheckBox* show = m_page->findChild<QCheckBox*>("showOrphaned");
    QVERIFY(show != nullptr);
    QTest::mouseClick(show, Qt::LeftButton);  // hide orphaned

    QVERIFY(row("loose.key") == nullptr);
}

void ConfigPageTest::modifiedOnlyFilter()
{
    m_settings->setValue("core.string", "x");

    QCheckBox* mod = m_page->findChild<QCheckBox*>("modifiedOnly");
    QVERIFY(mod != nullptr);
    QTest::mouseClick(mod, Qt::LeftButton);  // modified only

    QVERIFY(row("core.string") != nullptr);
    QVERIFY(row("core.bool") == nullptr);
    QVERIFY(row("plugin.key") == nullptr);
}

void ConfigPageTest::searchFilter()
{
    QLineEdit* search = m_page->findChild<QLineEdit*>("searchBox");
    QVERIFY(search != nullptr);

    search->setText("list");
    QVERIFY(row("core.list") != nullptr);
    QVERIFY(row("core.string") == nullptr);

    search->setText("bool setting");  // the title
    QVERIFY(row("core.bool") != nullptr);
    QVERIFY(row("core.string") == nullptr);

    search->setText("a plugin one");  // the description
    QVERIFY(row("plugin.key") != nullptr);
}

void ConfigPageTest::doubleClickOpensTypedEditor()
{
    doubleClick(row("core.string"));

    ValueEditDialog* dialog = m_page->findChild<ValueEditDialog*>("valueEditDialog");
    QVERIFY(dialog != nullptr);
    QVERIFY(dialog->findChild<QLineEdit*>("valueEditor") != nullptr);

    dialog->findChild<QPushButton*>("cancelButton")->click();
    QTest::qWait(0);  // the deferred delete needs the event loop
    QVERIFY(m_page->findChild<ValueEditDialog*>("valueEditDialog") == nullptr);
}

void ConfigPageTest::doubleClickOnGroupOpensNoEditor()
{
    // A group item carries no kind data, which reads back as SettingRow
    // — the editor must not open for it.
    doubleClick(tree()->topLevelItem(0));

    QVERIFY(m_page->findChild<ValueEditDialog*>("valueEditDialog") == nullptr);

    // Selecting a group keeps the Reset button disabled as well.
    tree()->setCurrentItem(tree()->topLevelItem(0));
    QPushButton* reset = m_page->findChild<QPushButton*>("resetButton");
    QVERIFY(reset != nullptr);
    QVERIFY(!reset->isEnabled());
}

void ConfigPageTest::editStringValue()
{
    doubleClick(row("core.string"));

    ValueEditDialog* dialog = m_page->findChild<ValueEditDialog*>("valueEditDialog");
    QVERIFY(dialog != nullptr);
    dialog->findChild<QLineEdit*>("valueEditor")->setText("edited");
    dialog->findChild<QPushButton*>("okButton")->click();

    QCOMPARE(m_settings->value("core.string").toString(), QString("edited"));
    QCOMPARE(row("core.string")->text(2), QString("edited"));
    QTest::qWait(0);  // the deferred delete needs the event loop
    QVERIFY(m_page->findChild<ValueEditDialog*>("valueEditDialog") == nullptr);
}

void ConfigPageTest::editBoolValue()
{
    doubleClick(row("core.bool"));

    ValueEditDialog* dialog = m_page->findChild<ValueEditDialog*>("valueEditDialog");
    QVERIFY(dialog != nullptr);
    dialog->show();  // a hidden checkbox has no clickable geometry
    QCheckBox* editor = dialog->findChild<QCheckBox*>("valueEditor");
    QVERIFY(editor != nullptr);
    QVERIFY(!editor->isChecked());
    editor->click();  // a synthetic press+release
    dialog->findChild<QPushButton*>("okButton")->click();

    QCOMPARE(m_settings->value("core.bool").toBool(), true);
}

void ConfigPageTest::editListValue()
{
    doubleClick(row("core.list"));

    ValueEditDialog* dialog = m_page->findChild<ValueEditDialog*>("valueEditDialog");
    QVERIFY(dialog != nullptr);
    QPlainTextEdit* editor = dialog->findChild<QPlainTextEdit*>("valueEditor");
    QVERIFY(editor != nullptr);
    editor->setPlainText("a\nb");
    dialog->findChild<QPushButton*>("okButton")->click();

    QCOMPARE(m_settings->value("core.list").toStringList(),
             QStringList() << "a" << "b");
    QCOMPARE(row("core.list")->text(2), QString("a, b"));
}

void ConfigPageTest::resetToDefault()
{
    m_settings->setValue("core.string", "x");
    tree()->setCurrentItem(row("core.string"));

    QPushButton* reset = m_page->findChild<QPushButton*>("resetButton");
    QVERIFY(reset != nullptr);
    QVERIFY(reset->isEnabled());
    QTest::mouseClick(reset, Qt::LeftButton);

    QCOMPARE(m_settings->value("core.string").toString(), QString("def"));
    QVERIFY(!m_settings->isModified("core.string"));
    QCOMPARE(row("core.string")->text(2), QString("def"));
}

void ConfigPageTest::resetButtonDisabledForUnmodifiedAndOrphanedRows()
{
    QPushButton* reset = m_page->findChild<QPushButton*>("resetButton");
    QVERIFY(reset != nullptr);

    tree()->setCurrentItem(row("core.bool"));  // unmodified
    QVERIFY(!reset->isEnabled());

    m_settings->setValue("loose.x", 1);
    tree()->setCurrentItem(row("loose.x"));  // orphaned
    QVERIFY(!reset->isEnabled());
}

void ConfigPageTest::settingsChangesRefreshTheView()
{
    m_settings->setValue("core.string", "from outside");
    QCOMPARE(row("core.string")->text(2), QString("from outside"));
}

// The tree's column widths ride the dialog's state scope: saved on the
// page's hide (its close — the dialog's close cascades one to it), and
// restored by a fresh page (a fresh dialog session).
void ConfigPageTest::columnWidthsPersistAcrossInstances()
{
    QTreeWidget* t = tree();
    m_page->show();
    t->header()->resizeSection(0, 250);
    t->header()->resizeSection(2, 300);
    m_page->hide();  // the page's close: the widths are saved

    // Under the dialog's key, in its "columns" field.
    const QVariantMap columns =
        m_settings->value("dialogs.preferences").toMap()
            .value("columns").toMap();
    QCOMPARE(columns.value("key").toInt(), 250);
    QCOMPARE(columns.value("value").toInt(), 300);

    // A fresh page restores the widths; an untouched column keeps the
    // view's default.
    ConfigPage* page2 = new ConfigPage;
    QTreeWidget* t2 = page2->findChild<QTreeWidget*>("settingsTree");
    QVERIFY(t2 != nullptr);
    QCOMPARE(t2->header()->sectionSize(0), 250);
    QCOMPARE(t2->header()->sectionSize(2), 300);
    delete page2;
}

} // namespace

QTEST_MAIN(ConfigPageTest)
#include "configpage_test.moc"
