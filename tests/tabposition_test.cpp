#include <QtTest>

#include "tabposition.h"

namespace {

class TabPositionTest : public QObject
{
    Q_OBJECT

private slots:
    void nameRoundTrips();
    void menuChecksCurrentAndAppliesOnChange();
};

void TabPositionTest::nameRoundTrips()
{
    const QList<QTabWidget::TabPosition> positions = {
        QTabWidget::North,
        QTabWidget::South,
        QTabWidget::West,
        QTabWidget::East,
    };
    for (QTabWidget::TabPosition p : positions)
        QCOMPARE(TabPosition::fromName(TabPosition::name(p)), p);

    // The default is "top" and an unknown name maps to the default.
    QCOMPARE(TabPosition::name(QTabWidget::North), QString("top"));
    QCOMPARE(TabPosition::fromName(""), QTabWidget::North);
    QCOMPARE(TabPosition::fromName("nope"), QTabWidget::North);
}

void TabPositionTest::menuChecksCurrentAndAppliesOnChange()
{
    QTabWidget tabs;
    tabs.setTabPosition(QTabWidget::South);

    QMenu parent;
    QList<QTabWidget::TabPosition> applied;
    QMenu* menu = TabPosition::positionMenu(
        &parent, &tabs, [&applied, &tabs](QTabWidget::TabPosition p) {
            applied.append(p);
            tabs.setTabPosition(p);
        });
    QVERIFY(menu != nullptr);

    const QList<QAction*> actions = menu->actions();
    QCOMPARE(actions.size(), 4);
    // The current position's action is the only checked one.
    QCOMPARE(actions.at(0)->isChecked(), false);  // Top
    QCOMPARE(actions.at(1)->isChecked(), true);   // Bottom (current)
    QCOMPARE(actions.at(2)->isChecked(), false);  // Left
    QCOMPARE(actions.at(3)->isChecked(), false);  // Right

    actions.at(2)->trigger();
    QCOMPARE(applied, QList<QTabWidget::TabPosition>({ QTabWidget::West }));
    QCOMPARE(tabs.tabPosition(), QTabWidget::West);

    // The options are mutually exclusive (the exclusive action group,
    // drawn as radio buttons): the choice checks Left and releases the
    // option that was checked before.
    QCOMPARE(actions.at(2)->isChecked(), true);
    QCOMPARE(actions.at(1)->isChecked(), false);
}

}  // namespace

QTEST_MAIN(TabPositionTest)
#include "tabposition_test.moc"
