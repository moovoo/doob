#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "basewidget.h"
#include "dspregistry.h"
#include "playerregistry.h"
#include "pluginmanager.h"
#include "settings.h"
#include "settingsmanager.h"
#include "sourcemanager.h"
#include "widgetregistry.h"

namespace {

class PluginManagerTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();

    void pluginRegistersItsComponents();
    void initializeReceivesTheApi();
    void failingPluginIsSkipped();
    void shutdownUnregistersEverything();
    void reloadAfterShutdownWorks();
    void missingDirsLoadNothing();
    void envVarDirIsUsed();

private:
    // The dev build drops the plugins/ .so files in <app dir>/plugins
    // (one of the search paths) and the test executable sits in the same
    // build tree, so every loadPlugins() finds them: the expectations
    // count them from the filesystem (0 for a DOOB_BUILD_PLUGINS=OFF build).
    qsizetype appDirPluginCount() const
    {
        // The plugins' .so files only (a plugin may ship non-plugin
        // executables in the same directory, e.g. the spotify plugin's
        // spotify-web helper).
        return QDir(QCoreApplication::applicationDirPath() + "/plugins")
            .entryList({QStringLiteral("*.so")}, QDir::Files).size();
    }

    // The data dir the plugin's settings file lands in (<dir>/plugins/).
    QTemporaryDir m_dataDir{QDir::tempPath() + "/doob_pluginmanager_test"};
};

void PluginManagerTest::initTestCase()
{
    qputenv("DOOB_DATA_DIR", m_dataDir.path().toUtf8());
    Settings::reset();
    // The SettingsManager's store, private to this test's data dir.
    SettingsManager* settings = SettingsManager::getInstance();
    settings->setSettingsPath(m_dataDir.path() + "/settings.json");
    settings->load();
}

void PluginManagerTest::cleanupTestCase()
{
    qunsetenv("DOOB_DATA_DIR");
    Settings::reset();
    SettingsManager::reset();
}

void PluginManagerTest::init()
{
    // A clean plugin state before every test (the plugin's shutdown() has
    // already unregistered its components).
    PluginManager::getInstance()->shutdownPlugins();
}

void PluginManagerTest::pluginRegistersItsComponents()
{
    PluginManager *pm = PluginManager::getInstance();
    pm->loadFromDir(FAKE_PLUGIN_DIR);

    QCOMPARE(pm->count(), 1);

    // The provider is registered and claims fake: URLs.
    SourceManager *sources = SourceManager::getInstance();
    QVERIFY(sources->providerById("fakeplugin"));
    QCOMPARE(sources->providerFor(QUrl("fake://host/song"))->id(),
             QString("fakeplugin"));

    // The widget is creatable.
    WidgetConfiguration cfg;
    BaseWidget *w = WidgetRegistry::getInstance()->create("fakepluginwidget", cfg);
    QVERIFY(w);
    QCOMPARE(w->widgetId(), QString("fakepluginwidget"));
    QCOMPARE(w->widgetTitle(), QString("Fake Plugin Widget"));
    delete w;

    // The driver is registered and creates players.
    PlayerRegistry *players = PlayerRegistry::getInstance();
    QVERIFY(players->contains("fakeplayer"));
    AudioPlayer *player = players->create("fakeplayer");
    QVERIFY(player);
    delete player;

    // The DSP is registered in the core's chain.
    QVERIFY(DspRegistry::getInstance()->contains("fakeplugin.dsp"));
    QCOMPARE(DspRegistry::getInstance()->owner("fakeplugin.dsp"),
             QString("fakeplugin"));
}

