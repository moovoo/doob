// The screenshot gallery (the repo's gallery/ directory): every preset
// layout in every preset style with a fake track playing (artwork, a
// non-zero position, live visualization data), the main media widgets
// on their own (the library, the current playlist, the playlist tabs),
// the "Set widget..." dialog, and the edit-mode right-click (the grayed
// widget with the container's edit menu — the default style). The
// presets come from the repo's presets/ tree (GALLERY_PRESETS_DIR, this
// run's DOOB_PRESETS_DIR); the shots land in GALLERY_DIR. Like the other
// tests, it runs headless (QT_QPA_PLATFORM=offscreen) — the grabs are
// widget renders, no display involved.
//
// The layout/style shots run one per fresh child process of this very
// binary (the shot mode, the GALLERY_SHOT="layout/style/name" variable):
// the layout switch is flaky when many of them accumulate in one
// process (a dangling pointer left by a teardown clobbers a later
// widget, the crash surfacing in a style's polish pass), while a single
// switch in a fresh process is clean. The child does the whole setup
// (its own temp dir, the fake library, the playback) and writes the
// shot to GALLERY_DIR; the parent verifies the file and its size.

#include <QtTest>
#include <QApplication>
#include <QTemporaryDir>

#include <QColor>
#include <QCursor>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRect>
#include <QUrl>
#include <QVector>

#include <array>
#include <cmath>
#include <iterator>
#include <memory>

#include "currentplaylistwidget.h"
#include "fakeplayer.h"
#include "layoutmanager.h"
#include "librarymanager.h"
#include "librarywidget.h"
#include "mainwindow.h"
#include "mediaitemstore.h"
#include "metadatadialog.h"
#include "playbackmanager.h"
#include "playlistmanager.h"
#include "playlistswidget.h"
#include "selectwidgetdialog.h"
#include "settings.h"
#include "singlemetadatadialog.h"
#include "settingsmanager.h"
#include "stylemanager.h"
#include "testsupport.h"
#include "widgetcontainer.h"
#include "widgetregistry.h"

namespace {

class GalleryTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void presetLayoutsInEveryStyle();
    void theLibraryWidget();
    void thePlaylistWidgets();
    void theWidgetSelectorDialog();
    void theMetadataDialogs();
    void theEditModeMenu();

private:
    QString makeTempFile(const QString &name);
    void createFakeItems();
    // A chunk of the fake stream (a chirped, amplitude-modulated sine):
    // `samples` mono samples continuing from `phase`.
    QVector<float> sineChunk(int samples, int phase) const;
    // Feed the fake player's tap (waveform, frames, levels) and spin the
    // loop long enough for the visualization timers to paint it.
    void feedAudio();
    // The layout's own main window size (the document's "window" state),
    // the fixed gallery size when the document carries none.
    QSize layoutWindowSize(const QString &layout) const;
    // The main window at a fixed geometry, the grab.
    QPixmap captureWindow(const QSize &size);
    // A standalone widget at a fixed geometry, the grab.
    QPixmap captureWidget(QWidget *w, const QSize &size);
    void saveShot(const QString &name, const QPixmap &pm);
    QStringList presetLayoutNames() const;
    QStringList presetStyleNames() const;
    // The single shot of the shot mode (a fresh child process).
    void runShot();
    // One fresh child process per layout/style shot; the file check.
    void shootShot(const QString &name, const QString &layout,
                   const QString &style);

    std::unique_ptr<QTemporaryDir> m_data;
    LayoutManager* m_lm = nullptr;
    StyleManager* m_sm = nullptr;
    PlaybackManager* m_pm = nullptr;
    PlaylistManager* m_pl = nullptr;
    MediaItemStore* m_store = nullptr;
    FakePlayer m_fake{this};

    // The fake library's ids (in the tracks' order).
    QList<quint64> m_itemIds;

    // The shot mode (a child process of the parent run, GALLERY_SHOT):
    // a single layout/style shot, then exit (the parent verifies the
    // file).
    bool m_shotMode = false;
    QString m_shotLayout;
    QString m_shotStyle;
    QString m_shotName;
};

