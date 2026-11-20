#ifndef EXAMPLEPLUGIN_H
#define EXAMPLEPLUGIN_H

#include <doob/doobapi.h>
#include <doob/doobplugin.h>
#include <doob/sourceprovider.h>

// A source provider that claims "example:" URLs and serves fixed metadata.
// A real provider would talk to its service and call cb on the GUI thread
// once per fetch (exactly once, an empty TrackMeta on failure).
class ExampleProvider : public SourceProvider
{
public:
    explicit ExampleProvider(QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("example"); }
    QString title() const override { return QStringLiteral("Example"); }
    bool accepts(const QUrl &url) const override;
    void fetchMetadata(const QUrl &url, MetadataCallback cb) override;
};

// A minimal doob plugin: initialize() registers the provider, shutdown()
// unregisters it again — the host unloads this library right after
// shutdown(), which deletes the provider, so the registration must be
// undone first.
class ExamplePlugin : public DoobPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DOOB_PLUGIN_IID FILE "exampleplugin.json")
public:
    explicit ExamplePlugin(QObject *parent = nullptr);

    void initialize(DoobApi *api) override;
    void shutdown() override;

private:
    DoobApi *m_api = nullptr;
    ExampleProvider *m_provider = nullptr;
};

#endif // EXAMPLEPLUGIN_H
