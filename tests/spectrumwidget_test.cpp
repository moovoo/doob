#include <QtTest>
#include <QFile>

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QMenu>
#include <QPixmap>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <array>
#include <cmath>
#include <memory>

#include "layoutmanager.h"
#include "paintrate.h"
#include "settingsmanager.h"
#include "spectrumwidget.h"
#include "stylemanager.h"
#include "testsupport.h"

namespace {

constexpr float kPi = 3.14159265358979323846F;

// A stereo tone: frameCount frames at `freq` (Hz on `rate`), the
// amplitudes per channel (a zero amplitude is a silent channel).
QVector<float> tone(float freq, int frameCount, quint32 rate,
                    float leftAmp = 1.0F, float rightAmp = 1.0F)
{
    QVector<float> out(static_cast<qsizetype>(2 * frameCount));
    for (qsizetype i = 0; i < frameCount; ++i) {
        const float s = std::sin(2.0F * kPi * freq * static_cast<float>(i)
                                 / static_cast<float>(rate));
        out[2 * i] = leftAmp * s;
        out[(2 * i) + 1] = rightAmp * s;
    }
    return out;
}

class SpectrumWidgetTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void registeredInLayoutManager();
    void minimumSize();
    void initiallyIdle();
    void framesAttack();
    void framesAttackLowBand();
    void framesAttackHighBand();
    void subBinBandsAreAlive();
    void windowsCloseOnTheWindowSize();
    void channelSelection();
    void framesDecay();
    void stopClearsBands();
    void rateChangeResetsAccumulation();
    void rendersBar();
    void rendersFillDirections();
    void contextMenuOffersDisplayStyles();
    void displayStyleToggles();
    void storeRestoreDisplayStyle();
    void contextMenuOffersFillDirections();
    void fillDirectionToggles();
    void storeRestoreOrientation();
    void contextMenuOffersChannels();
    void channelToggles();
    void storeRestoreChannel();
    void contextMenuOffersBandCounts();
    void bandCountToggles();
    void storeRestoreBands();
    void stylesTuneBackgroundAndBars();
    void rendersSegments();
    void xBarMarginSetsTheBandSpacing();
    void framesIgnoredWhenNotPlaying();
    void paintRateFollowsTheSetting();
    void paintsAtTheConfiguredRate();
    void releaseTimeIndependentOfPaintRate();

private:
    MediaItem makeItem(quint64 id, const QString &title);
    void startPlayback();
    // One closed window of a `freq` tone (the tap is one window).
    void feedWindow(float freq, quint32 rate = 48000,
                    float leftAmp = 1.0F, float rightAmp = 1.0F);
    QAction* findAction(QMenu* menu, const QString& text) const;

    FakePlayer m_fake{this};
    PlaybackManager* m_pm = nullptr;
    PlaylistManager* m_pl = nullptr;
    MediaItemStore* m_store = nullptr;
    SettingsManager* m_settings = nullptr;
    std::unique_ptr<QTemporaryDir> m_dir;
    SpectrumWidget* m_widget = nullptr;
};

void SpectrumWidgetTest::init()
{
    StyleManager::getInstance()->setStyleSheet(QString());

    m_pl = PlaylistManager::getInstance();
    m_store = MediaItemStore::getInstance();
    m_dir = std::make_unique<QTemporaryDir>();
    test::usePrivateDatabase(m_dir->path());
    m_pl->reset();
    m_store->clear();

    m_pm = PlaybackManager::getInstance();
    test::installFakePlayer(m_pm, &m_fake);

    m_settings = SettingsManager::getInstance();

    m_widget = new SpectrumWidget();
    m_widget->show();
    qApp->processEvents();
}

void SpectrumWidgetTest::cleanup()
{
    delete m_widget;
    StyleManager::getInstance()->setStyleSheet(QString());
    m_settings->resetToDefault(PaintRate::key());
    m_pm->stop();
    m_pl->reset();
    m_store->clear();
}

MediaItem SpectrumWidgetTest::makeItem(quint64 id, const QString &title)
{
    MediaItem item;
    item.id = id;
    item.title = title;
    const QString path = "/tmp/spectrum_" + title + ".mp3";
    {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.write("\x00\x01\x02");
    }
    item.source = QUrl::fromLocalFile(path);
    m_store->remove(id);
    m_store->insert(item);
    return item;
}

