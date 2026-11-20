#include <QtTest>

#include <QDialog>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>

#include "audioprocessor.h"
#include "dspregistry.h"
#include "dspsettingspage.h"
#include "settingsmanager.h"

namespace
{

// A pass-through processor (the page's concern is the list, not the
// samples).
class TestProcessor : public AudioProcessor
{
public:
    TestProcessor(QString id, QString title,
                  QObject *parent = nullptr)
        : AudioProcessor(parent)
        , m_id(std::move(id))
        , m_title(std::move(title))
    {
    }

    QString id() const override { return m_id; }
    QString title() const override { return m_title; }
    size_t process(float *frames, size_t frameCount, int channels) override
    {
        Q_UNUSED(frames);
        Q_UNUSED(channels);
        return frameCount;
    }

    const QString m_id;
    const QString m_title;
};

int rowOf(DspSettingsPage *page, const QString &id)
{
    QListWidget* list = page->findChild<QListWidget*>("dspList");
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->data(Qt::UserRole).toString() == id)
            return i;
    return -1;
}

class DspSettingsPageTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void theEntriesAreListedInTheChainOrder();
    void aPluginDspCarriesItsOwner();
    void theSwitchWritesThroughTheRegistry();
    void theMoveButtonsRearrangeTheChain();
    void theConfigureButtonOpensTheDspPage();

private:
    SettingsManager *m_settings = nullptr;
    DspRegistry *m_registry = nullptr;
    QTemporaryDir m_dir;
};

void DspSettingsPageTest::init()
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
    m_registry->registerDsp(
        new TestProcessor("p1", "First DSP", this), QStringLiteral("plug"));
    m_registry->registerDsp(new TestProcessor("p2", "Second DSP", this),
                            QStringLiteral("plug"));
}

void DspSettingsPageTest::cleanup()
{
    DspRegistry::reset();
    SettingsManager::reset();
}

void DspSettingsPageTest::theEntriesAreListedInTheChainOrder()
{
    DspSettingsPage page;
    QListWidget* list = page.findChild<QListWidget*>("dspList");
    QCOMPARE(list->count(), 3);  // the built-in and the two test DSPs
    QCOMPARE(list->item(0)->data(Qt::UserRole).toString(),
             QString("silenceskip"));
    QCOMPARE(list->item(1)->data(Qt::UserRole).toString(),
             QString("p1"));
    QCOMPARE(list->item(2)->data(Qt::UserRole).toString(),
             QString("p2"));
    // p1 is a plugin's DSP, so its row carries the owner's title.
    QCOMPARE(list->item(1)->text(), QString("First DSP — plug"));
    // All on by default.
    for (int i = 0; i < list->count(); ++i)
        QCOMPARE(list->item(i)->checkState(), Qt::Checked);
}

void DspSettingsPageTest::aPluginDspCarriesItsOwner()
{
    DspSettingsPage page;
    QListWidget* list = page.findChild<QListWidget*>("dspList");
    // The plugin's DSP lists its owner (the core's does not).
    QCOMPARE(list->item(rowOf(&page, "p1"))->text(),
             QString("First DSP — plug"));
    QCOMPARE(list->item(rowOf(&page, "silenceskip"))->text(),
             QString("Skip leading silence"));
}

void DspSettingsPageTest::theSwitchWritesThroughTheRegistry()
{
    DspSettingsPage page;
    QListWidget* list = page.findChild<QListWidget*>("dspList");

    list->item(rowOf(&page, "p1"))->setCheckState(Qt::Unchecked);
    QVERIFY(!m_registry->isEnabled("p1"));
    QCOMPARE(m_settings->value("dsp.enabled").toMap().value("p1").toBool(),
             false);
    // The untouched entries stay on.
    QVERIFY(m_registry->isEnabled("p2"));

    list->item(rowOf(&page, "p1"))->setCheckState(Qt::Checked);
    QVERIFY(m_registry->isEnabled("p1"));
    QCOMPARE(m_settings->value("dsp.enabled").toMap().value("p1").toBool(),
             true);
}

void DspSettingsPageTest::theMoveButtonsRearrangeTheChain()
{
    DspSettingsPage page;
    QListWidget* list = page.findChild<QListWidget*>("dspList");

    list->setCurrentRow(rowOf(&page, "p1"));
    page.findChild<QPushButton*>("moveDownButton")->click();
    QCOMPARE(rowOf(&page, "p1"), 2);
    QCOMPARE(rowOf(&page, "p2"), 1);
    // The move is written through (the full effective order).
    QCOMPARE(m_settings->value("dsp.order").toStringList(),
             QStringList({"silenceskip", "p2", "p1"}));

    list->setCurrentRow(rowOf(&page, "p1"));
    page.findChild<QPushButton*>("moveUpButton")->click();
    QCOMPARE(m_settings->value("dsp.order").toStringList(),
             QStringList({"silenceskip", "p1", "p2"}));
}

void DspSettingsPageTest::theConfigureButtonOpensTheDspPage()
{
    auto* dspPage = new QLabel("Fake DSP page");
    dspPage->setObjectName("fakeDspPage");
    m_registry->registerDsp(
        new TestProcessor("p3", "Third DSP", this), QString(),
        [dspPage] { return dspPage; });

    DspSettingsPage page;
    QListWidget* list = page.findChild<QListWidget*>("dspList");
    QPushButton* configure = page.findChild<QPushButton*>("configureButton");

    // The DSP with a page: the button is enabled and opens it.
    list->setCurrentRow(rowOf(&page, "p3"));
    QVERIFY(configure->isEnabled());
    configure->click();
    QDialog* dialog = page.findChild<QDialog*>();
    QVERIFY(dialog != nullptr);
    QCOMPARE(dialog->findChild<QLabel*>("fakeDspPage"), dspPage);
    dialog->deleteLater();

    // A DSP without a page: the button stays disabled.
    list->setCurrentRow(rowOf(&page, "p1"));
    QVERIFY(!configure->isEnabled());
}

} // namespace

QTEST_MAIN(DspSettingsPageTest)
#include "dspsettingspage_test.moc"
