#include <QtTest>

#include <QTemporaryDir>

#include <array>

#include "audioprocessor.h"
#include "dspchain.h"
#include "dspregistry.h"
#include "settingsmanager.h"

namespace
{

// A processor that offsets every sample by a fixed amount (the
// in-place contract); the id is per instance.
class OffsetProcessor : public AudioProcessor
{
public:
    OffsetProcessor(QString id, float offset, QObject *parent = nullptr)
        : AudioProcessor(parent)
        , m_id(std::move(id))
        , m_offset(offset)
    {
    }

    QString id() const override { return m_id; }
    QString title() const override { return m_id; }
    size_t process(float *frames, size_t frameCount, int channels) override
    {
        const size_t samples = frameCount * static_cast<size_t>(channels);
        for (size_t i = 0; i < samples; ++i)
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            frames[i] += m_offset;
        return frameCount;
    }

    const QString m_id;
    const float m_offset;
};

// A processor that scales every sample.
class ScaleProcessor : public AudioProcessor
{
public:
    ScaleProcessor(QString id, float scale, QObject *parent = nullptr)
        : AudioProcessor(parent)
        , m_id(std::move(id))
        , m_scale(scale)
    {
    }

    QString id() const override { return m_id; }
    QString title() const override { return m_id; }
    size_t process(float *frames, size_t frameCount, int channels) override
    {
        const size_t samples = frameCount * static_cast<size_t>(channels);
        for (size_t i = 0; i < samples; ++i)
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            frames[i] *= m_scale;
        return frameCount;
    }

    const QString m_id;
    const float m_scale;
};

// A processor that drops its first chunks whole, then passes through
// (the dropping contract: it returns fewer frames).
class DropProcessor : public AudioProcessor
{
public:
    DropProcessor(QString id, int chunksToDrop,
                  QObject *parent = nullptr)
        : AudioProcessor(parent)
        , m_id(std::move(id))
        , m_left(chunksToDrop)
    {
    }

    QString id() const override { return m_id; }
    QString title() const override { return m_id; }
    size_t process(float *frames, size_t frameCount, int channels) override
    {
        Q_UNUSED(frames);
        Q_UNUSED(channels);
        if (m_left-- > 0)
            return 0;
        return frameCount;
    }

    const QString m_id;
    int m_left;
};

// A processor that counts its lifecycle hooks.
class LifecycleProcessor : public AudioProcessor
{
public:
    explicit LifecycleProcessor(QObject *parent = nullptr)
        : AudioProcessor(parent)
    {
    }

    QString id() const override { return QStringLiteral("lifecycle"); }
    QString title() const override { return QStringLiteral("lifecycle"); }
    void start(int sampleRate, int channels) override
    {
        ++m_starts;
        m_rate = sampleRate;
        m_channels = channels;
    }
    void stop() override { ++m_stops; }
    size_t process(float *frames, size_t frameCount, int channels) override
    {
        Q_UNUSED(frames);
        Q_UNUSED(channels);
        return frameCount;
    }

    int m_starts = 0;
    int m_stops = 0;
    int m_rate = 0;
    int m_channels = 0;
};

class DspChainTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void theProcessorsRunInTheUsersOrder();
    void aDroppingProcessorShortensTheChunk();
    void aDroppedChunkStopsTheRestOfTheChain();
    void theLifecycleHooksForward();
    void aDeadProcessorIsSkipped();
    void aChainEditTakesEffectFromTheNextChunk();
    void aMidStreamEditStartsAndStopsTheJoinedProcessors();
    void anEditWithoutAStreamTouchesNoLifecycle();

private:
    SettingsManager *m_settings = nullptr;
    DspRegistry *m_registry = nullptr;
    QTemporaryDir m_dir;
};

void DspChainTest::init()
{
    // The SettingsManager first (the registry connects to it in its ctor),
    // the registry reset so the ctor re-runs against the fresh SettingsManager.
    SettingsManager::reset();
    m_settings = SettingsManager::getInstance();
    m_dir.remove();
    m_dir = QTemporaryDir();
    QVERIFY(m_dir.isValid());
    m_settings->setSettingsPath(m_dir.path() + "/settings.json");
    m_settings->load();

    DspRegistry::reset();
    m_registry = DspRegistry::getInstance();
    // The built-in (a dropping processor of its own) stays out of the
    // way: the tests register their own chain.
    m_registry->unregisterDsp("silenceskip");
}

void DspChainTest::cleanup()
{
    DspRegistry::reset();
    SettingsManager::reset();
}

void DspChainTest::theProcessorsRunInTheUsersOrder()
{
    // Registered a (x + 1) then b (x * 2): the default order runs
    // (x + 1) * 2.
    m_registry->registerDsp(new OffsetProcessor("a", 1.0F, this), "plug");
    m_registry->registerDsp(new ScaleProcessor("b", 2.0F, this), "plug");

    DspChain chain;
    std::array<float, 2> frames = {1.0F, -1.0F};
    QCOMPARE(chain.process(frames.data(), 1, 2), static_cast<size_t>(1));
    QCOMPARE(frames[0], 4.0F);  // (1 + 1) * 2
    QCOMPARE(frames[1], 0.0F);  // (-1 + 1) * 2

    // The user re-orders (b first): x * 2 + 1 — a fresh chain (a
    // running one picks it up from its next refresh, see below).
    m_registry->moveUp("b");
    DspChain chain2;
    frames[0] = 1.0F;
    frames[1] = -1.0F;
    QCOMPARE(chain2.process(frames.data(), 1, 2), static_cast<size_t>(1));
    QCOMPARE(frames[0], 3.0F);
    QCOMPARE(frames[1], -1.0F);
}