void SpectrumWidgetTest::startPlayback()
{
    m_pl->addItems({ makeItem(1, "Track1").id });
    m_pm->playCurrent();
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
}

void SpectrumWidgetTest::feedWindow(float freq, quint32 rate,
                                    float leftAmp, float rightAmp)
{
    m_fake.triggerFrames(tone(freq, SpectrumWidget::kWindowSize, rate,
                              leftAmp, rightAmp),
                         rate);
}

QAction* SpectrumWidgetTest::findAction(QMenu* menu, const QString& text) const
{
    for (QAction* action : menu->actions())
        if (action->text() == text)
            return action;
    return nullptr;
}

void SpectrumWidgetTest::registeredInLayoutManager()
{
    WidgetConfiguration cfg;
    BaseWidget* w = LayoutManager::getInstance()->createWidget(SpectrumWidget::id, cfg);
    QVERIFY(w != nullptr);
    QCOMPARE(w->widgetId(), SpectrumWidget::id);
    QCOMPARE(w->widgetTitle(), SpectrumWidget::title);
    delete w;
}

void SpectrumWidgetTest::minimumSize()
{
    // The spectrum paints its own bars (no children to carry a layout
    // minimum): the floor keeps a splitter from collapsing it to
    // nothing.
    QCOMPARE(m_widget->minimumSize(), SpectrumWidget::kMinimumSize);
    m_widget->resize(0, 0);
    QCOMPARE(m_widget->size(), SpectrumWidget::kMinimumSize);
}

void SpectrumWidgetTest::initiallyIdle()
{
    QCOMPARE(m_widget->bandCount(), SpectrumWidget::kDefaultBands);
    const QVector<float> levels = m_widget->bandLevels();
    QCOMPARE(levels.size(), SpectrumWidget::kDefaultBands);
    for (const float level : levels)
        QCOMPARE(level, 0.0F);
}

// A full-scale 1 kHz tone fills its window: the band containing 1 kHz
// (band 9 on the default 16-band 20 Hz..20 kHz grid) jumps to a high
// level (fast attack), the far bands stay down.
void SpectrumWidgetTest::framesAttack()
{
    startPlayback();

    feedWindow(1000.0F);

    const QVector<float> levels = m_widget->bandLevels();
    QCOMPARE(m_widget->accumulatedSamples(), 0);  // the window closed
    int peak = 0;
    for (int i = 1; i < levels.size(); ++i)
        if (levels[i] > levels[peak])
            peak = i;
    QCOMPARE(peak, 9);
    qWarning() << "1 kHz band too low:" << levels[9];
    QVERIFY(levels[9] > 0.5F);
    QVERIFY(levels[0] < 0.15F);
    QVERIFY(levels[15] < 0.15F);
}

// 93.75 Hz is exactly bin 2 at 48 kHz and lands in the low band (band
// 3 of the default grid — band 2's 47..73 Hz span is narrower than one
// bin at 48 kHz / 1024 samples and sees only bin 2's straddling edge).
void SpectrumWidgetTest::framesAttackLowBand()
{
    startPlayback();

    feedWindow(93.75F);

    const QVector<float> levels = m_widget->bandLevels();
    int peak = 0;
    for (int i = 1; i < levels.size(); ++i)
        if (levels[i] > levels[peak])
            peak = i;
    QCOMPARE(peak, 3);
    QVERIFY(levels[3] > 0.5F);
}

// 15 kHz lands in the top band (band 15 of the default grid).
void SpectrumWidgetTest::framesAttackHighBand()
{
    startPlayback();

    feedWindow(15000.0F);

    const QVector<float> levels = m_widget->bandLevels();
    int peak = 0;
    for (int i = 1; i < levels.size(); ++i)
        if (levels[i] > levels[peak])
            peak = i;
    QCOMPARE(peak, 15);
}

// The low bands of the 16-band grid are narrower than one bin at 48 kHz
// (band 0 spans 20..31 Hz and band 2 spans 47..73 Hz, the 46.9 Hz grid
// holds no whole bin in either): they must not be dead columns. A low
// tone lights them through the bins' overlap with the bands.
void SpectrumWidgetTest::subBinBandsAreAlive()
{
    startPlayback();

    feedWindow(60.0F);

    const QVector<float> levels = m_widget->bandLevels();
    qWarning() << "sub-bin bands dead:" << levels[0] << levels[2];
    QVERIFY(levels[0] > 0.5F);
    QVERIFY(levels[2] > 0.5F);
}

