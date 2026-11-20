#ifndef PLUGINMANAGER_H
#define PLUGINMANAGER_H

#include <QList>
#include <QObject>
#include <QString>

#include "doobapi.h"
#include "doobplugin.h"

class QPluginLoader;

// Loads the doob plugins: scans the plugin directories, keeps the .so
// files that expose a DoobPlugin under the current IID, calls
// initialize() with the api facade, and — on shutdownPlugins() — calls
// shutdown() and unloads them again. A failing plugin (unreadable, a
// wrong or missing IID, not a DoobPlugin) is skipped with a qWarning;
// the rest load.
//
// Load on the GUI thread, before LayoutManager::show(): a saved layout
// may reference a plugin's widgets. Unload on aboutToQuit.
class PluginManager : public QObject
{
    Q_OBJECT
public:
    static PluginManager* getInstance();

    // Scans the standard plugin directories in order: the
    // DOOB_PLUGIN_DIR environment variable (when set), <app dir>/plugins
    // and the GenericData location's plugins directory.
    void loadPlugins();

    // Loads every plugin in one directory (loadPlugins' per-directory
    // step; also the test seam). A missing directory loads nothing.
    void loadFromDir(const QString &dir);

    // Calls shutdown() on every loaded plugin (reverse load order, so
    // each plugin undoes its own initialize() last-in-first-out) and
    // unloads the libraries (which deletes the instances).
    void shutdownPlugins();

    qsizetype count() const { return m_loaded.size(); }
    QList<DoobPlugin*> plugins() const;

private:
    struct Loaded
    {
        QPluginLoader *loader;
        DoobPlugin *plugin;
    };

    explicit PluginManager(QObject *parent = nullptr);

    void loadFile(const QString &file);
    QList<QString> searchPaths() const;

    QList<Loaded> m_loaded;
    DoobApi m_api;

    inline static PluginManager* m_instance;
};

#endif // PLUGINMANAGER_H
