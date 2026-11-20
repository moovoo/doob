#include <QtTest/QtTest>
#include <QApplication>
#include <QTabWidget>
#include <QTabBar>
#include <QStackedWidget>
#include <QLineEdit>
#include <QDialog>
#include <QTimer>
#include <QJsonDocument>
#include <QAction>
#include <QMenu>

#include "layoutmanager.h"
#include "tabswidget.h"
#include "widgetcontainer.h"

namespace {

class TabsWidgetTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testDefaultTabs();
    void testLayoutGeometry();
    void testAddAndRemoveTab();
    void testDblClickRenamesTab();
    void testDblClickIgnoredOutsideEditMode();
    void testStoreRestoreRoundtrip();
    void testJsonRoundtrip();
    void testNestedInContainer();
    void testContextMenuTabOptions();
    void testContextMenuCarriesStyleAction();
    void testStoreRestoreTabOptions();
    void testMovableTabOrderStaysInSync();

private:
    static QAction* findAction(QMenu* menu, const QString& text);
    static QMenu* findSubmenu(QMenu* menu, const QString& text);
};

void TabsWidgetTest::initTestCase()
{
    LayoutManager::getInstance();
}

void TabsWidgetTest::testDefaultTabs()
{
    TabsWidget tabs;
    tabs.show();
    qApp->processEvents();
    QTabBar* bar = tabs.findChild<QTabBar*>();
    QStackedWidget* stack = tabs.findChild<QStackedWidget*>();
    QVERIFY(bar);
    QVERIFY(stack);
    QCOMPARE(bar->count(), 2);
    QCOMPARE(stack->count(), 2);

    QWidget* container = stack->currentWidget();
    QVERIFY(container);
    QVERIFY(container->isVisible());
}

void TabsWidgetTest::testLayoutGeometry()
{
    TabsWidget tabs;
    tabs.resize(800, 600);
    tabs.show();
    qApp->processEvents();

    QTabBar* bar = tabs.findChild<QTabBar*>();
    QStackedWidget* stack = tabs.findChild<QStackedWidget*>();

    QCOMPARE(bar->geometry().top(), 0);
    QCOMPARE(stack->geometry().top(), bar->geometry().y() + bar->geometry().height());
    // The tab widget is a framed QFrame, so the stack is inset by the
    // frame width on each side; assert it spans the full width within that.
    QVERIFY(stack->geometry().width() >= tabs.width() - 8);
    QVERIFY(stack->geometry().height() >= tabs.height() - bar->geometry().height() - 8);

    QWidget* container = stack->currentWidget();
    QVERIFY(container);
    QCOMPARE(container->geometry().size(), stack->geometry().size());
}

void TabsWidgetTest::testAddAndRemoveTab()
{
    TabsWidget tabs;
    QTabBar* bar = tabs.findChild<QTabBar*>();
    QStackedWidget* stack = tabs.findChild<QStackedWidget*>();

    QMetaObject::invokeMethod(&tabs, "onAddTab", Qt::DirectConnection, Q_ARG(bool, false));
    QCOMPARE(bar->count(), 3);
    QCOMPARE(stack->count(), 3);
    QCOMPARE(bar->tabText(2), QString("Tab 3"));

    bar->setCurrentIndex(2);
    QMetaObject::invokeMethod(&tabs, "onRemoveTab", Qt::DirectConnection, Q_ARG(bool, false));
    QCOMPARE(bar->count(), 2);
    QCOMPARE(stack->count(), 2);
    QVERIFY(bar->currentIndex() >= 0);
    QCOMPARE(stack->currentIndex(), bar->currentIndex());

    QMetaObject::invokeMethod(&tabs, "onRemoveTab", Qt::DirectConnection, Q_ARG(bool, false));
    QCOMPARE(bar->count(), 1);
    QMetaObject::invokeMethod(&tabs, "onRemoveTab", Qt::DirectConnection, Q_ARG(bool, false));
    QCOMPARE(bar->count(), 1);
}

void TabsWidgetTest::testDblClickRenamesTab()
{
    TabsWidget tabs;
    tabs.resize(400, 300);
    tabs.show();
    qApp->processEvents();
    LayoutManager::getInstance()->setEditMode(true);

    QTabBar* bar = tabs.findChild<QTabBar*>();

    bool dialogHandled = false;
    QTimer::singleShot(100, &tabs, [&dialogHandled] {
        QWidget* dlg = QApplication::activeModalWidget();
        if (!dlg)
            return;
        QLineEdit* edit = dlg->findChild<QLineEdit*>();
        QVERIFY(edit);
        edit->setText("Renamed");
        dialogHandled = true;
        qobject_cast<QDialog*>(dlg)->accept();
    });

    QTest::mouseDClick(bar, Qt::LeftButton, Qt::NoModifier, bar->tabRect(0).center());

    QVERIFY(dialogHandled);
    QCOMPARE(bar->tabText(0), QString("Renamed"));
}

