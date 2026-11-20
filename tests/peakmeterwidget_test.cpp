#include <QtTest>
#include <QFile>

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QDialog>
#include <QListWidget>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <memory>

#include "checkablelistdialog.h"
#include "layoutmanager.h"
#include "paintrate.h"
#include "peakmeterwidget.h"
#include "settingsmanager.h"
#include "stylemanager.h"
#include "testsupport.h"

namespace {

class PeakMeterWidgetTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void registeredInLayoutManager();
    void minimumSize();
    void initiallyIdle();
    void levelsAttack();
    void levelsDecay();
    void stopResetsLevels();
    void rendersBar();
    void contextMenuOffersDisplayStyles();
    void displayStyleToggles();
    void storeRestoreDisplayStyle();
    void contextMenuOffersFillDirections();
    void fillDirectionToggles();
    void channelsDialogAppliesVisibilityAndOrder();
    void storeRestoreOrientation();
    void storeRestoreChannels();
    void rendersFillDirections();
    void singleChannelTakesTheWholeArea();
    void stylesTuneBackgroundAndBars();
    void xBarMarginSetsTheBarSpacing();
    void xTrackColorRecolorsTheUnfilledBar();
    void rendersSegments();
    void levelsIgnoredWhenNotPlaying();
    void paintRateFollowsTheSetting();
    void paintsAtTheConfiguredRate();
    void releaseTimeIndependentOfPaintRate();

private:
    MediaItem makeItem(quint64 id, const QString &title);
    void startPlayback();
    QAction* findAction(QMenu* menu, const QString& text) const;

    FakePlayer m_fake{this};
    PlaybackManager* m_pm = nullptr;
    PlaylistManager* m_pl = nullptr;
    MediaItemStore* m_store = nullptr;
    SettingsManager* m_settings = nullptr;
    std::unique_ptr<QTemporaryDir> m_dir;
    PeakMeterWidget* m_widget = nullptr;
};

void PeakMeterWidgetTest::init()
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

    // The Channels... dialog's UiState wiring writes through to the
    // SettingsManager: keep that store private too, so the test does
    // not touch the user's settings.json.
    m_settings = SettingsManager::getInstance();
    m_settings->setSettingsPath(m_dir->path() + "/settings.json");
    m_settings->load();

    m_widget = new PeakMeterWidget();
    m_widget->show();
    qApp->processEvents();
}

void PeakMeterWidgetTest::cleanup()
{
    delete m_widget;
    StyleManager::getInstance()->setStyleSheet(QString());
    m_settings->resetToDefault(PaintRate::key());
    m_pm->stop();
    m_pl->reset();
    m_store->clear();
    SettingsManager::reset();
}

