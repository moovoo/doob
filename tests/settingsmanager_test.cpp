#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "settingsmanager.h"

namespace {

class SettingsManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void registerIsRefusedForTakenAndEmptyKeys();
    void valueFallsBackToDefaultThenInvalid();
    void setValueWritesThroughAndEmits();
    void setValueEmitsOnlyOnChange();
    void setValueRejectsTypeMismatch();
    void unregisteredKeyAcceptsAnyType();
    void resetToDefaultDropsTheKey();
    void resetToDefaultIsANoOpWhenUnset();
    void storedValuesSurviveUnregistration();
    void roundTripCoercesContainerTypes();
    void missingFileLoadsEmpty();
    void corruptFileLoadsEmpty();
    void loadPicksUpExternalChanges();
    void memoryOnlyModeWorks();

private:
    SettingsManager* m_settings;
    QTemporaryDir m_dir;
};

void SettingsManagerTest::initTestCase()
{
}

void SettingsManagerTest::cleanupTestCase()
{
}

void SettingsManagerTest::init()
{
    // A fresh SettingsManager pointed at a fresh file before every test.
    m_dir.remove();
    m_dir = QTemporaryDir();
    QVERIFY(m_dir.isValid());
    m_settings = SettingsManager::getInstance();
    m_settings->setSettingsPath(m_dir.path() + "/settings.json");
    m_settings->load();
}

void SettingsManagerTest::cleanup()
{
    SettingsManager::reset();
}

void SettingsManagerTest::registerIsRefusedForTakenAndEmptyKeys()
{
    QVERIFY(m_settings->registerSetting(
        { "a.b", "T", "D", 1, QString() }));
    QVERIFY(!m_settings->registerSetting(
        { "a.b", "Other", "D", 2, QString() }));
    QVERIFY(!m_settings->registerSetting(
        { QString(), "Empty", "D", 1, QString() }));

    QCOMPARE(m_settings->settings().size(), 1);
    QCOMPARE(m_settings->setting("a.b").title, QString("T"));
    QVERIFY(!m_settings->setting("nope").isValid());
}

void SettingsManagerTest::valueFallsBackToDefaultThenInvalid()
{
    m_settings->registerSetting({ "a.b", "T", "D", 42, QString() });

    QCOMPARE(m_settings->value("a.b").toInt(), 42);
    QCOMPARE(m_settings->defaultValue("a.b").toInt(), 42);
    QVERIFY(!m_settings->value("unregistered").isValid());
    QVERIFY(!m_settings->defaultValue("unregistered").isValid());

    m_settings->setValue("a.b", 43);
    QCOMPARE(m_settings->value("a.b").toInt(), 43);
    QCOMPARE(m_settings->defaultValue("a.b").toInt(), 42);  // the default is untouched
}

void SettingsManagerTest::setValueWritesThroughAndEmits()
{
    m_settings->registerSetting({ "a.b", "T", "D", 1, QString() });

    QSignalSpy spy(m_settings, &SettingsManager::changed);
    m_settings->setValue("a.b", 7);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QString("a.b"));
    QCOMPARE(spy.first().at(1).toInt(), 7);

    // The value is in the file without an explicit save.
    QFile f(m_dir.path() + "/settings.json");
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    QCOMPARE(obj.value("a.b").toInt(), 7);
}

void SettingsManagerTest::setValueEmitsOnlyOnChange()
{
    m_settings->registerSetting({ "a.b", "T", "D", 1, QString() });

    QSignalSpy spy(m_settings, &SettingsManager::changed);
    m_settings->setValue("a.b", 1);  // unset -> set (the default, but stored)
    QCOMPARE(spy.count(), 1);
    m_settings->setValue("a.b", 1);  // same as the stored value
    QCOMPARE(spy.count(), 1);
    m_settings->setValue("a.b", 2);
    m_settings->setValue("a.b", 2);  // same as the stored value
    QCOMPARE(spy.count(), 2);
}

void SettingsManagerTest::setValueRejectsTypeMismatch()
{
    m_settings->registerSetting({ "a.b", "T", "D", 1, QString() });

    m_settings->setValue("a.b", "not a number");
    QCOMPARE(m_settings->value("a.b").toInt(), 1);  // the default, nothing stored
    QVERIFY(!m_settings->isModified("a.b"));
}

void SettingsManagerTest::unregisteredKeyAcceptsAnyType()
{
    m_settings->setValue("loose.key", "anything");
    m_settings->setValue("loose.other", 3.5);

    QCOMPARE(m_settings->value("loose.key").toString(), QString("anything"));
    QCOMPARE(m_settings->value("loose.other").toDouble(), 3.5);
    QCOMPARE(m_settings->storedKeys(),
             QStringList() << "loose.key" << "loose.other");
}

