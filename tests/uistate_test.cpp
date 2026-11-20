#include <QtTest>

#include <QFile>
#include <QGuiApplication>
#include <QHeaderView>
#include <QScreen>
#include <QStandardItemModel>
#include <QTableView>
#include <QTemporaryDir>
#include <QWidget>

#include "settingsmanager.h"
#include "uistate.h"

namespace {

class UiStateTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void registersItsKeyOnce();
    void saveLoadRoundTrip();
    void setWritesOneField();
    void persistsAcrossReload();
    void geometryRoundTrip();
    void offScreenGeometryRecenters();
    void degenerateSizeKeepsTheDefault();
    void oversizedGeometryKeepsTheDefault();
    void maximizedFlagSurvivesTheRoundTrip();
    void trackSavesTheGeometryOnHide();
    void columnsRoundTrip();
    void staleColumnNamesAreIgnored();
    void onAnyScreenChecksEveryScreen();

private:
    SettingsManager* m_settings = nullptr;
    QTemporaryDir m_dir;
    QWidget* m_widget = nullptr;
};

void UiStateTest::init()
{
    // A fresh SettingsManager pointed at a fresh file before every test.
    m_dir.remove();
    m_dir = QTemporaryDir();
    QVERIFY(m_dir.isValid());
    m_settings = SettingsManager::getInstance();
    m_settings->setSettingsPath(m_dir.path() + "/settings.json");
    m_settings->load();
}

void UiStateTest::cleanup()
{
    delete m_widget;
    m_widget = nullptr;
    SettingsManager::reset();
}

void UiStateTest::registersItsKeyOnce()
{
    UiState state("dialogs.test", this);
    const Setting s = m_settings->setting("dialogs.test");
    QVERIFY(s.isValid());
    QCOMPARE(s.defaultValue.userType(), QMetaType::QVariantMap);
    // A second UiState of the same scope shares the registration (no
    // refused-registration warning, the default untouched).
    UiState other("dialogs.test", this);
    Q_UNUSED(other);
    QCOMPARE(m_settings->setting("dialogs.test").defaultValue.userType(),
             QMetaType::QVariantMap);
    QCOMPARE(m_settings->settings().size(), 1);
}

void UiStateTest::saveLoadRoundTrip()
{
    UiState state("dialogs.test", this);
    QVERIFY(state.load().isEmpty());
    QVariantMap stateMap;
    stateMap.insert("filter", "abc");
    stateMap.insert("tab", 2);
    state.save(stateMap);
    QCOMPARE(state.load().value("filter").toString(), QString("abc"));
    QCOMPARE(state.load().value("tab").toInt(), 2);
}

void UiStateTest::setWritesOneField()
{
    UiState state("dialogs.test", this);
    state.set("a", 1);
    state.set("b", "two");
    QCOMPARE(state.field("a").toInt(), 1);
    QCOMPARE(state.field("b").toString(), QString("two"));
    // A set does not drop the other fields.
    QVariantMap all = state.load();
    QCOMPARE(all.size(), 2);
}

void UiStateTest::persistsAcrossReload()
{
    UiState state("dialogs.test", this);
    QVariantMap stateMap;
    QVariantMap geo;
    geo.insert("width", 42);
    stateMap.insert("geometry", geo);
    state.save(stateMap);

    // A fresh manager (a new process) reads the same file: the stored
    // map comes back coerced to its registered type.
    SettingsManager::reset();
    SettingsManager* fresh = SettingsManager::getInstance();
    fresh->setSettingsPath(m_dir.path() + "/settings.json");
    UiState freshState("dialogs.test", this);
    fresh->load();
    QCOMPARE(freshState.load().value("geometry").toMap()
                 .value("width")
                 .toInt(),
             42);
}

void UiStateTest::geometryRoundTrip()
{
    m_widget = new QWidget;
    m_widget->setGeometry(10, 20, 300, 200);
    UiState state("dialogs.test", this);
    state.saveGeometry(m_widget);

    QWidget* second = new QWidget;
    second->setGeometry(0, 0, 640, 480);
    state.restoreGeometry(second);
    QCOMPARE(second->geometry(), QRect(10, 20, 300, 200));
    delete second;
}

void UiStateTest::offScreenGeometryRecenters()
{
    m_widget = new QWidget;
    m_widget->setGeometry(100000, 100000, 300, 200);
    UiState state("dialogs.test", this);
    state.saveGeometry(m_widget);

    QWidget* second = new QWidget;
    state.restoreGeometry(second);
    // The size survives, the position lands on a screen.
    QCOMPARE(second->size(), QSize(300, 200));
    QVERIFY(UiState::onAnyScreen(second->geometry()));
    delete second;
}

void UiStateTest::degenerateSizeKeepsTheDefault()
{
    m_widget = new QWidget;
    m_widget->setGeometry(0, 0, 640, 480);
    UiState state("dialogs.test", this);
    QVariantMap stateMap;
    QVariantMap geo;
    geo.insert("width", 0);
    geo.insert("height", 200);
    stateMap.insert("geometry", geo);
    state.save(stateMap);

    state.restoreGeometry(m_widget);
    QCOMPARE(m_widget->geometry(), QRect(0, 0, 640, 480));
}

