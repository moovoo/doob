#include <QtTest>

#include <QSignalSpy>
#include <QTemporaryDir>

#include "audioprocessor.h"
#include "dspregistry.h"
#include "settingsmanager.h"

namespace
{

// A pass-through processor that records nothing: the registry's
// concern is the id and the state, not the samples.
class TestProcessor : public AudioProcessor
{
public:
    explicit TestProcessor(QString id, QObject *parent = nullptr)
        : AudioProcessor(parent)
        , m_id(std::move(id))
    {
    }

    QString id() const override { return m_id; }
    QString title() const override
    {
        return m_id + QStringLiteral(" title");
    }
    size_t process(float *frames, size_t frameCount, int channels) override
    {
        Q_UNUSED(frames);
        Q_UNUSED(channels);
        return frameCount;
    }

    const QString m_id;
};

// The chain's options as a plain id list, effective order.
QStringList ids(DspRegistry *registry)
{
    QStringList out;
    for (const QPair<QString, QString> &entry : registry->options())
        out.append(entry.first);
    return out;
}

class DspRegistryTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void theBuiltinIsRegistered();
    void aNewDspIsEnabledByDefault();
    void aDuplicateIdIsRefused();
    void setEnabledWritesThroughAndChangesTheChain();
    void anEngineEditOfTheStateChangesTheChain();
    void theOrderFollowsTheStoredList();
    void aStoredUnknownIdIsIgnored();
    void aNewDspRunsAfterTheStoredOrder();
    void moveUpAndDownPersistTheOrder();
    void unregisterDropsTheEntryAndKeepsTheStoredState();

private:
    SettingsManager *m_settings = nullptr;
    DspRegistry *m_registry = nullptr;
    QTemporaryDir m_dir;
};

void DspRegistryTest::init()
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
}

void DspRegistryTest::cleanup()
{
    DspRegistry::reset();
    SettingsManager::reset();
}

void DspRegistryTest::theBuiltinIsRegistered()
{
    // The core's built-in (the leading-skip), registered from the
    // registry's ctor, owner empty.
    QVERIFY(m_registry->contains("silenceskip"));
    QCOMPARE(m_registry->owner("silenceskip"), QString());
    QVERIFY(m_registry->isEnabled("silenceskip"));
    QVERIFY(!m_registry->chain().isEmpty());
}

void DspRegistryTest::aNewDspIsEnabledByDefault()
{
    auto *p = new TestProcessor("a", this);
    QVERIFY(m_registry->registerDsp(p, "plug"));

    QVERIFY(m_registry->contains("a"));
    QCOMPARE(m_registry->owner("a"), QString("plug"));
    QVERIFY(m_registry->isEnabled("a"));
    QVERIFY(m_registry->chain().contains(p));
    // The title is the processor's own.
    QCOMPARE(ids(m_registry).last(), QString("a"));
    QCOMPARE(m_registry->options().last().second, QString("a title"));
}

void DspRegistryTest::aDuplicateIdIsRefused()
{
    auto *p = new TestProcessor("a", this);
    QVERIFY(m_registry->registerDsp(p, "plug"));
    auto *p2 = new TestProcessor("a", this);
    QVERIFY(!m_registry->registerDsp(p2, "plug"));
    QVERIFY(!m_registry->registerDsp(nullptr, "plug"));
    QCOMPARE(m_registry->chain().size(), 2);  // the built-in and "a"
}

void DspRegistryTest::setEnabledWritesThroughAndChangesTheChain()
{
    auto *p = new TestProcessor("a", this);
    m_registry->registerDsp(p, "plug");
    QSignalSpy spy(m_registry, &DspRegistry::chainChanged);

    m_registry->setEnabled("a", false);
    QCOMPARE(spy.count(), 1);
    QVERIFY(!m_registry->isEnabled("a"));
    QVERIFY(!m_registry->chain().contains(p));
    QCOMPARE(m_settings->value("dsp.enabled").toMap().value("a").toBool(),
             false);

    m_registry->setEnabled("a", true);
    QCOMPARE(spy.count(), 2);
    QVERIFY(m_registry->isEnabled("a"));
    QVERIFY(m_registry->chain().contains(p));
    QCOMPARE(m_settings->value("dsp.enabled").toMap().value("a").toBool(),
             true);
}