void SettingsManagerTest::resetToDefaultDropsTheKey()
{
    m_settings->registerSetting({ "a.b", "T", "D", 1, QString() });
    m_settings->setValue("a.b", 7);
    QVERIFY(m_settings->isModified("a.b"));

    QSignalSpy spy(m_settings, &SettingsManager::changed);
    m_settings->resetToDefault("a.b");

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(1).toInt(), 1);  // the default
    QCOMPARE(m_settings->value("a.b").toInt(), 1);
    QVERIFY(!m_settings->isModified("a.b"));

    // The key is gone from the file (the default applies to a fresh load).
    QFile f(m_dir.path() + "/settings.json");
    QVERIFY(f.open(QIODevice::ReadOnly));
    QVERIFY(!QJsonDocument::fromJson(f.readAll())
                .object()
                .contains("a.b"));
}

void SettingsManagerTest::resetToDefaultIsANoOpWhenUnset()
{
    m_settings->registerSetting({ "a.b", "T", "D", 1, QString() });

    QSignalSpy spy(m_settings, &SettingsManager::changed);
    m_settings->resetToDefault("a.b");

    QCOMPARE(spy.count(), 0);
}

void SettingsManagerTest::storedValuesSurviveUnregistration()
{
    m_settings->registerSetting({ "a.b", "T", "D", 1, QString() });
    m_settings->setValue("a.b", 7);

    m_settings->unregisterSetting("a.b");

    // The metadata is gone, the value is not (a re-added plugin finds it).
    QVERIFY(!m_settings->setting("a.b").isValid());
    QCOMPARE(m_settings->value("a.b").toInt(), 7);
    QVERIFY(m_settings->isModified("a.b"));
    QCOMPARE(m_settings->storedKeys(), QStringList() << "a.b");
}

void SettingsManagerTest::roundTripCoercesContainerTypes()
{
    m_settings->registerSetting(
        { "a.list", "T", "D", QStringList() << "x" << "y", QString() });
    m_settings->registerSetting(
        { "a.map", "T", "D", QVariantMap{ {"k", "v"} }, QString() });
    m_settings->setValue("a.list", QStringList() << "p" << "q");
    m_settings->setValue("a.map", QVariantMap{ {"k2", "v2"} });

    // A fresh SettingsManager reads the file back: the containers come back as
    // JSON-shaped QVariants and are coerced to the registered types.
    SettingsManager::reset();
    m_settings = SettingsManager::getInstance();
    m_settings->setSettingsPath(m_dir.path() + "/settings.json");
    m_settings->registerSetting(
        { "a.list", "T", "D", QStringList(), QString() });
    m_settings->registerSetting(
        { "a.map", "T", "D", QVariantMap(), QString() });
    m_settings->load();

    QCOMPARE(m_settings->value("a.list").userType(),
             QVariant(QStringList()).userType());
    QCOMPARE(m_settings->value("a.list").toStringList(),
             QStringList() << "p" << "q");
    QCOMPARE(m_settings->value("a.map").userType(),
             QVariant(QVariantMap()).userType());
    QCOMPARE(m_settings->value("a.map").toMap().value("k2").toString(),
             QString("v2"));
}

void SettingsManagerTest::missingFileLoadsEmpty()
{
    QVERIFY(!QFile::exists(m_dir.path() + "/settings.json"));
    m_settings->load();  // no warning, nothing to load
    QVERIFY(m_settings->storedKeys().isEmpty());
}

void SettingsManagerTest::corruptFileLoadsEmpty()
{
    {
        QFile f(m_dir.path() + "/settings.json");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("this is not json");
    }

    m_settings->load();
    QVERIFY(m_settings->storedKeys().isEmpty());
}

void SettingsManagerTest::loadPicksUpExternalChanges()
{
    m_settings->setValue("a.b", 1);
    {
        QFile f(m_dir.path() + "/settings.json");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"a.b": 9})");
    }

    m_settings->load();
    QCOMPARE(m_settings->value("a.b").toInt(), 9);
}

void SettingsManagerTest::memoryOnlyModeWorks()
{
    SettingsManager::reset();
    m_settings = SettingsManager::getInstance();
    // No paths set: values work, nothing is written.
    m_settings->registerSetting({ "a.b", "T", "D", 1, QString() });
    m_settings->setValue("a.b", 7);
    QCOMPARE(m_settings->value("a.b").toInt(), 7);
    QVERIFY(!QFile::exists(m_dir.path() + "/settings.json"));
}

} // namespace

QTEST_MAIN(SettingsManagerTest)
#include "settingsmanager_test.moc"
