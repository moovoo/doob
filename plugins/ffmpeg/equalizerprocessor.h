#ifndef FFMPEG_EQUALIZERPROCESSOR_H
#define FFMPEG_EQUALIZERPROCESSOR_H

#include <QMutex>
#include <QString>
#include <QVariant>

#include <doob/audioprocessor.h>

#include "equalizer.h"

// The FFmpeg plugin's DSP: the 10-band equalizer, registered in the
// core's DspRegistry (id eq::Id) by the plugin. The gains travel
// through the SettingsManager (eq::GainsKey — the core's
// EqualizerWidget writes them); the on/off switch is the DSP's entry
// in the registry, so there is no bypass of its own: an enabled
// chain runs it, a disabled chain skips it.
//
// The coefficients and delay lines are touched on the audio thread
// (process, the chain's pump) and the GUI thread (the SettingsManager's
// changed()) alike — the mutex is the hand-off (the same one the
// output used to keep around the bare equalizer).
class EqualizerProcessor : public AudioProcessor
{
    Q_OBJECT
public:
    explicit EqualizerProcessor(QObject *parent = nullptr);

    QString id() const override
    {
        return QString::fromLatin1(eq::Id);
    }
    QString title() const override { return QStringLiteral("Equalizer"); }

    // A fresh stream is re-tuned to its rate (the coefficients depend
    // on it) and starts with the stored gains (the SettingsManager's
    // current value — the load emits no changed(), so start() is where
    // a session's stored curve first lands).
    void start(int sampleRate, int channels) override;
    void stop() override {}
    size_t process(float *frames, size_t frameCount, int channels) override;

private slots:
    void onSettingChanged(const QString &key, const QVariant &value);

private:
    void applyGains();
    // The list's shape is the SettingsManager's (10 doubles, in dB); a
    // malformed one degrades to flat (the missing bands are 0). The
    // caller holds m_mutex.
    void applyGainsFrom(const QVariantList &gainsDb);

    Equalizer m_eq;
    QMutex m_mutex;
};

#endif // FFMPEG_EQUALIZERPROCESSOR_H