void PluginManagerTest::initializeReceivesTheApi()
{
    PluginManager *pm = PluginManager::getInstance();
    pm->loadFromDir(FAKE_PLUGIN_DIR);
    QCOMPARE(pm->count(), 1);

    // The plugin recorded what initialize() received as the value of a
    // registered setting (a test seam — this test must not link against
    // the plugin, so it reaches the api through the record, not a cast).
    SettingsManager* settings = SettingsManager::getInstance();
    const QVariantMap api = settings->value("fakeplugin.api").toMap();
    QCOMPARE(api.value("loaded").toBool(), true);

    // The plugin (the api's observer) checked what it received against
    // the singletons and recorded the results.
    QCOMPARE(api.value("version").toInt(), DoobApi::ApiVersion);
    QCOMPARE(api.value("itemsIsSingleton").toBool(), true);
    QCOMPARE(api.value("libraryIsSingleton").toBool(), true);
    QCOMPARE(api.value("playlistsIsSingleton").toBool(), true);
    QCOMPARE(api.value("playbackIsSingleton").toBool(), true);
    QCOMPARE(api.value("sourcesIsSingleton").toBool(), true);
    QCOMPARE(api.value("playersIsSingleton").toBool(), true);
    QCOMPARE(api.value("widgetsIsSingleton").toBool(), true);
    QCOMPARE(api.value("settingsIsSingleton").toBool(), true);
    QCOMPARE(api.value("settingsManagerIsSingleton").toBool(), true);
    QCOMPARE(api.value("dpspIsSingleton").toBool(), true);
    // No main window until LayoutManager::show().
    QCOMPARE(api.value("windowIsNull").toBool(), true);
}

void PluginManagerTest::failingPluginIsSkipped()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // A shared library that is not a Qt plugin (no IID metadata).
    QVERIFY(QFile::copy(DOOB_LIB_PATH, dir.filePath("libnotaplugin.so")));

    PluginManager *pm = PluginManager::getInstance();
    pm->loadFromDir(dir.path());
    QCOMPARE(pm->count(), 0);
}

void PluginManagerTest::shutdownUnregistersEverything()
{
    PluginManager *pm = PluginManager::getInstance();
    pm->loadFromDir(FAKE_PLUGIN_DIR);
    QCOMPARE(pm->count(), 1);

    pm->shutdownPlugins();
    QCOMPARE(pm->count(), 0);

    // The plugin's shutdown() unregistered its components before the
    // unload deleted them.
    QCOMPARE(SourceManager::getInstance()->providerById("fakeplugin"), nullptr);
    WidgetConfiguration cfg;
    QCOMPARE(WidgetRegistry::getInstance()->create("fakepluginwidget", cfg), nullptr);
    QVERIFY(!PlayerRegistry::getInstance()->contains("fakeplayer"));
    QVERIFY(!DspRegistry::getInstance()->contains("fakeplugin.dsp"));

    // The setting's registration is gone, its stored value is not
    // (a re-loaded plugin would find it intact).
    SettingsManager* settings = SettingsManager::getInstance();
    QVERIFY(!settings->setting("fakeplugin.api").isValid());
    QVERIFY(settings->isModified("fakeplugin.api"));
}

void PluginManagerTest::reloadAfterShutdownWorks()
{
    PluginManager *pm = PluginManager::getInstance();
    pm->loadFromDir(FAKE_PLUGIN_DIR);
    QCOMPARE(pm->count(), 1);
    pm->shutdownPlugins();
    QCOMPARE(pm->count(), 0);

    // The unloaded .so loads again (a new instance, fresh registrations).
    pm->loadFromDir(FAKE_PLUGIN_DIR);
    QCOMPARE(pm->count(), 1);
    QVERIFY(SourceManager::getInstance()->providerById("fakeplugin"));
}

void PluginManagerTest::missingDirsLoadNothing()
{
    PluginManager *pm = PluginManager::getInstance();
    pm->loadPlugins();
    // Nothing beyond the dev-tree plugins in <app dir>/plugins.
    QCOMPARE(pm->count(), appDirPluginCount());
}

void PluginManagerTest::envVarDirIsUsed()
{
    qputenv("DOOB_PLUGIN_DIR", FAKE_PLUGIN_DIR);
    PluginManager *pm = PluginManager::getInstance();
    pm->loadPlugins();
    // The env-var plugin plus the dev-tree plugins in <app dir>/plugins.
    QCOMPARE(pm->count(), 1 + appDirPluginCount());
    qunsetenv("DOOB_PLUGIN_DIR");
}

} // namespace

QTEST_MAIN(PluginManagerTest)
#include "pluginmanager_test.moc"