void DspChainTest::aDroppingProcessorShortensTheChunk()
{
    m_registry->registerDsp(new DropProcessor("drop", 1, this), "plug");
    m_registry->registerDsp(new ScaleProcessor("scale", 2.0F, this), "plug");

    DspChain chain;
    std::array<float, 8> frames = {1, 2, 3, 4, 5, 6, 7, 8};
    // The first chunk is dropped whole; the second passes, scaled.
    QCOMPARE(chain.process(frames.data(), 4, 2), static_cast<size_t>(0));
    QCOMPARE(chain.process(frames.data(), 4, 2), static_cast<size_t>(4));
    QCOMPARE(frames[0], 2.0F);
    QCOMPARE(frames[3], 8.0F);
}

void DspChainTest::aDroppedChunkStopsTheRestOfTheChain()
{
    m_registry->registerDsp(new DropProcessor("drop", 1, this), "plug");
    m_registry->registerDsp(new ScaleProcessor("scale", 100.0F, this), "plug");

    DspChain chain;
    std::array<float, 2> frames = {1.0F, 1.0F};
    QCOMPARE(chain.process(frames.data(), 1, 2), static_cast<size_t>(0));
    // The scaling processor never ran on the dropped chunk.
    QCOMPARE(frames[0], 1.0F);
}

void DspChainTest::theLifecycleHooksForward()
{
    auto *proc = new LifecycleProcessor(this);
    m_registry->registerDsp(proc, "plug");

    DspChain chain;
    chain.start(48000, 2);
    QCOMPARE(proc->m_starts, 1);
    QCOMPARE(proc->m_rate, 48000);
    QCOMPARE(proc->m_channels, 2);
    chain.stop();
    QCOMPARE(proc->m_stops, 1);
}

void DspChainTest::aDeadProcessorIsSkipped()
{
    // The plugin-unload case: the processor dies while a chain runs
    // (the snapshot keeps a dead pointer and must skip it).
    auto *dead = new ScaleProcessor("dead", 2.0F);  // no parent
    m_registry->registerDsp(dead, "plug");
    m_registry->registerDsp(new ScaleProcessor("scale", 10.0F, this), "plug");

    DspChain chain;  // the snapshot holds "dead"
    delete dead;     // the unload's delete

    std::array<float, 2> frames = {1.0F, 1.0F};
    QCOMPARE(chain.process(frames.data(), 1, 2), static_cast<size_t>(1));
    QCOMPARE(frames[0], 10.0F);  // only the live processor ran
}

void DspChainTest::aChainEditTakesEffectFromTheNextChunk()
{
    m_registry->registerDsp(new ScaleProcessor("scale", 2.0F, this), "plug");

    DspChain chain;
    std::array<float, 2> frames = {1.0F, 1.0F};
    QCOMPARE(chain.process(frames.data(), 1, 2), static_cast<size_t>(1));
    QCOMPARE(frames[0], 2.0F);

    // The user disables it (the preferences page): the next chunk is
    // untouched (the running chain re-snapshots on the change).
    m_registry->setEnabled("scale", false);
    QCOMPARE(chain.process(frames.data(), 1, 2), static_cast<size_t>(1));
    QCOMPARE(frames[0], 2.0F);

    // ...and re-enables it.
    m_registry->setEnabled("scale", true);
    QCOMPARE(chain.process(frames.data(), 1, 2), static_cast<size_t>(1));
    QCOMPARE(frames[0], 4.0F);
}

// A mid-stream enable must not wait for the next track: the joined
// processor is started on the running stream's parameters (and the
// removed one stopped), so a rate-dependent DSP (the equalizer) takes
// effect from the next chunk.
void DspChainTest::aMidStreamEditStartsAndStopsTheJoinedProcessors()
{
    auto *proc = new LifecycleProcessor(this);
    m_registry->registerDsp(proc, "plug");

    DspChain chain;
    chain.start(48000, 2);
    QCOMPARE(proc->m_starts, 1);

    // The user disables it mid-stream: the processor leaves the chain
    // and is stopped (its per-stream state must not survive the edit).
    m_registry->setEnabled("lifecycle", false);
    QCOMPARE(proc->m_stops, 1);

    // ...and re-enables it: it joins the running chain and is started
    // on the stream's parameters.
    m_registry->setEnabled("lifecycle", true);
    QCOMPARE(proc->m_starts, 2);
    QCOMPARE(proc->m_rate, 48000);
    QCOMPARE(proc->m_channels, 2);

    // The stream's own teardown still stops it.
    chain.stop();
    QCOMPARE(proc->m_stops, 2);
}

// With no stream running, a refresh only swaps the snapshot: the
// processors' lifecycle is the next start()'s business.
void DspChainTest::anEditWithoutAStreamTouchesNoLifecycle()
{
    auto *proc = new LifecycleProcessor(this);
    m_registry->registerDsp(proc, "plug");

    DspChain chain;
    m_registry->setEnabled("lifecycle", false);
    m_registry->setEnabled("lifecycle", true);
    QCOMPARE(proc->m_starts, 0);
    QCOMPARE(proc->m_stops, 0);

    // The stream start reaches the joined processor.
    chain.start(44100, 2);
    QCOMPARE(proc->m_starts, 1);
    QCOMPARE(proc->m_rate, 44100);
}

} // namespace

QTEST_MAIN(DspChainTest)
#include "dspchain_test.moc"