void GalleryTest::initTestCase()
{
    // The shot mode (the parent's child, GALLERY_SHOT="layout/style/
    // name"): a single shot, then exit. An environment variable — the
    // child's argv belongs to QTest's parser.
    const QByteArray shot = qgetenv("GALLERY_SHOT");
    if (!shot.isEmpty()) {
        const QStringList parts =
            QString::fromUtf8(shot).split(QLatin1Char('/'));
        if (parts.size() == 3) {
            m_shotMode = true;
            m_shotLayout = parts.at(0);
            m_shotStyle = parts.at(1);
            m_shotName = parts.at(2);
        }
        QVERIFY(m_shotMode);
    }

    m_data = std::make_unique<QTemporaryDir>();
    QVERIFY(m_data->isValid());
    qputenv("DOOB_DATA_DIR", m_data->path().toUtf8());
    qputenv("DOOB_PRESETS_DIR", QByteArray(GALLERY_PRESETS_DIR));
    Settings::reset();

    // The settings and the database live in the private temp dir (the
    // store, the library and the playlists point at it too).
    SettingsManager* settings = SettingsManager::getInstance();
    settings->setSettingsPath(Settings::instance()->settingsPath());
    settings->load();
    test::usePrivateDatabase(m_data->path());

    m_store = MediaItemStore::getInstance();
    m_pl = PlaylistManager::getInstance();
    m_pm = PlaybackManager::getInstance();
    LibraryManager::getInstance()->clear();
    m_pl->reset();
    m_store->clear();

    test::installFakePlayer(m_pm, &m_fake);

    // The managers' singletons (the layout manager registers its
    // settings — after the settings manager pointed at the temp file).
    m_lm = LayoutManager::getInstance();
    m_sm = StyleManager::getInstance();

    // The "default" preset is empty (0 bytes): applying it would send a
    // truly empty sheet to the application and tear down Qt's
    // stylesheet wrapper mid-suite — the wrapper's re-creation leaves
    // previously-styled widgets registered in the fresh caches without
    // their cleanup connection (QTBUG-149945), and a later repolish
    // walks the stale entry and crashes. A comment-only user file
    // shadows it with the identical look, keeping every sheet of the
    // run non-empty.
    {
        const QString stylesDir = m_data->path() + "/styles";
        QDir().mkpath(stylesDir);
        QFile f(stylesDir + "/default.qss");
        if (f.open(QIODevice::WriteOnly)) {
            f.write("/* default */\n");
            f.close();
        }
    }

    createFakeItems();

    // The fake playing track (the gallery's "music"): the first row of
    // the current playlist, a duration and a position so the position
    // displays are not at zero.
    m_pm->playFromPlaylist(m_pl->currentPlaylistId(), 0);
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    m_fake.triggerDuration(213000);
    m_fake.setPositionMs(45000);

    m_lm->show();

    if (m_shotMode)
        runShot(); // the child's single shot (the parent spawns it)
}

void GalleryTest::cleanupTestCase()
{
    m_pm->stop();
    LibraryManager::getInstance()->clear();
    m_pl->reset();
    m_store->clear();
    qunsetenv("DOOB_DATA_DIR");
    qunsetenv("DOOB_PRESETS_DIR");
    Settings::reset();
    StyleManager::reset();
    qApp->setStyleSheet(QString());
}

QString GalleryTest::makeTempFile(const QString &name)
{
    QString path = m_data->path() + "/" + name;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("makeTempFile: cannot open %s", qPrintable(path));
        return {};
    }
    f.write("\x00\x01\x02");
    f.close();
    return path;
}