// The taps' frames accumulate into windows of kWindowSize samples: a
// partial tap stays in the accumulator, whole windows close.
void SpectrumWidgetTest::windowsCloseOnTheWindowSize()
{
    startPlayback();

    m_fake.triggerFrames(tone(1000.0F, 512, 48000), 48000);
    QCOMPARE(m_widget->accumulatedSamples(), 512);

    m_fake.triggerFrames(tone(1000.0F, 1024, 48000), 48000);
    QCOMPARE(m_widget->accumulatedSamples(), 512);

    m_fake.triggerFrames(tone(1000.0F, 2560, 48000), 48000);
    QCOMPARE(m_widget->accumulatedSamples(), 0);  // three windows closed
}

// The analyzed channel: a left-only tone lights the Left analysis at
// full level, Mixed at roughly half (the per-sample average), and Right
// not at all.
void SpectrumWidgetTest::channelSelection()
{
    const float leftLevel = [&] {
        m_pm->stop();  // a fresh accumulator and levels
        startPlayback();
        m_widget->setChannel(SpectrumWidget::Channel::Left);
        feedWindow(1000.0F, 48000, 1.0F, 0.0F);
        return m_widget->bandLevels().at(9);
    }();

    m_pm->stop();
    startPlayback();
    m_widget->setChannel(SpectrumWidget::Channel::Mixed);
    feedWindow(1000.0F, 48000, 1.0F, 0.0F);
    const float mixedLevel = m_widget->bandLevels().at(9);
    qWarning() << "mixed level too low:" << mixedLevel;
    QVERIFY(mixedLevel > 0.3F);
    QVERIFY(mixedLevel < leftLevel);

    m_pm->stop();
    startPlayback();
    m_widget->setChannel(SpectrumWidget::Channel::Right);
    feedWindow(1000.0F, 48000, 1.0F, 0.0F);
    for (const float level : m_widget->bandLevels())
        QCOMPARE(level, 0.0F);
}

// While playing the bands decay toward silence on the release timer.
void SpectrumWidgetTest::framesDecay()
{
    startPlayback();
    feedWindow(1000.0F);
    const float peak = m_widget->bandLevels().at(9);
    QVERIFY(peak > 0.0F);

    QTest::qWait(100);  // ~6 release steps

    const float after = m_widget->bandLevels().at(9);
    QVERIFY(after < peak);
    QVERIFY(after > 0.0F);
}

// A stop is silence: the bands clear immediately.
void SpectrumWidgetTest::stopClearsBands()
{
    startPlayback();
    feedWindow(1000.0F);
    QVERIFY(m_widget->bandLevels().at(9) > 0.0F);

    m_pm->stop();

    for (const float level : m_widget->bandLevels())
        QCOMPARE(level, 0.0F);
    QCOMPARE(m_widget->accumulatedSamples(), 0);
}

// A new stream may carry a different sample rate: a partial window of
// the old rate must not mix into the new one (the bin grid changes).
void SpectrumWidgetTest::rateChangeResetsAccumulation()
{
    startPlayback();

    m_fake.triggerFrames(tone(1000.0F, 512, 48000), 48000);
    QCOMPARE(m_widget->accumulatedSamples(), 512);
    QCOMPARE(m_widget->sampleRate(), 48000U);

    m_fake.triggerFrames(tone(1000.0F, 512, 44100), 44100);
    QCOMPARE(m_widget->sampleRate(), 44100U);
    QCOMPARE(m_widget->accumulatedSamples(), 512);  // reset, not 1024
}

// The painted output carries the bar color when a level is present.
void SpectrumWidgetTest::rendersBar()
{
    startPlayback();
    feedWindow(1000.0F);

    m_widget->resize(200, 100);
    qApp->processEvents();
    QPixmap pm(200, 100);
    m_widget->render(&pm);
    const QImage img = pm.toImage();
    const QColor bar = m_widget->palette().color(QPalette::Highlight);
    bool foundBar = false;
    for (int y = 0; y < img.height() && !foundBar; ++y)
        for (int x = 0; x < img.width() && !foundBar; ++x)
            if (img.pixelColor(x, y) == bar)
                foundBar = true;
    QVERIFY(foundBar);
}

