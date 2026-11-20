#include <QtTest>
#include <QApplication>
#include <QTemporaryDir>

#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>

#include <memory>

#include "layoutmanager.h"
#include "mainwindow.h"
#include "menubutton.h"
#include "settingsmanager.h"
#include "testsupport.h"

namespace {

// The click's popup seam (a modal popup is untestable offscreen — the
// chooseFiles pattern): the tests count the calls.
class TestMenuButtonWidget : public MenuButtonWidget
{
public:
    using MenuButtonWidget::MenuButtonWidget;

    int m_showMenuCalls = 0;
    void showMenu() override { ++m_showMenuCalls; }
};

class MenuButtonTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void registeredInLayoutManager();
    void aClickPopsTheMainMenu();
    void theMainMenuCarriesFileAndViewAndTheToggle();

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    MainWindow* m_window = nullptr;
    TestMenuButtonWidget* m_widget = nullptr;
};

void MenuButtonTest::init()
{
    // The hint's setting and the F10 registration write through to the
    // SettingsManager: keep that store private, so the test does not
    // touch the user's settings.json.
    m_dir = std::make_unique<QTemporaryDir>();
    SettingsManager* settings = SettingsManager::getInstance();
    settings->setSettingsPath(m_dir->path() + "/settings.json");
    settings->load();

    m_window = new MainWindow();
    m_window->show();

    m_widget = new TestMenuButtonWidget();
    m_widget->resize(200, 40);
    m_widget->show();
    qApp->processEvents();
}

void MenuButtonTest::cleanup()
{
    delete m_widget;
    delete m_window;
    SettingsManager::reset();
}

void MenuButtonTest::registeredInLayoutManager()
{
    WidgetConfiguration cfg;
    BaseWidget* w = LayoutManager::getInstance()->createWidget(MenuButtonWidget::id, cfg);

    QVERIFY(w != nullptr);
    QCOMPARE(w->widgetId(), MenuButtonWidget::id);
    QCOMPARE(w->widgetTitle(), MenuButtonWidget::title);
    delete w;
}

void MenuButtonTest::aClickPopsTheMainMenu()
{
    QPushButton* button = m_widget->findChild<QPushButton*>("menuButton");
    QVERIFY(button != nullptr);

    QTest::mouseClick(button, Qt::LeftButton);

    QCOMPARE(m_widget->m_showMenuCalls, 1);
}

void MenuButtonTest::theMainMenuCarriesFileAndViewAndTheToggle()
{
    QMenu* menu = m_window->mainMenu();
    QVERIFY(menu != nullptr);

    bool hasFile = false;
    bool hasView = false;
    for (QAction* a : menu->actions()) {
        if (a->menu() == nullptr)
            continue;
        if (a->menu()->title() == "File")
            hasFile = true;
        if (a->menu()->title() == "View")
            hasView = true;
    }
    QVERIFY(hasFile);
    QVERIFY(hasView);

    // The checkable "Show menu bar" entry (the platform supports the
    // menu bar's hiding here) follows the live visibility — a re-access
    // of the menu re-syncs it (the event-filter's sync runs there).
    QAction* toggle = menu->findChild<QAction*>("mainMenuShowMenuBarAction");
    QVERIFY(toggle != nullptr);
    QVERIFY(toggle->isCheckable());
    QVERIFY(toggle->isChecked());

    m_window->setMenuBarVisible(false);
    toggle = m_window->mainMenu()->findChild<QAction*>("mainMenuShowMenuBarAction");
    QVERIFY(toggle != nullptr);
    QVERIFY(!toggle->isChecked());

    // A true toggle: triggering it flips the bar's visibility both
    // ways (the check follows along — the sync's setChecked on a
    // matching state is a no-op, so no loop).
    toggle->trigger();
    QVERIFY(!m_window->menuBar()->isHidden());
    QVERIFY(toggle->isChecked());
    toggle->trigger();
    QVERIFY(m_window->menuBar()->isHidden());
    QVERIFY(!toggle->isChecked());
}

}  // namespace

QTEST_MAIN(MenuButtonTest)
#include "menubutton_test.moc"