void GalleryTest::createFakeItems()
{
    // The metadata of the "Classic Rock Hits (2026)" compilation (read
    // from the files' tags): the gallery's fake library, so the views
    // are not empty. The track number comes from the files' names (the
    // tag is absent), the art is a solid color per track (a stand-in
    // cover).
    struct Track {
        const char *title;
        const char *artist;
        const char *album;
        int trackNumber;
        int year;
        qint64 durationMs;
        int bitrate;
        QColor art;
    };
    std::array<Track, 26> tracks = {{
        { "Dream On", "Aerosmith", "Classic Rock Hits", 1, 2026, 267000, 328,
          QColor(226, 60, 60), },
        { "Go Your Own Way", "Fleetwood Mac", "Classic Rock Hits", 2, 2026,
          223000, 330, QColor(60, 140, 220), },
        { "Don't Do Me Like That", "Tom Petty", "Classic Rock Hits", 3, 2026,
          161000, 334, QColor(80, 190, 120), },
        { "Hotel California", "Eagles", "Classic Rock Hits", 4, 2026, 391000,
          326, QColor(235, 170, 50), },
        { "Streets of Minneapolis", "Bruce Springsteen", "Classic Rock Hits",
          5, 2026, 270000, 328, QColor(160, 90, 200), },
        { "American Pie", "Don McLean", "Classic Rock Hits", 6, 2026, 513000,
          324, QColor(70, 190, 190), },
        { "The Joker", "The Steve Miller Band", "Classic Rock Hits", 7, 2026,
          264000, 329, QColor(220, 120, 40), },
        { "Have You Ever Seen The Rain (John's)", "John Fogerty",
          "Classic Rock Hits", 8, 2026, 162000, 334, QColor(110, 110, 180), },
        { "Blinded By The Light", "Manfred Mann's Earth Band",
          "Classic Rock Hits", 9, 2026, 228000, 330,
          QColor(240, 200, 60), },
        { "Should I Stay or Should I Go", "The Clash", "Classic Rock Hits",
          10, 2026, 188000, 332, QColor(90, 60, 60), },
        { "Jump", "Van Halen", "Classic Rock Hits", 11, 2026, 241000, 329,
          QColor(200, 60, 120), },
        { "Take It On the Run", "REO Speedwagon", "Classic Rock Hits", 12,
          2026, 241000, 329, QColor(60, 170, 90), },
        { "Shine", "Collective Soul", "Classic Rock Hits", 13, 2026, 306000,
          327, QColor(140, 160, 60), },
        { "In the Air Tonight", "Phil Collins", "Classic Rock Hits", 14, 2026,
          335000, 327, QColor(50, 130, 200), },
        { "Born To Be Wild", "Steppenwolf", "Classic Rock Hits", 15, 2026,
          209000, 331, QColor(180, 80, 40), },
        { "Simple Man", "Lynyrd Skynyrd", "Classic Rock Hits", 16, 2026,
          357000, 326, QColor(100, 190, 60), },
        { "Fortunate Son", "Creedence Clearwater Revival",
          "Classic Rock Hits", 17, 2026, 140000, 336,
          QColor(210, 150, 190), },
        { "Mary Jane's Last Dance", "Tom Petty", "Classic Rock Hits", 18, 2026,
          273000, 328, QColor(230, 90, 90), },
        { "Gimme Three Steps", "Lynyrd Skynyrd", "Classic Rock Hits", 19, 2026,
          270000, 328, QColor(90, 150, 220), },
        { "Enter Sandman", "Metallica", "Classic Rock Hits", 20, 2026, 331000,
          327, QColor(70, 70, 70), },
        { "Stairway To Heaven", "Led Zeppelin", "Classic Rock Hits", 21, 2026,
          482000, 324, QColor(240, 210, 120), },
        { "You Ain't Seen Nothing Yet", "Bachman-Turner Overdrive",
          "Classic Rock Hits", 22, 2026, 234000, 330,
          QColor(120, 100, 60), },
        { "Heroes", "David Bowie", "Classic Rock Hits", 23, 2026, 371000, 326,
          QColor(200, 60, 160), },
        { "Pinball Wizard", "The Who", "Classic Rock Hits", 24, 2026, 182000,
          332, QColor(60, 120, 140), },
        { "Dreams", "Fleetwood Mac", "Classic Rock Hits", 25, 2026, 257000,
          329, QColor(190, 190, 90), },
        { "House Of The Rising Sun", "The Animals", "Classic Rock Hits", 26,
          2026, 271000, 328, QColor(150, 70, 50), },
    },};

    QList<quint64> ids;
    const int n = static_cast<int>(std::size(tracks));
    for (int i = 0; i < n; ++i) {
        const Track &t = tracks.at(i);
        MediaItem item;
        // A file per item: the store keys items by source (a repeated
        // source is the same item).
        item.source = QUrl::fromLocalFile(makeTempFile("song" +
                                                       QString::number(i) +
                                                       ".flac"));
        item.title = t.title;
        item.artist = t.artist;
        item.album = t.album;
        item.trackNumber = t.trackNumber;
        item.year = t.year;
        item.durationMs = t.durationMs;
        item.bitrate = t.bitrate;
        QVERIFY(!item.source.isEmpty());
        QVERIFY(m_store->insert(item));
        // The list views' and the current track's artwork (the store's
        // side table; a solid color stands in for a cover).
        QVERIFY(m_store->setArtwork(item.id, "image/png",
                                    test::solidImageBytes(t.art, 128)));
        ids << item.id;
    }

    m_itemIds = ids;

    // Everything in the library and in the current playlist; the second
    // playlist (the tabs' second tab) holds three of the rows.
    LibraryManager::getInstance()->addItems(ids);
    m_pl->addItems(ids);
    const quint64 mix = m_pl->addPlaylist("Mixtape");
    QVERIFY(mix != 0);
    m_pl->insertItemsInto(mix, static_cast<int>(m_pl->playlist(mix).items.size()),
                          { ids.first(), ids.at(2), ids.at(4) });
}

