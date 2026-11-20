#ifndef SPOTIFYPLUGIN_H
#define SPOTIFYPLUGIN_H

#include <doob/doobapi.h>
#include <doob/doobplugin.h>

class SpotifyAuth;
class SpotifyPlayer;
class SpotifyProvider;

// The plugin: registers the source provider, the playback driver, the
// settings (the client id, the loopback port) and the "Spotify"
// settings topic in initialize(), unregisters them in shutdown() (the
// host unloads this library right after, which deletes the provider,
// the driver and the auth — the registrations must be undone first).
class SpotifyPlugin : public DoobPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DOOB_PLUGIN_IID FILE "spotify.json")
public:
    explicit SpotifyPlugin(QObject *parent = nullptr);

    void initialize(DoobApi *api) override;
    void shutdown() override;

private:
    DoobApi* m_api = nullptr;
    SpotifyAuth* m_auth = nullptr;
    SpotifyPlayer* m_player = nullptr;
    SpotifyProvider* m_provider = nullptr;
    QString m_dataDir;
};

#endif // SPOTIFYPLUGIN_H
