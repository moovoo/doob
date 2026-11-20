#include <QtTest>
#include <QFile>

#include <QAction>
#include <QFontMetrics>
#include <QMenu>
#include <QPixmap>
#include <QTemporaryDir>

#include <memory>

#include "layoutmanager.h"
#include "tracklabel.h"
#include "testsupport.h"

namespace {

// A valid item for the pattern engine (id + source; no file needed —
// the engine never touches the disk).
MediaItem engineItem()
{
    MediaItem item;
    item.id = 1;
    item.source = QUrl::fromLocalFile("/tmp/engine.mp3");
    return item;
}

// The patternDialog seam: records the dialog's seed and answers the
// recorded pattern (an empty string = cancel).
class TestTrackLabel : public TrackLabelWidget
{
public:
    QString next;
    QString lastCurrent;

    QString patternDialog(const QString &current) override
    {
        lastCurrent = current;
        return next;
    }
};

class TrackLabelWidgetTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void registeredInLayoutManager();
    // The pattern engine (the pure static).
    void defaultPatternRendered();
    void allPlaceholdersResolved();
    void emptyFieldsRenderEmpty();
    void unknownPlaceholderRendersEmpty();
    void unterminatedPlaceholderIsLiteral();
    void closingParenthesisFollowsItsField();
    void extraTagResolvedCaseInsensitively();
    void prefixLiteralKept();
    void placeholderlessPatternIsPlainText();
    void invalidItemRendersEmpty();
    // The widget over the manager.
    void initiallyEmpty();
    void newWidgetPicksUpAlreadyPlayingTrack();
    void switchingTrackUpdatesText();
    void stoppingClearsText();
    void patternChangeRerenders();
    void tooltipCarriesFullText();
    // Persistence.
    void patternNotStoredWhenDefault();
    void patternStoredWhenChanged();
    void patternRestoredThroughRegistry();
    // The marquee (off by default: a static label).
    void marqueeScrollsLongText();
    void marqueeIdleWhenTextFits();
    void marqueeResumesWhenShrunk();
    void scrollOffsetWraps();
    void marqueeKeepsTextVisible();
    void scrollToggledFromEditMenu();
    void scrollStatePersists();
    void staticLabelDrawsFromLeft();
    // The surface.
    void minimumSizeFitsOneLine();
    void paintsFrameAndText();
    // The edit-mode menu.
    void contextMenuOffersPatternAction();
    void patternActionEditsPattern();

private:
    MediaItem makeItem(quint64 id, const QString &title, const QString &artist,
                       const QString &album, int year);
    void fillPlaylist(int n);
    void startPlayback();

    FakePlayer m_fake{this};
    PlaybackManager* m_pm = nullptr;
    PlaylistManager* m_pl = nullptr;
    MediaItemStore* m_store = nullptr;
    // Both managers are write-through: keep their database private to
    // this test (a shared default-path file would collide on the
    // explicit ids).
    std::unique_ptr<QTemporaryDir> m_dir;
    TrackLabelWidget* m_widget = nullptr;
};

void TrackLabelWidgetTest::init()
{
    m_pl = PlaylistManager::getInstance();
    m_store = MediaItemStore::getInstance();
    m_dir = std::make_unique<QTemporaryDir>();
    test::usePrivateDatabase(m_dir->path());
    m_pl->reset();
    m_store->clear();

    m_pm = PlaybackManager::getInstance();
    test::installFakePlayer(m_pm, &m_fake);

    m_widget = new TrackLabelWidget();
    m_widget->show();
    qApp->processEvents();
}

void TrackLabelWidgetTest::cleanup()
{
    delete m_widget;
    m_pm->stop();
    m_pl->reset();
    m_store->clear();
    LayoutManager::getInstance()->setEditMode(true);
}