QVector<float> GalleryTest::sineChunk(int samples, int phase) const
{
    QVector<float> out(samples);
    for (int i = 0; i < samples; ++i) {
        const double t = (phase + i) / 48000.0;
        // An amplitude modulation (the slow swell) over a chirped sine.
        const double amp = 0.6 + (0.3 * std::sin(2 * M_PI * 0.5 * t));
        const double freq = 440.0 + (220.0 * std::sin(2 * M_PI * 0.2 * t));
        out[i] = static_cast<float>(amp * std::sin(2 * M_PI * freq * t));
    }
    return out;
}

void GalleryTest::feedAudio()
{
    int phase = 0;
    for (int chunk = 0; chunk < 12; ++chunk) {
        // The tap: the waveform (the oscillogram), the stereo frames
        // (the spectrum, the spectrogram, the peak meter's input) and
        // the levels (the peak meter).
        const QVector<float> mono = sineChunk(4800, phase);
        QVector<float> frames(2 * mono.size());
        for (qsizetype i = 0; i < mono.size(); ++i) {
            frames[2 * i] = mono[i];
            frames[(2 * i) + 1] = mono[i] * 0.8F;
        }
        m_fake.triggerWaveform(mono, 48000);
        m_fake.triggerFrames(frames, 48000);
        m_fake.triggerLevels(0.8F, 0.6F);
        phase += static_cast<int>(mono.size());
    }
    // Spin until the visualization timers (60 fps by default) have
    // painted the data.
    for (int i = 0; i < 30; ++i)
        QTest::qWait(10);
}

// The layout's own main window size (the document's "window" state,
// saved when the layout was stored), so each preset is shot at the size
// it was made for (the compact bar at its bar size, not stretched).
// The layout switch's own geometry restore is no use here: offscreen,
// the screen is 800x800, and a size no screen can hold is rejected by
// the restore.
QSize GalleryTest::layoutWindowSize(const QString &layout) const
{
    const QJsonObject window =
        m_lm->layoutDocument(layout).value("window").toObject();
    const QSize size(window.value("width").toInt(),
                     window.value("height").toInt());
    if (size.width() <= 0 || size.height() <= 0)
        return { 1440, 900 }; // no stored size: the gallery's default
    return size;
}

QPixmap GalleryTest::captureWindow(const QSize &size)
{
    MainWindow* window = m_lm->window();
    window->setGeometry(QRect(QPoint(0, 0), size));
    window->show();
    qApp->processEvents();
    return window->grab();
}