// Each fill direction paints toward the expected side. (240x120, 16
// bands: vertical bars are 8px wide at x 6 + 14i (band 9: x 132..139,
// probe x 135), horizontal bars 4px high at y 3 + 7i (band 9: y
// 66..69, probe y 67); the probes sit in the filled / empty halves of
// band 9's fill, computed from its actual level.)
void SpectrumWidgetTest::rendersFillDirections()
{
    startPlayback();
    feedWindow(1000.0F);

    m_widget->resize(240, 120);
    qApp->processEvents();
    const QColor bar = m_widget->palette().color(QPalette::Highlight);
    const float level = m_widget->bandLevels().at(9);
    const int fillV = qRound(level * 120.0F);
    const int fillH = qRound(level * 234.0F);
    QVERIFY(fillV >= 20);
    QVERIFY(fillH >= 40);

    auto isBar = [&](int x, int y) {
        QPixmap pm(240, 120);
        m_widget->render(&pm);
        return pm.toImage().pixelColor(x, y) == bar;
    };

    // The probes sit in the middle of the filled and of the empty half
    // of band 9's slot.
    m_widget->setOrientation(SpectrumWidget::Orientation::Up);
    QVERIFY(isBar(135, 120 - (fillV / 2)));       // filled half
    QVERIFY(!isBar(135, (120 - fillV) / 2));      // empty half

    m_widget->setOrientation(SpectrumWidget::Orientation::Down);
    QVERIFY(isBar(135, fillV / 2));
    QVERIFY(!isBar(135, (120 + fillV) / 2));

    m_widget->setOrientation(SpectrumWidget::Orientation::LeftToRight);
    QVERIFY(isBar(3 + (fillH / 2), 67));
    QVERIFY(!isBar((240 + fillH) / 2, 67));

    m_widget->setOrientation(SpectrumWidget::Orientation::RightToLeft);
    QVERIFY(isBar(237 - (fillH / 2), 67));
    QVERIFY(!isBar((240 - fillH) / 2, 67));
}

// The edit menu offers the two display styles, "Solid" checked (and
// four fill directions, three channels and three band counts).
void SpectrumWidgetTest::contextMenuOffersDisplayStyles()
{
    QMenu* menu = m_widget->widgetContextMenu();
    QVERIFY(menu != nullptr);

    QAction* solid = findAction(menu, "Solid");
    QVERIFY(solid != nullptr && solid->isCheckable() && solid->isChecked());
    QAction* segments = findAction(menu, "Segments");
    QVERIFY(segments != nullptr && segments->isCheckable() && !segments->isChecked());
    int checkable = 0;
    for (QAction* action : menu->actions())
        if (action->isCheckable())
            ++checkable;
    QCOMPARE(checkable, 12);
    menu->deleteLater();
}

// Triggering a display style changes the state; a fresh menu
// reflects it.
void SpectrumWidgetTest::displayStyleToggles()
{
    QMenu* menu = m_widget->widgetContextMenu();
    QAction* segments = findAction(menu, "Segments");
    QVERIFY(segments != nullptr);

    segments->trigger();
    QCOMPARE(m_widget->displayStyle(), SpectrumWidget::Style::Segments);
    menu->deleteLater();

    QMenu* fresh = m_widget->widgetContextMenu();
    QVERIFY(findAction(fresh, "Solid") != nullptr);
    QVERIFY(!findAction(fresh, "Solid")->isChecked());
    QVERIFY(findAction(fresh, "Segments")->isChecked());
    fresh->deleteLater();
}

// The default display style is not persisted; a non-default one
// round-trips; a stale out-of-range value is ignored.
void SpectrumWidgetTest::storeRestoreDisplayStyle()
{
    QVERIFY(!m_widget->storeWidget().contains("displayStyle"));

    m_widget->setDisplayStyle(SpectrumWidget::Style::Segments);
    WidgetConfiguration stored = m_widget->storeWidget();
    QCOMPARE(stored.value("displayStyle").toInt(),
             static_cast<int>(SpectrumWidget::Style::Segments));

    SpectrumWidget restored;
    restored.restoreWidget(stored);
    QCOMPARE(restored.displayStyle(), SpectrumWidget::Style::Segments);

    WidgetConfiguration stale;
    stale["displayStyle"] = 99;
    SpectrumWidget guarded;
    guarded.restoreWidget(stale);
    QCOMPARE(guarded.displayStyle(), SpectrumWidget::Style::Solid);
}

