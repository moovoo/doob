#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include <doob/eq.h>

// The core headers go through the <doob/...> prefix (the doob_lib
// target's include layout), so the test builds both in-tree and
// standalone against the installed API.
#include <doob/audioplayer.h>
#include <doob/dspregistry.h>
#include <doob/playerregistry.h>
#include <doob/pluginmanager.h>
#include <doob/settings.h>
#include <doob/settingsmanager.h>

namespace {

class FFmpegPluginTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();

    void driverIsRegisteredAndCreatesPlayers();
    void shutdownUnregistersTheDriver();
    void eqDspAndSettingsAreRegisteredAndUnregistered();

private:
    QTemporaryDir m_dataDir{QDir::tempPath() + "/doob_ffmpegplugin_test"};
};

void FFmpegPluginTest::initTestCase()
{
    qputenv("DOOB_DATA_DIR", m_dataDir.path().toUtf8());
    Settings::reset();
    // The SettingsManager's store, private to this test's data dir.
    SettingsManager *settings = SettingsManager::getInstance();
    settings->setSettingsPath(m_dataDir.path() + "/settings.json");
    settings->load();
}

void FFmpegPluginTest::cleanupTestCase()
{
    qunsetenv("DOOB_DATA_DIR");
    Settings::reset();
    SettingsManager::reset();
}

void FFmpegPluginTest::init()
{
    // A clean plugin state before every test (the plugin's shutdown()
    // has already unregistered its components).
    PluginManager::getInstance()->shutdownPlugins();
}

void FFmpegPluginTest::driverIsRegisteredAndCreatesPlayers()
{
    PluginManager *pm = PluginManager::getInstance();
    pm->loadFromDir(FFMPEG_PLUGIN_DIR);

    PlayerRegistry *players = PlayerRegistry::getInstance();
    QVERIFY(players->contains("ffmpeg"));
    // The plugin advertises its driver as the preferred default (the
    // driver used when the user has made no explicit selection).
    QCOMPARE(players->preferredDefault(), QStringLiteral("ffmpeg"));
    // The created player is parented to the test (the test owns it).
    AudioPlayer *player = players->create("ffmpeg", this);
    QVERIFY(player);
    QCOMPARE(player->positionMs(), qint64(0));
    QCOMPARE(player->durationMs(), qint64(0));
    delete player;
}

void FFmpegPluginTest::shutdownUnregistersTheDriver()
{
    PluginManager *pm = PluginManager::getInstance();
    pm->loadFromDir(FFMPEG_PLUGIN_DIR);
    QVERIFY(PlayerRegistry::getInstance()->contains("ffmpeg"));

    pm->shutdownPlugins();
    QVERIFY(!PlayerRegistry::getInstance()->contains("ffmpeg"));
    // The preferred default is cleared with the registration.
    QVERIFY(PlayerRegistry::getInstance()->preferredDefault().isEmpty());
}

// The equalizer rides the plugin's lifecycle: its DSP entry in the
// core's registry and its gains setting are registered in
// initialize() and unregistered in shutdown() — but the stored values
// survive, so a re-added plugin finds them intact.
void FFmpegPluginTest::eqDspAndSettingsAreRegisteredAndUnregistered()
{
    SettingsManager *settings = SettingsManager::getInstance();
    DspRegistry *dpsp = DspRegistry::getInstance();
    PluginManager *pm = PluginManager::getInstance();
    pm->loadFromDir(FFMPEG_PLUGIN_DIR);

    // The DSP is registered (its switch is the registry's entry, not a
    // setting) and the gains are a plugin-owned setting.
    QVERIFY(dpsp->contains(QString::fromLatin1(eq::Id)));
    QCOMPARE(dpsp->owner(QString::fromLatin1(eq::Id)),
             QStringLiteral("ffmpeg"));
    QVERIFY(settings->setting(QString::fromLatin1(eq::GainsKey)).isValid());
    QCOMPARE(settings->setting(QString::fromLatin1(eq::GainsKey)).owner,
             QStringLiteral("ffmpeg"));
    QCOMPARE(settings->value(QString::fromLatin1(eq::GainsKey)).toList().size(),
             eq::BandCount);

    // A stored value survives the unregistration.
    QVariantList list =
        settings->value(QString::fromLatin1(eq::GainsKey)).toList();
    list[0] = 6.0;
    settings->setValue(QString::fromLatin1(eq::GainsKey), list);

    pm->shutdownPlugins();
    QVERIFY(!dpsp->contains(QString::fromLatin1(eq::Id)));
    QVERIFY(!settings->setting(QString::fromLatin1(eq::GainsKey)).isValid());
    const QVariantList stored =
        settings->value(QString::fromLatin1(eq::GainsKey)).toList();
    QCOMPARE(stored.size(), eq::BandCount);
    QCOMPARE(stored.at(0).toDouble(), 6.0);
}

} // namespace

QTEST_MAIN(FFmpegPluginTest)
#include "ffmpegplugin_test.moc"