QPixmap GalleryTest::captureWidget(QWidget *w, const QSize &size)
{
    w->resize(size);
    w->show();
    qApp->processEvents();
    return w->grab();
}

void GalleryTest::saveShot(const QString &name, const QPixmap &pm)
{
    const QString dir = GALLERY_DIR;
    QVERIFY(QDir().mkpath(dir));
    const QString path = dir + "/" + name + ".png";
    QVERIFY(pm.save(path, "PNG"));
    QVERIFY(QFile::exists(path));
    QVERIFY(QFile(path).size() > 0);
}

QStringList GalleryTest::presetLayoutNames() const
{
    QStringList names;
    for (const auto &layout : m_lm->layouts())
        if (layout.readOnly)
            names << layout.name;
    return names;
}

QStringList GalleryTest::presetStyleNames() const
{
    QStringList names;
    // All the styles: the private dir holds only the presets — except
    // "default", shadowed by the comment-only user file written in
    // initTestCase (the empty preset is unshootable: applying it tears
    // down Qt's stylesheet wrapper, QTBUG-149945).
    for (const auto &style : m_sm->styles())
        names << style.name;
    return names;
}

// The single shot of the shot mode (a fresh child process): the setup
// ran in initTestCase, this switches to the shot's layout and style,
// feeds the audio and saves the shot (the parent verifies the file).
void GalleryTest::runShot()
{
    QVERIFY(m_lm->switchLayout(m_shotLayout));
    QVERIFY(m_sm->switchStyle(m_shotStyle));
    feedAudio();
    saveShot(m_shotName, captureWindow(layoutWindowSize(m_shotLayout)));
}

// One fresh child process per layout/style shot (this binary with
// GALLERY_SHOT): the layout switch is flaky when many accumulate in one
// process (a dangling pointer from a teardown clobbers a later widget),
// while a single switch in a fresh process is clean. The child writes
// the shot to GALLERY_DIR; the file and its size are verified here.
void GalleryTest::shootShot(const QString &name, const QString &layout,
                            const QString &style)
{
    QProcess proc;
    // The shot's spec (this Qt's QProcess has no per-key setter: the
    // system environment, the one variable added).
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GALLERY_SHOT"),
               layout + QLatin1Char('/') + style + QLatin1Char('/')
               + name);
    proc.setProcessEnvironment(env);
    proc.start(QCoreApplication::applicationFilePath());
    QVERIFY2(proc.waitForStarted(30000),
             qPrintable("the shot process did not start: " + name));
    QVERIFY2(proc.waitForFinished(180000),
             qPrintable("the shot process hung: " + name));
    const QByteArray err = proc.readAllStandardError();
    QVERIFY2(proc.exitStatus() == QProcess::NormalExit
               && proc.exitCode() == 0,
             qPrintable("the shot process failed: " + name + "\n"
                        + QString::fromUtf8(err)));

    // The shot on disk, at the layout's own size.
    const QString path = QString(GALLERY_DIR) + "/" + name + ".png";
    QVERIFY2(QFile::exists(path) && QFile(path).size() > 0,
             qPrintable("the shot is missing: " + path));
    const QImage image(path);
    QVERIFY2(!image.isNull(), qPrintable("the shot is unreadable: " + path));
    QCOMPARE(image.size(), layoutWindowSize(layout));
}

// Every preset layout, in every preset style: the main window with the
// fake track playing (the shots are named "<layout>-<style>.png", one
// fresh child process each — see shootShot).
void GalleryTest::presetLayoutsInEveryStyle()
{
    if (m_shotMode)
        return; // the child's only job was initTestCase's runShot

    const QStringList layouts = presetLayoutNames();
    const QStringList styles = presetStyleNames();
    QVERIFY(!layouts.isEmpty());
    QVERIFY(!styles.isEmpty());

    for (const QString &layout : layouts)
        for (const QString &style : styles)
            shootShot(layout.toLower() + "-" + style.toLower(), layout,
                      style);
}

