#ifndef SILENCESKIP_H
#define SILENCESKIP_H

#include <QMutex>
#include <QString>
#include <QVariant>

#include <doob/audioprocessor.h>

// The core's built-in DSP: a fast track start. It drops the leading
// silence of a track (the chunks whose peak stays under the
// threshold) until the first audible chunk — which passes whole — or
// until the cap (a track that is silent throughout must not stall its
// start). After that it passes everything through, and start()
// re-arms it for the next track.
//
// The parameters are the core's settings (dsp.silence.*), read on
// start and live-updated through the SettingsManager's changed().
class SilenceSkipProcessor : public AudioProcessor
{
    Q_OBJECT
public:
    explicit SilenceSkipProcessor(QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("silenceskip"); }
    QString title() const override
    {
        return QStringLiteral("Skip leading silence");
    }

    void start(int sampleRate, int channels) override;
    void stop() override;
    size_t process(float *frames, size_t frameCount, int channels) override;

private slots:
    void onSettingChanged(const QString &key, const QVariant &value);

private:
    void applySettings();

    // The state the audio thread (process) and the GUI thread (the
    // settings) share: under m_mutex.
    QMutex m_mutex;
    float m_threshold = 0.0F; // the linear amplitude, from the dB setting
    qint64 m_maxMs = 0;
    int m_rate = 0;
    qint64 m_droppedMs = 0;
    bool m_active = false;
};

#endif // SILENCESKIP_H