// The edit menu offers the four fill directions, "Fill up" checked.
void SpectrumWidgetTest::contextMenuOffersFillDirections()
{
    QMenu* menu = m_widget->widgetContextMenu();
    QVERIFY(menu != nullptr);

    bool upChecked = false;
    for (QAction* action : menu->actions())
        if (action->text() == "Fill up")
            upChecked = action->isChecked();
    QVERIFY(upChecked);
    menu->deleteLater();
}

// Triggering a direction changes the state; a fresh menu reflects it.
void SpectrumWidgetTest::fillDirectionToggles()
{
    QMenu* menu = m_widget->widgetContextMenu();
    QAction* down = findAction(menu, "Fill down");
    QVERIFY(down != nullptr);

    down->trigger();
    QCOMPARE(static_cast<int>(m_widget->orientation()),
             static_cast<int>(SpectrumWidget::Orientation::Down));
    menu->deleteLater();

    QMenu* fresh = m_widget->widgetContextMenu();
    QVERIFY(findAction(fresh, "Fill up") != nullptr);
    QVERIFY(findAction(fresh, "Fill down") != nullptr);
    QVERIFY(!findAction(fresh, "Fill up")->isChecked());
    QVERIFY(findAction(fresh, "Fill down")->isChecked());
    fresh->deleteLater();
}

// The default orientation is not persisted; a non-default one round-trips;
// a stale out-of-range value is ignored.
void SpectrumWidgetTest::storeRestoreOrientation()
{
    QVERIFY(!m_widget->storeWidget().contains("orientation"));

    m_widget->setOrientation(SpectrumWidget::Orientation::RightToLeft);
    WidgetConfiguration stored = m_widget->storeWidget();
    QCOMPARE(stored.value("orientation").toInt(),
             static_cast<int>(SpectrumWidget::Orientation::RightToLeft));

    SpectrumWidget restored;
    restored.restoreWidget(stored);
    QCOMPARE(static_cast<int>(restored.orientation()),
             static_cast<int>(SpectrumWidget::Orientation::RightToLeft));

    WidgetConfiguration stale;
    stale["orientation"] = 99;
    SpectrumWidget guarded;
    guarded.restoreWidget(stale);
    QCOMPARE(static_cast<int>(guarded.orientation()),
             static_cast<int>(SpectrumWidget::Orientation::Up));
}

// The edit menu offers the three analyzed channels, "Mixed" checked.
void SpectrumWidgetTest::contextMenuOffersChannels()
{
    QMenu* menu = m_widget->widgetContextMenu();
    QVERIFY(menu != nullptr);

    QAction* mixed = findAction(menu, "Mixed");
    QVERIFY(mixed != nullptr && mixed->isCheckable() && mixed->isChecked());
    QAction* left = findAction(menu, "Left");
    QVERIFY(left != nullptr && left->isCheckable() && !left->isChecked());
    QAction* right = findAction(menu, "Right");
    QVERIFY(right != nullptr && right->isCheckable() && !right->isChecked());
    menu->deleteLater();
}

// Triggering a channel changes the state; a fresh menu reflects it.
void SpectrumWidgetTest::channelToggles()
{
    QMenu* menu = m_widget->widgetContextMenu();
    QAction* left = findAction(menu, "Left");
    QVERIFY(left != nullptr);

    left->trigger();
    QCOMPARE(m_widget->channel(), SpectrumWidget::Channel::Left);
    menu->deleteLater();

    QMenu* fresh = m_widget->widgetContextMenu();
    QVERIFY(findAction(fresh, "Mixed") != nullptr);
    QVERIFY(!findAction(fresh, "Mixed")->isChecked());
    QVERIFY(findAction(fresh, "Left")->isChecked());
    fresh->deleteLater();
}

