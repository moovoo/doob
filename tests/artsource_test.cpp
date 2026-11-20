#include <QtTest>

#include <QSignalSpy>
#include <QTemporaryDir>

#include "artsource.h"
#include "settingsmanager.h"

namespace {

// A source that answers its fetches with canned bytes: the registry's
// concern is the id and the registration, not the lookup.
class TestSource : public ArtSource
{
public:
    explicit TestSource(QString id, QObject *parent = nullptr)
        : ArtSource(parent)
        , m_id(std::move(id))
    {
    }

    QString id() const override { return m_id; }
    QString title() const override
    {
        return m_id + QStringLiteral(" title");
    }
    void fetch(const MediaItem &item, ArtKind kind,
               const Callback &cb) override
    {
        Q_UNUSED(item);
        Q_UNUSED(kind);
        ++m_fetchCount;
        cb(m_mime, m_data);
    }

    const QString m_id;
    int m_fetchCount = 0;
    QString m_mime;
    QByteArray m_data;
};

class ArtRegistryTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void theBuiltinIsRegistered();
    void aRegistrationIsFoundByItsId();
    void aDuplicateIdIsRefused();
    void aNullSourceIsRefused();
    void theOwnerRidesWithTheEntry();
    void unregisterDropsTheEntry();
    void anUnregisteredIdIsNotFound();
    void sourcesChangedFiresOnRegistrationAndUnregistration();

private:
    ArtRegistry* m_registry = nullptr;
};

void ArtRegistryTest::init()
{
    // The SettingsManager first (the built-in's fetch path reads
    // through it), the registry reset so the ctor re-runs (re-registering
    // the built-in against the fresh state).
    SettingsManager::reset();
    SettingsManager* settings = SettingsManager::getInstance();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    settings->setSettingsPath(dir.path() + "/settings.json");
    settings->load();

    ArtRegistry::reset();
    m_registry = ArtRegistry::getInstance();
}

void ArtRegistryTest::cleanup()
{
    ArtRegistry::reset();
}

void ArtRegistryTest::theBuiltinIsRegistered()
{
    QVERIFY(m_registry->contains(QStringLiteral("itunes")));
    QVERIFY(m_registry->source(QStringLiteral("itunes")) != nullptr);
    // The built-in is the core's: no owner.
    QCOMPARE(m_registry->owner(QStringLiteral("itunes")), QString());
}

void ArtRegistryTest::aRegistrationIsFoundByItsId()
{
    auto* source = new TestSource(QStringLiteral("fake"), this);
    QVERIFY(m_registry->registerSource(source, QStringLiteral("plug")));
    QVERIFY(m_registry->contains(QStringLiteral("fake")));
    QCOMPARE(m_registry->source(QStringLiteral("fake")),
             static_cast<ArtSource*>(source));

    // The options carry the (id, title) pair, the built-in first
    // (registration order).
    const QList<QPair<QString, QString>> options = m_registry->options();
    QCOMPARE(options.size(), 2);
    QCOMPARE(options.first().first, QString("itunes"));
    QCOMPARE(options.last(),
             qMakePair(QString("fake"), QString("fake title")));
}

void ArtRegistryTest::aDuplicateIdIsRefused()
{
    auto* first = new TestSource(QStringLiteral("dup"), this);
    QVERIFY(m_registry->registerSource(first, QString()));
    auto* second = new TestSource(QStringLiteral("dup"), this);
    const bool refused = !m_registry->registerSource(second, QString());
    delete second; // the refusal is non-owning: the test keeps it
    QVERIFY(refused);
    QCOMPARE(m_registry->source(QStringLiteral("dup")),
             static_cast<ArtSource*>(first));
    // The built-in's id is taken too.
    QVERIFY(!m_registry->registerSource(new TestSource(QStringLiteral("itunes"), this),
                                        QString()));
}

void ArtRegistryTest::aNullSourceIsRefused()
{
    QVERIFY(!m_registry->registerSource(nullptr, QString()));
}

void ArtRegistryTest::theOwnerRidesWithTheEntry()
{
    auto* source = new TestSource(QStringLiteral("owned"), this);
    QVERIFY(m_registry->registerSource(source, QStringLiteral("plug")));
    QCOMPARE(m_registry->owner(QStringLiteral("owned")), QString("plug"));
    // The core's entries have no owner.
    QCOMPARE(m_registry->owner(QStringLiteral("itunes")), QString());
}

void ArtRegistryTest::unregisterDropsTheEntry()
{
    auto* source = new TestSource(QStringLiteral("gone"), this);
    QVERIFY(m_registry->registerSource(source, QString()));
    QVERIFY(m_registry->contains(QStringLiteral("gone")));

    m_registry->unregisterSource(QStringLiteral("gone"));
    QVERIFY(!m_registry->contains(QStringLiteral("gone")));
    QVERIFY(m_registry->source(QStringLiteral("gone")) == nullptr);
    QCOMPARE(m_registry->owner(QStringLiteral("gone")), QString());
}

void ArtRegistryTest::anUnregisteredIdIsNotFound()
{
    QVERIFY(m_registry->source(QStringLiteral("nope")) == nullptr);
    QCOMPARE(m_registry->owner(QStringLiteral("nope")), QString());
}

void ArtRegistryTest::sourcesChangedFiresOnRegistrationAndUnregistration()
{
    QSignalSpy spy(m_registry, &ArtRegistry::sourcesChanged);
    auto* source = new TestSource(QStringLiteral("fired"), this);
    QVERIFY(m_registry->registerSource(source, QString()));
    QCOMPARE(spy.count(), 1);
    m_registry->unregisterSource(QStringLiteral("fired"));
    QCOMPARE(spy.count(), 2);
}

}

QTEST_MAIN(ArtRegistryTest)
#include "artsource_test.moc"
