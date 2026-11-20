#include <QtTest>
#include <QFile>

#include <QAbstractTextDocumentLayout>
#include <QContextMenuEvent>
#include <QDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QScrollArea>
#include <QTimer>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextFragment>
#include <QTextLayout>
#include <QUrl>
#include <QTemporaryDir>

#include <memory>

#include "layoutmanager.h"
#include "currenttrackwidget.h"
#include "settingsmanager.h"
#include "testsupport.h"
#include "widgetcontainer.h"

namespace {

class CurrentTrackWidgetTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void registeredInLayoutManager();
    void initiallyEmpty();
    void newWidgetPicksUpAlreadyPlayingTrack();
    void playingTrackShowsOnlySetFields();
    void switchingTrackUpdatesMetadata();
    void extraTagsShown();
    void stoppingClearsMetadata();
    void streamInfoShown();
    void newTrackClearsStreamInfo();
    void fileLinkActivatesOnClick();
    void editModeRightClickShowsContainerMenuOnly();
    void normalModeRightClickShowsLabelMenu();
    void artBlockHiddenWithoutArt();
    void artBlockShowsPlayingItemArt();
    void artBlockFollowsPlayingItemAndArtChanges();
    void lineCatalogHasTheDefaultOrder();
    void alwaysLineWithEmptyValueRendersNameAlone();
    void neverLineRendersNothing();
    void alwaysArtRendersPlaceholderWithoutArt();
    void extraGroupCanBeTurnedOff();
    void extraGroupFollowsTheSequence();
    void storeRestoresNonDefaultsRoundTrip();
    void restoreDropsStaleKeysAndValues();
    void editMenuOffersLinesAction();

private:
    MediaItem makeItem(quint64 id, const QString &title, const QString &artist,
                       const QString &album, qint64 durationMs);
    void fillPlaylist(int n);
    void startPlayback();
    static QMenu* findOpenMenu();
    static QStringList menuTexts(const QMenu* menu);
    // Pixel rect of the link with the given URL inside the label's rich text,
    // in label-local coordinates (mirrors the label's own document layout).
    static QRect linkRect(const QLabel* view, const QString &href);

    FakePlayer m_fake{this};
    PlaybackManager* m_pm = nullptr;
    PlaylistManager* m_pl = nullptr;
    MediaItemStore* m_store = nullptr;
    // Both managers are write-through: keep their database private to this
    // test (a shared default-path file would collide on the explicit ids).
    std::unique_ptr<QTemporaryDir> m_dir;
    CurrentTrackWidget* m_widget = nullptr;
    QLabel* m_view = nullptr;
    QLabel* m_art = nullptr;
};

void CurrentTrackWidgetTest::init()
{
    m_pl = PlaylistManager::getInstance();
    m_store = MediaItemStore::getInstance();
    m_dir = std::make_unique<QTemporaryDir>();
    test::usePrivateDatabase(m_dir->path());
    m_pl->reset();
    m_store->clear();

    m_pm = PlaybackManager::getInstance();
    test::installFakePlayer(m_pm, &m_fake);

    // The Lines... dialog's UiState wiring writes through to the
    // SettingsManager: keep that store private too, so the test does
    // not touch the user's settings.json.
    SettingsManager* settings = SettingsManager::getInstance();
    settings->setSettingsPath(m_dir->path() + "/settings.json");
    settings->load();

    m_widget = new CurrentTrackWidget();
    m_widget->show();
    qApp->processEvents();

    m_view = m_widget->findChild<QLabel*>("currentTrackView");
    QVERIFY(m_view != nullptr);
    m_art = m_widget->findChild<QLabel*>("currentTrackArt");
    QVERIFY(m_art != nullptr);
}

void CurrentTrackWidgetTest::cleanup()
{
    delete m_widget;
    m_pm->stop();
    m_pl->reset();
    m_store->clear();
    SettingsManager::reset();
    LayoutManager::getInstance()->setEditMode(true);
}

