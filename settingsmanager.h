#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

// One registered setting: the key it is stored under, the human-readable
// title/description the settings browser shows, and the default value —
// which doubles as the type (a value set for the key must be convertible
// to the default's type). The key is flat and dotted
// ("library.watch.dirs"); the owner is empty for the core's settings and
// the id of the plugin that registered the setting (a convention, not
// enforced — the plugin passes the full key).
struct Setting
{
    QString key;
    QString title;
    QString description;
    QVariant defaultValue;
    QString owner;

    bool isValid() const { return !key.isEmpty(); }
};

// The central SettingsManager: the single place app settings and session
// state live — one in-memory map and one file (settings.json), write-
// through on every change (a crash and a clean exit leave the same state
// — there is no save on quit).
//
// A component registers its settings (key, title, description, default,
// owner) and reads/writes them through the SettingsManager; the file
// holds only explicitly set values, so a new default applies to an old
// file automatically. Session state (the current playlist, the per-playlist
// current indices, the current layout and style) is registered like any
// other setting. Stored values live independently of the registrations:
// a value whose setting was unregistered (the plugin was removed) stays
// in the file (an orphaned key) and a re-added plugin finds its settings
// intact.
//
// The file format is a flat JSON object with the dotted keys at the top
// level, written atomically (temp file + rename), so a crash mid-write
// cannot truncate the previous file.
class SettingsManager : public QObject
{
    Q_OBJECT
public:
    static SettingsManager* getInstance();
    static void reset(); // for tests: drop the instance so it re-resolves

    // Point the store at its file (the app does this from the Settings
    // paths; tests point it at a temp dir). With no path set the store
    // is memory-only: values work, nothing is written.
    void setSettingsPath(const QString &path);
    // Read the file into memory. A missing file loads as empty; an
    // unreadable or invalid file warns and loads as empty. Stored values
    // of registered settings are coerced to the registered type (a JSON
    // array comes back as a QStringList, a JSON object as a QVariantMap),
    // so value() returns a stable type.
    void load();

    // Registration: metadata only, idempotent in the other registries'
    // sense — a taken key is refused (false, a qWarning).
    // unregisterSetting drops the metadata but not the stored value (see
    // above).
    bool registerSetting(const Setting &setting);
    void unregisterSetting(const QString &key);
    // The registered setting (an empty Setting when the key is not
    // registered).
    Setting setting(const QString &key) const;
    // All registered settings, registration order.
    QList<Setting> settings() const;

    // The settings store. value() is the stored value, else the
    // registered default, else invalid. setValue type-checks against the
    // registered default (an unregistered key accepts anything), is
    // write-through and emits changed() — only when the value actually
    // changes. resetToDefault drops the stored key (the default applies
    // again) and emits changed() with the default.
    QVariant value(const QString &key) const;
    QVariant defaultValue(const QString &key) const;
    // True when the key is explicitly set (present in the file).
    bool isModified(const QString &key) const;
    void setValue(const QString &key, const QVariant &value);
    void resetToDefault(const QString &key);
    // The keys present in the store (including orphaned ones), sorted.
    QStringList storedKeys() const;

signals:
    // A value was set (or reset to its default).
    void changed(const QString &key, const QVariant &value);

private:
    explicit SettingsManager();

    static bool loadStore(const QString &path, QVariantMap &store);
    // The atomic write (temp file + rename). Returns false (the previous
    // file untouched) when the write fails.
    static bool saveStore(const QString &path, const QVariantMap &store);
    // A JSON round trip changes container types (array -> QVariantList,
    // object -> QVariantMap): convert a stored value to the registered
    // type when possible, keeping it unchanged when not.
    static QVariant coerceToType(const QVariant &stored, const QVariant &type);

    QVariantMap m_settings;  // the store
    QList<Setting> m_registry; // the registrations, in order
    QString m_settingsPath;
    inline static SettingsManager* m_instance;
};

#endif // SETTINGSMANAGER_H
