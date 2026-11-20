#include <QtTest>
#include <QFile>

#include <QSlider>
#include <QLabel>
#include <QMouseEvent>
#include <QFontMetrics>
#include <QTemporaryDir>

#include <memory>

#include "layoutmanager.h"
#include "positionslider.h"
#include "testsupport.h"

namespace {

class PositionSliderTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void registeredInLayoutManager();
    void initialStateDisabledAndEmpty();
    void playingTrackEnablesSliderWithDuration();
    void positionUpdatesSliderAndLabel();
    void scrubbingSeeksPlayer();
    void positionChangedWhileScrubbingIgnored();
    void scrubbingWhileStoppedDoesNothing();
    void grooveClickSeeksToClickedPosition();
    void grooveClickHeldSuppressesPositionUpdates();
    void stoppingResetsWidget();
    void trackChangeDuringScrubAbortsScrub();
    void insertedMidPlaybackShowsCurrentState();
    void sliderAndLabelsKeepStableSizes();

private:
    MediaItem makeItem(quint64 id, const QString &title);
    void fillPlaylist(int n);
    void startPlayback();
    void pressSliderAt(const QPoint &pos);
    void releaseSliderAt(const QPoint &pos);

    FakePlayer m_fake{this};
    PlaybackManager* m_pm = nullptr;
    PlaylistManager* m_pl = nullptr;
    MediaItemStore* m_store = nullptr;
    // Both managers are write-through: keep their database private to this
    // test (a shared default-path file would collide on the explicit ids).
    std::unique_ptr<QTemporaryDir> m_dir;
    PositionSliderWidget* m_widget = nullptr;
    QSlider* m_slider = nullptr;
    QLabel* m_positionLabel = nullptr;
    QLabel* m_durationLabel = nullptr;
};

void PositionSliderTest::init()
{
    m_pl = PlaylistManager::getInstance();
    m_store = MediaItemStore::getInstance();
    m_dir = std::make_unique<QTemporaryDir>();
    test::usePrivateDatabase(m_dir->path());
    m_pl->reset();
    m_store->clear();

    m_pm = PlaybackManager::getInstance();
    test::installFakePlayer(m_pm, &m_fake);

    m_widget = new PositionSliderWidget();
    m_widget->show();

    m_slider = m_widget->findChild<QSlider*>("positionSlider");
    m_positionLabel = m_widget->findChild<QLabel*>("positionLabel");
    m_durationLabel = m_widget->findChild<QLabel*>("durationLabel");
    QVERIFY(m_slider != nullptr);
    QVERIFY(m_positionLabel != nullptr);
    QVERIFY(m_durationLabel != nullptr);
}

void PositionSliderTest::cleanup()
{
    delete m_widget;
    m_pm->stop();
    m_pl->reset();
    m_store->clear();
}