// Builds the item and registers it in the standalone item store, so
// currentItem() can resolve it.
MediaItem CurrentTrackWidgetTest::makeItem(quint64 id, const QString &title,
                                           const QString &artist, const QString &album,
                                           qint64 durationMs)
{
    MediaItem item;
    item.id = id;
    item.title = title;
    item.artist = artist;
    item.album = album;
    item.durationMs = durationMs;
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

void CurrentTrackWidgetTest::fillPlaylist(int n)
{
    const QList<qint64> durations = {215000, 300000};
    QList<quint64> ids;
    for (int i = 1; i <= n; ++i)
        ids.append(makeItem(i,
                            QString("Track%1").arg(i),
                            QString("Artist%1").arg(i),
                            QString("Album%1").arg(i),
                            durations.at(i - 1)).id);
    m_pl->addItems(ids);
}

void CurrentTrackWidgetTest::startPlayback()
{
    fillPlaylist(2);
    m_pm->playCurrent();
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
}

QMenu* CurrentTrackWidgetTest::findOpenMenu()
{
    for (QWidget* w : QApplication::topLevelWidgets()) {
        QMenu* menu = qobject_cast<QMenu*>(w);
        if (menu != nullptr && menu->isVisible())
            return menu;
    }
    return nullptr;
}

QStringList CurrentTrackWidgetTest::menuTexts(const QMenu* menu)
{
    QStringList texts;
    for (QAction* a : menu->actions())
        if (!a->isSeparator())
            texts << a->text();
    return texts;
}

QRect CurrentTrackWidgetTest::linkRect(const QLabel* view, const QString &href)
{
    QTextDocument doc;
    doc.setDefaultFont(view->font());
    doc.setHtml(view->text());
    QTextFrameFormat frameFormat = doc.rootFrame()->frameFormat();
    frameFormat.setMargin(0);
    doc.rootFrame()->setFrameFormat(frameFormat);
    doc.setTextWidth(view->width());

    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        const QRectF blockRect = doc.documentLayout()->blockBoundingRect(block);
        QTextLayout* layout = block.layout();
        for (auto it = block.begin(); it != block.end(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.charFormat().anchorHref() != href)
                continue;
            const QTextLine line = layout->lineForTextPosition(fragment.position());
            if (!line.isValid())
                continue;
            int pos = fragment.position();
            const QRectF lineRect = line.rect();
            const qreal x = blockRect.x() + lineRect.x()
                + line.cursorToX(&pos, QTextLine::Leading);
            return QRectF(x + 1, blockRect.y() + lineRect.y() + 1,
                          10, lineRect.height()).toRect();
        }
    }
    return {};
}

void CurrentTrackWidgetTest::registeredInLayoutManager()
{
    WidgetConfiguration cfg;
    BaseWidget* w = LayoutManager::getInstance()->createWidget(CurrentTrackWidget::id, cfg);

    QVERIFY(w != nullptr);
    QCOMPARE(w->widgetId(), CurrentTrackWidget::id);
    QCOMPARE(w->widgetTitle(), CurrentTrackWidget::title);
    delete w;
}

void CurrentTrackWidgetTest::initiallyEmpty()
{
    QCOMPARE(m_view->text(), QString());
}

void CurrentTrackWidgetTest::newWidgetPicksUpAlreadyPlayingTrack()
{
    startPlayback();

    // The playingItemChanged signal fired before this widget existed, so
    // the constructor must sync with the current item on its own. Create it
    // the same way the "Set widget..." dialog does.
    WidgetConfiguration cfg;
    BaseWidget* w = LayoutManager::getInstance()->createWidget(CurrentTrackWidget::id, cfg);
    QVERIFY(w != nullptr);

    QLabel* view = w->findChild<QLabel*>("currentTrackView");
    QVERIFY(view != nullptr);
    QCOMPARE(view->text(), QString(
        "<b>Title</b>: Track1<br>"
        "<b>Artist</b>: Artist1<br>"
        "<b>Album</b>: Album1<br>"
        "<b>Duration</b>: 3:35<br>"
        "<b>File</b>: <a href=\"file:///tmp/Track1.mp3\">/tmp/Track1.mp3</a>"));
    delete w;
}

