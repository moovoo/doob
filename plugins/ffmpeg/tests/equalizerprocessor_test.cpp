#include <QtTest>

#include <QTemporaryDir>

#include <array>
#include <cmath>
#include <vector>

#include <doob/eq.h>
#include <doob/settingsmanager.h>

#include "equalizerprocessor.h"

namespace
{

// The processor's stream lifecycle against the SettingsManager: the
// stored gains (the plugin registers the setting in initialize(); the
// test does the same) must be applied by start() — the load emits no
// changed(), so start() is where a session's stored curve first lands.
class EqualizerProcessorTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void unstartedIsAWire();
    void startAppliesTheStoredGains();
    void aTrackSwitchKeepsTheStoredGains();
    void aGainsChangeAppliesImmediately();

private:
    // The DC level after a second of a constant tone through the
    // processor (the low shelf's DC gain is the band's full gain).
    float processDc(EqualizerProcessor &processor, float level,
                    std::size_t frames = 48000);

    SettingsManager *m_settings = nullptr;
    QTemporaryDir m_dir;
};

void EqualizerProcessorTest::init()
{
    SettingsManager::reset();
    m_settings = SettingsManager::getInstance();
    m_dir.remove();
    m_dir = QTemporaryDir();
    QVERIFY(m_dir.isValid());
    m_settings->setSettingsPath(m_dir.path() + "/settings.json");
    m_settings->load();

    // The plugin's own registration (initialize): the 10-band gains,
    // in dB, 0 = flat.
    QVariantList flat;
    for (int i = 0; i < eq::BandCount; ++i)
        flat << 0.0;
    QVERIFY(m_settings->registerSetting(
        { QString::fromLatin1(eq::GainsKey),
          QStringLiteral("Equalizer gains (dB)"),
          QStringLiteral("The 10-band gains, in dB, from 31 Hz to 16 kHz "
                         "(0 = flat)."),
          flat, QStringLiteral("ffmpeg"), }));
}

void EqualizerProcessorTest::cleanup()
{
    SettingsManager::reset();
}

float EqualizerProcessorTest::processDc(EqualizerProcessor &processor,
                                        float level, std::size_t frames)
{
    std::vector<float> data(2 * frames, level);
    processor.process(data.data(), frames, 2);
    return data.back();
}

// Before start() the processor has no rate: process() is a no-op
// (a mid-stream enable is started by the chain's refresh, and a
// pre-stream construction must not touch the audio).
void EqualizerProcessorTest::unstartedIsAWire()
{
    EqualizerProcessor processor(this);
    QCOMPARE(processDc(processor, 0.5F, 4800), 0.5F);
}

// The startup case: the stored gains (a +12 dB low shelf) are loaded
// from disk without a changed() signal; the first stream's start()
// must apply them (a flat start would keep the curve dead for the
// whole session until a slider move).
void EqualizerProcessorTest::startAppliesTheStoredGains()
{
    QVariantList gains =
        m_settings->value(QString::fromLatin1(eq::GainsKey)).toList();
    gains[0] = 12.0;
    m_settings->setValue(QString::fromLatin1(eq::GainsKey), gains);

    EqualizerProcessor processor(this);
    processor.start(48000, 2);
    const double expected = 0.5 * std::pow(10.0, 12.0 / 20.0);
    QVERIFY2(qAbs(processDc(processor, 0.5F) - expected) < 0.03,
             "the stored gains are not applied by start()");
}

// A track switch is a fresh start(): the stored curve must survive it
// (the old start() flattened the gains and waited for a slider move).
void EqualizerProcessorTest::aTrackSwitchKeepsTheStoredGains()
{
    QVariantList gains =
        m_settings->value(QString::fromLatin1(eq::GainsKey)).toList();
    gains[5] = 12.0;  // the 1 kHz peaking band
    m_settings->setValue(QString::fromLatin1(eq::GainsKey), gains);

    EqualizerProcessor processor(this);
    processor.start(48000, 2);
    // One track: a fresh 1 kHz tone (a second, the other bands are
    // flat, hence the identity) through the processor, the steady
    // peak (the first quarter is the filter settling).
    const std::size_t frames = 48000;
    auto peak = [&] {
        std::vector<float> tone(2 * frames);
        for (std::size_t f = 0; f < frames; ++f) {
            const float s = static_cast<float>(
                0.5 * std::sin(2.0 * M_PI * 1000.0
                                * static_cast<double>(f) / 48000.0));
            tone[2 * f] = s;
            tone[(2 * f) + 1] = s;
        }
        processor.process(tone.data(), frames, 2);
        float p = 0.0F;
        for (std::size_t f = frames / 4; f < frames; ++f)
            p = qMax(p, qAbs(tone[2 * f]));
        return p;
    };
    const double expected = 0.5 * std::pow(10.0, 12.0 / 20.0);
    QVERIFY2(qAbs(peak() - expected) < 0.02, "first track is not boosted");

    // The next track (a fresh start, same rate): the curve is intact.
    processor.start(48000, 2);
    QVERIFY2(qAbs(peak() - expected) < 0.02,
             "the track switch flattened the stored gains");
}

// The slider's path: a gain change mid-stream applies at once (the
// changed() signal, no track switch needed).
void EqualizerProcessorTest::aGainsChangeAppliesImmediately()
{
    EqualizerProcessor processor(this);
    processor.start(48000, 2);
    QCOMPARE(processDc(processor, 0.5F, 4800), 0.5F);  // flat at first

    QVariantList gains =
        m_settings->value(QString::fromLatin1(eq::GainsKey)).toList();
    gains[0] = 12.0;
    m_settings->setValue(QString::fromLatin1(eq::GainsKey), gains);
    const double expected = 0.5 * std::pow(10.0, 12.0 / 20.0);
    QVERIFY2(qAbs(processDc(processor, 0.5F) - expected) < 0.03,
             "the gain change is not applied mid-stream");
}

} // namespace

QTEST_MAIN(EqualizerProcessorTest)
#include "equalizerprocessor_test.moc"