// Builds the item and registers it in the standalone item store, so
// currentItem() can resolve it.
MediaItem PositionSliderTest::makeItem(quint64 id, const QString &title)
{
    MediaItem item;
    item.id = id;
    item.title = title;
    // A real (dummy) file: playback is checked against the disk.
    const QString path = "/tmp/" + title + ".mp3";
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

void PositionSliderTest::fillPlaylist(int n)
{
    QList<quint64> ids;
    for (int i = 1; i <= n; ++i)
        ids.append(makeItem(i, QString("Track%1").arg(i)).id);
    m_pl->addItems(ids);
}

void PositionSliderTest::startPlayback()
{
    fillPlaylist(2);
    m_pm->playCurrent();
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    m_fake.triggerDuration(215000);
}

void PositionSliderTest::pressSliderAt(const QPoint &pos)
{
    QMouseEvent ev(QEvent::MouseButtonPress, QPointF(pos), QPointF(pos),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(m_slider, &ev);
}

void PositionSliderTest::releaseSliderAt(const QPoint &pos)
{
    QMouseEvent ev(QEvent::MouseButtonRelease, QPointF(pos), QPointF(pos),
                   Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(m_slider, &ev);
}

void PositionSliderTest::registeredInLayoutManager()
{
    WidgetConfiguration cfg;
    BaseWidget* w = LayoutManager::getInstance()->createWidget(PositionSliderWidget::id, cfg);

    QVERIFY(w != nullptr);
    QCOMPARE(w->widgetId(), PositionSliderWidget::id);
    QCOMPARE(w->widgetTitle(), PositionSliderWidget::title);
    delete w;
}

void PositionSliderTest::initialStateDisabledAndEmpty()
{
    QVERIFY(!m_slider->isEnabled());
    QCOMPARE(m_slider->maximum(), 0);
    QCOMPARE(m_slider->value(), 0);
    QCOMPARE(m_positionLabel->text(), QString("0:00"));
    QCOMPARE(m_durationLabel->text(), QString("0:00"));
}

void PositionSliderTest::playingTrackEnablesSliderWithDuration()
{
    startPlayback();

    QVERIFY(m_slider->isEnabled());
    QCOMPARE(m_slider->maximum(), 215000);
    QCOMPARE(m_durationLabel->text(), QString("3:35"));
}

void PositionSliderTest::positionUpdatesSliderAndLabel()
{
    startPlayback();

    m_fake.triggerPosition(65000);

    QCOMPARE(m_slider->value(), 65000);
    QCOMPARE(m_positionLabel->text(), QString("1:05"));
}

void PositionSliderTest::scrubbingSeeksPlayer()
{
    startPlayback();

    m_slider->setValue(90000);
    Q_EMIT m_slider->sliderReleased();

    QCOMPARE(m_fake.m_position, 90000);
    QCOMPARE(m_fake.m_seekCount, 1);
}

void PositionSliderTest::positionChangedWhileScrubbingIgnored()
{
    startPlayback();

    Q_EMIT m_slider->sliderPressed();
    m_slider->setValue(90000);

    m_fake.triggerPosition(65000);

    // The slider must not jump back to the reported position while the user
    // is dragging it.
    QCOMPARE(m_slider->value(), 90000);

    Q_EMIT m_slider->sliderReleased();
    QCOMPARE(m_fake.m_position, 90000);
    QCOMPARE(m_fake.m_seekCount, 1);
}

void PositionSliderTest::scrubbingWhileStoppedDoesNothing()
{
    m_slider->setValue(5000);
    Q_EMIT m_slider->sliderReleased();

    QCOMPARE(m_fake.m_seekCount, 0);
    QCOMPARE(m_fake.m_position, 0);
}

void PositionSliderTest::grooveClickSeeksToClickedPosition()
{
    startPlayback();
    m_widget->resize(400, 50);
    QTest::qWait(0);

    // Click the groove far from the handle (which sits at 0): the press
    // alone must jump the handle to the clicked position, not step it by
    // one page.
    const QPoint pos(static_cast<int>(m_slider->width() * 0.75), m_slider->height() / 2);
    pressSliderAt(pos);

    const int clicked = m_slider->value();
    QVERIFY(clicked > m_slider->maximum() / 2);
    QVERIFY(clicked < m_slider->maximum());

    releaseSliderAt(pos);

    QCOMPARE(m_fake.m_seekCount, 1);
    QCOMPARE(m_fake.m_position, clicked);
}

void PositionSliderTest::grooveClickHeldSuppressesPositionUpdates()
{
    startPlayback();
    m_widget->resize(400, 50);
    QTest::qWait(0);

    const QPoint pos(m_slider->width() / 2, m_slider->height() / 2);
    pressSliderAt(pos);

    const int clicked = m_slider->value();
    QVERIFY(clicked > m_slider->maximum() / 4);
    QVERIFY(clicked < 3 * m_slider->maximum() / 4);

    // While the click is held, reported positions must not yank the handle
    // back, exactly like a held drag.
    m_fake.triggerPosition(65000);
    QCOMPARE(m_slider->value(), clicked);

    releaseSliderAt(pos);

    QCOMPARE(m_fake.m_seekCount, 1);
    QCOMPARE(m_fake.m_position, clicked);
}

void PositionSliderTest::stoppingResetsWidget()
{
    startPlayback();
    m_fake.triggerPosition(65000);
    QCOMPARE(m_slider->value(), 65000);

    m_pm->stop();

    QVERIFY(!m_slider->isEnabled());
    QCOMPARE(m_slider->maximum(), 0);
    QCOMPARE(m_slider->value(), 0);
    QCOMPARE(m_positionLabel->text(), QString("0:00"));
    QCOMPARE(m_durationLabel->text(), QString("0:00"));
}

void PositionSliderTest::trackChangeDuringScrubAbortsScrub()
{
    // A track change while the user is holding the slider (auto-advance)
    // must abort the stale scrub: the slider follows the new track instead
    // of keeping a position that belongs to the old one.
    fillPlaylist(2);
    m_pm->playCurrent(); // track 1
    m_fake.triggerDuration(215000);

    Q_EMIT m_slider->sliderPressed();
    m_slider->setValue(90000); // the user is dragging

    // Track 2 starts while the user is still holding the slider.
    m_pm->playFromPlaylist(m_pl->currentPlaylistId(), 1);
    m_fake.triggerDuration(100000);

    // The scrub is aborted, so reported positions of the new track are
    // applied to the slider again.
    m_fake.triggerPosition(5000);
    QCOMPARE(m_slider->value(), 5000);
    QCOMPARE(m_positionLabel->text(), QString("0:05"));
}

void PositionSliderTest::insertedMidPlaybackShowsCurrentState()
{
    // A widget created while something is already playing must start from
    // the current state: the playback signals only fire for future changes,
    // so the constructor has to sync from the live state.
    fillPlaylist(2);
    m_pm->playCurrent();
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    m_fake.triggerDuration(215000);
    m_fake.triggerPosition(65000);

    // The init() widget was built before playback, so it is stale. A
    // freshly created widget must reflect the live state immediately.
    PositionSliderWidget fresh;
    QSlider* slider = fresh.findChild<QSlider*>("positionSlider");
    QLabel* posLabel = fresh.findChild<QLabel*>("positionLabel");
    QLabel* durLabel = fresh.findChild<QLabel*>("durationLabel");
    QVERIFY(slider != nullptr);

    QVERIFY(slider->isEnabled());
    QCOMPARE(slider->maximum(), 215000);
    QCOMPARE(slider->value(), 65000);
    QCOMPARE(posLabel->text(), QString("1:05"));
    QCOMPARE(durLabel->text(), QString("3:35"));
}

void PositionSliderTest::sliderAndLabelsKeepStableSizes()
{
    // The slider is the row's only stretchable part: it must keep a
    // usable width in a collapsed widget (an unmovable handle is
    // unusable, and there is nothing else left to drag).
    QVERIFY(m_slider->minimumWidth() > 0);

    // The labels must not resize with their text while a track plays
    // ("0:00" .. "3:35"): that would steal space from the slider.
    // Their minimum covers the widest text they show — the duration.
    startPlayback(); // 215000 ms -> "3:35"
    const QFontMetrics fm(m_positionLabel->font());
    QCOMPARE(m_positionLabel->minimumWidth(), fm.horizontalAdvance(QStringLiteral("3:35")));
    QCOMPARE(m_durationLabel->minimumWidth(), fm.horizontalAdvance(QStringLiteral("3:35")));

    // The minimum must be binding as the position text grows.
    m_fake.triggerPosition(65000);
    QCOMPARE(m_positionLabel->text(), QString("1:05"));
    QCOMPARE(m_positionLabel->minimumWidth(), fm.horizontalAdvance(QStringLiteral("3:35")));

    // A longer track widens the pair's minimum with it.
    m_fake.triggerDuration(7200000); // 2 hours -> "120:00"
    QCOMPARE(m_durationLabel->text(), QString("120:00"));
    QCOMPARE(m_positionLabel->minimumWidth(), fm.horizontalAdvance(QStringLiteral("120:00")));
    QCOMPARE(m_durationLabel->minimumWidth(), fm.horizontalAdvance(QStringLiteral("120:00")));

    // Stopping shrinks it back to the "0:00" width.
    m_pm->stop();
    QCOMPARE(m_positionLabel->minimumWidth(), fm.horizontalAdvance(QStringLiteral("0:00")));
    QCOMPARE(m_durationLabel->minimumWidth(), fm.horizontalAdvance(QStringLiteral("0:00")));
}
}


QTEST_MAIN(PositionSliderTest)

#include "positionslider_test.moc"
