#include "spotify.h"

#include "spotifyauth.h"
#include "spotifyplayer.h"
#include "spotifyprovider.h"
#include "spotifysettingspage.h"

#include <doob/playerregistry.h>
#include <doob/settingsmanager.h>
#include <doob/settingsregistry.h>
#include <doob/sourcemanager.h>

SpotifyPlugin::SpotifyPlugin(QObject *parent) : DoobPlugin(parent)
{
}

void SpotifyPlugin::initialize(DoobApi *api)
{
    // An api this plugin does not understand: stay a no-op (shutdown()
    // guards the resulting null api).
    if (api->version != DoobApi::ApiVersion)
        return;

    m_api = api;
    m_dataDir = api->pluginDataPath(QStringLiteral("spotify"));
    m_auth = new SpotifyAuth(this);
    // The token file lives in the plugin's data directory
    // (<data dir>/plugins/spotify/spotify-token.json).
    m_auth->setTokenPath(m_dataDir);
    // The playback driver (plugin-owned — the PlaybackManager never
    // deletes a provider's own player): the provider hands it to the
    // manager for spotify items, and it starts its device lazily on
    // the first play.
    m_player = new SpotifyPlayer(m_auth, m_dataDir, this);
    m_provider = new SpotifyProvider(m_auth, m_player, this);
    api->sources->registerProvider(m_provider);
    // The driver in the picker too ("spotify", not the preferred
    // default — local files and plain streams keep their own backends):
    // a user selection creates a fresh instance through the creator
    // (parented to the PlaybackManager, which owns and deletes it). The
    // instances share the sign-in state; the driver's QPointer keeps a
    // manager-owned one alive-safe across this plugin's unload.
    const QString dataDir = m_dataDir;
    api->players->registerPlayer(
        QStringLiteral("spotify"), QStringLiteral("Spotify"),
        [auth = m_auth, dataDir](QObject *parent) {
            return new SpotifyPlayer(auth, dataDir, parent);
        });

    // The plugin's settings (the central settings.json; the keys are
    // prefixed with the plugin's id — a convention the SettingsManager
    // does not enforce). The stored values survive the unregistration in
    // shutdown(), so a re-added plugin finds them intact. The client id
    // is a public identifier (the PKCE security comes from the
    // challenge): the default is the build-time id, a user's own app
    // overrides it.
    api->settingsManager->registerSetting(
        { QString::fromLatin1(kSpotifyClientIdKey),
          QStringLiteral("Client ID"),
          QStringLiteral("The Spotify app's client id (a public "
                         "identifier; your app's from the Spotify "
                         "Developer Dashboard). Changing it signs the "
                         "account out."),
          QString::fromLatin1(SPOTIFY_CLIENT_ID_DEFAULT),
          m_provider->id(), });
    api->settingsManager->registerSetting(
        { QString::fromLatin1(kSpotifyRedirectPortKey),
          QStringLiteral("Loopback port"),
          QStringLiteral("The port the sign-in callback listens on — "
                         "the redirect URI http://127.0.0.1:<port>"
                         "/callback must match the app's registered "
                         "redirect URI."),
          kDefaultSpotifyRedirectPort,
          m_provider->id(), });

    // The plugin's settings topic (the preferences dialog's "Plugins"
    // group): the sign-in state and controls, the client id / port.
    api->settings->registerTopic(
        m_provider->id(), m_provider->title(), m_provider->id(),
        [provider = m_provider] {
            return new SpotifySettingsPage(provider);
        });
}

void SpotifyPlugin::shutdown()
{
    if (m_api == nullptr)
        return;
    m_api->sources->unregisterProvider(m_provider);
    m_api->players->unregisterPlayer(QStringLiteral("spotify"));
    m_api->settingsManager->unregisterSetting(
        QString::fromLatin1(kSpotifyClientIdKey));
    m_api->settingsManager->unregisterSetting(
        QString::fromLatin1(kSpotifyRedirectPortKey));
    m_api->settings->unregisterTopic(m_provider->id());
    m_provider = nullptr;
    m_auth = nullptr;
    m_api = nullptr;
}
