#include "equalizerprocessor.h"

#include <doob/eq.h>
#include <doob/settingsmanager.h>

EqualizerProcessor::EqualizerProcessor(QObject *parent)
    : AudioProcessor(parent)
{
    connect(SettingsManager::getInstance(), &SettingsManager::changed, this,
            &EqualizerProcessor::onSettingChanged);
    applyGains();
}

void EqualizerProcessor::start(int sampleRate, int channels)
{
    Q_UNUSED(channels);
    QMutexLocker lock{&m_mutex};
    // A fresh stream is re-tuned to its rate and starts with the
    // stored gains (they do not depend on the rate). start() — not a
    // changed() signal — is where a loaded value lands: the
    // SettingsManager's load() emits no change, so a flat start would
    // wait for a slider move that may never come (a stored curve would
    // be dead for the whole session, track after track).
    m_eq.setSampleRate(sampleRate);
    applyGainsFrom(SettingsManager::getInstance()
                       ->value(QString::fromLatin1(eq::GainsKey))
                       .toList());
}

size_t EqualizerProcessor::process(float *frames, size_t frameCount,
                                   int channels)
{
    Q_UNUSED(channels);
    QMutexLocker lock{&m_mutex};
    m_eq.process(frames, frameCount);
    return frameCount;
}

void EqualizerProcessor::onSettingChanged(const QString &key,
                                          const QVariant &value)
{
    if (key != QLatin1String(eq::GainsKey))
        return;
    QMutexLocker lock{&m_mutex};
    applyGainsFrom(value.toList());
}

void EqualizerProcessor::applyGains()
{
    QMutexLocker lock{&m_mutex};
    applyGainsFrom(SettingsManager::getInstance()
                       ->value(QString::fromLatin1(eq::GainsKey))
                       .toList());
}

void EqualizerProcessor::applyGainsFrom(const QVariantList &gainsDb)
{
    std::array<float, eq::BandCount> gains{};
    for (int i = 0; i < eq::BandCount; ++i)
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        gains[i] = i < gainsDb.size() ? gainsDb.at(i).toFloat() : 0.0F;
    m_eq.setGains(gains);
}