// Builds the item and registers it in the standalone item store, so
// currentItem() can resolve it.
MediaItem TrackLabelWidgetTest::makeItem(quint64 id, const QString &title,
                                         const QString &artist, const QString &album,
                                         int year)
{
    MediaItem item;
    item.id = id;
    item.title = title;
    item.artist = artist;
    item.album = album;
    item.year = year;
    item.durationMs = 215000;
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

void TrackLabelWidgetTest::fillPlaylist(int n)
{
    QList<quint64> ids;
    for (int i = 1; i <= n; ++i)
        ids.append(makeItem(i,
                            QString("Track%1").arg(i),
                            QString("Artist%1").arg(i),
                            QString("Album%1").arg(i),
                            2000 + i).id);
    m_pl->addItems(ids);
}

void TrackLabelWidgetTest::startPlayback()
{
    fillPlaylist(2);
    m_pm->playCurrent();
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
}

void TrackLabelWidgetTest::registeredInLayoutManager()
{
    WidgetConfiguration cfg;
    BaseWidget* w = LayoutManager::getInstance()->createWidget(TrackLabelWidget::id, cfg);

    QVERIFY(w != nullptr);
    QCOMPARE(w->widgetId(), TrackLabelWidget::id);
    QCOMPARE(w->widgetTitle(), TrackLabelWidget::title);
    delete w;
}

// The pattern engine: the default pattern over the standard fields.
void TrackLabelWidgetTest::defaultPatternRendered()
{
    MediaItem item = engineItem();
    item.title = "Title";
    item.artist = "Artist";
    item.year = 2001;

    QCOMPARE(TrackLabelWidget::renderPattern(TrackLabelWidget::defaultPattern(), item),
             QString("Title - Artist - 2001"));
}

void TrackLabelWidgetTest::allPlaceholdersResolved()
{
    MediaItem item = engineItem();
    item.title = "T";
    item.artist = "A";
    item.album = "B";
    item.albumArtist = "AA";
    item.composer = "C";
    item.genre = "G";
    item.comment = "K";
    item.year = 1999;
    item.trackNumber = 3;
    item.trackTotal = 12;
    item.discNumber = 2;
    item.discTotal = 2;
    item.durationMs = 215000;
    item.bitrate = 320;
    item.sampleRate = 44100;
    item.channels = 2;
    item.source = QUrl::fromLocalFile("/tmp/T.mp3");

    QCOMPARE(TrackLabelWidget::renderPattern(
                 QStringLiteral("{title}|{artist}|{album}|{albumartist}|{composer}|"
                                "{genre}|{year}|{track}|{disc}|{duration}|{comment}|"
                                "{bitrate}|{samplerate}|{channels}|{file}"),
                 item),
             QStringLiteral("T|A|B|AA|C|G|1999|3/12|2/2|3:35|K|320 kbps|44100 Hz|2"
                            "|/tmp/T.mp3"));
}

void TrackLabelWidgetTest::emptyFieldsRenderEmpty()
{
    MediaItem item = engineItem();
    item.title = "OnlyTitle";

    QCOMPARE(TrackLabelWidget::renderPattern(
                 QStringLiteral("{title} - {artist} - {year}"), item),
             QStringLiteral("OnlyTitle -  - "));
}

void TrackLabelWidgetTest::unknownPlaceholderRendersEmpty()
{
    MediaItem item = engineItem();
    item.title = "T";

    QCOMPARE(TrackLabelWidget::renderPattern(QStringLiteral("{title} {nope}"), item),
             QStringLiteral("T "));
}

void TrackLabelWidgetTest::unterminatedPlaceholderIsLiteral()
{
    MediaItem item = engineItem();
    item.title = "T";

    QCOMPARE(TrackLabelWidget::renderPattern(QStringLiteral("{title} {open"), item),
             QStringLiteral("T {open"));
    QCOMPARE(TrackLabelWidget::renderPattern(QStringLiteral("}{title}"), item),
             QStringLiteral("}T"));
}

// The literals around a field render as typed, whatever the field's
// value (an empty field leaves the bare literals).
void TrackLabelWidgetTest::closingParenthesisFollowsItsField()
{
    MediaItem item = engineItem();
    item.title = "T";
    item.artist = "A";

    QCOMPARE(TrackLabelWidget::renderPattern(
                 QStringLiteral("{title} ({artist})"), item),
             QStringLiteral("T (A)"));
    QCOMPARE(TrackLabelWidget::renderPattern(
                 QStringLiteral("{artist} ({title})"), item),
             QStringLiteral("A (T)"));

    item.artist.clear();
    QCOMPARE(TrackLabelWidget::renderPattern(
                 QStringLiteral("{title} ({artist})"), item),
             QStringLiteral("T ()"));
}

// A prefix literal renders with the field (and with the empty field's
// empty value).
void TrackLabelWidgetTest::prefixLiteralKept()
{
    MediaItem item = engineItem();
    item.title = "T";

    QCOMPARE(TrackLabelWidget::renderPattern(QStringLiteral("Now: {title}"), item),
             QStringLiteral("Now: T"));
    QCOMPARE(TrackLabelWidget::renderPattern(QStringLiteral("Now: {artist}"), item),
             QStringLiteral("Now: "));
}

void TrackLabelWidgetTest::placeholderlessPatternIsPlainText()
{
    MediaItem item = engineItem();
    item.title = "T";

    QCOMPARE(TrackLabelWidget::renderPattern(QStringLiteral("now playing"), item),
             QStringLiteral("now playing"));
}

void TrackLabelWidgetTest::invalidItemRendersEmpty()
{
    // No track: a placeholder pattern renders nothing (not the bare
    // separators).
    QCOMPARE(TrackLabelWidget::renderPattern(
                 TrackLabelWidget::defaultPattern(), MediaItem()),
             QString());
}

void TrackLabelWidgetTest::extraTagResolvedCaseInsensitively()
{
    MediaItem item = engineItem();
    item.title = "T";
    // The extra keys are stored normalized to uppercase.
    item.extra.insert("BPM", "128");

    QCOMPARE(TrackLabelWidget::renderPattern(QStringLiteral("{title} @ {bpm}"), item),
             QStringLiteral("T @ 128"));
}

void TrackLabelWidgetTest::initiallyEmpty()
{
    QCOMPARE(m_widget->text(), QString());
}

void TrackLabelWidgetTest::newWidgetPicksUpAlreadyPlayingTrack()
{
    startPlayback();

    // The playingItemChanged signal fired before this widget existed, so
    // the constructor must sync with the current item on its own. Create
    // it the same way the "Set widget..." dialog does.
    WidgetConfiguration cfg;
    BaseWidget* w = LayoutManager::getInstance()->createWidget(TrackLabelWidget::id, cfg);
    QVERIFY(w != nullptr);

    TrackLabelWidget* label = qobject_cast<TrackLabelWidget*>(w);
    QVERIFY(label != nullptr);
    QCOMPARE(label->text(), QString("Track1 - Artist1 - 2001"));
    delete w;
}

void TrackLabelWidgetTest::switchingTrackUpdatesText()
{
    startPlayback();
    m_pm->next();

    QCOMPARE(m_widget->text(), QString("Track2 - Artist2 - 2002"));
}

void TrackLabelWidgetTest::stoppingClearsText()
{
    startPlayback();
    QVERIFY(!m_widget->text().isEmpty());

    m_pm->stop();

    QCOMPARE(m_widget->text(), QString());
    QCOMPARE(m_widget->toolTip(), QString());
}

void TrackLabelWidgetTest::patternChangeRerenders()
{
    startPlayback();

    m_widget->setPattern(QStringLiteral("{year} ({album})"));
    QCOMPARE(m_widget->text(), QString("2001 (Album1)"));

    // The same pattern is a no-op.
    m_widget->setPattern(QStringLiteral("{year} ({album})"));
    QCOMPARE(m_widget->pattern(), QString("{year} ({album})"));
}

void TrackLabelWidgetTest::tooltipCarriesFullText()
{
    startPlayback();

    QCOMPARE(m_widget->toolTip(), m_widget->text());
}

void TrackLabelWidgetTest::patternNotStoredWhenDefault()
{
    const WidgetConfiguration cfg = m_widget->storeWidget();
    QVERIFY(!cfg.contains("pattern"));
}

void TrackLabelWidgetTest::patternStoredWhenChanged()
{
    m_widget->setPattern(QStringLiteral("{title}"));

    const WidgetConfiguration cfg = m_widget->storeWidget();
    QCOMPARE(cfg.value("pattern").toString(), QString("{title}"));
}

void TrackLabelWidgetTest::patternRestoredThroughRegistry()
{
    startPlayback();
    m_widget->setPattern(QStringLiteral("{year} - {title}"));
    QCOMPARE(m_widget->text(), QString("2001 - Track1"));

    WidgetConfiguration cfg = m_widget->storeWidget();
    BaseWidget* w = LayoutManager::getInstance()->createWidget(TrackLabelWidget::id, cfg);
    QVERIFY(w != nullptr);

    TrackLabelWidget* label = qobject_cast<TrackLabelWidget*>(w);
    QVERIFY(label != nullptr);
    QCOMPARE(label->pattern(), QString("{year} - {title}"));
    // A fresh instance renders the current item with the restored
    // pattern.
    QCOMPARE(label->text(), QString("2001 - Track1"));
    delete w;
}

void TrackLabelWidgetTest::marqueeScrollsLongText()
{
    startPlayback();
    m_widget->resize(100, 30);
    qApp->processEvents();

    // A pattern long enough not to fit the 98 px content.
    m_widget->setPattern(QString(120, QLatin1Char('x')));
    // The marquee is off by default (a static label).
    QVERIFY(!m_widget->scrolling());

    m_widget->setScroll(true);
    QVERIFY(m_widget->scrolling());

    QTRY_COMPARE_WITH_TIMEOUT(m_widget->scrollOffset() > 0, true, 2000);
}

void TrackLabelWidgetTest::marqueeIdleWhenTextFits()
{
    startPlayback();
    m_widget->resize(800, 40);
    qApp->processEvents();

    QVERIFY(!m_widget->scrolling());
    QCOMPARE(m_widget->scrollOffset(), 0);

    // Even with scrolling on, a fit is no marquee.
    m_widget->setScroll(true);
    QVERIFY(!m_widget->scrolling());
}

void TrackLabelWidgetTest::marqueeResumesWhenShrunk()
{
    startPlayback();
    m_widget->resize(800, 40);
    qApp->processEvents();
    QVERIFY(!m_widget->scrolling());
    m_widget->setScroll(true);

    m_widget->resize(100, 30);
    qApp->processEvents();

    QVERIFY(m_widget->scrolling());
}

// The offset advances one step per tick, stays below the cycle (one
// text plus the separator), and wraps at it: a decreasing offset.
void TrackLabelWidgetTest::scrollOffsetWraps()
{
    startPlayback();
    // 8 chars do not fit the 28 px content of a 30 px widget.
    m_widget->setPattern(QStringLiteral("xxxxxxxx"));
    m_widget->resize(30, 30);
    qApp->processEvents();
    m_widget->setScroll(true);
    QVERIFY(m_widget->scrolling());

    const int cycle = m_widget->scrollCycle();
    QVERIFY(cycle > 0);
    int last = m_widget->scrollOffset();
    bool wrapped = false;
    for (int i = 0; i < 400 && !wrapped; ++i) {
        QTest::qWait(25);
        const int now = m_widget->scrollOffset();
        QVERIFY(now < cycle);
        if (now < last)
            wrapped = true;
        last = now;
    }
    QVERIFY(wrapped);
}

// The cyclic stream keeps the label full of text at any scroll
// position (the repetitions leave no empty gap).
void TrackLabelWidgetTest::marqueeKeepsTextVisible()
{
    startPlayback();
    m_widget->resize(100, 30);
    qApp->processEvents();
    // A pattern long enough not to fit the 98 px content.
    m_widget->setPattern(QString(120, QLatin1Char('x')));
    m_widget->setScroll(true);
    QVERIFY(m_widget->scrolling());

    const QColor base = m_widget->palette().color(QPalette::Base);
    for (int i = 0; i < 20; ++i) {
        QTest::qWait(25);
        const QImage image = m_widget->grab().toImage();
        bool visible = false;
        for (int x = 1; x < image.width() - 1 && !visible; ++x)
            if (image.pixelColor(x, image.height() / 2).rgb() != base.rgb())
                visible = true;
        QVERIFY(visible);
    }
}

// The edit-mode menu carries the marquee switch (off by default: a
// static label); a trigger re-renders and the rebuilt menu carries
// the new state.
void TrackLabelWidgetTest::scrollToggledFromEditMenu()
{
    startPlayback();
    m_widget->resize(100, 30);
    qApp->processEvents();
    m_widget->setPattern(QString(120, QLatin1Char('x')));
    QVERIFY(!m_widget->scrolling());

    QMenu* menu = m_widget->widgetContextMenu();
    QVERIFY(menu != nullptr);
    QAction* scroll = nullptr;
    for (QAction* a : menu->actions())
        if (a->text() == QLatin1String("Scroll"))
            scroll = a;
    if (scroll == nullptr) {
        menu->deleteLater();
        QFAIL("the menu has no Scroll action");
    }
    QVERIFY(scroll->isCheckable());
    QVERIFY(!scroll->isChecked());

    scroll->trigger();
    menu->deleteLater();
    QVERIFY(m_widget->scrolling());

    // The rebuilt menu carries the new state.
    menu = m_widget->widgetContextMenu();
    for (QAction* a : menu->actions())
        if (a->text() == QLatin1String("Scroll"))
            QVERIFY(a->isChecked());
    menu->deleteLater();
}

// The scroll state travels with the widget: the default (static) is
// not persisted, a non-default one is, and a restored instance
// carries it.
void TrackLabelWidgetTest::scrollStatePersists()
{
    startPlayback();
    WidgetConfiguration cfg = m_widget->storeWidget();
    QVERIFY(!cfg.contains("scroll"));

    m_widget->setScroll(true);
    cfg = m_widget->storeWidget();
    QCOMPARE(cfg.value("scroll").toBool(), true);

    BaseWidget* w = LayoutManager::getInstance()->createWidget(TrackLabelWidget::id, cfg);
    if (w == nullptr)
        QFAIL("createWidget returned no widget");
    TrackLabelWidget* label = qobject_cast<TrackLabelWidget*>(w);
    if (label == nullptr) {
        delete w;
        QFAIL("not a TrackLabelWidget");
    }
    QVERIFY(label->scroll());
    delete w;
}

// A static label with non-fitting text (scrolling off): the text is
// drawn from the left edge and clipped at the right one (its
// beginning stays visible, not centered).
void TrackLabelWidgetTest::staticLabelDrawsFromLeft()
{
    startPlayback();
    m_widget->resize(100, 30);
    qApp->processEvents();
    m_widget->setPattern(QString(120, QLatin1Char('x')));
    QVERIFY(!m_widget->scrolling());

    const QImage image = m_widget->grab().toImage();
    const QColor base = m_widget->palette().color(QPalette::Base);
    const int y = image.height() / 2;
    int first = -1;
    for (int x = 1; x < image.width() - 1 && first < 0; ++x)
        if (image.pixelColor(x, y).rgb() != base.rgb())
            first = x;
    // The first glyph starts at the content's left edge (the 1 px
    // frame plus the glyph's small bearing).
    QVERIFY(first > 0);
    QVERIFY(first < 6);
}

// The label's minimum keeps it visible in a tight layout (never 0):
// the border plus one character wide, one line of text high.
void TrackLabelWidgetTest::minimumSizeFitsOneLine()
{
    const QFontMetrics fm = m_widget->fontMetrics();
    QCOMPARE(m_widget->minimumWidth(),
             fm.horizontalAdvance(QLatin1Char('x')) + 2);
    QCOMPARE(m_widget->minimumHeight(), fm.height() + 2);
    QVERIFY(m_widget->minimumWidth() > 0);
    QVERIFY(m_widget->minimumHeight() > 0);
}

void TrackLabelWidgetTest::paintsFrameAndText()
{
    startPlayback();
    m_widget->resize(200, 40);
    qApp->processEvents();
    QVERIFY(!m_widget->scrolling()); // centered branch

    const QImage image = m_widget->grab().toImage();
    const QColor base = m_widget->palette().color(QPalette::Base);
    const QColor mid = m_widget->palette().color(QPalette::Mid);

    // The border is painted along the edge (compared by .rgb(): this
    // Qt's QColor operator== is representation-sensitive).
    QCOMPARE(image.pixelColor(0, 0).rgb(), mid.rgb());
    QCOMPARE(image.pixelColor(image.width() - 1, image.height() / 2).rgb(), mid.rgb());
    // The background fills the inside.
    QCOMPARE(image.pixelColor(10, 5).rgb(), base.rgb());
    // The centered text leaves non-background pixels on the middle row
    // (matched against the background, not the text color: antialiasing
    // blends the glyph edges and a thin stem may never reach the full
    // text color in a single pixel).
    bool hasTextPixel = false;
    for (int x = 1; x < image.width() - 1 && !hasTextPixel; ++x)
        if (image.pixelColor(x, image.height() / 2).rgb() != base.rgb())
            hasTextPixel = true;
    QVERIFY(hasTextPixel);
}

void TrackLabelWidgetTest::contextMenuOffersPatternAction()
{
    // The edit-mode menu: the base's "Style this widget..." action plus
    // this widget's own "Pattern..." entry.
    QMenu* menu = m_widget->widgetContextMenu();
    QVERIFY(menu != nullptr);

    QStringList texts;
    QAction* pattern = nullptr;
    for (QAction* a : menu->actions()) {
        if (a->text() == QLatin1String("Pattern..."))
            pattern = a;
        if (!a->isSeparator())
            texts << a->text();
    }
    QVERIFY(pattern != nullptr);
    QVERIFY(texts.contains(QLatin1String("Style this widget...")));
    menu->deleteLater();
}

// The "Pattern..." action feeds the dialog's accepted text into
// setPattern (an empty answer = cancel, the pattern is kept).
void TrackLabelWidgetTest::patternActionEditsPattern()
{
    startPlayback();
    TestTrackLabel* label = new TestTrackLabel();
    label->next = QStringLiteral("{artist} / {title}");

    QMenu* menu = label->widgetContextMenu();
    QVERIFY(menu != nullptr);
    QAction* pattern = nullptr;
    for (QAction* a : menu->actions())
        if (a->text() == QLatin1String("Pattern..."))
            pattern = a;
    QVERIFY(pattern != nullptr);
    if (pattern == nullptr)
        return; // the QVERIFY above already failed (analyzer guard)

    pattern->trigger();

    QCOMPARE(label->lastCurrent, TrackLabelWidget::defaultPattern());
    QCOMPARE(label->pattern(), QString("{artist} / {title}"));
    QCOMPARE(label->text(), QString("Artist1 / Track1"));

    // Cancel: the pattern is kept.
    label->next = QString();
    pattern->trigger();
    QCOMPARE(label->pattern(), QString("{artist} / {title}"));

    delete menu;
    delete label;
}

}

QTEST_MAIN(TrackLabelWidgetTest)
#include "tracklabel_test.moc"
