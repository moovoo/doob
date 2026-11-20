#include <QtTest/QtTest>

#include <QApplication>

#include "splitterwidget.h"
#include "widgetregistry.h"

namespace {

class WidgetRegistryTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void registrationCarriesTheTag();
    void registrationFallsBackToOther();
    void emptyTagFallsBackToOther();
    void optionsCarryTheTagsInRegistrationOrder();
    void unregisterDropsTheEntry();

private:
    // The creator's stand-in: a real built-in widget under a test id
    // (the registry tests never create the registered type through
    // createWidget — the type is the point under test, not the
    // instance).
    static WidgetRegistry::Creator creator()
    {
        return [](WidgetConfiguration &) { return new SplitterWidget; };
    }
};

void WidgetRegistryTest::init()
{
    WidgetRegistry::getInstance()->clear();
}

void WidgetRegistryTest::cleanup()
{
    WidgetRegistry::getInstance()->clear();
}

void WidgetRegistryTest::registrationCarriesTheTag()
{
    WidgetRegistry *registry = WidgetRegistry::getInstance();
    QVERIFY(registry->registerWidget("regtest-a", "A", creator(),
                                     widgetTags::playback));

    const QList<WidgetRegistry::WidgetOption> options = registry->options();
    QCOMPARE(options.size(), 1);
    QCOMPARE(options.first().id, QStringLiteral("regtest-a"));
    QCOMPARE(options.first().title, QStringLiteral("A"));
    QCOMPARE(options.first().tag, widgetTags::playback);
}

void WidgetRegistryTest::registrationFallsBackToOther()
{
    // A registration without a tag (the pre-tag plugin API shape)
    // falls back to "other".
    WidgetRegistry *registry = WidgetRegistry::getInstance();
    QVERIFY(registry->registerWidget("regtest-b", "B", creator()));

    const QList<WidgetRegistry::WidgetOption> options = registry->options();
    QCOMPARE(options.size(), 1);
    QCOMPARE(options.first().tag, widgetTags::other);
}

void WidgetRegistryTest::emptyTagFallsBackToOther()
{
    WidgetRegistry *registry = WidgetRegistry::getInstance();
    QVERIFY(registry->registerWidget("regtest-c", "C", creator(),
                                     QString()));

    const QList<WidgetRegistry::WidgetOption> options = registry->options();
    QCOMPARE(options.first().tag, widgetTags::other);
}

void WidgetRegistryTest::optionsCarryTheTagsInRegistrationOrder()
{
    WidgetRegistry *registry = WidgetRegistry::getInstance();
    QVERIFY(registry->registerWidget("regtest-1", "One", creator(),
                                     widgetTags::visualization));
    QVERIFY(registry->registerWidget("regtest-2", "Two", creator(),
                                     widgetTags::layout));
    // A plugin's own group (no core constant names it) rides through
    // as-is.
    QVERIFY(registry->registerWidget("regtest-3", "Three", creator(),
                                     QStringLiteral("streaming")));

    const QList<WidgetRegistry::WidgetOption> options = registry->options();
    QCOMPARE(options.size(), 3);
    QCOMPARE(options.at(0).id, QStringLiteral("regtest-1"));
    QCOMPARE(options.at(0).tag, widgetTags::visualization);
    QCOMPARE(options.at(1).id, QStringLiteral("regtest-2"));
    QCOMPARE(options.at(1).tag, widgetTags::layout);
    QCOMPARE(options.at(2).id, QStringLiteral("regtest-3"));
    QCOMPARE(options.at(2).tag, QStringLiteral("streaming"));
}

void WidgetRegistryTest::unregisterDropsTheEntry()
{
    WidgetRegistry *registry = WidgetRegistry::getInstance();
    QVERIFY(registry->registerWidget("regtest-4", "Four", creator(),
                                     widgetTags::other));
    registry->unregisterWidget("regtest-4");

    QVERIFY(registry->options().isEmpty());
}

}

QTEST_MAIN(WidgetRegistryTest)
#include "widgetregistry_test.moc"