void CurrentTrackWidgetTest::playingTrackShowsOnlySetFields()
{
    startPlayback();

    // Bold names, plain values, formatted duration, the file path as a
    // clickable link; empty and zero-valued fields (and the internal
    // id/kind) are absent.
    QCOMPARE(m_view->text(), QString(
        "<b>Title</b>: Track1<br>"
        "<b>Artist</b>: Artist1<br>"
        "<b>Album</b>: Album1<br>"
        "<b>Duration</b>: 3:35<br>"
        "<b>File</b>: <a href=\"file:///tmp/Track1.mp3\">/tmp/Track1.mp3</a>"));
}

void CurrentTrackWidgetTest::switchingTrackUpdatesMetadata()
{
    startPlayback();
    m_pm->next();

    QCOMPARE(m_view->text(), QString(
        "<b>Title</b>: Track2<br>"
        "<b>Artist</b>: Artist2<br>"
        "<b>Album</b>: Album2<br>"
        "<b>Duration</b>: 5:00<br>"
        "<b>File</b>: <a href=\"file:///tmp/Track2.mp3\">/tmp/Track2.mp3</a>"));
}

void CurrentTrackWidgetTest::extraTagsShown()
{
    // A real (dummy) file: playback is checked against the disk.
    {
        QFile f("/tmp/Tagged.mp3");
        if (f.open(QIODevice::WriteOnly))
            f.write("\x00\x01\x02");
    }
    MediaItem item;
    item.id = 9;
    item.title = "Tagged";
    item.source = QUrl::fromLocalFile("/tmp/Tagged.mp3");
    item.extra.insert("BPM", "128");
    item.extra.insert("LYRICIST", "Someone");
    QVERIFY(m_store->insert(item));
    m_pl->addItems({ item.id });
    m_pm->playCurrent();

    // Extra tags render as their own bold-named lines, after the standard
    // ones, in sorted order.
    QCOMPARE(m_view->text(), QString(
        "<b>Title</b>: Tagged<br>"
        "<b>File</b>: <a href=\"file:///tmp/Tagged.mp3\">/tmp/Tagged.mp3</a><br>"
        "<b>BPM</b>: 128<br>"
        "<b>LYRICIST</b>: Someone"));
}

void CurrentTrackWidgetTest::stoppingClearsMetadata()
{
    startPlayback();
    QVERIFY(!m_view->text().isEmpty());

    m_pm->stop();

    QCOMPARE(m_view->text(), QString());
}

// The driver's stream info (the codec, container and the decoded
// stream's actual parameters) renders as its own section below the
// metadata, alongside (not in place of) the tag-derived values.
void CurrentTrackWidgetTest::streamInfoShown()
{
    // An item that carries tag values too: the tags keep their own
    // lines, the stream info gets its section.
    MediaItem item = makeItem(7, "Streamed", "Artist", "Album", 215000);
    item.bitrate = 320;
    item.sampleRate = 44100;
    item.channels = 2;
    m_store->remove(item.id);
    m_store->insert(item);
    m_pl->addItems({ item.id });
    m_pm->playCurrent();

    m_fake.triggerStreamInfo({
        { QStringLiteral("codec"), QStringLiteral("mp3") },
        { QStringLiteral("format"), QStringLiteral("mp3") },
        { QStringLiteral("bitrate"), 128000 },
        { QStringLiteral("sampleRate"), 48000 },
        { QStringLiteral("channels"), 2 },
        { QStringLiteral("channelLayout"), QStringLiteral("stereo") },
    });

    QCOMPARE(m_view->text(), QString(
        "<b>Title</b>: Streamed<br>"
        "<b>Artist</b>: Artist<br>"
        "<b>Album</b>: Album<br>"
        "<b>Duration</b>: 3:35<br>"
        "<b>File</b>: <a href=\"file:///tmp/Streamed.mp3\">/tmp/Streamed.mp3</a><br>"
        "<b>Bitrate</b>: 320 kbps<br>"
        "<b>Sample rate</b>: 44100 Hz<br>"
        "<b>Channels</b>: 2<br>"
        "<b>Stream</b><br>"
        "<b>Codec</b>: mp3<br>"
        "<b>Format</b>: mp3<br>"
        "<b>Bitrate</b>: 128 kbps<br>"
        "<b>Sample rate</b>: 48000 Hz<br>"
        "<b>Channels</b>: 2 (stereo)"));
}

