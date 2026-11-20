#include "silenceskip.h"

#include <cmath>

#include <doob/settingsmanager.h>

namespace
{
const QString ThresholdKey = QStringLiteral("dsp.silence.threshold_db");
const QString MaxKey = QStringLiteral("dsp.silence.max_seconds");

// The dBFS threshold to a linear amplitude (peak scale 0..1).
float toLinear(double db)
{
    return static_cast<float>(std::pow(10.0, db / 20.0));
}
}

SilenceSkipProcessor::SilenceSkipProcessor(QObject *parent)
    : AudioProcessor(parent)
{
    connect(SettingsManager::getInstance(), &SettingsManager::changed, this,
            &SilenceSkipProcessor::onSettingChanged);
    applySettings();
}

void SilenceSkipProcessor::applySettings()
{
    SettingsManager* settings = SettingsManager::getInstance();
    const QVariant threshold = settings->value(ThresholdKey);
    const QVariant maxSeconds = settings->value(MaxKey);
    QMutexLocker lock{&m_mutex};
    if (threshold.isValid())
        m_threshold = toLinear(threshold.toDouble());
    if (maxSeconds.isValid())
        m_maxMs = static_cast<qint64>(maxSeconds.toDouble() * 1000.0);
}

void SilenceSkipProcessor::onSettingChanged(const QString &key,
                                            const QVariant &value)
{
    Q_UNUSED(key);
    Q_UNUSED(value);
    applySettings();
}

void SilenceSkipProcessor::start(int sampleRate, int channels)
{
    Q_UNUSED(channels);
    QMutexLocker lock{&m_mutex};
    m_rate = sampleRate;
    m_droppedMs = 0;
    m_active = m_rate > 0;
}

void SilenceSkipProcessor::stop()
{
    QMutexLocker lock{&m_mutex};
    m_active = false;
}

size_t SilenceSkipProcessor::process(float *frames, size_t frameCount,
                                     int channels)
{
    if (frameCount == 0)
        return 0;
    QMutexLocker lock{&m_mutex};
    if (!m_active)
        return frameCount;
    // The chunk's peak (every channel, interleaved).
    float peak = 0.0F;
    const size_t samples = frameCount * static_cast<size_t>(channels);
    for (size_t i = 0; i < samples; ++i)
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        peak = qMax(peak, qAbs(frames[i]));
    if (peak >= m_threshold) {
        // The first audible chunk passes whole (its leading silence is
        // at most a chunk long) and the skip is done for this track.
        m_active = false;
        return frameCount;
    }
    m_droppedMs += static_cast<qint64>(frameCount) * 1000 / m_rate;
    if (m_droppedMs >= m_maxMs) {
        // The cap: the rest passes through.
        m_active = false;
        return frameCount;
    }
    return 0;
}
