#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QSlider>
#include <QToolButton>
#include <QTemporaryDir>

#include <memory>

#include "audioprocessor.h"
#include "dspregistry.h"
#include "equalizerwidget.h"
#include "eq.h"
#include "layoutmanager.h"
#include "settingsmanager.h"
#include "testsupport.h"

namespace
{

QAction *findAction(QMenu *menu, const QString &text)
{
    for (QAction *action : menu->actions())
        if (action->text() == text)
            return action;
    return nullptr;
}

// The SettingsManager's stored gains as plain doubles (the widget's contract
// is the SettingsManager's value, not the slider's position).
QList<double> settingsGains(SettingsManager *settings)
{
    const QVariantList list =
        settings->value(QString::fromLatin1(eq::GainsKey)).toList();
    QList<double> gains;
    for (const QVariant &item : list)
        gains << item.toDouble();
    return gains;
}

// The DSP the FFmpeg plugin would register (id eq::Id): the widget's
// availability and its enable toggle key on the registry, not on the
// samples, so a pass-through stands in for the equalizer.
class FakeEqProcessor : public AudioProcessor
{
public:
    explicit FakeEqProcessor(QObject *parent = nullptr)
        : AudioProcessor(parent)
    {
    }

    QString id() const override
    {
        return QString::fromLatin1(eq::Id);
    }
    QString title() const override { return QStringLiteral("Equalizer"); }
    size_t process(float *frames, size_t frameCount, int channels) override
    {
        Q_UNUSED(frames);
        Q_UNUSED(channels);
        return frameCount;
    }
};

class EqualizerWidgetTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void registeredInLayoutManager();
    void slidersStartFlatAndEnabled();
    void unavailableWithoutTheDriver();
    void movingASliderWritesTheSetting();
    void anExternalChangeMovesTheSliders();
    void resetFlattensAllBands();
    void presetsApplyTheirGains();
    void theEnableToggleWritesTheEnabledSetting();
    void toolbarButtonsDoNotOverlap();
    void storeWidgetCarriesNoWidgetState();
    void contextMenuCarriesTheBaseEntries();

private:
    PlaylistManager *m_pl = nullptr;
    MediaItemStore *m_store = nullptr;
    PlaybackManager *m_pm = nullptr;
    SettingsManager *m_settings = nullptr;
    std::unique_ptr<QTemporaryDir> m_dir;
    FakePlayer m_fake{this};
    EqualizerWidget *m_widget = nullptr;
};

void EqualizerWidgetTest::init()
{
    m_pl = PlaylistManager::getInstance();
    m_store = MediaItemStore::getInstance();
    m_dir = std::make_unique<QTemporaryDir>();
    test::usePrivateDatabase(m_dir->path());
    m_pl->reset();
    m_store->clear();

    m_pm = PlaybackManager::getInstance();
    test::installFakePlayer(m_pm, &m_fake);

    m_settings = SettingsManager::getInstance();
    // Mimic the FFmpeg plugin's initialize(): the EQ's setting exists
    // and its DSP is registered (the switch is the registry's entry).
    QVariantList flat;
    for (int i = 0; i < eq::BandCount; ++i)
        flat << 0.0;
    m_settings->registerSetting(
        { QString::fromLatin1(eq::GainsKey),
          QStringLiteral("Equalizer gains (dB)"), QString(), flat,
          QStringLiteral("ffmpeg"), });
    DspRegistry::getInstance()->registerDsp(new FakeEqProcessor(this),
                                            QStringLiteral("ffmpeg"));

    m_widget = new EqualizerWidget();
    m_widget->show();
    qApp->processEvents();
}

void EqualizerWidgetTest::cleanup()
{
    delete m_widget;
    DspRegistry::getInstance()->unregisterDsp(QString::fromLatin1(eq::Id));
    m_settings->unregisterSetting(QString::fromLatin1(eq::GainsKey));
    m_pm->stop();
    m_pl->reset();
    m_store->clear();
}

void EqualizerWidgetTest::registeredInLayoutManager()
{
    WidgetConfiguration cfg;
    BaseWidget *w =
        LayoutManager::getInstance()->createWidget(EqualizerWidget::id, cfg);
    QVERIFY(w != nullptr);
    QCOMPARE(w->widgetId(), EqualizerWidget::id);
    QCOMPARE(w->widgetTitle(), EqualizerWidget::title);
    delete w;
}

void EqualizerWidgetTest::slidersStartFlatAndEnabled()
{
    for (int i = 0; i < eq::BandCount; ++i) {
        QSlider *slider =
            m_widget->findChild<QSlider *>(QStringLiteral("bandSlider%1")
                                              .arg(i));
        QVERIFY(slider != nullptr);
        QVERIFY(slider->isEnabled());
        QCOMPARE(slider->value(), 0);
    }
    QToolButton *enable = m_widget->findChild<QToolButton *>("enableButton");
    QVERIFY(enable != nullptr);
    QVERIFY(enable->isEnabled());
    QVERIFY(enable->isChecked());
}

