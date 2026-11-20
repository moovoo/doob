#ifndef DOOBPLUGIN_H
#define DOOBPLUGIN_H

#include <QObject>

struct DoobApi;

// The IID every doob plugin's Q_PLUGIN_METADATA must carry. The host
// (PluginManager) reads the metadata before instantiating a plugin and
// refuses to load a .so that names anything else.
#define DOOB_PLUGIN_IID "doob.plugin/1"

// The base class of a doob plugin. The host (PluginManager) constructs
// the instance, calls initialize() with the api facade, and calls
// shutdown() before unloading the library.
//
// Contract: shutdown() must undo initialize() — unregister every
// provider, widget and driver the plugin registered, because the unload
// that follows deletes the plugin and everything it parented (a
// registration kept alive by the core would then dangle).
class DoobPlugin : public QObject
{
    Q_OBJECT
public:
    explicit DoobPlugin(QObject *parent = nullptr) : QObject(parent) {}
    ~DoobPlugin() override = default;

    // The api pointer is owned by the host and outlives the plugin; a
    // plugin must not delete it. Check api->version first and bail (a
    // no-op) when it is not one this plugin understands.
    virtual void initialize(DoobApi *api) = 0;

    // Called before the library is unloaded; the default is a no-op.
    virtual void shutdown() {}
};

#endif // DOOBPLUGIN_H
