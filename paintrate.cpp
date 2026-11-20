#include "paintrate.h"

#include "settingsmanager.h"

#include <QTimer>

#include <cmath>

namespace PaintRate
{

QString key()
{
    return QStringLiteral("visualization.fps");
}

int defaultFps()
{
    return 60;
}

int minFps()
{
    return 5;
}

int maxFps()
{
    return 240;
}

void ensureRegistered()
{
    SettingsManager* settings = SettingsManager::getInstance();
    // The visualization widgets register from their constructors; the
    // later calls must not warn (registerSetting warns on a taken key).
    if (settings->setting(key()).isValid())
        return;
    settings->registerSetting(
        { key(),
          QStringLiteral("Visualization paint rate"),
          QStringLiteral("How often per second the audio visualizations "
                         "(oscillogram, peak meter, spectrum, spectrogram) "
                         "may paint while playing (%1-%2).")
              .arg(minFps())
              .arg(maxFps()),
          defaultFps(),
          QString(), });
}

int fps()
{
    const QVariant value = SettingsManager::getInstance()->value(key());
    return qBound(minFps(), value.isValid() ? value.toInt() : defaultFps(), maxFps());
}

int intervalMs(int fps)
{
    return 1000 / qBound(minFps(), fps, maxFps());
}

void applyTo(QTimer *timer)
{
    timer->setInterval(intervalMs(fps()));
}

double decayFactor(int intervalMs, double base, double baseMs)
{
    return std::pow(base, static_cast<double>(intervalMs) / baseMs);
}

} // namespace PaintRate
