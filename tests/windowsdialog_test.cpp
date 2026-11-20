#include <QtTest/QtTest>
#include <QApplication>
#include <QComboBox>
#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>

#include "layoutmanager.h"
#include "settings.h"
#include "settingsmanager.h"
#include "windowmanager.h"
#include "windowsdialog.h"

#include <memory>

namespace {

// The modal sub-dialogs are overridden: they cannot be driven offscreen
// (the chooseFiles() pattern).
class TestWindowsDialog : public WindowsDialog
{
public:
    TestWindowsDialog(Mode mode, QWidget *parent, const QString &selectedId = QString())
        : WindowsDialog(mode, parent, selectedId) {}

    QPair<QString, WindowManager::Kind> promptNewWindow() override
    {
        ++newCount;
        return {newTitle, newKind};
    }
    bool confirmRemoveWindow(const QString &id, const QString &title) override
    {
        removed.append({id, title});
        return confirmResult;
    }

    int newCount = 0;
    QString newTitle = "Fresh";
    WindowManager::Kind newKind = WindowManager::Kind::Standalone;
    bool confirmResult = true;
    QList<QPair<QString, QString>> removed;
};

class WindowsDialogTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void addCreatesWindowFromPrompt();
    void addCancelCreatesNothing();
    void removeConfirmsAndRemoves();
    void removeDeclinedKeepsWindow();
    void removeWindowTextCarriesTitle();
    void renameInlineUpdatesTitle();
    void visibilityCheckUpdatesWindow();
    void typeComboConvertsKind();
    void toggleButtonFlipsVisibility();
    void tableFollowsExternalChanges();
    void pickerSelectsPreselectedWindow();
    void pickerDoubleclickAccepts();
    void pickerEmptyHasNoSelection();
    void rejectSavesTheColumnWidths();

private:
    QTableWidget* table(WindowsDialog& dialog) const
    {
        return dialog.findChild<QTableWidget*>("table");
    }
    QPushButton* button(WindowsDialog& dialog, const QString& name) const
    {
        return dialog.findChild<QPushButton*>(name);
    }

    std::unique_ptr<QTemporaryDir> m_dataDir;
    WindowManager* m_wm = nullptr;
};

void WindowsDialogTest::initTestCase()
{
    m_dataDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dataDir->isValid());
    qputenv("DOOB_DATA_DIR", m_dataDir->path().toUtf8());
    Settings::reset();
    LayoutManager::getInstance()->show();
}

void WindowsDialogTest::init()
{
    m_wm = WindowManager::getInstance();
    m_wm->clear();
}

void WindowsDialogTest::cleanup()
{
    m_wm->clear();
    qApp->processEvents();
}

void WindowsDialogTest::addCreatesWindowFromPrompt()
{
    TestWindowsDialog dialog(WindowsDialog::Mode::Editor, nullptr);
    dialog.show();
    qApp->processEvents();

    dialog.newTitle = "Fresh";
    dialog.newKind = WindowManager::Kind::Docked;
    button(dialog, "addButton")->click();

    QCOMPARE(dialog.newCount, 1);
    QCOMPARE(m_wm->windows().size(), 1);
    const WindowManager::Info info = m_wm->windows().first();
    QCOMPARE(info.title, QString("Fresh"));
    QCOMPARE(info.kind, WindowManager::Kind::Docked);
    // The new row is selected.
    QCOMPARE(table(dialog)->currentRow(), 0);
}

void WindowsDialogTest::addCancelCreatesNothing()
{
    TestWindowsDialog dialog(WindowsDialog::Mode::Editor, nullptr);
    dialog.show();
    qApp->processEvents();

    dialog.newTitle = QString(); // the prompt was cancelled
    button(dialog, "addButton")->click();

    QCOMPARE(dialog.newCount, 1);
    QVERIFY(m_wm->windows().isEmpty());
    QCOMPARE(table(dialog)->rowCount(), 0);
}

void WindowsDialogTest::removeConfirmsAndRemoves()
{
    const QString id = m_wm->addWindow(WindowManager::Kind::Standalone, "R");
    TestWindowsDialog dialog(WindowsDialog::Mode::Editor, nullptr);
    dialog.show();
    qApp->processEvents();

    dialog.confirmResult = true;
    button(dialog, "removeButton")->click();

    QCOMPARE(dialog.removed.size(), 1);
    QCOMPARE(dialog.removed.first().first, id);
    QCOMPARE(dialog.removed.first().second, QString("R"));
    QVERIFY(!m_wm->hasWindow(id));
    QCOMPARE(table(dialog)->rowCount(), 0);
}

void WindowsDialogTest::removeDeclinedKeepsWindow()
{
    const QString id = m_wm->addWindow(WindowManager::Kind::Standalone, "R");
    TestWindowsDialog dialog(WindowsDialog::Mode::Editor, nullptr);
    dialog.show();
    qApp->processEvents();

    dialog.confirmResult = false;
    button(dialog, "removeButton")->click();

    QCOMPARE(dialog.removed.size(), 1);
    QVERIFY(m_wm->hasWindow(id));
    QCOMPARE(table(dialog)->rowCount(), 1);
}

void WindowsDialogTest::removeWindowTextCarriesTitle()
{
    QCOMPARE(WindowsDialog::removeWindowText("My Window"),
             QString("Remove the window \"My Window\"? Its widgets are "
                     "lost with it."));
}

