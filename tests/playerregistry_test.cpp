#include <QtTest>

#include "fakeplayer.h"
#include "playerregistry.h"

namespace
{

class PlayerRegistryTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void anEmptyCreatorIsRefused();
    void aDuplicateIdIsRefused();
    void optionsListRegistrationOrder();
    void createAnswersNullForAnUnknownId();
    void unregisterDropsTheEntry();
    void thePreferredDefaultIsSetAndCleared();
    void anUnregisteredPreferredDefaultIsRefused();

private:
    PlayerRegistry *m_registry = nullptr;
};

void PlayerRegistryTest::init()
{
    m_registry = PlayerRegistry::getInstance();
    m_registry->clear();
}

void PlayerRegistryTest::cleanup()
{
    m_registry->clear();
}

void PlayerRegistryTest::anEmptyCreatorIsRefused()
{
    QVERIFY(!m_registry->registerPlayer("a", "A", nullptr));
    QVERIFY(!m_registry->contains("a"));
}

void PlayerRegistryTest::aDuplicateIdIsRefused()
{
    QVERIFY(m_registry->registerPlayer("a", "A",
        [](QObject *parent) { return new FakePlayer(parent); }));
    QVERIFY(!m_registry->registerPlayer("a", "A2",
        [](QObject *parent) { return new FakePlayer(parent); }));
    QCOMPARE(m_registry->options().size(), 1);
}

void PlayerRegistryTest::optionsListRegistrationOrder()
{
    QVERIFY(m_registry->registerPlayer("a", "A",
        [](QObject *parent) { return new FakePlayer(parent); }));
    QVERIFY(m_registry->registerPlayer("b", "B",
        [](QObject *parent) { return new FakePlayer(parent); }));

    const QList<QPair<QString, QString>> options = m_registry->options();
    QCOMPARE(options.size(), 2);
    QCOMPARE(options.at(0).first, QString("a"));
    QCOMPARE(options.at(0).second, QString("A"));
    QCOMPARE(options.at(1).first, QString("b"));
    QCOMPARE(options.at(1).second, QString("B"));
}

void PlayerRegistryTest::createAnswersNullForAnUnknownId()
{
    QVERIFY(m_registry->create("nope", this) == nullptr);
}

void PlayerRegistryTest::unregisterDropsTheEntry()
{
    QVERIFY(m_registry->registerPlayer("a", "A",
        [](QObject *parent) { return new FakePlayer(parent); }));
    m_registry->unregisterPlayer("a");  // unknown ids are a no-op
    QVERIFY(!m_registry->contains("a"));
    m_registry->unregisterPlayer("nope");
}

void PlayerRegistryTest::thePreferredDefaultIsSetAndCleared()
{
    QVERIFY(m_registry->preferredDefault().isEmpty());

    // An empty id clears (and never fails).
    QVERIFY(m_registry->setPreferredDefault(QString()));
    QVERIFY(m_registry->preferredDefault().isEmpty());

    QVERIFY(m_registry->registerPlayer("a", "A",
        [](QObject *parent) { return new FakePlayer(parent); }));
    QVERIFY(m_registry->setPreferredDefault("a"));
    QCOMPARE(m_registry->preferredDefault(), QString("a"));

    // Re-setting the same id is a no-op in effect.
    QVERIFY(m_registry->setPreferredDefault("a"));
    QCOMPARE(m_registry->preferredDefault(), QString("a"));

    m_registry->setPreferredDefault(QString());
    QVERIFY(m_registry->preferredDefault().isEmpty());
}

void PlayerRegistryTest::anUnregisteredPreferredDefaultIsRefused()
{
    QVERIFY(!m_registry->setPreferredDefault("nope"));
    QVERIFY(m_registry->preferredDefault().isEmpty());
}

} // namespace

QTEST_MAIN(PlayerRegistryTest)
#include "playerregistry_test.moc"
