#include <QtTest>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTableWidgetItem>
#include <QTableWidget>
#include <QPushButton>

#include <memory>

#include "shortcutmanager.h"
#include "shortcutspage.h"
#include "settingsmanager.h"

namespace {

class ShortcutsPageTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void listsRegisteredActionsGroupedByCategory();
    void editingTheSequenceWritesToTheEngine();
    void clearingTheSequenceDisablesTheAction();
    void togglingGlobalWritesToTheEngine();
    void settingsChangeSyncsTheRow();
    void resetSelectionRestoresTheDefaults();
    void resetAllRestoresTheDefaults();

private:
    QTableWidget* actionsTable() const { return m_page->findChild<QTableWidget*>("actionsTable"); }
    // The sequence edit of the row that shows the action with the given id.
    QKeySequenceEdit* editFor(const QString &actionTitle) const;
    int rowFor(const QString &actionTitle) const;

    SettingsManager* m_settings;
    ShortcutManager* m_manager;
    ShortcutsPage* m_page;
    std::unique_ptr<QTemporaryDir> m_dir;
};

void ShortcutsPageTest::init()
{
    SettingsManager::reset();
    m_settings = SettingsManager::getInstance();
    m_dir = std::make_unique<QTemporaryDir>();
    m_settings->setSettingsPath(m_dir->path() + "/settings.json");
    m_settings->load();

    ShortcutManager::reset();
    m_manager = ShortcutManager::getInstance();
    // Two actions in two categories (the page groups by category,
    // registration order within one).
    m_manager->registerAction(
        { QStringLiteral("page.act_b"), QStringLiteral("Action B"),
          QStringLiteral("Beta"), QKeySequence(QStringLiteral("Ctrl+2")),
          true, [] {}, });
    m_manager->registerAction(
        { QStringLiteral("page.act_a"), QStringLiteral("Action A"),
          QStringLiteral("Alpha"), QKeySequence(QStringLiteral("Ctrl+1")),
          false, [] {}, });

    m_page = new ShortcutsPage();
    m_page->show();
}

void ShortcutsPageTest::cleanup()
{
    // A QTest::keyClick with a modifier sticks the app's keyboard modifier
    // state (the synthetic press is delivered through
    // qt_sendShortcutOverrideEvent, and no event ever releases the
    // modifier key itself): reset it with a modifier-less key press so it
    // cannot leak into a later test's item-view selection handling.
    QTest::keyClick(m_page, Qt::Key_unknown, Qt::NoModifier);
    delete m_page;
    ShortcutManager::reset();
    SettingsManager::reset();
}

int ShortcutsPageTest::rowFor(const QString &actionTitle) const
{
    QTableWidget *table = actionsTable();
    for (int row = 0; row < table->rowCount(); ++row) {
        if (table->item(row, 1)->text() == actionTitle)
            return row;
    }
    return -1;
}

QKeySequenceEdit* ShortcutsPageTest::editFor(const QString &actionTitle) const
{
    const int row = rowFor(actionTitle);
    return row < 0 ? nullptr
                   : qobject_cast<QKeySequenceEdit*>(actionsTable()->cellWidget(row, 2));
}

void ShortcutsPageTest::listsRegisteredActionsGroupedByCategory()
{
    QTableWidget *table = actionsTable();
    QCOMPARE(table->rowCount(), 2);
    // Alpha (Action A) before Beta (Action B), registration order inside.
    QCOMPARE(table->item(0, 0)->text(), QString("Alpha"));
    QCOMPARE(table->item(0, 1)->text(), QString("Action A"));
    QCOMPARE(table->item(1, 0)->text(), QString("Beta"));
    QCOMPARE(table->item(1, 1)->text(), QString("Action B"));

    // The effective values (no stored overrides yet: the defaults).
    QKeySequenceEdit *editA = editFor("Action A");
    QVERIFY(editA != nullptr);
    QCOMPARE(editA->keySequence().toString(), QString("Ctrl+1"));
    QCOMPARE(table->item(0, 3)->checkState(), Qt::Unchecked);
    QCOMPARE(table->item(1, 3)->checkState(), Qt::Checked);
}