// A stream report belongs to the track that opened it: starting the
// next track clears it (the driver re-reports when the new source
// opens), so the technical lines fall back to the item's tags.
void CurrentTrackWidgetTest::newTrackClearsStreamInfo()
{
    startPlayback();
    m_fake.triggerStreamInfo({
        { QStringLiteral("codec"), QStringLiteral("mp3") },
        { QStringLiteral("format"), QStringLiteral("mp3") },
    });
    QVERIFY(m_view->text().contains("<b>Codec</b>: mp3"));

    m_pm->next();

    // Track 2 has no tags and no (stale) stream report: the technical
    // lines are gone.
    QVERIFY(!m_view->text().contains("Codec"));
    QVERIFY(!m_view->text().contains("Bitrate"));
    QCOMPARE(m_view->text(), QString(
        "<b>Title</b>: Track2<br>"
        "<b>Artist</b>: Artist2<br>"
        "<b>Album</b>: Album2<br>"
        "<b>Duration</b>: 5:00<br>"
        "<b>File</b>: <a href=\"file:///tmp/Track2.mp3\">/tmp/Track2.mp3</a>"));
}

void CurrentTrackWidgetTest::fileLinkActivatesOnClick()
{
    // A track with only a source: the label is a single line whose whole
    // value is a link.
    // A real (dummy) file: playback is checked against the disk.
    {
        QFile f("/tmp/OnlyFile.mp3");
        if (f.open(QIODevice::WriteOnly))
            f.write("\x00\x01\x02");
    }
    MediaItem item;
    item.id = 1;
    item.source = QUrl::fromLocalFile("/tmp/OnlyFile.mp3");
    QVERIFY(m_store->insert(item));
    m_pl->addItems({ item.id });
    m_pm->playCurrent();

    m_widget->resize(400, 200);
    qApp->processEvents();

    const QRect rect = linkRect(m_view, "file:///tmp/OnlyFile.mp3");
    QVERIFY(!rect.isEmpty());

    QSignalSpy spy(m_view, &QLabel::linkActivated);
    QTest::mouseClick(m_view, Qt::LeftButton, Qt::NoModifier,
                      QPoint(rect.left() + 2, rect.center().y()));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QString("file:///tmp/OnlyFile.mp3"));
}

void CurrentTrackWidgetTest::editModeRightClickShowsContainerMenuOnly()
{
    LayoutManager::getInstance()->setEditMode(true);

    WidgetContainer container;
    container.setWidget(new CurrentTrackWidget);
    container.resize(400, 300);
    container.show();
    qApp->processEvents();

    QLabel* view = container.findChild<QLabel*>("currentTrackView");
    QVERIFY(view);

    // A right-click in edit mode opens the container menu, so the widget
    // can be replaced or removed.
    QTest::mouseClick(view, Qt::RightButton, Qt::NoModifier, view->rect().center());

    QMenu* menu = findOpenMenu();
    QVERIFY(menu);
    QVERIFY(menuTexts(menu).contains("Set widget..."));
    menu->hide();
    qApp->processEvents();

    // The context-menu event Qt synthesizes after the right-button release
    // must be swallowed by the container's filter, not handled by the
    // label (which would pop up its own Copy/Select All menu).
    QContextMenuEvent cme(QContextMenuEvent::Mouse, view->rect().center(),
                          view->mapToGlobal(view->rect().center()), Qt::NoModifier);
    qApp->sendEvent(view, &cme);

    QVERIFY(cme.isAccepted());
    QVERIFY(findOpenMenu() == nullptr);
}

