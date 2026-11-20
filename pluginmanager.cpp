#include "pluginmanager.h"

#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QJsonObject>
#include <QPluginLoader>
#include <QStandardPaths>

#include "artsource.h"
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

PluginManager* PluginManager::getInstance()
{
    if (m_instance == nullptr)
        m_instance = new PluginManager;
    return m_instance;
}

PluginManager::PluginManager(QObject *parent)
    : QObject(parent)
{
}

QList<DoobPlugin*> PluginManager::plugins() const
{
    QList<DoobPlugin*> out;
    out.reserve(m_loaded.size());
    for (const Loaded &loaded : m_loaded)
        out.append(loaded.plugin);
    return out;
}

void PluginManager::loadPlugins()
{
    for (const QString &dir : searchPaths())
        loadFromDir(dir);
}

QList<QString> PluginManager::searchPaths() const
{
    QList<QString> paths;
    const QString env = qEnvironmentVariable("DOOB_PLUGIN_DIR");
    if (!env.isEmpty())
        paths.append(env);
    paths.append(QCoreApplication::applicationDirPath() + "/plugins");
    paths.append(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                 + "/plugins");
    return paths;
}

void PluginManager::loadFromDir(const QString &dir)
{
    // The facade the plugins see: the singletons, the window and the
    // plugin's data directory (re-filled on every load — the pointers
    // are stable, this is cheap).
    m_api.version = DoobApi::ApiVersion;
    m_api.items = MediaItemStore::getInstance();
    m_api.library = LibraryManager::getInstance();
    m_api.playlists = PlaylistManager::getInstance();
    m_api.playback = PlaybackManager::getInstance();
    m_api.sources = SourceManager::getInstance();
    m_api.players = PlayerRegistry::getInstance();
    m_api.widgets = WidgetRegistry::getInstance();
    m_api.settings = SettingsRegistry::getInstance();
    m_api.settingsManager = SettingsManager::getInstance();
    m_api.dpsp = DspRegistry::getInstance();
    m_api.arts = ArtRegistry::getInstance();

    const QDir d(dir);
    if (!d.exists())
        return;
    const QStringList files = d.entryList(QDir::Files, QDir::Name);
    for (const QString &name : files)
        loadFile(d.absoluteFilePath(name));
}

void PluginManager::loadFile(const QString &file)
{
    QPluginLoader *loader = new QPluginLoader(file, this);

    // The IID is the version gate: a .so that is not a doob plugin (no
    // metadata, a foreign plugin) is skipped without being instantiated.
    const QJsonObject meta = loader->metaData();
    if (meta.value("IID").toString() != QString(DOOB_PLUGIN_IID)) {
        qWarning("PluginManager: %s is not a doob plugin (IID %s)",
                 qPrintable(file), qPrintable(meta.value("IID").toString()));
        delete loader;
        return;
    }

    DoobPlugin *plugin = qobject_cast<DoobPlugin *>(loader->instance());
    if (plugin == nullptr) {
        qWarning("PluginManager: %s does not expose a DoobPlugin", qPrintable(file));
        // Deleting the loader unloads the .so and deletes the instance
        // (the plugin has no parent).
        delete loader;
        return;
    }

    plugin->initialize(&m_api);
    m_loaded.append(Loaded{loader, plugin});
    qDebug("PluginManager: loaded %s", qPrintable(file));
}

void PluginManager::shutdownPlugins()
{
    // Reverse load order: each plugin's shutdown() undoes its own
    // initialize(), and the last loaded is the first to go.
    for (qsizetype i = m_loaded.size() - 1; i >= 0; --i) {
        m_loaded.at(i).plugin->shutdown();
        m_loaded.at(i).loader->unload();
        delete m_loaded.at(i).loader;
    }
    m_loaded.clear();
}
