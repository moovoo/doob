#include <QtTest>
#include <QFile>

#include <QAction>
#include <QMenu>
#include <QPixmap>
#include <QColor>
#include <QTemporaryDir>
#include <QUrl>

#include <memory>

#include "layoutmanager.h"
#include "oscillogramwidget.h"
#include "paintrate.h"
#include "settingsmanager.h"
#include "stylemanager.h"
#include "testsupport.h"

namespace {

// Scan for a pixel of the given rgb (QColor's operator== is sensitive
// to the internal representation, so the scan compares rgb()).
bool findPixel(const QImage &img, uint rgb)
{
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (img.pixelColor(x, y).rgb() == rgb)
                return true;
    return false;
}

class OscillogramWidgetTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void registeredInLayoutManager();
    void minimumSize();
    void initiallyEmpty();
    void bucketsTapsIntoDisplayColumns();
    void partialTapsDoNotCloseAColumn();
    void capsAtCapacity();
    void windowEndIsTheNewestColumn();
    void stopClearsTrace();
    void rendersTrace();
    void stylesTuneTraceAndBackground();
    void contextMenuOffersTraceStyles();
    void contextMenuOffersWindowLengths();
    void menuSeparatesOptionGroups();
    void traceStyleToggles();
    void storeRestoreTraceStyle();
    void windowLengthRoundTrips();
    void windowLengthChangesTheBucketing();
    void traceStyleLine();
    void traceStyleBoxes();
    void waveformIgnoredWhenNotPlaying();
    void paintRateFollowsTheSetting();
    void paintsAtTheConfiguredRate();

private:
    MediaItem makeItem(quint64 id, const QString &title);
    void startPlayback();
    // Feeds `count` closed display columns of one envelope: each is a
    // tap of K columns (K from the fake's 48 kHz default rate and the
    // widget's 1 s window over its capacity — the same math the widget
    // applies, see its onWaveform).
    void feedColumns(int count, float mn, float mx);
    QAction* findAction(QMenu* menu, const QString& text) const;

    FakePlayer m_fake{this};
    PlaybackManager* m_pm = nullptr;
    PlaylistManager* m_pl = nullptr;
    MediaItemStore* m_store = nullptr;
    SettingsManager* m_settings = nullptr;
    std::unique_ptr<QTemporaryDir> m_dir;
    OscillogramWidget* m_widget = nullptr;
};

void OscillogramWidgetTest::init()
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

    m_widget = new OscillogramWidget();
    m_widget->show();
    qApp->processEvents();
}

void OscillogramWidgetTest::cleanup()
{
    delete m_widget;
    StyleManager::getInstance()->setStyleSheet(QString());
    m_settings->resetToDefault(PaintRate::key());
    m_pm->stop();
    m_pl->reset();
    m_store->clear();
}