// The default channel is not persisted; a non-default one round-trips;
// a stale out-of-range value is ignored.
void SpectrumWidgetTest::storeRestoreChannel()
{
    QVERIFY(!m_widget->storeWidget().contains("channel"));

    m_widget->setChannel(SpectrumWidget::Channel::Right);
    WidgetConfiguration stored = m_widget->storeWidget();
    QCOMPARE(stored.value("channel").toInt(),
             static_cast<int>(SpectrumWidget::Channel::Right));

    SpectrumWidget restored;
    restored.restoreWidget(stored);
    QCOMPARE(restored.channel(), SpectrumWidget::Channel::Right);

    WidgetConfiguration stale;
    stale["channel"] = 99;
    SpectrumWidget guarded;
    guarded.restoreWidget(stale);
    QCOMPARE(guarded.channel(), SpectrumWidget::Channel::Mixed);
}

// The edit menu offers the three band counts, the default (16) checked.
void SpectrumWidgetTest::contextMenuOffersBandCounts()
{
    QMenu* menu = m_widget->widgetContextMenu();
    QVERIFY(menu != nullptr);

    QAction* eight = findAction(menu, "8 bands");
    QVERIFY(eight != nullptr && eight->isCheckable() && !eight->isChecked());
    QAction* sixteen = findAction(menu, "16 bands");
    QVERIFY(sixteen != nullptr && sixteen->isCheckable() && sixteen->isChecked());
    QAction* thirtyTwo = findAction(menu, "32 bands");
    QVERIFY(thirtyTwo != nullptr && thirtyTwo->isCheckable()
            && !thirtyTwo->isChecked());
    menu->deleteLater();
}

// Triggering a band count changes the state (the levels reset to the
// new grid); a fresh menu reflects it.
void SpectrumWidgetTest::bandCountToggles()
{
    QMenu* menu = m_widget->widgetContextMenu();
    QAction* thirtyTwo = findAction(menu, "32 bands");
    QVERIFY(thirtyTwo != nullptr);

    thirtyTwo->trigger();
    QCOMPARE(m_widget->bandCount(), 32);
    QCOMPARE(m_widget->bandLevels().size(), 32);
    menu->deleteLater();

    QMenu* fresh = m_widget->widgetContextMenu();
    QVERIFY(!findAction(fresh, "16 bands")->isChecked());
    QVERIFY(findAction(fresh, "32 bands")->isChecked());
    fresh->deleteLater();
}

// The default band count is not persisted; a non-default one round-trips;
// a stale value outside the offered counts is ignored.
void SpectrumWidgetTest::storeRestoreBands()
{
    QVERIFY(!m_widget->storeWidget().contains("bands"));

    m_widget->setBandCount(32);
    WidgetConfiguration stored = m_widget->storeWidget();
    QCOMPARE(stored.value("bands").toInt(), 32);

    SpectrumWidget restored;
    restored.restoreWidget(stored);
    QCOMPARE(restored.bandCount(), 32);

    WidgetConfiguration stale;
    stale["bands"] = 99;
    SpectrumWidget guarded;
    guarded.restoreWidget(stale);
    QCOMPARE(guarded.bandCount(), SpectrumWidget::kDefaultBands);
}

// The stylesheet is the styling surface: "background" recolors the
// background (the track follows) and "color" the bars; without a rule
// the theme's roles stand in (the unstyled look).
void SpectrumWidgetTest::stylesTuneBackgroundAndBars()
{
    startPlayback();
    feedWindow(1000.0F);
    m_widget->resize(200, 100);
    qApp->processEvents();

    // (Pixels are compared by rgb(): QColor's operator== is sensitive to
    // the internal representation, equal colors can fail it.)
    // (200x100, 16 bands: margin 5, the bars are 7px wide at x
    // 5 + 12i — band 9 (the 1 kHz tone) spans x 113..119, band 0 is
    // x 5..11 (silent: track only).)
    QPixmap pm(200, 100);
    m_widget->render(&pm);
    const QImage img = pm.toImage();
    QCOMPARE(img.pixelColor(116, 80).rgb(),
             m_widget->palette().color(QPalette::Highlight).rgb());
    QCOMPARE(img.pixelColor(8, 20).rgb(),
             m_widget->palette().color(QPalette::Base).lighter(120).rgb());

    StyleManager::getInstance()->setStyleSheet(
        "SpectrumWidget { background: #101020; color: #00ff00; }");
    qApp->processEvents();
    pm = QPixmap(200, 100);
    m_widget->render(&pm);
    const QImage styled = pm.toImage();
    QCOMPARE(styled.pixelColor(116, 80).rgb(), QColor("#00ff00").rgb());    // the bar
    QCOMPARE(styled.pixelColor(2, 50).rgb(), QColor("#101020").rgb());      // the background
    QCOMPARE(styled.pixelColor(8, 20).rgb(),
             QColor("#101020").lighter(120).rgb());                         // the track follows

    // Clearing the sheet restores the unstyled look.
    StyleManager::getInstance()->setStyleSheet(QString());
    qApp->processEvents();
    pm = QPixmap(200, 100);
    m_widget->render(&pm);
    QCOMPARE(pm.toImage().pixelColor(116, 80).rgb(),
             m_widget->palette().color(QPalette::Highlight).rgb());
}