void CurrentTrackWidgetTest::normalModeRightClickShowsLabelMenu()
{
    // Outside edit mode the label keeps its own standard context menu
    // (Copy / Select All) for copying the displayed metadata.
    startPlayback();
    LayoutManager::getInstance()->setEditMode(false);
    qApp->processEvents();

    QContextMenuEvent cme(QContextMenuEvent::Mouse, m_view->rect().center(),
                          m_view->mapToGlobal(m_view->rect().center()), Qt::NoModifier);
    qApp->sendEvent(m_view, &cme);
    qApp->processEvents();

    QMenu* menu = findOpenMenu();
    QVERIFY(menu);
    // The action text carries the keyboard accelerator suffix
    // ("Select All\tCtrl+A"), so match on the prefix.
    bool hasSelectAll = false;
    for (QAction* a : menu->actions())
        if (a->text().startsWith("Select All"))
            hasSelectAll = true;
    QVERIFY(hasSelectAll);
    menu->hide();
    qApp->processEvents();
}

void CurrentTrackWidgetTest::artBlockHiddenWithoutArt()
{
    startPlayback();

    // The text-only look: no art label.
    QVERIFY(!m_art->isVisible());
    QVERIFY(m_art->pixmap().isNull());
}

void CurrentTrackWidgetTest::artBlockShowsPlayingItemArt()
{
    startPlayback();
    const MediaItem playing = m_pm->playingItem();
    QVERIFY(playing.isValid());

    const QColor cover(200, 30, 30);
    QVERIFY(m_store->setArtwork(playing.id, "image/png",
                                test::solidImageBytes(cover)));

    QVERIFY(m_art->isVisible());
    const QPixmap pixmap = m_art->pixmap();
    QVERIFY(!pixmap.isNull());
    QCOMPARE(pixmap.toImage().pixelColor(0, 0).rgb(), cover.rgb());
}

void CurrentTrackWidgetTest::artBlockFollowsPlayingItemAndArtChanges()
{
    startPlayback();
    const MediaItem first = m_pm->playingItem();
    const MediaItem second = m_store->item(first.id + 1);

    const QColor cover(200, 30, 30);
    QVERIFY(m_store->setArtwork(first.id, "image/png",
                                test::solidImageBytes(cover)));
    QVERIFY(m_art->isVisible());

    // A switch to an item without art clears the block.
    m_pm->next();
    QCOMPARE(m_pm->playingItem().id, second.id);
    QVERIFY(!m_art->isVisible());

    // The store's art signal for the new playing item shows it.
    const QColor secondCover(30, 30, 200);
    QVERIFY(m_store->setArtwork(second.id, "image/png",
                                test::solidImageBytes(secondCover)));
    QVERIFY(m_art->isVisible());
    QCOMPARE(m_art->pixmap().toImage().pixelColor(0, 0).rgb(),
             secondCover.rgb());

    // An art change of a non-playing item does not touch the block.
    QVERIFY(m_store->setArtwork(first.id, "image/png",
                                test::solidImageBytes(QColor(1, 2, 3))));
    QCOMPARE(m_art->pixmap().toImage().pixelColor(0, 0).rgb(),
             secondCover.rgb());
}