void UiStateTest::oversizedGeometryKeepsTheDefault()
{
    QVERIFY(UiState::fitsSomeScreen(QSize(10, 10)));
    QVERIFY(!UiState::fitsSomeScreen(QSize(100000, 10)));

    m_widget = new QWidget;
    m_widget->setGeometry(0, 0, 640, 480);
    UiState state("dialogs.test", this);
    QVariantMap stateMap;
    QVariantMap geo;
    geo.insert("x", 10);
    geo.insert("y", 20);
    geo.insert("width", 100000);
    geo.insert("height", 100000);
    stateMap.insert("geometry", geo);
    state.save(stateMap);

    // A size no screen can hold is invalid state: the saved size AND
    // position are ignored, the default is kept.
    state.restoreGeometry(m_widget);
    QCOMPARE(m_widget->geometry(), QRect(0, 0, 640, 480));
}

void UiStateTest::maximizedFlagSurvivesTheRoundTrip()
{
    m_widget = new QWidget;
    m_widget->show();
    m_widget->showMaximized();
    UiState state("dialogs.test", this);
    state.saveGeometry(m_widget);
    const QVariantMap geo = state.load().value("geometry").toMap();
    QVERIFY(geo.value("maximized").toBool());
    // A maximized window stores its normal (un-maximized) geometry.
    QVERIFY(geo.value("width").toInt() > 0);

    m_widget->close();
    QWidget* second = new QWidget;
    state.restoreGeometry(second);
    state.track(second); // the flag applies on the first show
    second->show();
    QVERIFY(second->windowState() & Qt::WindowMaximized);
    delete second;
}

void UiStateTest::trackSavesTheGeometryOnHide()
{
    m_widget = new QWidget;
    m_widget->setGeometry(5, 6, 321, 242);
    UiState state("dialogs.test", this);
    state.track(m_widget);
    m_widget->show();
    m_widget->setGeometry(11, 22, 555, 444);
    m_widget->hide();
    const QVariantMap geo = state.load().value("geometry").toMap();
    QCOMPARE(geo.value("x").toInt(), 11);
    QCOMPARE(geo.value("y").toInt(), 22);
    QCOMPARE(geo.value("width").toInt(), 555);
    QCOMPARE(geo.value("height").toInt(), 444);
    QCOMPARE(geo.value("maximized").toBool(), false);
}

void UiStateTest::columnsRoundTrip()
{
    QStandardItemModel* model = new QStandardItemModel(0, 3, this);
    QTableView* view = new QTableView;
    view->setModel(model);
    // QTableView stretches the last section by default (a resize of it
    // is a no-op): the test resizes every column.
    view->horizontalHeader()->setStretchLastSection(false);
    view->horizontalHeader()->resizeSection(0, 111);
    view->horizontalHeader()->resizeSection(1, 222);
    view->horizontalHeader()->resizeSection(2, 333);
    UiState state("dialogs.test", this);
    state.saveColumns(view->horizontalHeader(), { "a", "b", "c" });

    QTableView* second = new QTableView;
    second->setModel(model);
    second->horizontalHeader()->setStretchLastSection(false);
    state.restoreColumns(second->horizontalHeader(), { "a", "b", "c" });
    QCOMPARE(second->horizontalHeader()->sectionSize(0), 111);
    QCOMPARE(second->horizontalHeader()->sectionSize(1), 222);
    QCOMPARE(second->horizontalHeader()->sectionSize(2), 333);
    delete view;
    delete second;
}

void UiStateTest::staleColumnNamesAreIgnored()
{
    QStandardItemModel* model = new QStandardItemModel(0, 2, this);
    QTableView* view = new QTableView;
    view->setModel(model);
    view->horizontalHeader()->setStretchLastSection(false);
    view->horizontalHeader()->resizeSection(0, 100);
    view->horizontalHeader()->resizeSection(1, 150);

    // A saved state whose names no longer match this view's columns:
    // the restore keeps the view's own sizes, nothing crashes.
    UiState state("dialogs.test", this);
    QVariantMap stateMap;
    QVariantMap columns;
    columns.insert("obsolete", 999);
    columns.insert("b", 77);
    stateMap.insert("columns", columns);
    state.save(stateMap);
    state.restoreColumns(view->horizontalHeader(), { "a", "b" });
    QCOMPARE(view->horizontalHeader()->sectionSize(0), 100);
    QCOMPARE(view->horizontalHeader()->sectionSize(1), 77);
    delete view;
}

void UiStateTest::onAnyScreenChecksEveryScreen()
{
    const QScreen* primary = QGuiApplication::primaryScreen();
    QVERIFY(primary != nullptr);
    if (primary == nullptr)
        QSKIP("no primary screen"); // the QVERIFY above is the guard
    QVERIFY(UiState::onAnyScreen(primary->geometry()));
    QVERIFY(!UiState::onAnyScreen(QRect(100000, 100000, 10, 10)));
}

}

QTEST_MAIN(UiStateTest)
#include "uistate_test.moc"