void WindowsDialogTest::renameInlineUpdatesTitle()
{
    const QString id = m_wm->addWindow(WindowManager::Kind::Standalone, "Old");
    TestWindowsDialog dialog(WindowsDialog::Mode::Editor, nullptr);
    dialog.show();
    qApp->processEvents();

    table(dialog)->item(0, 0)->setText("New"); // the inline edit commits

    QCOMPARE(m_wm->windowTitle(id), QString("New"));
    // The live repopulation keeps the row in sync.
    QCOMPARE(table(dialog)->item(0, 0)->text(), QString("New"));
}

void WindowsDialogTest::visibilityCheckUpdatesWindow()
{
    const QString id = m_wm->addWindow(WindowManager::Kind::Standalone, "V");
    TestWindowsDialog dialog(WindowsDialog::Mode::Editor, nullptr);
    dialog.show();
    qApp->processEvents();

    table(dialog)->item(0, 2)->setCheckState(Qt::Unchecked);

    QCOMPARE(m_wm->windowVisible(id), false);
    QCOMPARE(table(dialog)->item(0, 2)->checkState(), Qt::Unchecked);

    table(dialog)->item(0, 2)->setCheckState(Qt::Checked);
    QCOMPARE(m_wm->windowVisible(id), true);
}

void WindowsDialogTest::typeComboConvertsKind()
{
    const QString id = m_wm->addWindow(WindowManager::Kind::Standalone, "K");
    TestWindowsDialog dialog(WindowsDialog::Mode::Editor, nullptr);
    dialog.show();
    qApp->processEvents();

    QComboBox* combo = qobject_cast<QComboBox*>(table(dialog)->cellWidget(0, 1));
    QVERIFY(combo != nullptr);
    combo->setCurrentIndex(1);

    QCOMPARE(m_wm->windowKind(id), WindowManager::Kind::Docked);
    // The live repopulation rebuilds the combo on the new kind.
    combo = qobject_cast<QComboBox*>(table(dialog)->cellWidget(0, 1));
    QVERIFY(combo != nullptr);
    QCOMPARE(combo->currentIndex(), 1);
}

void WindowsDialogTest::toggleButtonFlipsVisibility()
{
    const QString id = m_wm->addWindow(WindowManager::Kind::Standalone, "T");
    TestWindowsDialog dialog(WindowsDialog::Mode::Editor, nullptr);
    dialog.show();
    qApp->processEvents();

    button(dialog, "toggleButton")->click();
    QCOMPARE(m_wm->windowVisible(id), false);
    button(dialog, "toggleButton")->click();
    QCOMPARE(m_wm->windowVisible(id), true);
}

void WindowsDialogTest::tableFollowsExternalChanges()
{
    const QString id = m_wm->addWindow(WindowManager::Kind::Standalone, "L");
    TestWindowsDialog dialog(WindowsDialog::Mode::Editor, nullptr);
    dialog.show();
    qApp->processEvents();
    QCOMPARE(table(dialog)->rowCount(), 1);

    // A change from anywhere (the toggle widget's path) refreshes the
    // rows.
    m_wm->setWindowTitle(id, "External");
    qApp->processEvents();
    QCOMPARE(table(dialog)->item(0, 0)->text(), QString("External"));

    m_wm->removeWindow(id);
    qApp->processEvents();
    QCOMPARE(table(dialog)->rowCount(), 0);
}

void WindowsDialogTest::pickerSelectsPreselectedWindow()
{
    m_wm->addWindow(WindowManager::Kind::Standalone, "A");
    const QString b = m_wm->addWindow(WindowManager::Kind::Standalone, "B");

    TestWindowsDialog dialog(WindowsDialog::Mode::Picker, nullptr, b);
    dialog.show();
    qApp->processEvents();

    button(dialog, "okButton")->click();

    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
    QCOMPARE(dialog.selectedId(), b);
}

void WindowsDialogTest::pickerDoubleclickAccepts()
{
    m_wm->addWindow(WindowManager::Kind::Standalone, "A");
    const QString b = m_wm->addWindow(WindowManager::Kind::Standalone, "B");

    TestWindowsDialog dialog(WindowsDialog::Mode::Picker, nullptr);
    dialog.show();
    qApp->processEvents();

    QTableWidgetItem* item = table(dialog)->item(1, 0);
    std::array<void*, 2> args{ nullptr, static_cast<void*>(&item) };
    QMetaObject::activate(table(dialog),
                          table(dialog)->metaObject()->indexOfSignal(
                              "itemDoubleClicked(QTableWidgetItem*)"),
                          args.data());

    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
    QCOMPARE(dialog.selectedId(), b);
}

void WindowsDialogTest::pickerEmptyHasNoSelection()
{
    TestWindowsDialog dialog(WindowsDialog::Mode::Picker, nullptr);
    dialog.show();
    qApp->processEvents();

    button(dialog, "okButton")->click();

    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
    QCOMPARE(dialog.selectedId(), QString());
}

void WindowsDialogTest::rejectSavesTheColumnWidths()
{
    // The button box closes through reject/accept, which hide the
    // dialog without a closeEvent — the table's column widths are
    // saved on the hide and a fresh instance restores them.
    // Column 0 is a Stretch section (its width is the layout's, never
    // the user's): the Interactive column 1 is what persists.
    TestWindowsDialog dialog(WindowsDialog::Mode::Editor, nullptr);
    dialog.show();
    qApp->processEvents();
    table(dialog)->horizontalHeader()->resizeSection(1, 321);
    dialog.reject();

    TestWindowsDialog second(WindowsDialog::Mode::Editor, nullptr);
    second.show();
    qApp->processEvents();
    QCOMPARE(table(second)->horizontalHeader()->sectionSize(1), 321);
}
}

QTEST_MAIN(WindowsDialogTest)
#include "windowsdialog_test.moc"
