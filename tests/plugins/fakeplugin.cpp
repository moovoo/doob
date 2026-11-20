#include "fakeplugin.h"

#include <QLabel>

#include "dspregistry.h"
#include "librarymanager.h"
#include "mediaitemstore.h"
#include "playbackmanager.h"
#include "playlistmanager.h"
#include "playerregistry.h"
#include "settingsmanager.h"
#include "settingsregistry.h"
#include "sourcemanager.h"
#include "widgetregistry.h"
#include "widgetconfiguration.h"

FakeSourceProvider::FakeSourceProvider(QObject *parent)
    : SourceProvider(parent)
{
}

bool FakeSourceProvider::accepts(const QUrl &url) const
{
    return url.scheme() == QLatin1String("fake");
}

void FakeSourceProvider::fetchMetadata(const QUrl &url, MetadataCallback cb)
{
    TrackMeta meta;
    meta.title = QStringLiteral("Fake %1").arg(url.path());
    cb(meta);
}

QUrl FakeSourceProvider::playableUrl(const MediaItem &item) const
{
    return {QStringLiteral("fake://stream%1").arg(item.source.path())};
}

FakePluginWidget::FakePluginWidget(QWidget *parent)
    : BaseWidget(parent)
{
    auto *label = new QLabel(QStringLiteral("Fake plugin widget"), this);
    label->setObjectName("fakeLabel");
    layout()->addWidget(label);
}

FakePlayer::FakePlayer(QObject *parent)
    : AudioPlayer(parent)
{
}

void FakePlayer::play(const MediaItem &item)
{
    Q_UNUSED(item);
}

void FakePlayer::pause()
{
}

void FakePlayer::resume()
{
}

void FakePlayer::stop()
{
}

qint64 FakePlayer::positionMs() const
{
    return 0;
}

FakePlugin::FakePlugin(QObject *parent)
    : DoobPlugin(parent)
{
}

void FakePlugin::initialize(DoobApi *api)
{
    if (api->version != DoobApi::ApiVersion)
        return; // an api this plugin does not understand: stay a no-op

    m_api = api;

    m_provider = new FakeSourceProvider(this);
    api->sources->registerProvider(m_provider);

    api->widgets->registerWidget(FakePluginWidget::id, FakePluginWidget::title,
                                 [](WidgetConfiguration &) { return new FakePluginWidget; });

    api->players->registerPlayer(QStringLiteral("fakeplayer"), QStringLiteral("Fake Player"),
                                 [](QObject *parent) { return new FakePlayer(parent); });

    // The plugin's DSP (a pass-through): registered in the core's
    // registry, parented to this plugin (it dies with the unload; a
    // running chain skips the dead pointer).
    m_processor = new FakeProcessor(this);
    api->dpsp->registerDsp(m_processor, QStringLiteral("fakeplugin"));

    // No settings topics, so declare the owner: the preferences dialog
    // lists the plugin with a "no settings" note in place of a page.
    api->settings->registerOwner(QStringLiteral("fakeplugin"));

    // The api record is a test seam: the pluginmanager test (which must
    // not link the plugin) reads it back from the central settings — as
    // identity checks, since the plugin is the api's observer. It is the
    // value of a registered setting (an object), so the round trip also
    // exercises the plugin's settings path.
    api->settingsManager->registerSetting(
        { QStringLiteral("fakeplugin.api"), QStringLiteral("Api record"),
          QStringLiteral("What the test plugin's initialize() received."),
          QVariantMap(), QStringLiteral("fakeplugin"), });
    QVariantMap record;
    record["loaded"] = true;
    record["version"] = api->version;
    record["itemsIsSingleton"] = api->items == MediaItemStore::getInstance();
    record["libraryIsSingleton"] = api->library == LibraryManager::getInstance();
    record["playlistsIsSingleton"] = api->playlists == PlaylistManager::getInstance();
    record["playbackIsSingleton"] = api->playback == PlaybackManager::getInstance();
    record["sourcesIsSingleton"] = api->sources == SourceManager::getInstance();
    record["playersIsSingleton"] = api->players == PlayerRegistry::getInstance();
    record["widgetsIsSingleton"] = api->widgets == WidgetRegistry::getInstance();
    record["settingsIsSingleton"] = api->settings == SettingsRegistry::getInstance();
    record["settingsManagerIsSingleton"] = api->settingsManager == SettingsManager::getInstance();
    record["dpspIsSingleton"] = api->dpsp == DspRegistry::getInstance();
    // No main window until LayoutManager::show().
    record["windowIsNull"] = api->window() == nullptr;
    api->settingsManager->setValue(QStringLiteral("fakeplugin.api"), record);
}

void FakePlugin::shutdown()
{
    // Undo initialize(): the unload that follows deletes this plugin and
    // whatever it parented, so the core-side registrations must go first.
    if (m_api == nullptr)
        return; // initialize() bailed (an api it does not understand)
    m_api->sources->unregisterProvider(m_provider);
    m_provider = nullptr;
    m_api->widgets->unregisterWidget(FakePluginWidget::id);
    m_api->players->unregisterPlayer(QStringLiteral("fakeplayer"));
    m_api->dpsp->unregisterDsp(QStringLiteral("fakeplugin.dsp"));
    m_api->settingsManager->unregisterSetting(QStringLiteral("fakeplugin.api"));
    m_api->settings->unregisterOwner(QStringLiteral("fakeplugin"));
    m_api = nullptr;
}

#include "fakeplugin.moc"
