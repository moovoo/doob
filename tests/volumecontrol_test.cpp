#include <QtTest>

#include <QSlider>
#include <QLabel>
#include <QToolButton>
#include <QFontMetrics>
#include <QIcon>
#include <QTemporaryDir>

#include <memory>

#include "layoutmanager.h"
#include "settingsmanager.h"
#include "volumecontrol.h"
#include "testsupport.h"

namespace {

// QIcon::operator== is unreliable for a freshly constructed icon (the
// pixmap cache is still empty), so compare the rendered images instead.
QImage iconImage(const QIcon &icon)
{
    return icon.pixmap(QSize(24, 24)).toImage();
}

class VolumeControlTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void registeredInLayoutManager();
    void initialStateFullAndEnabled();
    void sliderSetsVolumeOnManagerAndPlayer();
    void managerVolumeChangeMovesSlider();
    void muteSilencesPlayerKeepingSlider();
    void sliderWhileMutedUnmutes();
    void freshWidgetSyncsFromManager();
    void unsupportedDriverDisablesControls();
    void driverSwitchUpdatesSupportAndAppliesVolume();
    void volumeIsClampedToRange();
    void sliderAndLabelKeepStableSizes();
    void settingsEditUpdatesTheWidget();
    void volumeAndMutePersistInEngine();
    void muteButtonUsesSvgIcon();

private:
    FakePlayer m_fake{this};
    // A driver without the volume capability.
    FakePlayer m_quiet{this};
    PlaybackManager* m_pm = nullptr;
    PlaylistManager* m_pl = nullptr;
    MediaItemStore* m_store = nullptr;
    // Both managers are write-through: keep their database private to this
    // test (a shared default-path file would collide on the explicit ids).
    std::unique_ptr<QTemporaryDir> m_dir;
    VolumeControlWidget* m_widget = nullptr;
    QToolButton* m_muteButton = nullptr;
    QSlider* m_slider = nullptr;
    QLabel* m_label = nullptr;
};

void VolumeControlTest::init()
{
    m_pl = PlaylistManager::getInstance();
    m_store = MediaItemStore::getInstance();
    m_dir = std::make_unique<QTemporaryDir>();
    test::usePrivateDatabase(m_dir->path());
    m_pl->reset();
    m_store->clear();
    // The SettingsManager's store, private to this test's directory
    // (the volume and the mute are registered settings).
    SettingsManager* settings = SettingsManager::getInstance();
    settings->setSettingsPath(m_dir->path() + "/settings.json");
    settings->load();

    m_pm = PlaybackManager::getInstance();
    test::installFakePlayer(m_pm, &m_fake);

    m_widget = new VolumeControlWidget();
    m_widget->show();

    m_muteButton = m_widget->findChild<QToolButton*>("muteButton");
    m_slider = m_widget->findChild<QSlider*>("volumeSlider");
    m_label = m_widget->findChild<QLabel*>("volumeLabel");
    QVERIFY(m_muteButton != nullptr);
    QVERIFY(m_slider != nullptr);
    QVERIFY(m_label != nullptr);
}

void VolumeControlTest::cleanup()
{
    delete m_widget;
    m_pm->setMuted(false);
    m_pm->setVolume(100);
    m_pm->setVolumeBoost(100);
    m_pm->stop();
    m_pm->setPlayer(&m_fake);
    m_pl->reset();
    m_store->clear();
    SettingsManager::reset();
}

void VolumeControlTest::registeredInLayoutManager()
{
    WidgetConfiguration cfg;
    BaseWidget* w = LayoutManager::getInstance()->createWidget(VolumeControlWidget::id, cfg);

    QVERIFY(w != nullptr);
    QCOMPARE(w->widgetId(), VolumeControlWidget::id);
    QCOMPARE(w->widgetTitle(), VolumeControlWidget::title);
    delete w;
}

void VolumeControlTest::initialStateFullAndEnabled()
{
    QCOMPARE(m_slider->value(), 100);
    QCOMPARE(m_slider->maximum(), 100);
    QVERIFY(m_slider->isEnabled());
    QVERIFY(m_muteButton->isEnabled());
    QVERIFY(!m_muteButton->isChecked());
    QCOMPARE(m_label->text(), QString("100%"));
}

void VolumeControlTest::sliderSetsVolumeOnManagerAndPlayer()
{
    m_slider->setValue(60);

    QCOMPARE(m_pm->volume(), 60);
    QCOMPARE(m_fake.m_volume, 0.6F);
    QCOMPARE(m_label->text(), QString("60%"));
}

void VolumeControlTest::managerVolumeChangeMovesSlider()
{
    m_pm->setVolume(30);

    QCOMPARE(m_slider->value(), 30);
    QCOMPARE(m_label->text(), QString("30%"));
}

void VolumeControlTest::muteSilencesPlayerKeepingSlider()
{
    m_pm->setVolume(60);
    m_muteButton->click();

    QVERIFY(m_pm->muted());
    QCOMPARE(m_fake.m_muted, true);
    QCOMPARE(m_pm->volume(), 60);  // the level is kept, not zeroed
    QCOMPARE(m_fake.m_volume, 0.6F);
    QCOMPARE(m_slider->value(), 60);
    QCOMPARE(m_label->text(), QString("Muted"));

    m_muteButton->click();

    QVERIFY(!m_pm->muted());
    QCOMPARE(m_fake.m_muted, false);
    QCOMPARE(m_fake.m_volume, 0.6F);
    QCOMPARE(m_label->text(), QString("60%"));
}