// The "Segments" display style splits each bar into discrete blocks
// along the fill axis: the blocks the level reaches are lit, the rest
// carry the track color, and a 1px gap separates the blocks.
void SpectrumWidgetTest::rendersSegments()
{
    startPlayback();
    feedWindow(1000.0F);
    m_widget->setDisplayStyle(SpectrumWidget::Style::Segments);

    m_widget->resize(240, 120);
    qApp->processEvents();
    StyleManager::getInstance()->setStyleSheet(QStringLiteral(
        "SpectrumWidget { background: #202020; color: #00ff00;\n"
        "    x-segments: 4; }"));
    qApp->processEvents();

    // (Pixels are compared by rgb(): QColor's operator== is sensitive
    // to the internal representation, equal colors can fail it.)
    // (240x120, 16 bands: margin 6, band 9 (the 1 kHz tone) spans x
    // 132..139, probe x 135; four 30px blocks fill the 120px height
    // from the bottom: y 90..119, 60..88, 30..58, 0..28, the 1px gaps
    // at y 89, 59 and 29. The lit count follows the level.)
    const float level = m_widget->bandLevels().at(9);
    const int lit = qRound(level * 4.0F);
    QPixmap pm(240, 120);
    m_widget->render(&pm);
    const QImage img = pm.toImage();
    const uint bar = QColor("#00ff00").rgb();
    const uint track = QColor("#202020").lighter(120).rgb();
    const uint bg = QColor("#202020").rgb();
    const std::array<int, 4> probeY = {105, 75, 45, 15};  // one per block, bottom up
    for (int k = 0; k < 4; ++k)
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        QCOMPARE(img.pixelColor(135, probeY[k]).rgb(),
                 k < lit ? bar : track);
    QCOMPARE(img.pixelColor(135, 89).rgb(), bg);  // the gap
}

// "x-bar-margin" sets the space between and around the bands (the auto
// share when unset): the bands' slots move with it.
void SpectrumWidgetTest::xBarMarginSetsTheBandSpacing()
{
    startPlayback();
    feedWindow(1000.0F);
    m_widget->resize(200, 100);
    qApp->processEvents();

    // (Pixels are compared by rgb(): QColor's operator== is sensitive
    // to the internal representation, equal colors can fail it.)
    // Unset: the auto margin is max(2, 200/40) = 5, 16 bands 7px wide
    // at x 5 + 12i — band 9 (the 1 kHz tone) spans x 113..119.
    QPixmap pm(200, 100);
    m_widget->render(&pm);
    const uint bar = m_widget->palette().color(QPalette::Highlight).rgb();
    const uint bg = m_widget->palette().color(QPalette::Base).rgb();
    QCOMPARE(pm.toImage().pixelColor(116, 50).rgb(), bar);  // band 9
    QCOMPARE(pm.toImage().pixelColor(112, 50).rgb(), bg);   // the margin

    // Set: margin 10px (the "px" is a unit, like the sheet's lengths)
    // — the bands shift (band 9 spans x 109..109), the auto position is
    // background again.
    StyleManager::getInstance()->setStyleSheet(
        "SpectrumWidget { x-bar-margin: 10px; }");
    qApp->processEvents();
    pm = QPixmap(200, 100);
    m_widget->render(&pm);
    QCOMPARE(pm.toImage().pixelColor(109, 50).rgb(), bar);
    QCOMPARE(pm.toImage().pixelColor(116, 50).rgb(), bg);
    StyleManager::getInstance()->setStyleSheet(QString());
}

