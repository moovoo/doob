#ifndef SETTINGS_H
#define SETTINGS_H

#include <QString>

// Central configuration. Resolves the data directory (where the SQLite
// database, the settings files, the layouts and the styles live) with a
// simple precedence:
//   1. the DOOB_DATA_DIR environment variable, else
//   2. QStandardPaths::AppDataLocation (platform-correct default).
// Exposes the concrete file paths so no other component hard-codes
// them. The read-only presets (the shipped layouts and styles,
// installed by the package manager) are resolved the same way the
// plugin directories are: the DOOB_PRESETS_DIR environment variable,
// else <app dir>/share/doob (the dev build, next to the executable),
// else the doob directory of the standard data locations in order (the
// packaged install, /usr/share/doob) — the first candidate that exists
// ("" when none does). The data directory itself is never a preset
// source.
class Settings
{
public:
    static Settings* instance();
    static void reset(); // for tests: drop the instance so it re-resolves

    QString dataDir() const { return m_dataDir; }
    // The single SQLite database: the library, the playlists and the
    // standalone items are components of one file (each with its own
    // tables and schema version).
    QString dbPath() const { return m_dataDir + "/doob.db"; }
    // The central settings file: the user settings of the core and of
    // every plugin, and the session state (the current playlist, the
    // per-playlist current indices, the current layout and style) —
    // registered like any other setting.
    QString settingsPath() const { return m_dataDir + "/settings.json"; }
    // The user's layouts (layouts/<name>.json) and styles
    // (styles/<name>.qss); both created on demand.
    QString layoutsDir() const { return m_dataDir + "/layouts"; }
    QString stylesDir() const { return m_dataDir + "/styles"; }
    // The read-only presets' directories ("" when no preset dir
    // exists): see the class comment for the candidate search.
    QString presetLayoutsDir() const { return m_presetsDir + "/layouts"; }
    QString presetStylesDir() const { return m_presetsDir + "/styles"; }
    // A plugin's data directory: <data dir>/plugins/<pluginName> — the
    // files a plugin keeps that are not settings (a cookie snapshot, a
    // cache); the settings themselves live in the central settings.json.
    // Resolved for the caller by DoobApi::pluginDataPath.
    QString pluginDataPath(const QString &pluginName) const
    {
        return m_dataDir + "/plugins/" + pluginName;
    }

private:
    explicit Settings();

    QString m_dataDir;
    QString m_presetsDir; // "" when none of the candidates exists
    inline static Settings* m_instance;
};

#endif // SETTINGS_H