// The library widget on its own (the default style, the library's
// rows with their artwork).
void GalleryTest::theLibraryWidget()
{
    if (m_shotMode)
        return;
    m_sm->switchStyle("default");
    LibraryWidget* widget = new LibraryWidget;
    saveShot("widget-library", captureWidget(widget, QSize(1000, 700)));
    delete widget;
}

// The playlist views on their own (the current playlist's rows, the
// tabs with the two playlists).
void GalleryTest::thePlaylistWidgets()
{
    if (m_shotMode)
        return;
    m_sm->switchStyle("default");

    CurrentPlaylistWidget* current = new CurrentPlaylistWidget;
    saveShot("widget-playlist", captureWidget(current, QSize(1000, 700)));
    delete current;

    PlaylistsWidget* tabs = new PlaylistsWidget;
    saveShot("widget-playlists", captureWidget(tabs, QSize(1000, 700)));
    delete tabs;
}

// The "Set widget..." dialog (the widget selector), the default style.
void GalleryTest::theWidgetSelectorDialog()
{
    if (m_shotMode)
        return;
    m_sm->switchStyle("default");
    SelectWidgetDialog dialog(WidgetRegistry::getInstance()->options(),
                              QString());
    saveShot("dialog-select-widget",
             captureWidget(&dialog, QSize(560, 640)));
}

// The metadata editors (the default style): the single-item tag table
// (the first track) and the bulk table over a few selected items.
void GalleryTest::theMetadataDialogs()
{
    if (m_shotMode)
        return;
    m_sm->switchStyle("default");

    SingleMetaDataDialog single(m_itemIds.first());
    saveShot("dialog-metadata-single",
             captureWidget(&single, QSize(560, 480)));

    MetaDataDialog bulk(m_itemIds.mid(0, 6));
    saveShot("dialog-metadata-bulk", captureWidget(&bulk, QSize(960, 520)));
}

// The edit-mode right-click (the default style, the first preset
// layout): the emulated click on the library widget grays it and pops
// the container's edit menu — the two grabs (the window, the menu)
// composited into one shot.
void GalleryTest::theEditModeMenu()
{
    if (m_shotMode)
        return;
    m_sm->switchStyle("default");
    const QStringList layouts = presetLayoutNames();
    QVERIFY(!layouts.isEmpty());
    QVERIFY(m_lm->switchLayout(layouts.first()));

    MainWindow* window = m_lm->window();
    const QSize size = layoutWindowSize(layouts.first());
    window->setGeometry(QRect(QPoint(0, 0), size));
    window->show();
    qApp->processEvents();

    m_lm->setEditMode(true);
    QVERIFY(m_lm->isEditMode());

    // The emulated right-click: the library widget (its innermost
    // container gets the grayed veil and the menu), the cursor parked
    // on the widget (the menu pops at the cursor).
    // The preset's hidden docked child window also holds a library
    // widget (in the same QObject tree), so findChildren returns both —
    // the emulated click wants the main window's, the visible one.
    QList<LibraryWidget*> visible;
    for (LibraryWidget* l : window->findChildren<LibraryWidget*>())
        if (l->isVisible() && l->window() == window)
            visible << l;
    QVERIFY(!visible.isEmpty());
    LibraryWidget* target = visible.first();
    WidgetContainer* container =
        qobject_cast<WidgetContainer*>(target->parentWidget());
    QVERIFY(container != nullptr);
    QCursor::setPos(target->mapToGlobal(
        QPoint(target->width() / 2, target->height() / 2)));
    QTest::mouseClick(target, Qt::RightButton);

    QMenu* menu = qobject_cast<QMenu*>(qApp->activePopupWidget());
    QVERIFY(menu != nullptr);

    // The window's grab does not carry the menu (a separate top-level):
    // the menu's grab composited at its global offset.
    QPixmap shot = window->grab();
    {
        QPainter painter(&shot);
        painter.drawPixmap(menu->pos() - window->pos(), menu->grab());
    }
    saveShot("edit-mode-menu", shot);

    menu->hide();
    qApp->processEvents();
    m_lm->setEditMode(false);
}

}  // namespace

QTEST_MAIN(GalleryTest)
#include "gallery_test.moc"
