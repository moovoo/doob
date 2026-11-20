#include <QtTest>
#include <QFile>

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QMenu>
#include <QPixmap>
#include <QTemporaryDir>
#include <QUrl>

#include <cmath>
#include <memory>

#include "layoutmanager.h"
#include "paintrate.h"
#include "settingsmanager.h"
#include "spectrogramwidget.h"
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

// The test's geometry: 200x100 at 48 kHz. The 100-row grid spans 0..20
// kHz linearly (200 Hz per row, the top row the highest), so row 97
// (the 500 Hz row) samples bin 5 of the default (512) window's grid —
// and 468.75 Hz is exactly bin 5 (93.75 Hz per bin), a full-scale tone
// there lands on a bin center.
constexpr int kW = 200;
constexpr int kH = 100;
constexpr float kToneHz = 468.75F;
constexpr int kToneRow = 97;

// The row-major index of the image buffer (the widget's own size, kW x
// kH).
int cell(int row, int col)
{
    return (row * kW) + col;
}

class SpectrogramWidgetTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void registeredInLayoutManager();
    void minimumSize();
    void initiallyIdle();
    void windowLightsTheToneRow();
    void scrollShiftsTheImageLeft();
    void sweepRewritesAtTheLine();
    void sweepDecaysTheImage();
    void pauseStopFreezeTheImage();
    void rateChangeResetsAccumulation();
    void resizeDebouncePreservesTheImage();
    void resizeResetsTheDebounce();
    void contextMenuOffersModes();
    void modeToggles();
    void storeRestoreMode();
    void contextMenuOffersWindowSizes();
    void windowSizeOptionWorks();
    void windowSizeChangePreservesTheImage();
    void storeRestoreWindowSize();
    void rendersTheToneRow();
    void xColorMapColorsTheGradient();
    void xFloorDbSetsTheContrast();
    void framesIgnoredWhenNotPlaying();
    void paintRateFollowsTheSetting();

private:
    MediaItem makeItem(quint64 id, const QString &title);
    void startPlayback();
    // One closed window of a `freq` tone (the tap is one window, `n`
    // samples wide).
    void feedWindow(float freq, quint32 rate = 48000,
                    float leftAmp = 1.0F, float rightAmp = 1.0F,
                    int n = SpectrogramWidget::kDefaultWindowSize);
    QAction* findAction(QMenu* menu, const QString& text) const;

    FakePlayer m_fake{this};
    PlaybackManager* m_pm = nullptr;
    PlaylistManager* m_pl = nullptr;
    MediaItemStore* m_store = nullptr;
    SettingsManager* m_settings = nullptr;
    std::unique_ptr<QTemporaryDir> m_dir;
    SpectrogramWidget* m_widget = nullptr;
};

void SpectrogramWidgetTest::init()
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

    m_widget = new SpectrogramWidget();
    // The resize before the show: a resize with no image to preserve
    // takes the size at once (the show would size the top-level to its
    // minimum, and the explicit size after it would only be debounced
    // behind the image the minimum built).
    m_widget->resize(kW, kH);
    m_widget->show();
    qApp->processEvents();
}

void SpectrogramWidgetTest::cleanup()
{
    delete m_widget;
    StyleManager::getInstance()->setStyleSheet(QString());
    m_settings->resetToDefault(PaintRate::key());
    m_pm->stop();
    m_pl->reset();
    m_store->clear();
}