void DspRegistryTest::anEngineEditOfTheStateChangesTheChain()
{
    auto *p = new TestProcessor("a", this);
    m_registry->registerDsp(p, "plug");
    QSignalSpy spy(m_registry, &DspRegistry::chainChanged);

    // The config browser writes through the SettingsManager: the registry's
    // chain follows.
    m_settings->setValue("dsp.enabled", QVariantMap{{"a", false}});
    QCOMPARE(spy.count(), 1);
    QVERIFY(!m_registry->chain().contains(p));
}

void DspRegistryTest::theOrderFollowsTheStoredList()
{
    auto *a = new TestProcessor("a", this);
    auto *b = new TestProcessor("b", this);
    m_registry->registerDsp(a, "plug");
    m_registry->registerDsp(b, "plug");

    // Default: registration order (the built-in first).
    QCOMPARE(ids(m_registry),
             QStringList({"silenceskip", "a", "b"}));

    m_settings->setValue("dsp.order", QStringList({"b", "a"}));
    QCOMPARE(ids(m_registry), QStringList({"b", "a", "silenceskip"}));
    // The chain follows the order (the enabled entries, in it).
    const QList<AudioProcessor *> chain = m_registry->chain();
    QCOMPARE(chain.size(), 3);
    QCOMPARE(chain.at(0), static_cast<AudioProcessor *>(b));
    QCOMPARE(chain.at(1), static_cast<AudioProcessor *>(a));
}

void DspRegistryTest::aStoredUnknownIdIsIgnored()
{
    auto *a = new TestProcessor("a", this);
    m_registry->registerDsp(a, "plug");
    m_settings->setValue("dsp.order", QStringList({"ghost", "a"}));
    QCOMPARE(ids(m_registry), QStringList({"a", "silenceskip"}));
}

void DspRegistryTest::aNewDspRunsAfterTheStoredOrder()
{
    auto *a = new TestProcessor("a", this);
    m_registry->registerDsp(a, "plug");
    m_settings->setValue("dsp.order", QStringList({"a"}));

    auto *b = new TestProcessor("b", this);
    m_registry->registerDsp(b, "plug");
    // The stored order keeps "a" first; "b" (not in it) runs after.
    QCOMPARE(ids(m_registry), QStringList({"a", "silenceskip", "b"}));
}

void DspRegistryTest::moveUpAndDownPersistTheOrder()
{
    auto *a = new TestProcessor("a", this);
    auto *b = new TestProcessor("b", this);
    m_registry->registerDsp(a, "plug");
    m_registry->registerDsp(b, "plug");

    QSignalSpy spy(m_registry, &DspRegistry::chainChanged);

    m_registry->moveDown("a");
    QCOMPARE(ids(m_registry), QStringList({"silenceskip", "b", "a"}));
    QCOMPARE(m_settings->value("dsp.order").toStringList(),
             QStringList({"silenceskip", "b", "a"}));

    m_registry->moveUp("a");
    QCOMPARE(ids(m_registry), QStringList({"silenceskip", "a", "b"}));

    // At the ends: no-ops (no change, no signal).
    const qsizetype count = spy.count();
    m_registry->moveUp("silenceskip");
    m_registry->moveDown("b");
    QCOMPARE(spy.count(), count);
}

void DspRegistryTest::unregisterDropsTheEntryAndKeepsTheStoredState()
{
    auto *p = new TestProcessor("a", this);
    auto *b = new TestProcessor("b", this);
    m_registry->registerDsp(p, "plug");
    m_registry->registerDsp(b, "plug");
    m_registry->setEnabled("a", false);
    // "a" is not at the end (b follows it), so the move writes the
    // stored order.
    m_registry->moveDown("a");

    QSignalSpy spy(m_registry, &DspRegistry::chainChanged);
    m_registry->unregisterDsp("a");
    QCOMPARE(spy.count(), 1);
    QVERIFY(!m_registry->contains("a"));
    QVERIFY(!m_registry->chain().contains(p));
    // The stored state stays (a re-added plugin finds it intact).
    QCOMPARE(m_settings->value("dsp.enabled").toMap().value("a").toBool(),
             false);
    QVERIFY(m_settings->value("dsp.order").toStringList().contains("a"));
}

} // namespace

QTEST_MAIN(DspRegistryTest)
#include "dspregistry_test.moc"
