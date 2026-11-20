#include <QtTest>
#include <QApplication>
#include <QTemporaryDir>

#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QUrl>

#include <memory>

#include "fakeplayer.h"
#include "layoutmanager.h"
#include "mediaitemstore.h"
#include "playbackmanager.h"
#include "playlistmanager.h"
#include "settingsmanager.h"
#include "testsupport.h"
#include "titlebar.h"

namespace {

// The window operations' seams (offscreen the QWindow calls are no-ops
// — the tests count the calls).
class TestTitleBarWidget : public TitleBarWidget
{
public:
    using TitleBarWidget::TitleBarWidget;

    int m_moves = 0;
    int m_minimizes = 0;
    int m_maximizes = 0;
    int m_closes = 0;
    void startSystemMove() override { ++m_moves; }
    void minimizeWindow() override { ++m_minimizes; }
    void toggleMaximize() override { ++m_maximizes; }
    void closeWindow() override { ++m_closes; }
};

class TitleBarTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void registeredInLayoutManager();
    void titleFollowsThePlayingItem();
    void aPressStartsTheSystemMove();
    void aDoubleClickTogglesTheMaximize();
    void theButtonsRouteToTheWindowOperations();
    void theControlsStoreOnlyNonDefaults();
    void restoreDropsStaleControlKeys();

private:
    // A playable local item, added to the current playlist.
    MediaItem addItem(const QString &title, const QString &artist);

    FakePlayer m_fake{this};
    PlaybackManager* m_pm = nullptr;
    PlaylistManager* m_pl = nullptr;
    MediaItemStore* m_store = nullptr;
    std::unique_ptr<QTemporaryDir> m_dir;
    TestTitleBarWidget* m_widget = nullptr;
    QLabel* m_title = nullptr;
};

void TitleBarTest::init()
{
    m_pl = PlaylistManager::getInstance();
    m_store = MediaItemStore::getInstance();
    m_dir = std::make_unique<QTemporaryDir>();
    test::usePrivateDatabase(m_dir->path());
    m_pl->reset();
    m_store->clear();

    m_pm = PlaybackManager::getInstance();
    test::installFakePlayer(m_pm, &m_fake);

    // The style properties' editor state (and any setting the widget
    // writes) goes to the SettingsManager: keep that store private.
    SettingsManager* settings = SettingsManager::getInstance();
    settings->setSettingsPath(m_dir->path() + "/settings.json");
    settings->load();

    m_widget = new TestTitleBarWidget();
    m_widget->show();
    qApp->processEvents();

    m_title = m_widget->findChild<QLabel*>("titleLabel");
    QVERIFY(m_title != nullptr);
}

void TitleBarTest::cleanup()
{
    delete m_widget;
    m_pm->stop();
    m_pl->reset();
    m_store->clear();
    SettingsManager::reset();
}

MediaItem TitleBarTest::addItem(const QString &title, const QString &artist)
{
    MediaItem item;
    item.title = title;
    item.artist = artist;
    const QString path = m_dir->path() + "/" + title + ".mp3";
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("addItem: cannot open %s", qPrintable(path));
        return {};
    }
    f.write("\x00\x01\x02");
    f.close();
    item.source = QUrl::fromLocalFile(path);
    m_store->insert(item);
    m_pl->addItems({ item.id });
    return item;
}

void TitleBarTest::registeredInLayoutManager()
{
    WidgetConfiguration cfg;
    BaseWidget* w = LayoutManager::getInstance()->createWidget(TitleBarWidget::id, cfg);

    QVERIFY(w != nullptr);
    QCOMPARE(w->widgetId(), TitleBarWidget::id);
    QCOMPARE(w->widgetTitle(), TitleBarWidget::title);
    delete w;
}

void TitleBarTest::titleFollowsThePlayingItem()
{
    // The bare app name on stop (the state the widget starts in).
    QCOMPARE(m_title->text(), QString("doob"));

    const MediaItem item = addItem("Song", "Artist");
    QVERIFY(item.isValid());
    m_pm->playFromPlaylist(m_pl->currentPlaylistId(), 0);
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(m_title->text(), QString("Song — Artist"));

    // The paused state keeps the item (the state watch covers it).
    m_pm->pause();
    QCOMPARE(m_pm->state(), PlaybackManager::Paused);
    QCOMPARE(m_title->text(), QString("Song — Artist"));

    m_pm->stop();
    QCOMPARE(m_title->text(), QString("doob"));
}

void TitleBarTest::aPressStartsTheSystemMove()
{
    QTest::mouseClick(m_widget, Qt::LeftButton);

    QCOMPARE(m_widget->m_moves, 1);
    QCOMPARE(m_widget->m_maximizes, 0);
}

void TitleBarTest::aDoubleClickTogglesTheMaximize()
{
    QTest::mouseDClick(m_widget, Qt::LeftButton);

    QCOMPARE(m_widget->m_maximizes, 1);
}

void TitleBarTest::theButtonsRouteToTheWindowOperations()
{
    QPushButton* min = m_widget->findChild<QPushButton*>("minButton");
    QPushButton* max = m_widget->findChild<QPushButton*>("maxButton");
    QPushButton* close = m_widget->findChild<QPushButton*>("closeButton");
    QVERIFY(min != nullptr);
    QVERIFY(max != nullptr);
    QVERIFY(close != nullptr);

    min->click();
    max->click();
    close->click();

    QCOMPARE(m_widget->m_minimizes, 1);
    QCOMPARE(m_widget->m_maximizes, 1);
    QCOMPARE(m_widget->m_closes, 1);
}

void TitleBarTest::theControlsStoreOnlyNonDefaults()
{
    // The default (all three shown) stores no "controls" key, so a
    // fresh document cannot pin the defaults.
    const WidgetConfiguration fresh = m_widget->storeWidget();
    QVERIFY(!fresh.contains("controls"));

    // A non-default state round-trips.
    m_widget->setShowMinimizeButton(false);
    m_widget->setShowCloseButton(false);
    const WidgetConfiguration out = m_widget->storeWidget();
    const QVariantMap controls = out.value("controls").toMap();
    QCOMPARE(controls.value("min").toBool(), false);
    QCOMPARE(controls.value("max").toBool(), true);
    QCOMPARE(controls.value("close").toBool(), false);

    // A new widget restores them (the layout document's path).
    TestTitleBarWidget* restored = new TestTitleBarWidget();
    WidgetConfiguration in = out;
    restored->restoreWidget(in);
    QCOMPARE(restored->showMinimizeButton(), false);
    QCOMPARE(restored->showMaximizeButton(), true);
    QCOMPARE(restored->showCloseButton(), false);
    delete restored;
}

void TitleBarTest::restoreDropsStaleControlKeys()
{
    // An unknown key is ignored, a missing key keeps its default.
    WidgetConfiguration in;
    QVariantMap controls;
    controls.insert("min", true);
    controls.insert("bogus", false);
    in["controls"] = controls;
    m_widget->restoreWidget(in);

    QCOMPARE(m_widget->showMinimizeButton(), true);
    QCOMPARE(m_widget->showMaximizeButton(), true);
    QCOMPARE(m_widget->showCloseButton(), true);
}

}  // namespace

QTEST_MAIN(TitleBarTest)
#include "titlebar_test.moc"