MediaItem OscillogramWidgetTest::makeItem(quint64 id, const QString &title)
{
    MediaItem item;
    item.id = id;
    item.title = title;
    const QString path = "/tmp/oscillogram_" + title + ".mp3";
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

void OscillogramWidgetTest::startPlayback()
{
    m_pl->addItems({ makeItem(1, "Track1").id });
    m_pm->playCurrent();
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
}

void OscillogramWidgetTest::feedColumns(int count, float mn, float mx)
{
    const qsizetype k = qRound(48.0 * 1000.0 / (m_widget->capacity() * 8));
    QVector<float> waveform(2 * k);
    for (qsizetype i = 0; i < k; ++i) {
        waveform[2 * i] = mn;
        waveform[(2 * i) + 1] = mx;
    }
    for (int c = 0; c < count; ++c)
        m_fake.triggerWaveform(waveform);
}

QAction* OscillogramWidgetTest::findAction(QMenu* menu, const QString& text) const
{
    for (QAction* action : menu->actions())
        if (action->text() == text)
            return action;
    return nullptr;
}

void OscillogramWidgetTest::registeredInLayoutManager()
{
    WidgetConfiguration cfg;
    BaseWidget* w = LayoutManager::getInstance()->createWidget(OscillogramWidget::id, cfg);
    QVERIFY(w != nullptr);
    QCOMPARE(w->widgetId(), OscillogramWidget::id);
    QCOMPARE(w->widgetTitle(), OscillogramWidget::title);
    delete w;
}

void OscillogramWidgetTest::minimumSize()
{
    // The scope paints its own trace (no children to carry a layout
    // minimum): the floor keeps a splitter from collapsing it to
    // nothing.
    QCOMPARE(m_widget->minimumSize(), OscillogramWidget::kMinimumSize);
    m_widget->resize(0, 0);
    QCOMPARE(m_widget->size(), OscillogramWidget::kMinimumSize);
}

void OscillogramWidgetTest::initiallyEmpty()
{
    QVERIFY(m_widget->trace().isEmpty());
}

// A tap's columns are re-bucketed into display columns (K of them each):
// the closed column's envelope is the min of the mins and the max of
// the maxes of its K columns, whatever the sample rate.
void OscillogramWidgetTest::bucketsTapsIntoDisplayColumns()
{
    startPlayback();
    // The first tap sets the re-bucketing from the sample rate...
    m_fake.triggerWaveform(QVector<float>{0.0F, 0.5F});
    const qsizetype k = m_widget->tapsPerDisplayColumn();
    QVERIFY(k > 1);

    // ...and closes the open column: its envelope spans this tap's
    // columns (the rising mins, the falling maxes) plus the held one.
    QVector<float> waveform(2 * k);
    for (qsizetype i = 0; i < k; ++i) {
        waveform[2 * i] = static_cast<float>(i) / static_cast<float>(k);
        waveform[(2 * i) + 1] =
            1.0F - (static_cast<float>(i) / static_cast<float>(k));
    }
    m_fake.triggerWaveform(waveform);

    const QVector<float> trace = m_widget->trace();
    QCOMPARE(trace.size(), 2);
    QCOMPARE(trace[0], 0.0F);
    QCOMPARE(trace[1], 1.0F);
}

// The re-bucketing closes a column exactly at its K columns: a short tap
// stays open (and is carried into the next close), no partial column
// ever appears in the trace.
void OscillogramWidgetTest::partialTapsDoNotCloseAColumn()
{
    startPlayback();
    m_fake.triggerWaveform(QVector<float>{0.0F, 0.5F});  // sets the rate
    const qsizetype k = m_widget->tapsPerDisplayColumn();
    m_pm->stop();
    startPlayback();

    QVector<float> waveform(2 * (k - 1));
    for (qsizetype i = 0; i < k - 1; ++i) {
        waveform[2 * i] = 0.0F;
        waveform[(2 * i) + 1] = 1.0F;
    }
    m_fake.triggerWaveform(waveform);
    QVERIFY(m_widget->trace().isEmpty());  // k - 1 < k: nothing closed

    // The next column closes exactly at the boundary, carrying the held
    // columns' envelope with it.
    m_fake.triggerWaveform(QVector<float>{0.25F, 0.75F});
    const QVector<float> trace = m_widget->trace();
    QCOMPARE(trace.size(), 2);
    QCOMPARE(trace[0], 0.0F);
    QCOMPARE(trace[1], 1.0F);
}

// The trace keeps only the newest `capacity` display columns (a
// scrolling window).
void OscillogramWidgetTest::capsAtCapacity()
{
    startPlayback();
    feedColumns(300, 0.0F, 0.0F);
    feedColumns(100, 1.0F, 1.0F);

    // The window holds the last 256 of the 400 closed columns: the
    // zero run (columns 0..299) scrolled off down to column 144, the
    // one run (300..399) is the newest 100.
    const QVector<float> trace = m_widget->trace();
    QCOMPARE(trace.size(), 2 * m_widget->capacity());
    QCOMPARE(trace[0], 0.0F);
    QCOMPARE(trace.last(), 1.0F);
}

// The window's right edge is the newest closed column: the trace
// reaches the widget's edge with real data, and the edge does not
// breathe with the taps' chunked arrival.
void OscillogramWidgetTest::windowEndIsTheNewestColumn()
{
    startPlayback();
    feedColumns(1, 0.0F, 1.0F);
    QCOMPARE(m_widget->windowEnd(), 1.0);

    feedColumns(5, 0.0F, 1.0F);
    QCOMPARE(m_widget->windowEnd(), 6.0);
}

// A stop clears the trace.
void OscillogramWidgetTest::stopClearsTrace()
{
    startPlayback();
    feedColumns(1, 0.0F, 0.5F);
    QCOMPARE(m_widget->trace().size(), 2);

    m_pm->stop();

    QVERIFY(m_widget->trace().isEmpty());
}

// The painted output carries the trace color when a trace is present.
void OscillogramWidgetTest::rendersTrace()
{
    startPlayback();
    feedColumns(1, 0.0F, 1.0F);

    m_widget->resize(200, 100);
    qApp->processEvents();
    QPixmap pm(200, 100);
    m_widget->render(&pm);
    QVERIFY(findPixel(pm.toImage(),
                      m_widget->palette().color(QPalette::Highlight).rgb()));
}

// The stylesheet is the styling surface: "background" recolors the
// background and "color" the trace; without a rule the theme's roles
// stand in (the unstyled look).
void OscillogramWidgetTest::stylesTuneTraceAndBackground()
{
    startPlayback();
    feedColumns(1, 0.0F, 1.0F);
    m_widget->resize(200, 100);
    qApp->processEvents();

    // Unstyled: the theme's Highlight trace on the theme's Base (the
    // bar widgets' surface). The trace sits in the right part of the
    // window (the window's start is far to the left of the data), so
    // the center pixel is background.
    QPixmap pm(200, 100);
    m_widget->render(&pm);
    const QImage img = pm.toImage();
    QVERIFY(findPixel(img, m_widget->palette().color(QPalette::Highlight).rgb()));
    QCOMPARE(img.pixelColor(100, 50).rgb(),
             m_widget->palette().color(QPalette::Base).rgb());

    StyleManager::getInstance()->setStyleSheet(
        "OscillogramWidget { background: #101020; color: #00ff00; }");
    qApp->processEvents();
    pm = QPixmap(200, 100);
    m_widget->render(&pm);
    const QImage styled = pm.toImage();
    QVERIFY(findPixel(styled, QColor("#00ff00").rgb()));    // the trace
    QCOMPARE(styled.pixelColor(100, 50).rgb(), QColor("#101020").rgb());
    QCOMPARE(styled.pixelColor(100, 10).rgb(), QColor("#101020").rgb());

    // Clearing the sheet restores the unstyled look.
    StyleManager::getInstance()->setStyleSheet(QString());
    qApp->processEvents();
    pm = QPixmap(200, 100);
    m_widget->render(&pm);
    QVERIFY(findPixel(pm.toImage(),
                      m_widget->palette().color(QPalette::Highlight).rgb()));
}

// The edit menu offers the three trace styles, "Bars" checked.
void OscillogramWidgetTest::contextMenuOffersTraceStyles()
{
    QMenu* menu = m_widget->widgetContextMenu();
    QVERIFY(menu != nullptr);

    QAction* bars = findAction(menu, "Bars");
    QVERIFY(bars != nullptr && bars->isCheckable() && bars->isChecked());
    QAction* line = findAction(menu, "Line");
    QVERIFY(line != nullptr && line->isCheckable() && !line->isChecked());
    QAction* boxes = findAction(menu, "Boxes");
    QVERIFY(boxes != nullptr && boxes->isCheckable() && !boxes->isChecked());
    menu->deleteLater();
}

// The edit menu offers the window lengths (the scroll's speed is the
// capacity over the span), the default checked.
void OscillogramWidgetTest::contextMenuOffersWindowLengths()
{
    QMenu* menu = m_widget->widgetContextMenu();
    QVERIFY(menu != nullptr);

    for (const int option : OscillogramWidget::kWindowOptions) {
        const QString label =
            QStringLiteral("%1 s").arg(static_cast<double>(option) / 1000.0);
        QAction* action = findAction(menu, label);
        QVERIFY(action != nullptr && action->isCheckable());
        QCOMPARE(action->isChecked(),
                 option == OscillogramWidget::kDefaultWindowMs);
    }
    menu->deleteLater();
}

// The edit menu draws horizontal lines between its sections: the base's
// entry (the style action, this widget has no toolbar), then the trace
// style group, then the window lengths — QMenu reports its separators
// among the actions, as empty-texted QActions.
void OscillogramWidgetTest::menuSeparatesOptionGroups()
{
    QMenu* menu = m_widget->widgetContextMenu();
    QVERIFY(menu != nullptr);

    const QList<QAction*> actions = menu->actions();
    int barsIndex = -1;
    int boxesIndex = -1;
    int separators = 0;
    for (int i = 0; i < actions.size(); ++i) {
        if (actions.at(i)->text().isEmpty())
            ++separators;
        else if (actions.at(i)->text() == QStringLiteral("Bars"))
            barsIndex = i;
        else if (actions.at(i)->text() == QStringLiteral("Boxes"))
            boxesIndex = i;
    }
    QVERIFY(barsIndex >= 2);
    QVERIFY(boxesIndex + 2 < actions.size());
    QCOMPARE(separators, 2);
    // The trace style group starts below a separator, and the base's
    // style entry precedes it.
    QVERIFY(actions.at(barsIndex - 1)->text().isEmpty());
    QCOMPARE(actions.at(barsIndex - 2)->text(), QStringLiteral("Style this widget..."));
    // The window lengths start below the other separator, right after
    // the trace style group ends.
    QVERIFY(actions.at(boxesIndex + 1)->text().isEmpty());
    QCOMPARE(actions.at(boxesIndex + 2)->text(),
             QStringLiteral("%1 s")
                 .arg(static_cast<double>(OscillogramWidget::kWindowOptions[0]) / 1000.0));

    menu->deleteLater();
}

// Triggering a trace style changes the state; a fresh menu
// reflects it.
void OscillogramWidgetTest::traceStyleToggles()
{
    QMenu* menu = m_widget->widgetContextMenu();
    QAction* boxes = findAction(menu, "Boxes");
    QVERIFY(boxes != nullptr);

    boxes->trigger();
    QCOMPARE(m_widget->traceStyle(), OscillogramWidget::TraceStyle::Boxes);
    menu->deleteLater();

    QMenu* fresh = m_widget->widgetContextMenu();
    QVERIFY(findAction(fresh, "Bars") != nullptr);
    QVERIFY(!findAction(fresh, "Bars")->isChecked());
    QVERIFY(findAction(fresh, "Boxes")->isChecked());
    fresh->deleteLater();
}

// The default trace style is not persisted; a non-default one
// round-trips; a stale out-of-range value is ignored.
void OscillogramWidgetTest::storeRestoreTraceStyle()
{
    QVERIFY(!m_widget->storeWidget().contains("traceStyle"));

    m_widget->setTraceStyle(OscillogramWidget::TraceStyle::Line);
    WidgetConfiguration stored = m_widget->storeWidget();
    QCOMPARE(stored.value("traceStyle").toInt(),
             static_cast<int>(OscillogramWidget::TraceStyle::Line));

    OscillogramWidget restored;
    restored.restoreWidget(stored);
    QCOMPARE(restored.traceStyle(), OscillogramWidget::TraceStyle::Line);

    WidgetConfiguration stale;
    stale["traceStyle"] = 99;
    OscillogramWidget guarded;
    guarded.restoreWidget(stale);
    QCOMPARE(guarded.traceStyle(), OscillogramWidget::TraceStyle::Bars);
}

// The default window length is not persisted; a non-default one
// round-trips; a stale value (not one of the menu's lengths) is
// ignored.
void OscillogramWidgetTest::windowLengthRoundTrips()
{
    QCOMPARE(m_widget->windowMs(), OscillogramWidget::kDefaultWindowMs);
    QVERIFY(!m_widget->storeWidget().contains("windowMs"));

    m_widget->setWindowMs(2000);
    WidgetConfiguration stored = m_widget->storeWidget();
    QCOMPARE(stored.value("windowMs").toInt(), 2000);

    OscillogramWidget restored;
    restored.restoreWidget(stored);
    QCOMPARE(restored.windowMs(), 2000);

    WidgetConfiguration stale;
    stale["windowMs"] = 1234;
    OscillogramWidget guarded;
    guarded.restoreWidget(stale);
    QCOMPARE(guarded.windowMs(), OscillogramWidget::kDefaultWindowMs);
}

// The window length re-buckets the taps: a shorter window holds less
// audio per display column, so fewer tap columns close one.
void OscillogramWidgetTest::windowLengthChangesTheBucketing()
{
    m_widget->setWindowMs(500);
    startPlayback();
    m_fake.triggerWaveform(QVector<float>{0.0F, 0.5F});
    // 48 kHz over a 0.5 s window: 48 * 500 / (256 * 8) = 11.7 → 12 tap
    // columns per display column (the default 1 s window is 23).
    QCOMPARE(m_widget->tapsPerDisplayColumn(), 12);
}

// The "Line" trace style is a single polyline that oscillates between
// the columns' max and min values: a constant envelope fills the band
// the pen sweeps between the two edges (the same envelope data that
// fills the default bars mode spans its full height here).
void OscillogramWidgetTest::traceStyleLine()
{
    startPlayback();
    // A full window with every column at the envelope (-0.8..0.8): at
    // 100px height it spans y 10..90.
    feedColumns(m_widget->capacity(), -0.8F, 0.8F);

    m_widget->resize(200, 100);
    qApp->processEvents();

    // The default trace style: the bars carry the full envelope.
    {
        QPixmap pm(200, 100);
        m_widget->render(&pm);
        QCOMPARE(pm.toImage().pixelColor(100, 25).rgb(),
                 m_widget->palette().color(QPalette::Highlight).rgb());
    }

    m_widget->setTraceStyle(OscillogramWidget::TraceStyle::Line);
    StyleManager::getInstance()->setStyleSheet(QStringLiteral(
        "OscillogramWidget { background: #000000; color: #00ff00; }"));
    qApp->processEvents();
    QPixmap pm(200, 100);
    m_widget->render(&pm);
    const QImage img = pm.toImage();
    QCOMPARE(img.pixelColor(50, 10).rgb(), QColor("#00ff00").rgb());   // the top edge
    QCOMPARE(img.pixelColor(100, 10).rgb(), QColor("#00ff00").rgb());  // the top edge
    QCOMPARE(img.pixelColor(100, 90).rgb(), QColor("#00ff00").rgb());  // the bottom edge
    QCOMPARE(img.pixelColor(100, 50).rgb(), QColor("#00ff00").rgb());  // the oscillation sweeps the center
    QCOMPARE(img.pixelColor(100, 5).rgb(), QColor("#000000").rgb());   // above the envelope
    QCOMPARE(img.pixelColor(100, 95).rgb(), QColor("#000000").rgb());  // below the envelope
}

// The "Boxes" trace style quantizes the envelope to discrete LCD
// boxes: the boxes it covers are lit in the trace color, the rest of
// each column glows in the off color.
void OscillogramWidgetTest::traceStyleBoxes()
{
    startPlayback();
    // A full window with a small envelope (0..0.2): at 100px height it
    // spans y 40..50, which with 10px boxes lights y 40..60.
    feedColumns(m_widget->capacity(), 0.0F, 0.2F);

    m_widget->resize(200, 100);
    qApp->processEvents();
    m_widget->setTraceStyle(OscillogramWidget::TraceStyle::Boxes);
    StyleManager::getInstance()->setStyleSheet(QStringLiteral(
        "OscillogramWidget { background: #000000; color: #00ff00;\n"
        "    x-box-size: 10px; x-box-color-off: #101010; }"));
    qApp->processEvents();

    QPixmap pm(200, 100);
    m_widget->render(&pm);
    const QImage img = pm.toImage();
    QCOMPARE(img.pixelColor(100, 45).rgb(), QColor("#00ff00").rgb());  // lit box
    QCOMPARE(img.pixelColor(100, 55).rgb(), QColor("#00ff00").rgb());  // lit box
    QCOMPARE(img.pixelColor(100, 35).rgb(), QColor("#101010").rgb());  // the ghost
    QCOMPARE(img.pixelColor(100, 65).rgb(), QColor("#101010").rgb());  // the ghost
    QCOMPARE(img.pixelColor(100, 5).rgb(), QColor("#101010").rgb());   // the ghost

    // Without "x-box-color-off" the unlit grid is the widget's
    // background lightened by 20% (the bar widgets' auto track): a
    // subtle tint on a dark background, the same palette approach as
    // the other trace styles (everything from the widget's own roles).
    StyleManager::getInstance()->setStyleSheet(QStringLiteral(
        "OscillogramWidget { background: #101018; color: #00ff00; }"));
    qApp->processEvents();
    pm = QPixmap(200, 100);
    m_widget->render(&pm);
    const uint off = QColor("#101018").lighter(120).rgb();
    QCOMPARE(pm.toImage().pixelColor(100, 5).rgb(), off);    // the off grid
    QVERIFY(off != 0x101018);                                // lightened, not the bg

    // The tint follows the background: on a light background the off
    // grid is a light tint (clearly apart from the lit boxes, which
    // carry the trace) — the pattern of the other trace styles (a
    // dark trace on a light background).
    StyleManager::getInstance()->setStyleSheet(QStringLiteral(
        "OscillogramWidget { background: #e0e0e0; color: #000000;\n"
        "    x-box-size: 10px; }"));
    qApp->processEvents();
    pm = QPixmap(200, 100);
    m_widget->render(&pm);
    const uint lightOff = QColor("#e0e0e0").lighter(120).rgb();
    QCOMPARE(pm.toImage().pixelColor(100, 5).rgb(), lightOff);  // the off grid
    QVERIFY(lightOff != 0xe0e0e0);                              // lightened, not the bg
}

// The tap is latency-shifted, so a queued tap can land after a pause
// or a stop: without playback the trace stays empty.
void OscillogramWidgetTest::waveformIgnoredWhenNotPlaying()
{
    feedColumns(1, 0.0F, 1.0F);
    m_widget->resize(200, 100);
    qApp->processEvents();

    QPixmap pm(200, 100);
    m_widget->render(&pm);
    // No trace: no pixel carries the trace's color.
    QVERIFY(!findPixel(pm.toImage(),
                       m_widget->palette().color(QPalette::Highlight).rgb()));
}

// The paint's rhythm is the user's setting: the widget's timer runs at
// the configured rate while playing, follows the setting live, and is
// idle without playback.
void OscillogramWidgetTest::paintRateFollowsTheSetting()
{
    QVERIFY(!m_widget->paintTimerActive());  // stopped: the timer is idle
    startPlayback();
    QVERIFY(m_widget->paintTimerActive());
    QCOMPARE(m_widget->paintIntervalMs(), PaintRate::intervalMs(PaintRate::fps()));

    m_settings->setValue(PaintRate::key(), 30);
    QCOMPARE(m_widget->paintIntervalMs(), PaintRate::intervalMs(30));

    m_pm->stop();
    QVERIFY(!m_widget->paintTimerActive());
}

// While playing, the timer paints at the configured rate: the taps only
// feed the trace, the timer samples it — the cadence is steady and the
// user's choice, whatever the taps' chunky rhythm.
void OscillogramWidgetTest::paintsAtTheConfiguredRate()
{
    startPlayback();
    m_widget->resize(200, 100);
    qApp->processEvents();
    m_settings->setValue(PaintRate::key(), 20);

    PaintCounter counter;
    m_widget->installEventFilter(&counter);
    const int windowMs = 1000;
    int waited = 0;
    while (waited < windowMs) {
        m_fake.triggerWaveform(QVector<float>{0.0F, 0.5F});
        QTest::qWait(20);
        waited += 20;
    }
    m_widget->removeEventFilter(&counter);

    qInfo() << "paints in 1 s at the 20 fps rate:" << counter.count;
    QVERIFY2(counter.count >= 15 && counter.count <= 26,
             "expected ~20 paints per second");
    QVERIFY(!m_widget->trace().isEmpty());
}

}  // namespace

QTEST_MAIN(OscillogramWidgetTest)
#include "oscillogramwidget_test.moc"