MediaItem PeakMeterWidgetTest::makeItem(quint64 id, const QString &title)
{
    MediaItem item;
    item.id = id;
    item.title = title;
    const QString path = "/tmp/peakmeter_" + title + ".mp3";
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

void PeakMeterWidgetTest::startPlayback()
{
    m_pl->addItems({ makeItem(1, "Track1").id });
    m_pm->playCurrent();
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
}

QAction* PeakMeterWidgetTest::findAction(QMenu* menu, const QString& text) const
{
    for (QAction* action : menu->actions())
        if (action->text() == text)
            return action;
    return nullptr;
}

void PeakMeterWidgetTest::registeredInLayoutManager()
{
    WidgetConfiguration cfg;
    BaseWidget* w = LayoutManager::getInstance()->createWidget(PeakMeterWidget::id, cfg);
    QVERIFY(w != nullptr);
    QCOMPARE(w->widgetId(), PeakMeterWidget::id);
    QCOMPARE(w->widgetTitle(), PeakMeterWidget::title);
    delete w;
}

void PeakMeterWidgetTest::minimumSize()
{
    // The meter paints its own bars (no children to carry a layout
    // minimum): the floor keeps a splitter from collapsing it to
    // nothing.
    QCOMPARE(m_widget->minimumSize(), PeakMeterWidget::kMinimumSize);
    m_widget->resize(0, 0);
    QCOMPARE(m_widget->size(), PeakMeterWidget::kMinimumSize);
}

void PeakMeterWidgetTest::initiallyIdle()
{
    QCOMPARE(m_widget->leftLevel(), 0.0F);
    QCOMPARE(m_widget->rightLevel(), 0.0F);
}

// A new tap jumps the bars to the reported peaks (fast attack).
void PeakMeterWidgetTest::levelsAttack()
{
    startPlayback();
    QCOMPARE(m_widget->leftLevel(), 0.0F);

    m_fake.triggerLevels(0.8F, 0.4F);

    QCOMPARE(m_widget->leftLevel(), 0.8F);
    QCOMPARE(m_widget->rightLevel(), 0.4F);
}

// While playing the bars decay toward silence on the release timer.
void PeakMeterWidgetTest::levelsDecay()
{
    startPlayback();
    m_fake.triggerLevels(1.0F, 1.0F);
    QCOMPARE(m_widget->leftLevel(), 1.0F);

    QTest::qWait(100);  // ~6 release steps

    QVERIFY(m_widget->leftLevel() < 1.0F);
    QVERIFY(m_widget->leftLevel() > 0.0F);
}

// A stop is silence: the bars clear immediately.
void PeakMeterWidgetTest::stopResetsLevels()
{
    startPlayback();
    m_fake.triggerLevels(0.9F, 0.5F);
    QCOMPARE(m_widget->leftLevel(), 0.9F);

    m_pm->stop();

    QCOMPARE(m_widget->leftLevel(), 0.0F);
    QCOMPARE(m_widget->rightLevel(), 0.0F);
}

// The painted output carries the bar color when a level is present.
void PeakMeterWidgetTest::rendersBar()
{
    startPlayback();
    m_fake.triggerLevels(1.0F, 0.0F);

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

// The edit menu offers the two display styles, "Solid" checked.
void PeakMeterWidgetTest::contextMenuOffersDisplayStyles()
{
    QMenu* menu = m_widget->widgetContextMenu();
    QVERIFY(menu != nullptr);

    QAction* solid = findAction(menu, "Solid");
    QVERIFY(solid != nullptr && solid->isCheckable() && solid->isChecked());
    QAction* segments = findAction(menu, "Segments");
    QVERIFY(segments != nullptr && segments->isCheckable() && !segments->isChecked());
    menu->deleteLater();
}

// Triggering a display style changes the state; a fresh menu
// reflects it.
void PeakMeterWidgetTest::displayStyleToggles()
{
    QMenu* menu = m_widget->widgetContextMenu();
    QAction* segments = findAction(menu, "Segments");
    QVERIFY(segments != nullptr);

    segments->trigger();
    QCOMPARE(m_widget->displayStyle(), PeakMeterWidget::Style::Segments);
    menu->deleteLater();

    QMenu* fresh = m_widget->widgetContextMenu();
    QVERIFY(findAction(fresh, "Solid") != nullptr);
    QVERIFY(!findAction(fresh, "Solid")->isChecked());
    QVERIFY(findAction(fresh, "Segments")->isChecked());
    fresh->deleteLater();
}

// The default display style is not persisted; a non-default one
// round-trips; a stale out-of-range value is ignored.
void PeakMeterWidgetTest::storeRestoreDisplayStyle()
{
    QVERIFY(!m_widget->storeWidget().contains("displayStyle"));

    m_widget->setDisplayStyle(PeakMeterWidget::Style::Segments);
    WidgetConfiguration stored = m_widget->storeWidget();
    QCOMPARE(stored.value("displayStyle").toInt(),
             static_cast<int>(PeakMeterWidget::Style::Segments));

    PeakMeterWidget restored;
    restored.restoreWidget(stored);
    QCOMPARE(restored.displayStyle(), PeakMeterWidget::Style::Segments);

    WidgetConfiguration stale;
    stale["displayStyle"] = 99;
    PeakMeterWidget guarded;
    guarded.restoreWidget(stale);
    QCOMPARE(guarded.displayStyle(), PeakMeterWidget::Style::Solid);
}

// The edit menu offers the four fill directions, "Fill up" checked
// (next to the two display styles, six checkables in all).
void PeakMeterWidgetTest::contextMenuOffersFillDirections()
{
    QMenu* menu = m_widget->widgetContextMenu();
    QVERIFY(menu != nullptr);

    int checkable = 0;
    bool upChecked = false;
    for (QAction* action : menu->actions()) {
        if (!action->isCheckable())
            continue;
        ++checkable;
        if (action->text() == "Fill up")
            upChecked = action->isChecked();
    }
    QCOMPARE(checkable, 6);
    QVERIFY(upChecked);
    menu->deleteLater();
}

// Triggering a direction changes the state; a fresh menu reflects it.
void PeakMeterWidgetTest::fillDirectionToggles()
{
    QMenu* menu = m_widget->widgetContextMenu();
    QAction* down = findAction(menu, "Fill down");
    QVERIFY(down != nullptr);

    down->trigger();
    QCOMPARE(static_cast<int>(m_widget->orientation()),
             static_cast<int>(PeakMeterWidget::Orientation::Down));
    menu->deleteLater();

    QMenu* fresh = m_widget->widgetContextMenu();
    QVERIFY(findAction(fresh, "Fill up") != nullptr);
    QVERIFY(findAction(fresh, "Fill down") != nullptr);
    QVERIFY(!findAction(fresh, "Fill up")->isChecked());
    QVERIFY(findAction(fresh, "Fill down")->isChecked());
    fresh->deleteLater();
}

// "Channels..." opens the shared checkable-list dialog; hiding and
// reordering a channel is applied on accept.
void PeakMeterWidgetTest::channelsDialogAppliesVisibilityAndOrder()
{
    QMenu* menu = m_widget->widgetContextMenu();
    QAction* channels = findAction(menu, "Channels...");
    QVERIFY(channels != nullptr);

    QStringList selected;
    bool dialogSaw = false;
    QTimer::singleShot(0, m_widget, [&selected, &dialogSaw] {
        QWidget* w = QApplication::activeModalWidget();
        CheckableListDialog* dlg = qobject_cast<CheckableListDialog*>(w);
        if (dlg == nullptr)
            return;
        dialogSaw = true;
        QListWidget* list = dlg->findChild<QListWidget*>("itemList");
        if (list == nullptr || list->count() != 2)
            return;
        // The default list: Left (row 0), Right (row 1), both checked.
        list->item(0)->setCheckState(Qt::Unchecked); // hide Left
        list->setCurrentRow(0);
        QPushButton* down = dlg->findChild<QPushButton*>("moveDownButton");
        if (down == nullptr)
            return;
        down->click();
        selected = dlg->selectedOrder();
        dlg->accept();
    });

    channels->trigger();

    QVERIFY(dialogSaw);
    QCOMPARE(selected, (QStringList{"right"}));
    QCOMPARE(m_widget->visibleChannels(), (QStringList{"right"}));
    menu->deleteLater();
}

// The default orientation is not persisted; a non-default one round-trips;
// a stale out-of-range value is ignored.
void PeakMeterWidgetTest::storeRestoreOrientation()
{
    QVERIFY(!m_widget->storeWidget().contains("orientation"));

    m_widget->setOrientation(PeakMeterWidget::Orientation::RightToLeft);
    WidgetConfiguration stored = m_widget->storeWidget();
    QCOMPARE(stored.value("orientation").toInt(),
             static_cast<int>(PeakMeterWidget::Orientation::RightToLeft));

    PeakMeterWidget restored;
    restored.restoreWidget(stored);
    QCOMPARE(static_cast<int>(restored.orientation()),
             static_cast<int>(PeakMeterWidget::Orientation::RightToLeft));

    WidgetConfiguration stale;
    stale["orientation"] = 99;
    PeakMeterWidget guarded;
    guarded.restoreWidget(stale);
    QCOMPARE(static_cast<int>(guarded.orientation()),
             static_cast<int>(PeakMeterWidget::Orientation::Up));
}

// The channel list round-trips; a missing key keeps the default set,
// unknown keys are dropped, duplicates collapse, and an empty result
// falls back to the default set.
void PeakMeterWidgetTest::storeRestoreChannels()
{
    const WidgetConfiguration defaults = m_widget->storeWidget();
    QCOMPARE(defaults.value("channels").toStringList(), (QStringList{"left", "right"}));

    m_widget->setChannels(QStringList{"right"});
    WidgetConfiguration stored = m_widget->storeWidget();
    QCOMPARE(stored.value("channels").toStringList(), (QStringList{"right"}));

    PeakMeterWidget restored;
    restored.restoreWidget(stored);
    QCOMPARE(restored.visibleChannels(), (QStringList{"right"}));

    PeakMeterWidget fresh;
    WidgetConfiguration none;
    fresh.restoreWidget(none);
    QCOMPARE(fresh.visibleChannels(), (QStringList{"left", "right"}));

    WidgetConfiguration bogus;
    bogus["channels"] = QStringList{"middle", "right", "right"};
    fresh.restoreWidget(bogus);
    QCOMPARE(fresh.visibleChannels(), (QStringList{"right"}));

    WidgetConfiguration empty;
    empty["channels"] = QStringList{"middle"};
    fresh.restoreWidget(empty);
    QCOMPARE(fresh.visibleChannels(), (QStringList{"left", "right"}));
}

// Each fill direction paints toward the expected side (level 0.5, so
// the probes sit safely in the filled / empty halves).
void PeakMeterWidgetTest::rendersFillDirections()
{
    startPlayback();
    m_fake.triggerLevels(0.5F, 0.5F);

    m_widget->resize(240, 120);
    qApp->processEvents();
    const QColor bar = m_widget->palette().color(QPalette::Highlight);

    auto isBar = [&](int x, int y) {
        QPixmap pm(240, 120);
        m_widget->render(&pm);
        return pm.toImage().pixelColor(x, y) == bar;
    };

    m_widget->setOrientation(PeakMeterWidget::Orientation::Up);
    QVERIFY(isBar(60, 90));    // left bar, filled half
    QVERIFY(!isBar(60, 30));   // left bar, empty half

    m_widget->setOrientation(PeakMeterWidget::Orientation::Down);
    QVERIFY(isBar(60, 30));
    QVERIFY(!isBar(60, 90));

    m_widget->setOrientation(PeakMeterWidget::Orientation::LeftToRight);
    QVERIFY(isBar(60, 30));    // top bar, filled half
    QVERIFY(!isBar(180, 30));  // top bar, empty half

    m_widget->setOrientation(PeakMeterWidget::Orientation::RightToLeft);
    QVERIFY(isBar(180, 30));
    QVERIFY(!isBar(60, 30));
}

// With one visible channel the bar spans the whole area.
void PeakMeterWidgetTest::singleChannelTakesTheWholeArea()
{
    startPlayback();
    m_widget->setChannels(QStringList{"right"});
    m_fake.triggerLevels(0.0F, 0.5F);

    m_widget->resize(240, 120);
    qApp->processEvents();
    QPixmap pm(240, 120);
    m_widget->render(&pm);
    const QImage img = pm.toImage();
    const QColor bar = m_widget->palette().color(QPalette::Highlight);

    // The single bar spans the whole width (minus the margins); at
    // level 0.5 the fill covers the lower half (default: fill up).
    QVERIFY(img.pixelColor(120, 90) == bar);
    QVERIFY(img.pixelColor(120, 30) != bar);
}

// The stylesheet is the styling surface: "background" recolors the
// background (the track follows) and "color" the bars; without a rule
// the theme's roles stand in (the unstyled look).
void PeakMeterWidgetTest::stylesTuneBackgroundAndBars()
{
    startPlayback();
    m_fake.triggerLevels(1.0F, 0.0F);
    m_widget->resize(200, 100);
    qApp->processEvents();

    // (Pixels are compared by rgb(): QColor's operator== is sensitive to
    // the internal representation, equal colors can fail it.)
    // Unstyled: the bars use the theme's Highlight, the track derives
    // from the theme's Base. (200x100: margin 5, the left bar spans
    // x 5..97 filled, the right bar x 102..194 track-only.)
    QPixmap pm(200, 100);
    m_widget->render(&pm);
    const QImage img = pm.toImage();
    QCOMPARE(img.pixelColor(50, 50).rgb(),
             m_widget->palette().color(QPalette::Highlight).rgb());
    QCOMPARE(img.pixelColor(150, 50).rgb(),
             m_widget->palette().color(QPalette::Base).lighter(120).rgb());

    StyleManager::getInstance()->setStyleSheet(
        "PeakMeterWidget { background: #101020; color: #00ff00; }");
    qApp->processEvents();
    pm = QPixmap(200, 100);
    m_widget->render(&pm);
    const QImage styled = pm.toImage();
    QCOMPARE(styled.pixelColor(50, 50).rgb(), QColor("#00ff00").rgb());    // the bar
    QCOMPARE(styled.pixelColor(2, 50).rgb(), QColor("#101020").rgb());     // the background
    QCOMPARE(styled.pixelColor(150, 50).rgb(),
             QColor("#101020").lighter(120).rgb());                        // the track follows

    // Clearing the sheet restores the unstyled look.
    StyleManager::getInstance()->setStyleSheet(QString());
    qApp->processEvents();
    pm = QPixmap(200, 100);
    m_widget->render(&pm);
    QCOMPARE(pm.toImage().pixelColor(50, 50).rgb(),
             m_widget->palette().color(QPalette::Highlight).rgb());
}

// "x-bar-margin" sets the space between and around the bars (the auto
// share when unset): the bars' slots move with it.
void PeakMeterWidgetTest::xBarMarginSetsTheBarSpacing()
{
    startPlayback();
    m_fake.triggerLevels(1.0F, 0.0F);
    m_widget->resize(200, 100);
    qApp->processEvents();

    // (Pixels are compared by rgb(): QColor's operator== is sensitive
    // to the internal representation, equal colors can fail it.)
    // Unset: the auto margin is max(2, 200/40) = 5 — the left bar (the
    // filled one) starts at x 5.
    QPixmap pm(200, 100);
    m_widget->render(&pm);
    const uint bar = m_widget->palette().color(QPalette::Highlight).rgb();
    const uint bg = m_widget->palette().color(QPalette::Base).rgb();
    QCOMPARE(pm.toImage().pixelColor(4, 50).rgb(), bg);   // the margin
    QCOMPARE(pm.toImage().pixelColor(5, 50).rgb(), bar);  // the bar starts

    // Set: margin 10px (the "px" is a unit, like the sheet's lengths)
    // — the bar starts at x 10, the auto position is background again.
    StyleManager::getInstance()->setStyleSheet(
        "PeakMeterWidget { x-bar-margin: 10px; }");
    qApp->processEvents();
    pm = QPixmap(200, 100);
    m_widget->render(&pm);
    QCOMPARE(pm.toImage().pixelColor(9, 50).rgb(), bg);
    QCOMPARE(pm.toImage().pixelColor(10, 50).rgb(), bar);
    StyleManager::getInstance()->setStyleSheet(QString());
}

// "x-track-color" recolors a bar's unfilled part (auto: the background
// lightened).
void PeakMeterWidgetTest::xTrackColorRecolorsTheUnfilledBar()
{
    startPlayback();
    m_fake.triggerLevels(0.0F, 0.5F);  // the right bar only
    m_widget->resize(200, 100);
    qApp->processEvents();

    // (Pixels are compared by rgb(): QColor's operator== is sensitive
    // to the internal representation, equal colors can fail it.)
    // Unset: the track derives from the background (200x100: the right
    // bar spans x 102..194, its upper half unfilled — level 0.5, fill
    // up).
    QPixmap pm(200, 100);
    m_widget->render(&pm);
    QCOMPARE(pm.toImage().pixelColor(150, 30).rgb(),
             m_widget->palette().color(QPalette::Base).lighter(120).rgb());

    StyleManager::getInstance()->setStyleSheet(
        "PeakMeterWidget { x-track-color: #404040; }");
    qApp->processEvents();
    pm = QPixmap(200, 100);
    m_widget->render(&pm);
    QCOMPARE(pm.toImage().pixelColor(150, 30).rgb(), QColor("#404040").rgb());
    StyleManager::getInstance()->setStyleSheet(QString());
}

// The "Segments" display style splits each bar into discrete blocks
// along the fill axis: the blocks the level reaches are lit, the rest
// carry the track color, and a 1px gap separates the blocks.
void PeakMeterWidgetTest::rendersSegments()
{
    startPlayback();
    m_fake.triggerLevels(0.5F, 0.0F);
    m_widget->setDisplayStyle(PeakMeterWidget::Style::Segments);

    m_widget->resize(240, 120);
    qApp->processEvents();
    StyleManager::getInstance()->setStyleSheet(QStringLiteral(
        "PeakMeterWidget { background: #202020; color: #00ff00;\n"
        "    x-segments: 4; }"));
    qApp->processEvents();

    // (Pixels are compared by rgb(): QColor's operator== is sensitive
    // to the internal representation, equal colors can fail it.)
    // (240x120: margin 6, the left bar spans x 6..116 and the right
    // bar x 123..233; four 30px blocks fill the 120px height from the
    // bottom, level 0.5 lights the two lowest: y 90..119 and 60..88.)
    QPixmap pm(240, 120);
    m_widget->render(&pm);
    const QImage img = pm.toImage();
    const uint bar = QColor("#00ff00").rgb();
    const uint track = QColor("#202020").lighter(120).rgb();
    const uint bg = QColor("#202020").rgb();
    QCOMPARE(img.pixelColor(30, 110).rgb(), bar);    // lit block
    QCOMPARE(img.pixelColor(30, 70).rgb(), bar);     // lit block
    QCOMPARE(img.pixelColor(30, 45).rgb(), track);   // unlit block
    QCOMPARE(img.pixelColor(30, 15).rgb(), track);   // unlit block
    QCOMPARE(img.pixelColor(30, 59).rgb(), bg);      // the gap
    QCOMPARE(img.pixelColor(170, 60).rgb(), track);  // the right bar: empty
}

// The tap is latency-shifted, so a queued tap can land after a pause
// or a stop: without playback the meter stays down.
void PeakMeterWidgetTest::levelsIgnoredWhenNotPlaying()
{
    m_fake.triggerLevels(1.0F, 1.0F);
    m_widget->resize(200, 100);
    qApp->processEvents();

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
void PeakMeterWidgetTest::paintRateFollowsTheSetting()
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
// taps' rhythm: the taps feed the peaks at ~50/s, the paint stays at
// the setting's 20/s.
void PeakMeterWidgetTest::paintsAtTheConfiguredRate()
{
    startPlayback();
    m_settings->setValue(PaintRate::key(), 20);
    m_widget->resize(300, 100);
    qApp->processEvents();
    m_fake.triggerLevels(0.5F, 0.5F);

    PaintCounter counter;
    m_widget->installEventFilter(&counter);

    const int windowMs = 1000;
    int waited = 0;
    while (waited < windowMs) {
        m_fake.triggerLevels(0.5F, 0.5F);
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
// rate the ticks are coarser, but the bar still falls in the same time.
void PeakMeterWidgetTest::releaseTimeIndependentOfPaintRate()
{
    startPlayback();
    m_widget->resize(300, 100);
    qApp->processEvents();

    // The fall time from a full peak to silence. (The test macros
    // expand to a bare return and cannot live in a helper that
    // returns a value, so the level checks stay in the test.)
    auto fallToZero = [this] {
        m_fake.triggerLevels(1.0F, 1.0F);
        QElapsedTimer clock;
        clock.start();
        int waited = 0;
        while (m_widget->leftLevel() > 0.0F && waited < 5000) {
            QTest::qWait(10);
            waited += 10;
        }
        return clock.elapsed();
    };

    m_settings->setValue(PaintRate::key(), 60);
    const qint64 fallAt60 = fallToZero();
    QCOMPARE(m_widget->leftLevel(), 0.0F);

    m_settings->setValue(PaintRate::key(), 10);
    const qint64 fallAt10 = fallToZero();
    QCOMPARE(m_widget->leftLevel(), 0.0F);

    // The same within the timer granularity (the coarser the ticks,
    // the more the last step can overshoot: a 10 fps tick is 100 ms
    // wide).
    qInfo() << "release time at 60 fps:" << fallAt60 << "ms, at 10 fps:"
            << fallAt10 << "ms";
    QVERIFY2(qAbs(fallAt60 - fallAt10) <= 200,
             "the release time must not depend on the paint rate");
}

}  // namespace

QTEST_MAIN(PeakMeterWidgetTest)
#include "peakmeterwidget_test.moc"