// With the driver not loaded (its DSP not registered) the widget is
// disabled: there is no open PCM path to shape.
void EqualizerWidgetTest::unavailableWithoutTheDriver()
{
    DspRegistry::getInstance()->unregisterDsp(QString::fromLatin1(eq::Id));
    m_settings->unregisterSetting(QString::fromLatin1(eq::GainsKey));
    EqualizerWidget other;
    for (int i = 0; i < eq::BandCount; ++i) {
        QSlider *slider =
            other.findChild<QSlider *>(QStringLiteral("bandSlider%1")
                                          .arg(i));
        QVERIFY(slider != nullptr);
        QVERIFY(!slider->isEnabled());
    }
    QVERIFY(!other.findChild<QToolButton *>("resetButton")->isEnabled());
    QVERIFY(!other.findChild<QToolButton *>("enableButton")->isEnabled());
}

void EqualizerWidgetTest::movingASliderWritesTheSetting()
{
    QSlider *slider = m_widget->findChild<QSlider *>("bandSlider4");
    slider->setValue(8); // 8 * 0.5 dB steps
    QCOMPARE(settingsGains(m_settings).at(4), 4.0);
    QCOMPARE(settingsGains(m_settings).at(0), 0.0);
}

// The other direction: the SettingsManager's changed() (a second widget, the
// config page, the plugin) moves the sliders without an echo write.
void EqualizerWidgetTest::anExternalChangeMovesTheSliders()
{
    QVariantList list =
        m_settings->value(QString::fromLatin1(eq::GainsKey)).toList();
    list[7] = 6.0;
    m_settings->setValue(QString::fromLatin1(eq::GainsKey), list);
    QCOMPARE(m_widget->findChild<QSlider *>("bandSlider7")->value(), 12);
    QCOMPARE(settingsGains(m_settings).at(7), 6.0);
}

void EqualizerWidgetTest::resetFlattensAllBands()
{
    m_widget->findChild<QSlider *>("bandSlider0")->setValue(12);
    m_widget->findChild<QSlider *>("bandSlider9")->setValue(-12);
    m_widget->findChild<QToolButton *>("resetButton")->click();
    for (int i = 0; i < eq::BandCount; ++i)
        QCOMPARE(
            m_widget->findChild<QSlider *>(
                QStringLiteral("bandSlider%1").arg(i))
                ->value(),
            0);
    const QList<double> gains = settingsGains(m_settings);
    QCOMPARE(gains.size(), eq::BandCount);
    for (double gain : gains)
        QCOMPARE(gain, 0.0);
}

void EqualizerWidgetTest::presetsApplyTheirGains()
{
    QMenu *menu =
        m_widget->findChild<QToolButton *>("presetsButton")->menu();
    QAction *bass = findAction(menu, QStringLiteral("Bass boost"));
    QVERIFY(bass != nullptr);
    bass->trigger();
    QCOMPARE(settingsGains(m_settings), QList<double>({6, 6, 4, 2, 0, 0, 0, 0, 0, 0}));
    // The sliders follow the preset.
    QCOMPARE(m_widget->findChild<QSlider *>("bandSlider0")->value(), 12);
    QCOMPARE(m_widget->findChild<QSlider *>("bandSlider4")->value(), 0);
}

void EqualizerWidgetTest::theEnableToggleWritesTheEnabledSetting()
{
    QToolButton *enable = m_widget->findChild<QToolButton *>("enableButton");
    enable->click(); // toggles off
    QVERIFY(!enable->isChecked());
    // The switch is the DSP's entry in the registry (the same state
    // the DSP preferences topic edits), not a setting of its own.
    QVERIFY(!DspRegistry::getInstance()
                 ->isEnabled(QString::fromLatin1(eq::Id)));
}

// The buttons must be laid out in the strip, not just parented to it
// (parenting alone stacks them all at the toolbar's origin and the
// last one built eats every click).
void EqualizerWidgetTest::toolbarButtonsDoNotOverlap()
{
    QToolButton *enable = m_widget->findChild<QToolButton *>("enableButton");
    QToolButton *presets =
        m_widget->findChild<QToolButton *>("presetsButton");
    QToolButton *reset = m_widget->findChild<QToolButton *>("resetButton");
    QVERIFY(enable != nullptr && presets != nullptr && reset != nullptr);

    m_widget->resize(600, 200);
    qApp->processEvents();

    const QList<QRect> rects{enable->geometry(), presets->geometry(),
                             reset->geometry(), };
    for (const QRect &rect : rects)
        QVERIFY(!rect.isEmpty());
    for (int i = 0; i < rects.size(); ++i)
        for (int j = i + 1; j < rects.size(); ++j)
            QVERIFY(!rects[i].intersects(rects[j]));
}

// The gains are a global setting, not per-instance state: a fresh,
// unnamed widget at the default toolbar state stores nothing.
void EqualizerWidgetTest::storeWidgetCarriesNoWidgetState()
{
    QVERIFY(m_widget->storeWidget().isEmpty());
}

void EqualizerWidgetTest::contextMenuCarriesTheBaseEntries()
{
    QMenu *menu = m_widget->widgetContextMenu();
    QVERIFY(findAction(menu, QStringLiteral("Style this widget..."))
            != nullptr);
    QAction *show = findAction(menu, QStringLiteral("Show toolbar"));
    QVERIFY(show != nullptr);
    QVERIFY(show->isCheckable());
    QVERIFY(show->isChecked()); // the toolbar is visible by default
    menu->deleteLater();
}

} // namespace

QTEST_MAIN(EqualizerWidgetTest)
#include "equalizerwidget_test.moc"
