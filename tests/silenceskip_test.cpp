#include <QtTest>

#include <QTemporaryDir>
#include <QVector>

#include "dspregistry.h"
#include "settingsmanager.h"
#include "silenceskip.h"

namespace
{

// A stereo chunk (2 floats per frame) at a constant level.
QVector<float> chunk(int frames, float level)
{
    // The explicit ctor (not a braced list): {n, level} would match
    // QVector's initializer_list ctor and narrow n to a float element.
    // NOLINTNEXTLINE(modernize-return-braced-init-list)
    return QVector<float>(static_cast<qsizetype>(frames) * 2,
                          level);
}

class SilenceSkipTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void theSettingsAreRegisteredWithTheirDefaults();
    void leadingSilenceIsDroppedUntilTheFirstAudibleChunk();
    void theCapGivesUpOnASilentTrack();
    void theThresholdSettingIsHonoredLive();
    void startRearmsTheSkip();
    void stopDisarmsTheSkip();

private:
    SettingsManager *m_settings = nullptr;
    DspRegistry *m_registry = nullptr;
    SilenceSkipProcessor *m_proc = nullptr;
    QTemporaryDir m_dir;
};

void SilenceSkipTest::init()
{
    // The SettingsManager first (the registry's ctor registers the dsp.* keys
    // against it), the registry reset so the ctor re-runs.
    SettingsManager::reset();
    m_settings = SettingsManager::getInstance();
    m_dir.remove();
    m_dir = QTemporaryDir();
    QVERIFY(m_dir.isValid());
    m_settings->setSettingsPath(m_dir.path() + "/settings.json");
    m_settings->load();

    DspRegistry::reset();
    m_registry = DspRegistry::getInstance();
    m_proc = new SilenceSkipProcessor(this);
}

void SilenceSkipTest::cleanup()
{
    DspRegistry::reset();
    SettingsManager::reset();
}

void SilenceSkipTest::theSettingsAreRegisteredWithTheirDefaults()
{
    const Setting threshold = m_settings->setting("dsp.silence.threshold_db");
    QVERIFY(threshold.isValid());
    QCOMPARE(threshold.defaultValue.toDouble(), -40.0);
    const Setting max = m_settings->setting("dsp.silence.max_seconds");
    QVERIFY(max.isValid());
    QCOMPARE(max.defaultValue.toDouble(), 60.0);
}

void SilenceSkipTest::leadingSilenceIsDroppedUntilTheFirstAudibleChunk()
{
    m_proc->start(48000, 2);
    // 4800 frames at 48 kHz = 100 ms per chunk.
    QVector<float> silent = chunk(4800, 0.0F);
    QCOMPARE(m_proc->process(silent.data(), 4800, 2), static_cast<size_t>(0));
    QCOMPARE(m_proc->process(silent.data(), 4800, 2), static_cast<size_t>(0));

    QVector<float> tone = chunk(4800, 0.5F);
    QCOMPARE(m_proc->process(tone.data(), 4800, 2), static_cast<size_t>(4800));

    // Done for this track: silence passes through.
    QCOMPARE(m_proc->process(silent.data(), 4800, 2),
             static_cast<size_t>(4800));
}

void SilenceSkipTest::theCapGivesUpOnASilentTrack()
{
    m_settings->setValue("dsp.silence.max_seconds", 0.2);  // 200 ms
    m_proc->start(48000, 2);
    QVector<float> silent = chunk(4800, 0.0F);  // 100 ms
    QCOMPARE(m_proc->process(silent.data(), 4800, 2), static_cast<size_t>(0));
    // The cap: the next chunk passes whole.
    QCOMPARE(m_proc->process(silent.data(), 4800, 2),
             static_cast<size_t>(4800));
}

void SilenceSkipTest::theThresholdSettingIsHonoredLive()
{
    m_proc->start(48000, 2);
    // 2e-4 peak: below the default -40 dBFS (3.2e-3), above -80.
    QVector<float> low = chunk(4800, 2e-4F);
    QCOMPARE(m_proc->process(low.data(), 4800, 2), static_cast<size_t>(0));
    m_settings->setValue("dsp.silence.threshold_db", -80.0);
    QCOMPARE(m_proc->process(low.data(), 4800, 2),
             static_cast<size_t>(4800));
}

void SilenceSkipTest::startRearmsTheSkip()
{
    m_proc->start(48000, 2);
    QVector<float> tone = chunk(4800, 0.5F);
    QCOMPARE(m_proc->process(tone.data(), 4800, 2), static_cast<size_t>(4800));

    m_proc->start(48000, 2);  // a new track
    QVector<float> silent = chunk(4800, 0.0F);
    QCOMPARE(m_proc->process(silent.data(), 4800, 2), static_cast<size_t>(0));
}

void SilenceSkipTest::stopDisarmsTheSkip()
{
    m_proc->start(48000, 2);
    m_proc->stop();
    QVector<float> silent = chunk(4800, 0.0F);
    QCOMPARE(m_proc->process(silent.data(), 4800, 2),
             static_cast<size_t>(4800));
}

} // namespace

QTEST_MAIN(SilenceSkipTest)
#include "silenceskip_test.moc"