void CurrentTrackWidgetTest::lineCatalogHasTheDefaultOrder()
{
    const QList<QPair<QString, QString>> catalog =
        CurrentTrackWidget::lineCatalog();
    // The art block first, the fixed lines in the historical order, the
    // extra group last (the Stream section is not a line).
    QCOMPARE(catalog.size(), 17);
    QCOMPARE(catalog.at(0),
             qMakePair(QString("Art"), CurrentTrackWidget::artKey));
    QCOMPARE(catalog.at(1).second, QString("title"));
    QCOMPARE(catalog.at(15).second, QString("channels"));
    QCOMPARE(catalog.at(16),
             qMakePair(QString("Non-standard tags"),
                       CurrentTrackWidget::extraKey));

    // The default sequence is the catalog order, the default visibility
    // is when-non-empty.
    QStringList expected;
    for (const QPair<QString, QString> &entry : catalog)
        expected.append(entry.second);
    QCOMPARE(m_widget->sequence(), expected);
    QCOMPARE(static_cast<int>(m_widget->visibility("title")),
             static_cast<int>(LineVisibility::WhenNonEmpty));
}

void CurrentTrackWidgetTest::alwaysLineWithEmptyValueRendersNameAlone()
{
    startPlayback();

    // Track1 has no album artist, composer, genre or year: the "always"
    // lines render their name alone, in the sequence's position.
    WidgetConfiguration cfg;
    QVariantMap vis;
    vis.insert("albumartist", static_cast<int>(LineVisibility::Always));
    vis.insert("composer", static_cast<int>(LineVisibility::Always));
    vis.insert("genre", static_cast<int>(LineVisibility::Always));
    vis.insert("year", static_cast<int>(LineVisibility::Always));
    cfg["lineVisibility"] = vis;
    m_widget->restoreWidget(cfg);

    QCOMPARE(m_view->text(), QString(
        "<b>Title</b>: Track1<br>"
        "<b>Artist</b>: Artist1<br>"
        "<b>Album Artist</b><br>"
        "<b>Album</b>: Album1<br>"
        "<b>Composer</b><br>"
        "<b>Genre</b><br>"
        "<b>Year</b><br>"
        "<b>Duration</b>: 3:35<br>"
        "<b>File</b>: <a href=\"file:///tmp/Track1.mp3\">/tmp/Track1.mp3</a>"));
}

void CurrentTrackWidgetTest::neverLineRendersNothing()
{
    startPlayback();

    // The artist is set on the item, but a "never" line renders nothing.
    WidgetConfiguration cfg;
    QVariantMap vis;
    vis.insert("artist", static_cast<int>(LineVisibility::Never));
    cfg["lineVisibility"] = vis;
    m_widget->restoreWidget(cfg);

    QCOMPARE(m_view->text(), QString(
        "<b>Title</b>: Track1<br>"
        "<b>Album</b>: Album1<br>"
        "<b>Duration</b>: 3:35<br>"
        "<b>File</b>: <a href=\"file:///tmp/Track1.mp3\">/tmp/Track1.mp3</a>"));
}

void CurrentTrackWidgetTest::alwaysArtRendersPlaceholderWithoutArt()
{
    startPlayback();
    const MediaItem playing = m_pm->playingItem();

    // The "always" art renders a placeholder block for the missing
    // cover (a solid gray square of the art size).
    WidgetConfiguration cfg;
    QVariantMap vis;
    vis.insert(CurrentTrackWidget::artKey,
               static_cast<int>(LineVisibility::Always));
    cfg["lineVisibility"] = vis;
    m_widget->restoreWidget(cfg);

    QVERIFY(m_art->isVisible());
    QVERIFY(!m_art->pixmap().isNull());
    QCOMPARE(m_art->pixmap().toImage().pixelColor(80, 80).rgb(),
             QColor(90, 90, 90).rgb());

    // The stored art replaces the placeholder.
    const QColor cover(200, 30, 30);
    QVERIFY(m_store->setArtwork(playing.id, "image/png",
                                test::solidImageBytes(cover)));
    QCOMPARE(m_art->pixmap().toImage().pixelColor(0, 0).rgb(), cover.rgb());

    // A "never" art hides even a stored cover.
    vis.insert(CurrentTrackWidget::artKey,
               static_cast<int>(LineVisibility::Never));
    cfg["lineVisibility"] = vis;
    m_widget->restoreWidget(cfg);
    QVERIFY(!m_art->isVisible());
}