void VolumeControlTest::sliderWhileMutedUnmutes()
{
    m_pm->setMuted(true);
    QCOMPARE(m_label->text(), QString("Muted"));

    m_slider->setValue(50);

    QVERIFY(!m_pm->muted());
    QCOMPARE(m_pm->volume(), 50);
    QCOMPARE(m_fake.m_volume, 0.5F);
    QVERIFY(!m_muteButton->isChecked());
    QCOMPARE(m_label->text(), QString("50%"));
}

void VolumeControlTest::freshWidgetSyncsFromManager()
{
    m_pm->setVolume(80);
    m_pm->setMuted(true);

    // The init() widget was built before the changes, so it is stale. A
    // freshly created widget must reflect the live state immediately.
    VolumeControlWidget fresh;
    QToolButton* button = fresh.findChild<QToolButton*>("muteButton");
    QSlider* slider = fresh.findChild<QSlider*>("volumeSlider");
    QLabel* label = fresh.findChild<QLabel*>("volumeLabel");
    QVERIFY(button != nullptr);
    QVERIFY(slider != nullptr);
    QVERIFY(label != nullptr);

    QCOMPARE(slider->value(), 80);
    QVERIFY(button->isChecked());
    QCOMPARE(label->text(), QString("Muted"));
}

void VolumeControlTest::unsupportedDriverDisablesControls()
{
    m_quiet.m_supportsVolume = false;
    m_pm->setPlayer(&m_quiet);

    QVERIFY(!m_pm->volumeSupported());
    QVERIFY(!m_slider->isEnabled());
    QVERIFY(!m_muteButton->isEnabled());
    // The state is kept (and the label stays readable) — only the
    // controls go grey.
    QCOMPARE(m_pm->volume(), 100);
    QCOMPARE(m_label->text(), QString("100%"));
}

void VolumeControlTest::driverSwitchUpdatesSupportAndAppliesVolume()
{
    m_pm->setVolume(20);
    m_quiet.m_supportsVolume = false;
    m_pm->setPlayer(&m_quiet);
    QVERIFY(!m_slider->isEnabled());

    m_pm->setPlayer(&m_fake);

    QVERIFY(m_pm->volumeSupported());
    QVERIFY(m_slider->isEnabled());
    QVERIFY(m_muteButton->isEnabled());
    // The fresh driver starts at the current level.
    QCOMPARE(m_fake.m_volume, 0.2F);
}

void VolumeControlTest::volumeIsClampedToRange()
{
    m_pm->setVolume(150);
    QCOMPARE(m_pm->volume(), 100);
    QCOMPARE(m_slider->value(), 100);

    m_pm->setVolume(-5);
    QCOMPARE(m_pm->volume(), 0);
    QCOMPARE(m_slider->value(), 0);
    QCOMPARE(m_label->text(), QString("0%"));
}

void VolumeControlTest::sliderAndLabelKeepStableSizes()
{
    // The slider is the row's only stretchable part: it must keep a
    // usable width in a collapsed widget (an unmovable handle is
    // unusable, and there is nothing else left to drag).
    QVERIFY(m_slider->minimumWidth() > 0);

    // The label must not resize with its text ("5%" .. "100%" ..
    // "Muted") while sliding — that would steal space from the slider
    // under the cursor: its minimum covers the widest text it shows.
    const QFontMetrics fm(m_label->font());
    const int widest = qMax(fm.horizontalAdvance(QStringLiteral("Muted")),
                            fm.horizontalAdvance(QStringLiteral("100%")));
    QCOMPARE(m_label->minimumWidth(), widest);
    // The minimum must be binding: the usual texts are narrower than it.
    QVERIFY(fm.horizontalAdvance(QStringLiteral("5%")) < widest);

    // And the width really does not move while the value does.
    const int before = m_label->minimumWidth();
    m_pm->setVolume(5);
    QCOMPARE(m_label->text(), QString("5%"));
    QCOMPARE(m_label->minimumWidth(), before);
    m_pm->setMuted(true);
    QCOMPARE(m_label->text(), QString("Muted"));
    QCOMPARE(m_label->minimumWidth(), before);
}

void VolumeControlTest::settingsEditUpdatesTheWidget()
{
    // The live settings browser writes the SettingsManager directly: the
    // widget must follow (loadSettings() binds the manager to the test's
    // SettingsManager).
    m_pm->loadSettings();

    SettingsManager::getInstance()->setValue("playback.volume", 80);
    QCOMPARE(m_slider->value(), 80);
    QCOMPARE(m_label->text(), QString("80%"));

    SettingsManager::getInstance()->setValue("playback.muted", true);
    QVERIFY(m_muteButton->isChecked());
    QCOMPARE(m_label->text(), QString("Muted"));
}

void VolumeControlTest::volumeAndMutePersistInEngine()
{
    SettingsManager* settings = SettingsManager::getInstance();

    m_pm->setVolume(77);
    m_pm->setMuted(true);

    QVERIFY(settings->isModified("playback.volume"));
    QCOMPARE(settings->value("playback.volume").toInt(), 77);
    QVERIFY(settings->isModified("playback.muted"));
    QCOMPARE(settings->value("playback.muted").toBool(), true);
}

void VolumeControlTest::muteButtonUsesSvgIcon()
{
    // Icon-only button: no unicode caption.
    QVERIFY(m_muteButton->text().isEmpty());
    QCOMPARE(iconImage(m_muteButton->icon()), iconImage(QIcon(":/icons/volume.svg")));

    // The icon swaps with the mute state, and back.
    m_pm->setMuted(true);
    QCOMPARE(iconImage(m_muteButton->icon()), iconImage(QIcon(":/icons/volume-off.svg")));

    m_pm->setMuted(false);
    QCOMPARE(iconImage(m_muteButton->icon()), iconImage(QIcon(":/icons/volume.svg")));
}
}

QTEST_MAIN(VolumeControlTest)

#include "volumecontrol_test.moc"
