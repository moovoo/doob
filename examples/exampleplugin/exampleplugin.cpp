#include "exampleplugin.h"

#include <doob/settingsregistry.h>
#include <doob/sourcemanager.h>

ExampleProvider::ExampleProvider(QObject *parent)
    : SourceProvider(parent)
{
}

// Claims every "example:" URL (example://album/track, ...).
bool ExampleProvider::accepts(const QUrl &url) const
{
    return url.scheme() == QLatin1String("example");
}

// Serves fixed metadata synchronously (the contract only requires the
// callback to land on the GUI thread).
void ExampleProvider::fetchMetadata(const QUrl &url, MetadataCallback cb)
{
    TrackMeta meta;
    meta.title = url.path();
    meta.artist = QStringLiteral("Example Artist");
    cb(meta);
}

ExamplePlugin::ExamplePlugin(QObject *parent)
    : DoobPlugin(parent)
{
}

void ExamplePlugin::initialize(DoobApi *api)
{
    // An api this plugin does not understand: stay a no-op (shutdown()
    // guards the resulting null api).
    if (api->version != DoobApi::ApiVersion)
        return;

    m_api = api;
    m_provider = new ExampleProvider(this);
    api->sources->registerProvider(m_provider);

    // No settings topics, so declare the owner: the preferences dialog
    // lists the plugin with a "no settings" note in place of a page.
    api->settings->registerOwner(m_provider->id(), m_provider->title());
}

void ExamplePlugin::shutdown()
{
    if (m_api == nullptr)
        return;
    m_api->sources->unregisterProvider(m_provider);
    m_api->settings->unregisterOwner(m_provider->id());
    m_provider = nullptr;
    m_api = nullptr;
}

#include "exampleplugin.moc"