void CurrentTrackWidgetTest::extraGroupCanBeTurnedOff()
{
    {
        QFile f("/tmp/GroupOff.mp3");
        if (f.open(QIODevice::WriteOnly))
            f.write("\x00\x01\x02");
    }
    MediaItem item;
    item.id = 9;
    item.title = "GroupOff";
    item.source = QUrl::fromLocalFile("/tmp/GroupOff.mp3");
    item.extra.insert("BPM", "128");
    QVERIFY(m_store->insert(item));
    m_pl->addItems({ item.id });
    m_pm->playCurrent();

    WidgetConfiguration cfg;
    QVariantMap vis;
    vis.insert(CurrentTrackWidget::extraKey,
               static_cast<int>(LineVisibility::Never));
    cfg["lineVisibility"] = vis;
    m_widget->restoreWidget(cfg);

    QCOMPARE(m_view->text(), QString(
        "<b>Title</b>: GroupOff<br>"
        "<b>File</b>: <a href=\"file:///tmp/GroupOff.mp3\">/tmp/GroupOff.mp3</a>"));
}

void CurrentTrackWidgetTest::extraGroupFollowsTheSequence()
{
    {
        QFile f("/tmp/GroupTop.mp3");
        if (f.open(QIODevice::WriteOnly))
            f.write("\x00\x01\x02");
    }
    MediaItem item;
    item.id = 9;
    item.title = "GroupTop";
    item.source = QUrl::fromLocalFile("/tmp/GroupTop.mp3");
    item.extra.insert("BPM", "128");
    QVERIFY(m_store->insert(item));
    m_pl->addItems({ item.id });
    m_pm->playCurrent();

    // The group moved to the top of the sequence (the rest in catalog
    // order).
    QStringList order;
    order.append(CurrentTrackWidget::extraKey);
    for (const QPair<QString, QString> &entry : CurrentTrackWidget::lineCatalog())
        if (entry.second != CurrentTrackWidget::extraKey)
            order.append(entry.second);

    WidgetConfiguration cfg;
    cfg["sequence"] = order;
    m_widget->restoreWidget(cfg);

    QCOMPARE(m_view->text(), QString(
        "<b>BPM</b>: 128<br>"
        "<b>Title</b>: GroupTop<br>"
        "<b>File</b>: <a href=\"file:///tmp/GroupTop.mp3\">/tmp/GroupTop.mp3</a>"));
}

void CurrentTrackWidgetTest::storeRestoresNonDefaultsRoundTrip()
{
    // A fresh widget persists no line state (the defaults travel as
    // absence).
    const WidgetConfiguration fresh = m_widget->storeWidget();
    QVERIFY(!fresh.contains("sequence"));
    QVERIFY(!fresh.contains("lineVisibility"));

    // Non-defaults: the art moved out of the first position, two
    // visibilities off the default.
    QStringList order = m_widget->sequence();
    order.move(0, 1); // "art" after "title"
    WidgetConfiguration in;
    in["sequence"] = order;
    QVariantMap vis;
    vis.insert("genre", static_cast<int>(LineVisibility::Always));
    vis.insert("file", static_cast<int>(LineVisibility::Never));
    in["lineVisibility"] = vis;
    m_widget->restoreWidget(in);
    QCOMPARE(m_widget->sequence(), order);
    QCOMPARE(static_cast<int>(m_widget->visibility("genre")),
             static_cast<int>(LineVisibility::Always));
    QCOMPARE(static_cast<int>(m_widget->visibility("file")),
             static_cast<int>(LineVisibility::Never));

    // The store writes back exactly the non-defaults.
    const WidgetConfiguration out = m_widget->storeWidget();
    QStringList stored;
    const QList<QVariant> list = out.value("sequence").toList();
    for (const QVariant &value : list)
        stored.append(value.toString());
    QCOMPARE(stored, order);
    const QVariantMap storedVis = out.value("lineVisibility").toMap();
    QCOMPARE(storedVis.size(), 2);
    QCOMPARE(storedVis.value("genre").toInt(),
             static_cast<int>(LineVisibility::Always));
    QCOMPARE(storedVis.value("file").toInt(),
             static_cast<int>(LineVisibility::Never));

    // A new widget restores them (the layout document's path); the
    // untouched keys keep their default.
    auto* restored = new CurrentTrackWidget();
    WidgetConfiguration in2 = out;
    restored->restoreWidget(in2);
    QCOMPARE(restored->sequence(), order);
    QCOMPARE(static_cast<int>(restored->visibility("genre")),
             static_cast<int>(LineVisibility::Always));
    QCOMPARE(static_cast<int>(restored->visibility("file")),
             static_cast<int>(LineVisibility::Never));
    QCOMPARE(static_cast<int>(restored->visibility("title")),
             static_cast<int>(LineVisibility::WhenNonEmpty));
    delete restored;
}

