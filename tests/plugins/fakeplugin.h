#ifndef FAKEPLUGIN_H
#define FAKEPLUGIN_H

#include "audioplayer.h"
#include "audioprocessor.h"
#include "basewidget.h"
#include "doobapi.h"
#include "doobplugin.h"
#include "sourceprovider.h"

// A source provider that claims "fake:" URLs and serves fixed metadata
// (no I/O — the callback runs synchronously on the calling thread, which
// in the host is the GUI thread).
class FakeSourceProvider : public SourceProvider
{
public:
    explicit FakeSourceProvider(QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("fakeplugin"); }
    QString title() const override { return QStringLiteral("Fake"); }
    bool accepts(const QUrl &url) const override;
    void fetchMetadata(const QUrl &url, MetadataCallback cb) override;
    QUrl playableUrl(const MediaItem &item) const override;
};

// The plugin's widget (the core-side half is the WidgetRegistry entry).
class FakePluginWidget : public BaseWidget
{
    Q_OBJECT
public:
    inline static const QString id = "fakepluginwidget";
    inline static const QString title = "Fake Plugin Widget";

    explicit FakePluginWidget(QWidget *parent = nullptr);
    QString widgetId() const override { return id; }
    QString widgetTitle() const override { return title; }
};

// The plugin's playback driver (the core-side half is the
// PlayerRegistry entry).
class FakePlayer : public AudioPlayer
{
    Q_OBJECT
public:
    explicit FakePlayer(QObject *parent = nullptr);

    void play(const MediaItem &item) override;
    void pause() override;
    void resume() override;
    void stop() override;
    qint64 positionMs() const override;
};

// The plugin's DSP (the core-side half is the DspRegistry entry): a
// pass-through (its process() leaves the chunk untouched).
class FakeProcessor : public AudioProcessor
{
public:
    explicit FakeProcessor(QObject *parent = nullptr)
        : AudioProcessor(parent)
    {
    }

    QString id() const override { return QStringLiteral("fakeplugin.dsp"); }
    QString title() const override { return QStringLiteral("Fake DSP"); }
    size_t process(float *frames, size_t frameCount, int channels) override
    {
        Q_UNUSED(frames);
        Q_UNUSED(channels);
        return frameCount;
    }
};

// The test plugin: registers one provider, one widget, one driver,
// one DSP and one setting in initialize(), unregisters all of them in
// shutdown().
class FakePlugin : public DoobPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DOOB_PLUGIN_IID FILE "fakeplugin.json")
public:
    explicit FakePlugin(QObject *parent = nullptr);

    void initialize(DoobApi *api) override;
    void shutdown() override;

    // What initialize() received (the tests read it back).
    DoobApi* api() const { return m_api; }

private:
    DoobApi *m_api = nullptr;
    FakeSourceProvider *m_provider = nullptr;
    FakeProcessor *m_processor = nullptr;
};

#endif // FAKEPLUGIN_H