void TabsWidgetTest::testDblClickIgnoredOutsideEditMode()
{
    TabsWidget tabs;
    tabs.resize(400, 300);
    tabs.show();
    qApp->processEvents();
    LayoutManager::getInstance()->setEditMode(false);

    QTabBar* bar = tabs.findChild<QTabBar*>();

    bool dialogAppeared = false;
    QTimer::singleShot(150, &tabs, [&dialogAppeared] {
        if (QWidget* dlg = QApplication::activeModalWidget()) {
            dialogAppeared = true;
            qobject_cast<QDialog*>(dlg)->reject();
        }
    });

    QTest::mouseDClick(bar, Qt::LeftButton, Qt::NoModifier, bar->tabRect(0).center());
    QTest::qWait(200);

    QVERIFY(!dialogAppeared);
    QCOMPARE(bar->tabText(0), QString("Tab 1"));
    LayoutManager::getInstance()->setEditMode(true);
}

void TabsWidgetTest::testStoreRestoreRoundtrip()
{
    TabsWidget tabs1;
    QMetaObject::invokeMethod(&tabs1, "onAddTab", Qt::DirectConnection, Q_ARG(bool, false));
    WidgetConfiguration cfg = tabs1.storeWidget();

    TabsWidget tabs2;
    tabs2.restoreWidget(cfg);
    WidgetConfiguration cfg2 = tabs2.storeWidget();

    QCOMPARE(cfg2.value("tabs").toList().size(), 3);
    QCOMPARE(cfg2, cfg);
}

void TabsWidgetTest::testJsonRoundtrip()
{
    TabsWidget tabs1;
    QMetaObject::invokeMethod(&tabs1, "onAddTab", Qt::DirectConnection, Q_ARG(bool, false));
    WidgetConfiguration cfg = tabs1.storeWidget();

    QByteArray json = QJsonDocument(widgetConfigurationToJson(cfg)).toJson();
    WidgetConfiguration cfgJson = widgetConfigurationFromJson(QJsonDocument::fromJson(json).object());

    TabsWidget tabs2;
    tabs2.restoreWidget(cfgJson);
    QCOMPARE(tabs2.storeWidget(), cfg);
}

void TabsWidgetTest::testNestedInContainer()
{
    WidgetContainer c1;
    c1.setWidget(new TabsWidget);
    WidgetConfiguration cfg = c1.storeContainer();
    QCOMPARE(cfg.value("type").toString(), QString("tabs"));

    WidgetContainer c2;
    c2.restoreContainer(cfg);
    QVERIFY(c2.getWidget());
    QCOMPARE(c2.getWidget()->widgetId(), QString("tabs"));
}

QAction* TabsWidgetTest::findAction(QMenu* menu, const QString& text)
{
    foreach (QAction* action, menu->actions()) {
        if (action->text() == text) {
            return action;
        }
    }
    return nullptr;
}

QMenu* TabsWidgetTest::findSubmenu(QMenu* menu, const QString& text)
{
    QAction* action = findAction(menu, text);
    return action != nullptr ? action->menu() : nullptr;
}

void TabsWidgetTest::testContextMenuTabOptions()
{
    TabsWidget tabs;
    QTabWidget* tabWidget = tabs.findChild<QTabWidget*>();
    QVERIFY(tabWidget);

    QCOMPARE(tabWidget->tabPosition(), QTabWidget::North);
    QVERIFY(!tabWidget->isMovable());

    QMenu* menu = tabs.widgetContextMenu();
    QVERIFY(menu);

    // The menu chains the base's entries: the style action opens the
    // style editor for this widget, above the tab entries.
    QVERIFY(findAction(menu, "Style this widget...") != nullptr);

    QMenu* position = findSubmenu(menu, "Tab position");
    QVERIFY(position);
    QAction* top = findAction(position, "Top");
    QAction* bottom = findAction(position, "Bottom");
    QAction* left = findAction(position, "Left");
    QAction* right = findAction(position, "Right");
    QVERIFY(top && bottom && left && right);

    QAction* movable = findAction(menu, "Movable tabs");
    QVERIFY(movable);
    if (top == nullptr || bottom == nullptr || left == nullptr || right == nullptr
            || movable == nullptr)
        QFAIL("tab-position actions missing");

    QVERIFY(top->isChecked());
    QVERIFY(!bottom->isChecked());
    QVERIFY(!left->isChecked());
    QVERIFY(!right->isChecked());
    QVERIFY(!movable->isChecked());

    bottom->trigger();
    QCOMPARE(tabWidget->tabPosition(), QTabWidget::South);
    left->trigger();
    QCOMPARE(tabWidget->tabPosition(), QTabWidget::West);
    right->trigger();
    QCOMPARE(tabWidget->tabPosition(), QTabWidget::East);
    movable->trigger();
    QVERIFY(tabWidget->isMovable());

    // A freshly opened menu reflects the current state.
    QMenu* menu2 = tabs.widgetContextMenu();
    QVERIFY(menu2);
    QMenu* position2 = findSubmenu(menu2, "Tab position");
    QVERIFY(position2);
    QVERIFY(!findAction(position2, "Top")->isChecked());
    QVERIFY(!findAction(position2, "Bottom")->isChecked());
    QVERIFY(!findAction(position2, "Left")->isChecked());
    QVERIFY(findAction(position2, "Right")->isChecked());
    QVERIFY(findAction(menu2, "Movable tabs")->isChecked());

    delete menu;
    delete menu2;
}

