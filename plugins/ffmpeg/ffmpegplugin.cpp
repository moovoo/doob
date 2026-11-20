#include "ffmpegplugin.h"

#include <doob/dspregistry.h>
#include <doob/eq.h>
#include <doob/playerregistry.h>
#include <doob/settingsmanager.h>
#include <doob/settingsregistry.h>

#include <QVariantList>

#include "equalizerprocessor.h"
#include "ffmpegplayer.h"

void FFmpegPlugin::initialize(DoobApi *api)
{
    if (api == nullptr || api->version != DoobApi::ApiVersion)
        return;
    m_api = api;
    api->players->registerPlayer(
        QStringLiteral("ffmpeg"), QStringLiteral("FFmpeg"),
        [](QObject *parent) { return new FFmpegPlayer(parent); });
    // The preferred default driver: when the user has made no explicit
    // selection (an empty "playback.player"), this driver plays by
    // default — with the plugin present, FFmpeg is the backend out of
    // the box. An explicit selection always wins, and without the
    // plugin the built-in driver is the default as before.
    api->players->setPreferredDefault(QStringLiteral("ffmpeg"));
    // The plugin exports no settings page; the owner declaration keeps it
    // listed in the preferences dialog's Plugins group (with the
    // "no settings" note in place of a page).
    api->settings->registerOwner(QStringLiteral("ffmpeg"),
                                  QStringLiteral("FFmpeg"));
    // The equalizer's setting (the core's EqualizerWidget reads and
    // writes it through the SettingsManager). The stored value survives the
    // unregistration in shutdown(), so a re-added plugin finds it
    // intact.
    QVariantList flat;
    for (int i = 0; i < eq::BandCount; ++i)
        flat << 0.0;
    api->settingsManager->registerSetting(
        { QString::fromLatin1(eq::GainsKey),
          QStringLiteral("Equalizer gains (dB)"),
          QStringLiteral("The 10-band gains, in dB, from 31 Hz to 16 kHz "
                         "(0 = flat)."),
          flat, QStringLiteral("ffmpeg"), });
    // The equalizer as a DSP of the core's chain: registered in the
    // DspRegistry (id eq::Id), parented to this plugin (it dies with
    // the unload; a running chain skips the dead pointer). Its
    // on/off switch is the registry's entry, its gains the setting
    // above.
    m_eqProcessor = new EqualizerProcessor(this);
    api->dpsp->registerDsp(m_eqProcessor, QStringLiteral("ffmpeg"));
}

void FFmpegPlugin::shutdown()
{
    if (m_api != nullptr) {
        m_api->players->unregisterPlayer(QStringLiteral("ffmpeg"));
        m_api->players->setPreferredDefault(QString());
        m_api->settings->unregisterOwner(QStringLiteral("ffmpeg"));
        m_api->settingsManager->unregisterSetting(QString::fromLatin1(eq::GainsKey));
        m_api->dpsp->unregisterDsp(QString::fromLatin1(eq::Id));
    }
    m_api = nullptr;
}