MediaItem SpectrogramWidgetTest::makeItem(quint64 id, const QString &title)
{
    MediaItem item;
    item.id = id;
    item.title = title;
    const QString path = "/tmp/spectrogram_" + title + ".mp3";
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

void SpectrogramWidgetTest::startPlayback()
{
    m_pl->addItems({ makeItem(1, "Track1").id });
    m_pm->playCurrent();
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
}

void SpectrogramWidgetTest::feedWindow(float freq, quint32 rate,
                                       float leftAmp, float rightAmp,
                                       int n)
{
    m_fake.triggerFrames(tone(freq, n, rate, leftAmp, rightAmp), rate);
}

QAction* SpectrogramWidgetTest::findAction(QMenu* menu, const QString& text) const
{
    for (QAction* action : menu->actions())
        if (action->text() == text)
            return action;
    return nullptr;
}

void SpectrogramWidgetTest::registeredInLayoutManager()
{
    WidgetConfiguration cfg;
    BaseWidget* w =
        LayoutManager::getInstance()->createWidget(SpectrogramWidget::id, cfg);
    QVERIFY(w != nullptr);
    QCOMPARE(w->widgetId(), SpectrogramWidget::id);
    QCOMPARE(w->widgetTitle(), SpectrogramWidget::title);
    delete w;
}

void SpectrogramWidgetTest::minimumSize()
{
    // The spectrogram paints its own image (no children to carry a
    // layout minimum): the floor keeps a splitter from collapsing it to
    // nothing.
    QCOMPARE(m_widget->minimumSize(), SpectrogramWidget::kMinimumSize);
    m_widget->resize(0, 0);
    QCOMPARE(m_widget->size(), SpectrogramWidget::kMinimumSize);
}

void SpectrogramWidgetTest::initiallyIdle()
{
    QCOMPARE(m_widget->displayMode(), SpectrogramWidget::Mode::Scroll);
    QCOMPARE(m_widget->sweepX(), 0);
    QCOMPARE(m_widget->accumulatedSamples(), 0);
    QVERIFY(!m_widget->paintTimerActive());

    // The image is the widget's own size (the resolution is the
    // display's) and starts silent.
    const QVector<float> image = m_widget->image();
    QCOMPARE(image.size(), kW * kH);
    for (const float level : image)
        QCOMPARE(level, 0.0F);
}

// A full-scale tone on a bin center fills its window: the row that
// samples the tone's bin (row 97) jumps to a high intensity in the new
// column (the right edge, Scroll mode), the far rows and the previous
// columns stay down.
void SpectrogramWidgetTest::windowLightsTheToneRow()
{
    startPlayback();

    feedWindow(kToneHz);

    QCOMPARE(m_widget->accumulatedSamples(), 0);  // the window closed
    const QVector<float> image = m_widget->image();
    QVERIFY(image.at(cell(kToneRow, kW - 1)) > 0.9F);
    QCOMPARE(image.at(cell(50, kW - 1)), 0.0F);  // a far row
    QCOMPARE(image.at(cell(kToneRow, kW - 2)), 0.0F);  // the column before
}

// Scroll mode: each closed window appends its column at the right edge
// and the image shifts left — a silent window lands a dark column.
void SpectrogramWidgetTest::scrollShiftsTheImageLeft()
{
    startPlayback();

    feedWindow(kToneHz);
    QVERIFY(m_widget->image().at(cell(kToneRow, kW - 1)) > 0.9F);

    feedWindow(kToneHz, 48000, 0.0F, 0.0F);  // a silent window

    QVERIFY(m_widget->image().at(cell(kToneRow, kW - 2)) > 0.9F);
    QCOMPARE(m_widget->image().at(cell(kToneRow, kW - 1)), 0.0F);
}

// Sweep mode: the line rewrites its column and advances one (wrapping
// at the right edge); the columns it left behind stay until the decay
// takes them.
void SpectrogramWidgetTest::sweepRewritesAtTheLine()
{
    startPlayback();
    m_widget->setDisplayMode(SpectrogramWidget::Mode::Sweep);

    feedWindow(kToneHz);
    QCOMPARE(m_widget->sweepX(), 1);
    QVERIFY(m_widget->image().at(cell(kToneRow, 0)) > 0.9F);

    feedWindow(kToneHz);
    QCOMPARE(m_widget->sweepX(), 2);
    QVERIFY(m_widget->image().at(cell(kToneRow, 1)) > 0.9F);
    QVERIFY(m_widget->image().at(cell(kToneRow, 0)) > 0.5F);
}

// Sweep mode: between the line's passes the whole image decays toward
// silence on the paint ticks (the persistence, a 2 s half-life).
void SpectrogramWidgetTest::sweepDecaysTheImage()
{
    startPlayback();
    m_widget->setDisplayMode(SpectrogramWidget::Mode::Sweep);

    feedWindow(kToneHz);
    const float before = m_widget->image().at(cell(kToneRow, 0));
    QVERIFY(before > 0.9F);

    QTest::qWait(300);  // a few decay steps

    const float after = m_widget->image().at(cell(kToneRow, 0));
    qInfo() << "trail after 300 ms:" << after << "of" << before;
    QVERIFY2(after < before, "the trail must decay");
    QVERIFY2(after > 0.5F, "300 ms is less than the 2 s half-life");
}

// A pause freezes the image (the taps and the paint timer stop with
// the state — the waterfall stands still, it is not cleared) and a
// stop does the same; the next play continues over the history. The
// tap's partial window is dropped either way (the old stream's tail
// must not mix into the next track's first column).
void SpectrogramWidgetTest::pauseStopFreezeTheImage()
{
    startPlayback();
    feedWindow(kToneHz);
    QVERIFY(m_widget->image().at(cell(kToneRow, kW - 1)) > 0.9F);

    m_pm->pause();
    QVERIFY(m_widget->image().at(cell(kToneRow, kW - 1)) > 0.9F);
    QCOMPARE(m_widget->accumulatedSamples(), 0);
    QVERIFY(!m_widget->paintTimerActive());

    // A resume continues over the history: the next column lands at
    // the right edge, the old one shifted left as before.
    m_pm->togglePlayPause();
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    feedWindow(kToneHz, 48000, 0.0F, 0.0F);  // a silent window
    QVERIFY(m_widget->image().at(cell(kToneRow, kW - 2)) > 0.9F);
    QCOMPARE(m_widget->image().at(cell(kToneRow, kW - 1)), 0.0F);

    m_pm->stop();
    QVERIFY(m_widget->image().at(cell(kToneRow, kW - 2)) > 0.9F);
    QCOMPARE(m_widget->accumulatedSamples(), 0);
    QVERIFY(!m_widget->paintTimerActive());
}

// A new stream may carry a different sample rate: a partial window of
// the old rate must not mix into the new one (the bin grid changes).
void SpectrogramWidgetTest::rateChangeResetsAccumulation()
{
    startPlayback();

    m_fake.triggerFrames(tone(kToneHz, 256, 48000), 48000);
    QCOMPARE(m_widget->accumulatedSamples(), 256);
    QCOMPARE(m_widget->sampleRate(), 48000U);

    m_fake.triggerFrames(tone(kToneHz, 256, 44100), 44100);
    QCOMPARE(m_widget->sampleRate(), 44100U);
    QCOMPARE(m_widget->accumulatedSamples(), 256);  // reset, not 512
}

// A resize does not rebuild the image at once: it stays at its own
// size (the paint stretches it to the widget) until the debounce
// settles, and the settled rebuild resamples the old columns into the
// new grid (the tone survives the re-grid).
void SpectrogramWidgetTest::resizeDebouncePreservesTheImage()
{
    startPlayback();
    feedWindow(kToneHz);
    QVERIFY(m_widget->image().at(cell(kToneRow, kW - 1)) > 0.9F);

    m_widget->resize(kW / 2, kH);
    qApp->processEvents();
    // The rebuild is debounced: the image is still the old one.
    QCOMPARE(m_widget->image().size(), kW * kH);

    QTest::qWait(SpectrogramWidget::kResizeDebounceMs + 200);

    const QVector<float> image = m_widget->image();
    const int nw = kW / 2;
    QCOMPARE(image.size(), nw * kH);
    // The tone's cell (an identity row, the right edge) survives the
    // resample.
    QVERIFY(image.at((kToneRow * nw) + (nw - 1)) > 0.9F);
}

// The debounce is reset by every resize event: a second resize within
// the window postpones the rebuild (the image is still the original
// one, not the first resize's).
void SpectrogramWidgetTest::resizeResetsTheDebounce()
{
    startPlayback();
    feedWindow(kToneHz);
    QVERIFY(m_widget->image().at(cell(kToneRow, kW - 1)) > 0.9F);

    m_widget->resize(150, kH);
    QTest::qWait(SpectrogramWidget::kResizeDebounceMs / 2);
    m_widget->resize(kW / 2, kH);
    qApp->processEvents();
    // The first resize's timer was reset: no rebuild yet.
    QCOMPARE(m_widget->image().size(), kW * kH);

    QTest::qWait(SpectrogramWidget::kResizeDebounceMs + 200);

    QCOMPARE(m_widget->image().size(), (kW / 2) * kH);
}

// The edit menu offers the two display modes, "Scroll" checked.
void SpectrogramWidgetTest::contextMenuOffersModes()
{
    QMenu* menu = m_widget->widgetContextMenu();
    QVERIFY(menu != nullptr);

    QAction* scroll = findAction(menu, "Scroll");
    QVERIFY(scroll != nullptr && scroll->isCheckable() && scroll->isChecked());
    QAction* sweep = findAction(menu, "Sweep");
    QVERIFY(sweep != nullptr && sweep->isCheckable() && !sweep->isChecked());
}

void SpectrogramWidgetTest::modeToggles()
{
    QAction* sweep = findAction(m_widget->widgetContextMenu(), "Sweep");
    QVERIFY(sweep != nullptr);
    sweep->trigger();
    QCOMPARE(m_widget->displayMode(), SpectrogramWidget::Mode::Sweep);
}

void SpectrogramWidgetTest::storeRestoreMode()
{
    // The default mode is not persisted.
    WidgetConfiguration config = m_widget->storeWidget();
    QVERIFY(!config.contains("mode"));

    m_widget->setDisplayMode(SpectrogramWidget::Mode::Sweep);
    config = m_widget->storeWidget();
    QCOMPARE(config.value("mode").toInt(), 1);

    m_widget->setDisplayMode(SpectrogramWidget::Mode::Scroll);
    m_widget->restoreWidget(config);
    QCOMPARE(m_widget->displayMode(), SpectrogramWidget::Mode::Sweep);

    // A stale value outside the enum range is ignored.
    WidgetConfiguration stale;
    stale["mode"] = 5;
    m_widget->restoreWidget(stale);
    QCOMPARE(m_widget->displayMode(), SpectrogramWidget::Mode::Sweep);
}

// The edit menu offers the window sizes (the FFT's size) as an
// exclusive checkable group, the default (1024) checked.
void SpectrogramWidgetTest::contextMenuOffersWindowSizes()
{
    QMenu* menu = m_widget->widgetContextMenu();
    QVERIFY(menu != nullptr);

    for (const int size : SpectrogramWidget::kWindowSizeOptions) {
        QAction* action =
            findAction(menu, QStringLiteral("%1 samples").arg(size));
        QVERIFY(action != nullptr && action->isCheckable());
        QCOMPARE(action->isChecked(),
                 size == SpectrogramWidget::kDefaultWindowSize);
    }
}

// A larger window moves the bin grid: a full-scale tone on the 4096
// grid's bin 43 (503.5 Hz, the row 97's nearest bin) lights the same
// row (97, the 500 Hz row), and a 4096-sample tap closes one window.
void SpectrogramWidgetTest::windowSizeOptionWorks()
{
    startPlayback();
    m_widget->setWindowSize(4096);
    QCOMPARE(m_widget->windowSize(), 4096);

    feedWindow(503.515625F, 48000, 1.0F, 1.0F, 4096);

    QCOMPARE(m_widget->accumulatedSamples(), 0);  // the window closed
    QVERIFY(m_widget->image().at(cell(kToneRow, kW - 1)) > 0.9F);
}

// A window-size change re-grids the image: the old columns are
// resampled into the new one (the same-size grid keeps them in place —
// the history survives) and the tap's accumulation resets.
void SpectrogramWidgetTest::windowSizeChangePreservesTheImage()
{
    startPlayback();
    feedWindow(kToneHz);
    QVERIFY(m_widget->image().at(cell(kToneRow, kW - 1)) > 0.9F);

    m_widget->setWindowSize(4096);

    const QVector<float> image = m_widget->image();
    QCOMPARE(image.size(), kW * kH);  // the grid's size is unchanged
    QVERIFY(image.at(cell(kToneRow, kW - 1)) > 0.9F);  // the history kept
    QCOMPARE(m_widget->accumulatedSamples(), 0);
}

void SpectrogramWidgetTest::storeRestoreWindowSize()
{
    // The default size is not persisted.
    WidgetConfiguration config = m_widget->storeWidget();
    QVERIFY(!config.contains("windowSize"));

    m_widget->setWindowSize(256);
    config = m_widget->storeWidget();
    QCOMPARE(config.value("windowSize").toInt(), 256);

    m_widget->setWindowSize(SpectrogramWidget::kDefaultWindowSize);
    m_widget->restoreWidget(config);
    QCOMPARE(m_widget->windowSize(), 256);

    // A stale size outside the offered ones is ignored.
    WidgetConfiguration stale;
    stale["windowSize"] = 300;
    m_widget->restoreWidget(stale);
    QCOMPARE(m_widget->windowSize(), 256);
}

// The painted output: the tone's row in the new column is the gradient's
// loud end (the mono map's top, the bar color), the rest the background.
// In Sweep mode the line (the sweep's next column) paints in the same
// color.
void SpectrogramWidgetTest::rendersTheToneRow()
{
    startPlayback();
    feedWindow(kToneHz);
    qApp->processEvents();

    QPixmap pm(kW, kH);
    m_widget->render(&pm);
    const QImage img = pm.toImage();
    // (Pixels are compared by rgb(): QColor's operator== is sensitive
    // to the internal representation, equal colors can fail it.)
    const QColor bar = m_widget->palette().color(QPalette::Highlight);
    const QColor bg = m_widget->palette().color(QPalette::Base);
    QCOMPARE(img.pixelColor(kW - 1, kToneRow).rgb(), bar.rgb());
    QCOMPARE(img.pixelColor(kW - 1, 50).rgb(), bg.rgb());

    // Sweep mode: the tone's column (x 0) and the line (x 1, the next
    // column) are the bar color, the rest the background.
    m_widget->setDisplayMode(SpectrogramWidget::Mode::Sweep);
    m_pm->stop();
    startPlayback();
    feedWindow(kToneHz);
    pm = QPixmap(kW, kH);
    m_widget->render(&pm);
    const QImage img2 = pm.toImage();
    QCOMPARE(img2.pixelColor(0, kToneRow).rgb(), bar.rgb());
    QCOMPARE(img2.pixelColor(1, 50).rgb(), bar.rgb());
    QCOMPARE(img2.pixelColor(2, 50).rgb(), bg.rgb());
}

// "x-cmap" swaps the gradient for a fixed map: a full intensity is the
// map's top stop, silence its bottom (the named maps replace the whole
// gradient, the bottom included).
void SpectrogramWidgetTest::xColorMapColorsTheGradient()
{
    startPlayback();
    feedWindow(kToneHz);
    qApp->processEvents();

    StyleManager::getInstance()->setStyleSheet(
        "SpectrogramWidget { x-cmap: viridis; }");
    qApp->processEvents();

    QPixmap pm(kW, kH);
    m_widget->render(&pm);
    const QImage img = pm.toImage();
    QCOMPARE(img.pixelColor(kW - 1, kToneRow).rgb(),
             QColor(253, 231, 37).rgb());  // viridis' top
    QCOMPARE(img.pixelColor(kW - 1, 50).rgb(),
             QColor(68, 1, 84).rgb());  // viridis' bottom (silence)

    StyleManager::getInstance()->setStyleSheet(QString());
}

// "x-floor-db" sets the contrast's floor: a -20 dB tone (amplitude
// 0.1) sits two thirds up the gradient at the default floor (-60),
// halfway at a -40 floor and at the very bottom at its own level
// (a -20 floor).
void SpectrogramWidgetTest::xFloorDbSetsTheContrast()
{
    startPlayback();
    feedWindow(kToneHz, 48000, 0.1F, 0.1F);
    const float atDefault = m_widget->image().at(cell(kToneRow, kW - 1));
    QVERIFY2(atDefault > 0.5F && atDefault < 0.8F,
             "expected about two thirds at the default floor");

    m_pm->stop();
    startPlayback();
    StyleManager::getInstance()->setStyleSheet(
        "SpectrogramWidget { x-floor-db: -40; }");
    feedWindow(kToneHz, 48000, 0.1F, 0.1F);
    const float at40 = m_widget->image().at(cell(kToneRow, kW - 1));
    QVERIFY2(at40 > 0.4F && at40 < 0.6F, "expected halfway at a -40 floor");

    m_pm->stop();
    startPlayback();
    StyleManager::getInstance()->setStyleSheet(
        "SpectrogramWidget { x-floor-db: -20; }");
    feedWindow(kToneHz, 48000, 0.1F, 0.1F);
    const float at20 = m_widget->image().at(cell(kToneRow, kW - 1));
    QVERIFY2(at20 < 0.05F, "expected the bottom at its own floor");

    StyleManager::getInstance()->setStyleSheet(QString());
}

// The tap is latency-shifted: a frame that lands while not playing
// feeds nothing (the image stays silent).
void SpectrogramWidgetTest::framesIgnoredWhenNotPlaying()
{
    m_fake.triggerFrames(
        tone(kToneHz, SpectrogramWidget::kDefaultWindowSize, 48000), 48000);

    QCOMPARE(m_widget->accumulatedSamples(), 0);
    for (const float level : m_widget->image())
        QCOMPARE(level, 0.0F);
}

void SpectrogramWidgetTest::paintRateFollowsTheSetting()
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

}  // namespace

QTEST_MAIN(SpectrogramWidgetTest)
#include "spectrogramwidget_test.moc"