// The tap is latency-shifted, so a queued tap can land after a pause
// or a stop: without playback the spectrum stays down.
void SpectrumWidgetTest::framesIgnoredWhenNotPlaying()
{
    m_fake.triggerFrames(tone(1000.0F, SpectrumWidget::kWindowSize, 48000),
                         48000);
    m_widget->resize(200, 100);
    qApp->processEvents();

    for (const float level : m_widget->bandLevels())
        QCOMPARE(level, 0.0F);

    QPixmap pm(200, 100);
    m_widget->render(&pm);
    const QImage img = pm.toImage();
    // (Compared by rgb(): QColor's operator== is sensitive to the
    // internal representation, equal colors can fail it.) No bar: no
    // pixel carries the bar's color.
    const uint barRgb = m_widget->palette().color(QPalette::Highlight).rgb();
    bool foundBar = false;
    for (int y = 0; y < img.height() && !foundBar; ++y)
        for (int x = 0; x < img.width() && !foundBar; ++x)
            if (img.pixelColor(x, y).rgb() == barRgb)
                foundBar = true;
    QVERIFY(!foundBar);
}

// The paint timer runs at the configured rate, and only while playing.
void SpectrumWidgetTest::paintRateFollowsTheSetting()
{
    // The default rate paces the timer...
    QCOMPARE(m_widget->paintIntervalMs(), 1000 / PaintRate::defaultFps());
    // ...and the timer only runs while playing.
    QVERIFY(!m_widget->paintTimerActive());

    // A change to the setting re-paces the live timer.
    m_settings->setValue(PaintRate::key(), 30);
    QCOMPARE(m_widget->paintIntervalMs(), 1000 / 30);

    startPlayback();
    QVERIFY(m_widget->paintTimerActive());
    m_pm->stop();
    QVERIFY(!m_widget->paintTimerActive());
}

// While playing the widget paints at the configured rate, not at the
// taps' rhythm: the taps feed a window at ~50/s, the paint stays at
// the setting's 20/s.
void SpectrumWidgetTest::paintsAtTheConfiguredRate()
{
    startPlayback();
    m_settings->setValue(PaintRate::key(), 20);
    m_widget->resize(300, 100);
    qApp->processEvents();

    PaintCounter counter;
    m_widget->installEventFilter(&counter);

    const int windowMs = 1000;
    int waited = 0;
    while (waited < windowMs) {
        feedWindow(1000.0F);
        QTest::qWait(20);
        waited += 20;
    }
    m_widget->removeEventFilter(&counter);

    // The paint stayed at the configured rate.
    qInfo() << "paints in 1 s at the 20 fps setting:" << counter.count;
    QVERIFY2(counter.count >= 15 && counter.count <= 26,
             "expected ~20 paints per second");
}

// The release time is per unit of time, not per tick: at a lower paint
// rate the ticks are coarser, but the bands still fall in the same
// time.
void SpectrumWidgetTest::releaseTimeIndependentOfPaintRate()
{
    startPlayback();
    m_widget->resize(300, 100);
    qApp->processEvents();

    // The fall time from a full peak to silence. (The test macros
    // expand to a bare return and cannot live in a helper that
    // returns a value, so the level checks stay in the test.)
    auto fallToZero = [this] {
        feedWindow(1000.0F);
        QElapsedTimer clock;
        clock.start();
        int waited = 0;
        while (m_widget->bandLevels().at(9) > 0.0F && waited < 5000) {
            QTest::qWait(10);
            waited += 10;
        }
        return clock.elapsed();
    };

    m_settings->setValue(PaintRate::key(), 60);
    const qint64 fallAt60 = fallToZero();
    QCOMPARE(m_widget->bandLevels().at(9), 0.0F);

    m_settings->setValue(PaintRate::key(), 10);
    const qint64 fallAt10 = fallToZero();
    QCOMPARE(m_widget->bandLevels().at(9), 0.0F);

    // The same within the timer granularity (the coarser the ticks,
    // the more the last step can overshoot: a 10 fps tick is 100 ms
    // wide).
    qInfo() << "release time at 60 fps:" << fallAt60 << "ms, at 10 fps:"
            << fallAt10 << "ms";
    QVERIFY2(qAbs(fallAt60 - fallAt10) <= 200,
             "the release time must not depend on the paint rate");
}

}  // namespace

QTEST_MAIN(SpectrumWidgetTest)
#include "spectrumwidget_test.moc"