// The menu chains the base's entries: like every other widget, the tabs
// widget gets the "Style this widget..." action that gives the widget an
// instance name and opens the style sheet editor.
void TabsWidgetTest::testContextMenuCarriesStyleAction()
{
    TabsWidget tabs;
    tabs.show();
    qApp->processEvents();

    QMenu* menu = tabs.widgetContextMenu();
    QVERIFY(menu != nullptr);
    QVERIFY(findAction(menu, "Style this widget...") != nullptr);
    delete menu;
}

void TabsWidgetTest::testStoreRestoreTabOptions()
{
    TabsWidget tabs1;
    QTabWidget* tabWidget1 = tabs1.findChild<QTabWidget*>();
    QVERIFY(tabWidget1);

    // Defaults (top position, not movable) are not stored.
    WidgetConfiguration fresh = tabs1.storeWidget();
    QVERIFY(!fresh.contains("tabPosition"));
    QVERIFY(!fresh.contains("movableTabs"));

    QMenu* menu = tabs1.widgetContextMenu();
    findAction(findSubmenu(menu, "Tab position"), "Bottom")->trigger();
    delete menu;
    QMetaObject::invokeMethod(&tabs1, "onToggleMovable", Qt::DirectConnection, Q_ARG(bool, true));

    WidgetConfiguration cfg = tabs1.storeWidget();
    QCOMPARE(cfg.value("tabPosition").toString(), QString("bottom"));
    QVERIFY(cfg.value("movableTabs").toBool());

    TabsWidget tabs2;
    tabs2.restoreWidget(cfg);
    QTabWidget* tabWidget2 = tabs2.findChild<QTabWidget*>();
    QCOMPARE(tabWidget2->tabPosition(), QTabWidget::South);
    QVERIFY(tabWidget2->isMovable());
    QCOMPARE(tabs2.storeWidget(), cfg);

    // Left and right round-trip too.
    QMenu* menuLeft = tabs1.widgetContextMenu();
    findAction(findSubmenu(menuLeft, "Tab position"), "Left")->trigger();
    delete menuLeft;
    QCOMPARE(tabs1.storeWidget().value("tabPosition").toString(), QString("left"));

    QMenu* menuRight = tabs1.widgetContextMenu();
    findAction(findSubmenu(menuRight, "Tab position"), "Right")->trigger();
    delete menuRight;
    WidgetConfiguration cfgRight = tabs1.storeWidget();
    QCOMPARE(cfgRight.value("tabPosition").toString(), QString("right"));

    TabsWidget tabs4;
    tabs4.restoreWidget(cfgRight);
    QCOMPARE(tabs4.findChild<QTabWidget*>()->tabPosition(), QTabWidget::East);

    // A layout without the keys keeps the defaults.
    TabsWidget tabs3;
    tabs3.restoreWidget(fresh);
    QTabWidget* tabWidget3 = tabs3.findChild<QTabWidget*>();
    QCOMPARE(tabWidget3->tabPosition(), QTabWidget::North);
    QVERIFY(!tabWidget3->isMovable());
}

void TabsWidgetTest::testMovableTabOrderStaysInSync()
{
    TabsWidget tabs;
    QTabWidget* tw = tabs.findChild<QTabWidget*>();
    QVERIFY(tw);

    // Distinct widgets per tab so a name/container mismatch is visible.
    WidgetConfiguration cfg;
    WidgetContainer* page0 = qobject_cast<WidgetContainer*>(tw->widget(0));
    WidgetContainer* page1 = qobject_cast<WidgetContainer*>(tw->widget(1));
    QVERIFY(page0 && page1);
    page0->setWidget(LayoutManager::getInstance()->createWidget("currenttrack", cfg));
    page1->setWidget(LayoutManager::getInstance()->createWidget("position", cfg));

    // Move a tab (what a movable drag does) and save.
    tw->tabBar()->moveTab(1, 0);
    WidgetConfiguration stored = tabs.storeWidget();

    TabsWidget tabs2;
    tabs2.restoreWidget(stored);
    QTabWidget* tw2 = tabs2.findChild<QTabWidget*>();
    QCOMPARE(tw2->tabText(0), QString("Tab 2"));
    QCOMPARE(tw2->tabText(1), QString("Tab 1"));
    WidgetContainer* restored0 = qobject_cast<WidgetContainer*>(tw2->widget(0));
    WidgetContainer* restored1 = qobject_cast<WidgetContainer*>(tw2->widget(1));
    // The container saved for a tab belongs to the page that tab shows:
    // "Tab 2" carried the position slider, "Tab 1" the current track.
    QCOMPARE(restored0->getWidget()->widgetId(), QString("position"));
    QCOMPARE(restored1->getWidget()->widgetId(), QString("currenttrack"));
}
}


QTEST_MAIN(TabsWidgetTest)
#include "tabswidget_test.moc"
