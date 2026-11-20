#ifndef FFMPEG_FFMPEGPLUGIN_H
#define FFMPEG_FFMPEGPLUGIN_H

#include <doob/doobapi.h>
#include <doob/doobplugin.h>

class EqualizerProcessor;

// The plugin: registers the "ffmpeg" playback driver, its DSP (the
// equalizer, see equalizerprocessor.h) and its settings owner (the
// preferences dialog's Plugins group) in initialize(), unregisters
// them in shutdown() (the host unloads this library right after,
// which deletes the plugin and everything it parented — the
// registrations must be undone first).
class FFmpegPlugin : public DoobPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DOOB_PLUGIN_IID FILE "ffmpeg.json")
public:
    explicit FFmpegPlugin(QObject *parent = nullptr) : DoobPlugin(parent) {}

    void initialize(DoobApi *api) override;
    void shutdown() override;

private:
    DoobApi *m_api = nullptr;
    EqualizerProcessor *m_eqProcessor = nullptr;
};

#endif // FFMPEG_FFMPEGPLUGIN_H