void ShortcutsPageTest::editingTheSequenceWritesToTheEngine()
{
    QKeySequenceEdit *edit = editFor("Action A");
    QVERIFY(edit != nullptr);

    // The edit confirms a sequence one second after the last key release
    // (its release timer) — editingFinished (the page's commit point)
    // fires then.
    QTest::keyClick(edit, Qt::Key_F, Qt::ControlModifier);
    QTest::qWait(1100);

    QCOMPARE(m_settings->value(ShortcutManager::sequenceKey("page.act_a")).toString(),
             QString("Ctrl+F"));
    QCOMPARE(m_manager->sequence("page.act_a").toString(), QString("Ctrl+F"));
}

void ShortcutsPageTest::clearingTheSequenceDisablesTheAction()
{
    QKeySequenceEdit *edit = editFor("Action A");
    QVERIFY(edit != nullptr);

    // Clearing is the edit's clear button: it empties the internal line
    // edit, whose textChanged the edit turns into a cleared sequence
    // (committed through keySequenceChanged, no editingFinished — the
    // page's clear-button commit path).
    QLineEdit *line = edit->findChild<QLineEdit*>("qt_keysequenceedit_lineedit");
    QVERIFY(line != nullptr);
    line->clear();

    QCOMPARE(m_settings->value(ShortcutManager::sequenceKey("page.act_a")).toString(),
             QString());
    QVERIFY(m_manager->sequence("page.act_a").isEmpty());
}

void ShortcutsPageTest::togglingGlobalWritesToTheEngine()
{
    QTableWidget *table = actionsTable();
    const int row = rowFor("Action B");
    QVERIFY(row >= 0);

    // The checkbox toggle (cellChanged is the page's input signal).
    table->item(row, 3)->setCheckState(Qt::Unchecked);

    QCOMPARE(m_settings->value(ShortcutManager::globalKey("page.act_b")).toBool(),
             false);
    QCOMPARE(m_manager->isGlobal("page.act_b"), false);
}

void ShortcutsPageTest::settingsChangeSyncsTheRow()
{
    QKeySequenceEdit *edit = editFor("Action B");
    QVERIFY(edit != nullptr);

    // A change from elsewhere (the Settings browser writes the SettingsManager
    // directly, not the page).
    m_settings->setValue(ShortcutManager::sequenceKey("page.act_b"), "Ctrl+G");

    QCOMPARE(edit->keySequence().toString(), QString("Ctrl+G"));
}

void ShortcutsPageTest::resetSelectionRestoresTheDefaults()
{
    m_settings->setValue(ShortcutManager::sequenceKey("page.act_a"), "Ctrl+Z");
    m_settings->setValue(ShortcutManager::globalKey("page.act_a"), true);
    QTableWidget *table = actionsTable();
    table->setCurrentCell(rowFor("Action A"), 0);

    QPushButton *reset = m_page->findChild<QPushButton*>("resetButton");
    QVERIFY(reset != nullptr);
    QTest::mouseClick(reset, Qt::LeftButton);

    QVERIFY(!m_settings->isModified(ShortcutManager::sequenceKey("page.act_a")));
    QVERIFY(!m_settings->isModified(ShortcutManager::globalKey("page.act_a")));
    QCOMPARE(m_manager->sequence("page.act_a").toString(), QString("Ctrl+1"));
    QCOMPARE(m_manager->isGlobal("page.act_a"), false);
    QCOMPARE(editFor("Action A")->keySequence().toString(), QString("Ctrl+1"));
}

void ShortcutsPageTest::resetAllRestoresTheDefaults()
{
    m_settings->setValue(ShortcutManager::sequenceKey("page.act_a"), "Ctrl+Z");
    m_settings->setValue(ShortcutManager::globalKey("page.act_b"), false);

    QPushButton *resetAll = m_page->findChild<QPushButton*>("resetAllButton");
    QVERIFY(resetAll != nullptr);
    QTest::mouseClick(resetAll, Qt::LeftButton);

    QVERIFY(!m_settings->isModified(ShortcutManager::sequenceKey("page.act_a")));
    QVERIFY(!m_settings->isModified(ShortcutManager::globalKey("page.act_b")));
    QCOMPARE(m_manager->sequence("page.act_a").toString(), QString("Ctrl+1"));
    QCOMPARE(m_manager->isGlobal("page.act_b"), true);
}
}

QTEST_MAIN(ShortcutsPageTest)
#include "shortcutspage_test.moc"