void CurrentTrackWidgetTest::restoreDropsStaleKeysAndValues()
{
    // A stale (unknown) sequence key is dropped, a duplicate skipped,
    // the missing keys appended in catalog order.
    WidgetConfiguration in;
    in["sequence"] = QStringList{"title", "bogus", "title", "artist"};
    m_widget->restoreWidget(in);

    QStringList expected{"title", "artist"};
    for (const QPair<QString, QString> &entry : CurrentTrackWidget::lineCatalog()) {
        const QString key = entry.second;
        if (key != "title" && key != "artist")
            expected.append(key);
    }
    QCOMPARE(m_widget->sequence(), expected);

    // An unknown visibility key and an out-of-range value are ignored;
    // an "always" on the binary group clamps to when-non-empty.
    QVariantMap vis;
    vis.insert("bogus", static_cast<int>(LineVisibility::Always));
    vis.insert("genre", 99);
    vis.insert(CurrentTrackWidget::extraKey,
               static_cast<int>(LineVisibility::Always));
    vis.insert("title", static_cast<int>(LineVisibility::Always));
    in["lineVisibility"] = vis;
    m_widget->restoreWidget(in);

    QCOMPARE(static_cast<int>(m_widget->visibility("genre")),
             static_cast<int>(LineVisibility::WhenNonEmpty));
    QCOMPARE(static_cast<int>(m_widget->visibility(CurrentTrackWidget::extraKey)),
             static_cast<int>(LineVisibility::WhenNonEmpty));
    QCOMPARE(static_cast<int>(m_widget->visibility("title")),
             static_cast<int>(LineVisibility::Always));
}

void CurrentTrackWidgetTest::editMenuOffersLinesAction()
{
    QMenu* menu = m_widget->widgetContextMenu();
    QVERIFY(menu != nullptr);

    QAction* action = nullptr;
    for (QAction* a : menu->actions()) {
        if (a->text() == "Lines...")
            action = a;
    }
    QVERIFY(action != nullptr);

    bool dialogOpened = false;
    bool triState = false;
    QTimer::singleShot(0, m_widget, [&dialogOpened, &triState] {
        QWidget* dlg = QApplication::activeModalWidget();
        if (dlg == nullptr)
            return;
        dialogOpened = true;
        // The dialog is the tri-state variant: the art row (the catalog
        // first) carries the default state (partially checked).
        if (auto* list =
                qobject_cast<QListWidget*>(dlg->findChild<QListWidget*>("itemList"))) {
            triState = list->item(0)->data(Qt::UserRole).toString()
                == CurrentTrackWidget::artKey
                && list->item(0)->checkState() == Qt::PartiallyChecked;
        }
        qobject_cast<QDialog*>(dlg)->accept();
    });

    action->trigger();

    QVERIFY(dialogOpened);
    QVERIFY(triState);
    menu->deleteLater();
}
}


QTEST_MAIN(CurrentTrackWidgetTest)
#include "currenttrackwidget_test.moc"
