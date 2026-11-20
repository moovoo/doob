#ifndef DOOBAPI_H
#define DOOBAPI_H

#include <QString>

class ArtRegistry;
class DspRegistry;
class LibraryManager;
class MediaItemStore;
class PlaybackManager;
class PlaylistManager;
class PlayerRegistry;
class QMainWindow;
class SettingsManager;
class SettingsRegistry;
class SourceManager;
class WidgetRegistry;

// The facade the host (PluginManager) hands to every plugin's
// initialize(): the singletons a plugin needs, the main window, and the
// plugin's data directory. The pointer is owned by the host and
// outlives the plugin; a plugin must not delete it.
//
// version gates the ABI: a plugin checks it in initialize() and bails
// (a no-op) when it is not one it understands. The current host provides
// ApiVersion; a future host that changes the struct bumps it and plugins
// can support both.
struct DoobApi
{
    static constexpr int ApiVersion = 4;
    int version = 0;

    MediaItemStore *items = nullptr;
    LibraryManager *library = nullptr;
    PlaylistManager *playlists = nullptr;
    PlaybackManager *playback = nullptr;
    SourceManager *sources = nullptr;
    PlayerRegistry *players = nullptr;
    WidgetRegistry *widgets = nullptr;
    // The settings topics the preferences dialog (File -> Preferences)
    // lists: a plugin registers its topics in initialize() and calls
    // registerOwner() when it has none (so it is still listed).
    SettingsRegistry *settings = nullptr;
    // The central SettingsManager (settings.json): a plugin registers its
    // settings in initialize() (the keys conventionally prefixed with the
    // plugin's id, "tubemusic.playback_mode") and unregisters them in
    // shutdown() — the stored values survive the unregistration.
    SettingsManager *settingsManager = nullptr;
    // The DSP chain (audioprocessor.h): a plugin registers its
    // processors in initialize() (they are parented to the plugin and
    // must be unregistered in shutdown()) and the user's enable/order
    // state shows up in the preferences' DSP topic.
    DspRegistry *dpsp = nullptr;
    // The enrichment sources (artsource.h): the core registers its
    // built-in art source; a plugin registers its own in initialize()
    // (parented to the plugin, unregistered in shutdown()) and the
    // core's art.fallback setting can name it by id.
    ArtRegistry *arts = nullptr;

    // The main window (nullptr before LayoutManager::show()).
    QMainWindow *window() const;
    // The plugin's data directory: <data dir>/plugins/<pluginName> (a
    // cookie snapshot, a cache — the files a plugin keeps that are not
    // settings; the settings live in the central settings.json).
    QString pluginDataPath(const QString &pluginName) const;
};

#endif // DOOBAPI_H
