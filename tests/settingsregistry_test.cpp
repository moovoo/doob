#include <QtTest>

#include <QWidget>

#include "settingsregistry.h"

namespace {

class SettingsRegistryTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void registerAndLookUp();
    void duplicateIdIsRejected();
    void emptyCreatorIsRejected();
    void unregisterRemovesTheTopic();
    void topicsForOwnerFiltersByOwner();
    void ownersAreSortedAndUnique();
    void declaredOwnerWithNoTopicsIsListed();
    void ownerTitleFallsBackToTheId();
    void clearDropsTheRegistrations();

private:
    SettingsRegistry* m_registry = nullptr;
};

void SettingsRegistryTest::init()
{
    m_registry = SettingsRegistry::getInstance();
    m_registry->clear();
}

void SettingsRegistryTest::cleanup()
{
    m_registry->clear();
}

void SettingsRegistryTest::registerAndLookUp()
{
    QVERIFY(m_registry->registerTopic("one", "Topic One", QString(),
                                      []() { return new QWidget; }));
    QVERIFY(m_registry->registerTopic("two", "Topic Two", "pluginA",
                                      []() { return new QWidget; }));

    QVERIFY(m_registry->contains("one"));
    QVERIFY(m_registry->contains("two"));
    QVERIFY(!m_registry->contains("nope"));

    const QList<SettingsTopic> topics = m_registry->topics();
    QCOMPARE(topics.size(), 2);
    QCOMPARE(topics[0].id, QString("one"));
    QCOMPARE(topics[0].title, QString("Topic One"));
    QCOMPARE(topics[0].owner, QString());
    QVERIFY(topics[0].creator());
    QCOMPARE(topics[1].owner, QString("pluginA"));
}

void SettingsRegistryTest::duplicateIdIsRejected()
{
    QVERIFY(m_registry->registerTopic("one", "Topic One", QString(),
                                      []() { return new QWidget; }));

    QWidget* page = new QWidget;
    QVERIFY(!m_registry->registerTopic("one", "Twin", "pluginA",
                                       [page]() { return page; }));
    delete page;

    QCOMPARE(m_registry->topics().size(), 1);
    QCOMPARE(m_registry->topics()[0].title, QString("Topic One"));
}

void SettingsRegistryTest::emptyCreatorIsRejected()
{
    QVERIFY(!m_registry->registerTopic("one", "Topic One", QString(),
                                       SettingsRegistry::PageCreator()));
    QVERIFY(!m_registry->contains("one"));
}

void SettingsRegistryTest::unregisterRemovesTheTopic()
{
    QVERIFY(m_registry->registerTopic("one", "Topic One", QString(),
                                      []() { return new QWidget; }));
    m_registry->unregisterTopic("one");

    QVERIFY(!m_registry->contains("one"));
    QVERIFY(m_registry->topics().isEmpty());
}

void SettingsRegistryTest::topicsForOwnerFiltersByOwner()
{
    QVERIFY(m_registry->registerTopic("core", "Core", QString(),
                                      []() { return new QWidget; }));
    QVERIFY(m_registry->registerTopic("a1", "A One", "pluginA",
                                      []() { return new QWidget; }));
    QVERIFY(m_registry->registerTopic("b1", "B One", "pluginB",
                                      []() { return new QWidget; }));
    QVERIFY(m_registry->registerTopic("a2", "A Two", "pluginA",
                                      []() { return new QWidget; }));

    const QList<SettingsTopic> a = m_registry->topicsForOwner("pluginA");
    QCOMPARE(a.size(), 2);
    QCOMPARE(a[0].id, QString("a1"));
    QCOMPARE(a[1].id, QString("a2"));

    QVERIFY(m_registry->topicsForOwner("pluginC").isEmpty());
}

void SettingsRegistryTest::ownersAreSortedAndUnique()
{
    // The owners of the topics show up even without a declaration; the
    // explicit declaration of "pluginA" must not duplicate it.
    m_registry->registerOwner("pluginA");
    QVERIFY(m_registry->registerTopic("a1", "A One", "pluginA",
                                      []() { return new QWidget; }));
    QVERIFY(m_registry->registerTopic("b1", "B One", "pluginB",
                                      []() { return new QWidget; }));

    const QStringList expected{ "pluginA", "pluginB" };
    QCOMPARE(m_registry->owners(), expected);
}

void SettingsRegistryTest::declaredOwnerWithNoTopicsIsListed()
{
    m_registry->registerOwner("pluginA");

    QCOMPARE(m_registry->owners(), QStringList{ "pluginA" });
    QVERIFY(m_registry->topicsForOwner("pluginA").isEmpty());

    m_registry->unregisterOwner("pluginA");
    QVERIFY(m_registry->owners().isEmpty());
}

void SettingsRegistryTest::ownerTitleFallsBackToTheId()
{
    m_registry->registerOwner("pluginA", "Plugin A");
    m_registry->registerOwner("pluginB");

    QCOMPARE(m_registry->ownerTitle("pluginA"), QString("Plugin A"));
    QCOMPARE(m_registry->ownerTitle("pluginB"), QString("pluginB"));
    // An owner that only shows up through a topic has no declared title.
    QVERIFY(m_registry->registerTopic("a1", "A One", "pluginC",
                                      []() { return new QWidget; }));
    QCOMPARE(m_registry->ownerTitle("pluginC"), QString("pluginC"));
}

void SettingsRegistryTest::clearDropsTheRegistrations()
{
    m_registry->registerOwner("pluginA");
    QVERIFY(m_registry->registerTopic("one", "Topic One", "pluginA",
                                      []() { return new QWidget; }));

    m_registry->clear();

    QVERIFY(m_registry->topics().isEmpty());
    QVERIFY(m_registry->owners().isEmpty());
}

} // namespace

QTEST_MAIN(SettingsRegistryTest)
#include "settingsregistry_test.moc"
