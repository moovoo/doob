#include <QtTest/QtTest>
#include <QApplication>
#include <QColor>
#include <QDockWidget>
#include <QIcon>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QMenuBar>
#include <QMenu>
#include <QScreen>
#include <QTimer>

#include "layoutmanager.h"
#include "mainwindow.h"
#include "widgetcontainer.h"
#include "splitterwidget.h"
#include "tabswidget.h"
#include "widgetregistry.h"
#include "settings.h"
#include "settingsmanager.h"
#include "stylemanager.h"
#include "windowmanager.h"
#include "windowsdialog.h"

namespace {

// A "plugin" widget: not a built-in, registered in the registry exactly
// the way a real plugin's widget would be.
class PluginTestWidget : public BaseWidget
{
    Q_OBJECT
public:
    explicit PluginTestWidget(QWidget *parent = nullptr) : BaseWidget(parent) {}

    QString widgetId() const override { return QStringLiteral("plugtest"); }
    QString widgetTitle() const override { return QStringLiteral("Plugtest"); }
};

// A nested splitter deep enough for a payload larger than 32 KiB (the
// size at which /dev/full refuses a write): `levels` splitters total,
// each with its own node and the splitter state's base64 in the
// document.
SplitterWidget* deepSplitter(int levels)
{
    SplitterWidget* node = new SplitterWidget; // the leaf
    for (int i = 1; i < levels; ++i) {
        SplitterWidget* outer = new SplitterWidget;
        outer->containers().at(0)->setWidget(node);
        node = outer;
    }
    return node;
}

class LayoutManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    void testFactory();
    void testAppIconResource();
    void testPluginWidgetRegistration();
    void testEditModeSignal();
    void testEditModeSetting();
    void testSaveLoadRoundtrip();
    void testExportImportLayout();
    void testSaveLayoutFailureKeepsPreviousFile();
    void testImportUnknownRootTypeKeepsCurrentLayout();
    void testImportRejectsUnknownNestedType();
    void testAutoLoadSkipsUnknownSubtree();
    void testAutoLoadUnknownRootKeepsCurrentLayout();
    void testLayoutMenu();
    void testAutoLoadOnShow();
    void testWindowGeometrySavedAndRestored();
    void testWindowGeometryOffScreenIsCentered();
    void testWindowGeometryOversizedIsIgnored();
    void testWindowsSavedAndLoaded();
    void testDockedWindowSurvivesAppExit();
    void testDockedPositionPersists();
    void testWindowsEditorIsNonModal();
    void testStandaloneWindowSurvivesAppExit();
    void testRealQuitSavesWindowsAsTheyWere();
    void testMovedWindowPositionPersists();
    void testImportRejectsUnknownTypeInWindow();
    void testAutoLoadSkipsUnknownWindowSubtree();
    void testFreeInstanceName();
    void testWidgetNameRoundTrips();
    void testOverlayGrayedVeil();
    void testAutoSaveOnQuit(); // closes the main window in a real event loop

    // The layout store.
    void testLayoutsListing();
    void testUserFileShadowsPreset();
    void testCurrentLayoutPersistence();
    void testFirstRun();
    void testSwitchLayoutSavesCurrent();
    void testSwitchLayoutToPreset();
    void testSwitchToPresetAdoptsExistingCopy();
    void testSwitchToDefaultFileLoadsIt();
    void testNewLayoutSavesCurrentFirst();
    void testCowForksEditedPreset();
    void testCowLeavesUneditedPresetAlone();
    void testSaveLayoutAs();
    void testDeleteLayout();
    void testStaleCurrentFallsBackToFirstUserFile();
    void testImportLayoutIntoUserDir();

private:
    // The temp data (and, when a preset is wanted, presets) directory,
    // with the current layout set to `current`.
    void setDataDir(QTemporaryDir &data, const QString &current);
    void setPresetsDir(QTemporaryDir &presets);
    void writePreset(const QTemporaryDir &presets, const QString &name,
                     const QJsonObject &doc);

    // The always-present (empty) presets directory: the env var beats
    // every real candidate, isolating the test from the machine's
    // installed data (a real data dir would otherwise be resolved as
    // the presets' dir).
    QTemporaryDir m_presets;
};

void LayoutManagerTest::initTestCase()
{
    LayoutManager::getInstance()->show();
}

void LayoutManagerTest::cleanup()
{
    qunsetenv("DOOB_DATA_DIR");
    qunsetenv("DOOB_PRESETS_DIR");
    Settings::reset();
    StyleManager::reset();
    qApp->setStyleSheet(QString());
    // The currents are SettingsManager values (a process-wide
    // singleton): leave them unset so the first-run detection (and the
    // next test's expectations) start clean.
    SettingsManager* settings = SettingsManager::getInstance();
    settings->resetToDefault("layout.current");
    settings->resetToDefault("style.current");
}

void LayoutManagerTest::setDataDir(QTemporaryDir &data, const QString &current)
{
    data.remove();
    data = QTemporaryDir();
    qputenv("DOOB_DATA_DIR", data.path().toUtf8());
    m_presets.remove();
    m_presets = QTemporaryDir();
    qputenv("DOOB_PRESETS_DIR", m_presets.path().toUtf8());
    Settings::reset();
    LayoutManager::getInstance()->setCurrentLayout(current);
}

void LayoutManagerTest::setPresetsDir(QTemporaryDir &presets)
{
    presets.remove();
    presets = QTemporaryDir();
    qputenv("DOOB_PRESETS_DIR", presets.path().toUtf8());
    Settings::reset();
}

void LayoutManagerTest::writePreset(const QTemporaryDir &presets, const QString &name,
                                    const QJsonObject &doc)
{
    QDir().mkpath(presets.path() + "/layouts");
    QFile f(presets.path() + "/layouts/" + name + ".json");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(doc).toJson());
    f.close();
}

void LayoutManagerTest::testFactory()
{
    LayoutManager* lm = LayoutManager::getInstance();
    WidgetConfiguration empty;

    BaseWidget* w = lm->createWidget("splitter", empty);
    QVERIFY(w);
    QCOMPARE(w->widgetId(), QString("splitter"));
    delete w;

    w = lm->createWidget("tabs", empty);
    QVERIFY(w);
    QCOMPARE(w->widgetId(), QString("tabs"));
    delete w;

    QVERIFY(!lm->createWidget("nonexistent", empty));
}

// The app icon (main.cpp sets it on the application from this resource):
// the resource must exist and render at the sizes the icon is asked for.
void LayoutManagerTest::testAppIconResource()
{
    const QIcon icon(QStringLiteral(":/icons/doob.svg"));
    QVERIFY(!icon.isNull());
    QVERIFY(!icon.pixmap(QSize(48, 48)).isNull());
    QVERIFY(!icon.pixmap(QSize(256, 256)).isNull());
}

void LayoutManagerTest::testPluginWidgetRegistration()
{
    LayoutManager* lm = LayoutManager::getInstance();
    WidgetRegistry* registry = WidgetRegistry::getInstance();

    QVERIFY(registry->registerWidget("plugtest", "Plugtest",
                                     [](WidgetConfiguration &) {
                                         return new PluginTestWidget;
                                     }));

    WidgetConfiguration cfg;
    BaseWidget* w = lm->createWidget("plugtest", cfg);
    QVERIFY(w);
    QCOMPARE(w->widgetId(), QString("plugtest"));
    QCOMPARE(w->widgetTitle(), QString("Plugtest"));

    // The serialize + paste-validation path accepts it too.
    WidgetContainer container;
    container.setWidget(w);  // the container (and thus w) dies with the scope
    const QString text = lm->serializeContainer(&container);
    QVERIFY(lm->isSerializedWidget(text));
    BaseWidget* pasted = lm->createWidgetFromSerializedText(text);
    QVERIFY(pasted);
    QCOMPARE(pasted->widgetId(), QString("plugtest"));
    delete pasted;

    // Re-registering a taken id is rejected.
    QVERIFY(!registry->registerWidget("plugtest", "Plugtest",
                                      [](WidgetConfiguration &) {
                                          return new PluginTestWidget;
                                      }));

    // Once unregistered it is gone: the factory refuses the id and the
    // paste validation refuses the stale text.
    registry->unregisterWidget("plugtest");
    QVERIFY(!lm->createWidget("plugtest", cfg));
    QVERIFY(!lm->isSerializedWidget(text));
}

void LayoutManagerTest::testEditModeSignal()
{
    LayoutManager* lm = LayoutManager::getInstance();
    QSignalSpy spy(lm, &LayoutManager::editModeChanged);
    QVERIFY(spy.isValid());

    lm->setEditMode(false);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), false);

    lm->setEditMode(true);
    QCOMPARE(spy.count(), 2);
}

void LayoutManagerTest::testEditModeSetting()
{
    LayoutManager* lm = LayoutManager::getInstance();
    SettingsManager* settings = SettingsManager::getInstance();

    // Registered as a core setting, bool, default on (the previous
    // hard-coded startup state).
    const Setting s = settings->setting("layout.edit_mode");
    QVERIFY(s.isValid());
    QCOMPARE(s.defaultValue.toBool(), true);
    QCOMPARE(s.owner, QString());

    // The toggle is write-through: the SettingsManager holds the current
    // mode and is modified from the first toggle on.
    lm->setEditMode(false);
    QCOMPARE(lm->isEditMode(), false);
    QCOMPARE(settings->value("layout.edit_mode").toBool(), false);
    QVERIFY(settings->isModified("layout.edit_mode"));

    lm->setEditMode(true);
    QCOMPARE(lm->isEditMode(), true);
    QCOMPARE(settings->value("layout.edit_mode").toBool(), true);
}

void LayoutManagerTest::testSaveLoadRoundtrip()
{
    LayoutManager* lm = LayoutManager::getInstance();
    MainWindow* window = lm->window();
    QVERIFY(window);
    WidgetContainer* main = window->mainContainer();
    QVERIFY(main);

    QTemporaryDir tmp;
    setDataDir(tmp, "test1");
    const QString layoutFile = Settings::instance()->layoutsDir() + "/test1.json";

    main->setWidget(new TabsWidget);
    QVERIFY(main->getWidget());

    QMetaObject::invokeMethod(lm, "saveLayout");
    QVERIFY(QFile::exists(layoutFile));

    // A second save into the same, now-existing file must replace the
    // previous one (QFile::rename does not overwrite a destination).
    main->setWidget(new SplitterWidget);
    QCOMPARE(main->getWidget()->widgetId(), QString("splitter"));
    QMetaObject::invokeMethod(lm, "saveLayout");

    main->setWidget(new TabsWidget);
    QMetaObject::invokeMethod(lm, "loadLayout");
    QVERIFY(main->getWidget());
    QCOMPARE(main->getWidget()->widgetId(), QString("splitter"));
}

void LayoutManagerTest::testExportImportLayout()
{
    LayoutManager* lm = LayoutManager::getInstance();
    MainWindow* window = lm->window();
    QVERIFY(window);
    WidgetContainer* main = window->mainContainer();
    QVERIFY(main);

    QTemporaryDir dataDir;
    QTemporaryDir exportDir;
    setDataDir(dataDir, "test1");
    (void)exportDir;

    main->setWidget(new TabsWidget);
    window->setGeometry(120, 80, 500, 400);
    QApplication::processEvents();

    // Export to a file outside the data directory: the same document
    // format as the layouts' files (the "container", "window" and
    // "version" keys as siblings at the root).
    const QString exportPath = exportDir.filePath("exported.json");
    QVERIFY(lm->saveLayoutTo(exportPath));
    QVERIFY(QFile::exists(exportPath));

    QFile f(exportPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject doc = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    // The root container sits under its own key — the document root is
    // not the container itself (no "type" at the top level) — and the
    // document carries its format version.
    QVERIFY(doc.contains("container"));
    QVERIFY(doc.contains("window"));
    QVERIFY(!doc.contains("type"));
    QCOMPARE(doc.value("version").toInt(), 1);
    QCOMPARE(doc.value("container").toObject().value("type").toString(),
             QString("tabs"));

    // Swap the live layout and move the window; importing must bring
    // the exported state (widget and window geometry) back.
    main->setWidget(new SplitterWidget);
    QCOMPARE(main->getWidget()->widgetId(), QString("splitter"));
    window->setGeometry(0, 0, 200, 150);
    QApplication::processEvents();

    QVERIFY(lm->loadLayoutFrom(exportPath));
    QVERIFY(main->getWidget());
    QCOMPARE(main->getWidget()->widgetId(), QString("tabs"));
    QCOMPARE(window->geometry(), QRect(120, 80, 500, 400));

    // A missing or corrupt file fails and leaves the current layout
    // untouched.
    main->setWidget(new SplitterWidget);
    QCOMPARE(main->getWidget()->widgetId(), QString("splitter"));
    QVERIFY(!lm->loadLayoutFrom(exportDir.filePath("does-not-exist.json")));
    QCOMPARE(main->getWidget()->widgetId(), QString("splitter"));

    const QString corruptPath = exportDir.filePath("corrupt.json");
    QFile corrupt(corruptPath);
    QVERIFY(corrupt.open(QIODevice::WriteOnly));
    corrupt.write("not a layout document");
    corrupt.close();
    QVERIFY(!lm->loadLayoutFrom(corruptPath));
    QCOMPARE(main->getWidget()->widgetId(), QString("splitter"));
}

void LayoutManagerTest::testSaveLayoutFailureKeepsPreviousFile()
{
    // A save whose write fails must not destroy the previous file: the old
    // file stays intact, no temp file is left behind.
    if (!QFile::exists("/dev/full"))
        QSKIP("/dev/full is not available");

    LayoutManager* lm = LayoutManager::getInstance();
    MainWindow* window = lm->window();
    QVERIFY(window);
    WidgetContainer* main = window->mainContainer();

    QTemporaryDir dataDir;
    QTemporaryDir exportDir;
    setDataDir(dataDir, "test1");
    (void)exportDir;

    window->setGeometry(120, 80, 500, 400);
    // A payload larger than 32 KiB: /dev/full only fails oversized writes.
    main->setWidget(deepSplitter(400));
    QApplication::processEvents();

    const QString exportPath = exportDir.filePath("fragile.json");
    QVERIFY(lm->saveLayoutTo(exportPath));

    // Force the write to fail: the temp file is a symlink to /dev/full,
    // where oversized writes fail (ENOSPC).
    QFile::remove(exportPath + ".tmp"); // best effort: absent after a save
    QVERIFY(QFile::link(QStringLiteral("/dev/full"), exportPath + ".tmp"));

    main->setWidget(deepSplitter(401));
    QApplication::processEvents();
    QVERIFY(!lm->saveLayoutTo(exportPath));

    // The previous file survived: it holds the first (400-level) tree,
    // and no temp file is left behind.
    QFile f(exportPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray data = f.readAll();
    f.close();
    const QJsonObject doc = QJsonDocument::fromJson(data).object();
    // Walk down the nested splitters and count the levels.
    int levels = 0;
    QJsonValue node = doc.value("container");
    while (node.toObject().value("type").toString() == "splitter") {
        ++levels;
        const QJsonArray containers = node.toObject().value("containers").toArray();
        node = containers.isEmpty() ? QJsonValue() : containers.first();
    }
    QCOMPARE(levels, 400);
    QVERIFY(!QFile::exists(exportPath + ".tmp"));

    main->setWidget(nullptr);
}

void LayoutManagerTest::testImportUnknownRootTypeKeepsCurrentLayout()
{
    // A layout whose root is a widget this build does not know (a newer
    // version, a typo, an arbitrary JSON object) must fail the import and
    // keep the current layout — not wipe it.
    LayoutManager* lm = LayoutManager::getInstance();
    MainWindow* window = lm->window();
    QVERIFY(window);
    WidgetContainer* main = window->mainContainer();
    QVERIFY(main);

    QTemporaryDir dataDir;
    QTemporaryDir importDir;
    setDataDir(dataDir, "test1");
    (void)importDir;

    main->setWidget(new TabsWidget);
    QCOMPARE(main->getWidget()->widgetId(), QString("tabs"));

    const QString unknownPath = importDir.filePath("unknown.json");
    QFile f(unknownPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("{\"container\": {\"type\": \"nonexistent-widget\"}, "
            "\"window\": {\"x\": 0, \"y\": 0, \"width\": 400, \"height\": 300}}");
    f.close();

    QVERIFY(!lm->loadLayoutFrom(unknownPath));
    // The current layout survived the failed import.
    QVERIFY(main->getWidget());
    QCOMPARE(main->getWidget()->widgetId(), QString("tabs"));
}

void LayoutManagerTest::testImportRejectsUnknownNestedType()
{
    // The root is known, but a nested subtree references a widget this
    // build cannot create (a missing plugin, a typo): the strict import
    // rejects the file whole and keeps the current layout.
    LayoutManager* lm = LayoutManager::getInstance();
    MainWindow* window = lm->window();
    QVERIFY(window);
    WidgetContainer* main = window->mainContainer();
    QVERIFY(main);

    QTemporaryDir dataDir;
    QTemporaryDir importDir;
    setDataDir(dataDir, "test1");
    (void)importDir;

    main->setWidget(new TabsWidget);
    QCOMPARE(main->getWidget()->widgetId(), QString("tabs"));

    const QString unknownPath = importDir.filePath("nested-unknown.json");
    QFile f(unknownPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("{\"container\": {\"type\": \"splitter\", "
            "\"containers\": [{\"type\": \"ghostwidget\"}, {\"type\": \"currenttrack\"}]}, "
            "\"window\": {\"x\": 0, \"y\": 0, \"width\": 400, \"height\": 300}}");
    f.close();

    QVERIFY(!lm->loadLayoutFrom(unknownPath));
    // The current layout survived the failed import.
    QVERIFY(main->getWidget());
    QCOMPARE(main->getWidget()->widgetId(), QString("tabs"));
}

void LayoutManagerTest::testAutoLoadSkipsUnknownSubtree()
{
    // The automatic load is lenient: a saved layout that references a
    // widget whose plugin is missing skips the offending subtree (with a
    // warning) and restores the rest.
    LayoutManager* lm = LayoutManager::getInstance();
    MainWindow* window = lm->window();
    QVERIFY(window);
    WidgetContainer* main = window->mainContainer();
    QVERIFY(main);

    QTemporaryDir dataDir;
    setDataDir(dataDir, "test1");

    QFile f(Settings::instance()->layoutsDir() + "/test1.json");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("{\"container\": {\"type\": \"splitter\", "
            "\"containers\": [{\"type\": \"ghostwidget\"}, {\"type\": \"currenttrack\"}]}}");
    f.close();

    QMetaObject::invokeMethod(lm, "loadLayout");
    QApplication::processEvents();

    BaseWidget* w = main->getWidget();
    QVERIFY(w);
    QCOMPARE(w->widgetId(), QString("splitter"));
    SplitterWidget* splitter = qobject_cast<SplitterWidget*>(w);
    QVERIFY(splitter);
    // The unknown subtree is skipped (an empty container); the rest of
    // the layout is restored.
    QCOMPARE(splitter->containers().size(), 2);
    QCOMPARE(splitter->containers().at(0)->getWidget(), nullptr);
    QVERIFY(splitter->containers().at(1)->getWidget());
    QCOMPARE(splitter->containers().at(1)->getWidget()->widgetId(), QString("currenttrack"));
}

void LayoutManagerTest::testAutoLoadUnknownRootKeepsCurrentLayout()
{
    // An unknown root leaves nothing to restore, even in lenient mode:
    // the automatic load keeps the current layout.
    LayoutManager* lm = LayoutManager::getInstance();
    MainWindow* window = lm->window();
    QVERIFY(window);
    WidgetContainer* main = window->mainContainer();
    QVERIFY(main);

    QTemporaryDir dataDir;
    setDataDir(dataDir, "test1");

    main->setWidget(new TabsWidget);
    QCOMPARE(main->getWidget()->widgetId(), QString("tabs"));

    QFile f(Settings::instance()->layoutsDir() + "/test1.json");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("{\"container\": {\"type\": \"ghostwidget\", "
            "\"containers\": [{\"type\": \"currenttrack\"}]}}");
    f.close();

    QMetaObject::invokeMethod(lm, "loadLayout");
    QApplication::processEvents();

    QVERIFY(main->getWidget());
    QCOMPARE(main->getWidget()->widgetId(), QString("tabs"));
}

void LayoutManagerTest::testLayoutMenu()
{
    MainWindow* window = LayoutManager::getInstance()->window();
    QVERIFY(window);

    // The top-level menus are "File" and "View". (addMenu(QMenu*) does
    // not reparent the menu to the bar, so it is reached via the bar's
    // action, not findChild.)
    QMenuBar* bar = window->menuBar();
    QVERIFY(bar);
    QCOMPARE(bar->actions().size(), 2);

    QMenu* file = bar->actions().at(0)->menu();
    QVERIFY(file);
    QCOMPARE(file->title(), QString("File"));
    QStringList fileTitles;
    for (QAction* action : file->actions())
        fileTitles.append(action->text());
    // Open / Export / Import, a separator, Preferences, a separator,
    // the explicit exit (the separators' empty text included).
    const QStringList expectedFile = { QString("Open..."),
                                       QString("Export current playlist..."),
                                       QString("Import playlist..."),
                                       QString(),
                                       QString("Preferences"),
                                       QString(),
                                       QString("Exit doob"), };
    QCOMPARE(fileTitles, expectedFile);

    QMenu* view = bar->actions().at(1)->menu();
    QVERIFY(view);
    QCOMPARE(view->title(), QString("View"));

    // The View menu itself carries the edit-mode toggle, the "Window"
    // submenu (the chrome toggles — the platforms where the bar can be
    // hidden and the frame can be frameless, offscreen is such a
    // platform), the Layout submenu and the Styles submenu.
    QStringList viewTitles;
    for (QAction* action : view->actions())
        viewTitles.append(action->text());
    const QStringList expectedView = {QString("Edit mode"), QString("Window"),
                                      QString("Layout"), QString("Styles"),};
    QCOMPARE(viewTitles, expectedView);

    // The "Window" submenu: the chrome's checkable toggles, the menu
    // bar's visibility and the frameless state.
    QMenu* windowMenu = view->actions().at(1)->menu();
    QVERIFY(windowMenu);
    QCOMPARE(windowMenu->title(), QString("Window"));
    QStringList windowTitles;
    for (QAction* action : windowMenu->actions())
        windowTitles.append(action->text());
    QCOMPARE(windowTitles, (QStringList{QString("Menu bar"),
                                        QString("Frameless")}));

    QMenu* layout = view->actions().at(2)->menu();
    QVERIFY(layout);
    QCOMPARE(layout->title(), QString("Layout"));

    // The Layout menu is empty until its aboutToShow rebuilds it —
    // the dynamic entries first, then the management entries (the
    // windows editor and the picker dialog — the store's
    // New/Copy to/Delete/Import/Export operations are the picker's
    // per-pane buttons), divided by separators.
    QVERIFY(layout->actions().isEmpty());

    // The builders rebuild the menus on aboutToShow — drive them
    // directly (the popup event is unreliable offscreen). A user
    // layout and a preset in store, the user one current.
    QTemporaryDir tmp;
    setDataDir(tmp, "mine");
    QJsonObject tabs;
    tabs["type"] = "tabs";
    QJsonObject doc;
    doc["container"] = tabs;
    QDir().mkpath(Settings::instance()->layoutsDir());
    QFile mine(Settings::instance()->layoutsDir() + "/mine.json");
    QVERIFY(mine.open(QIODevice::WriteOnly));
    mine.write(QJsonDocument(doc).toJson());
    mine.close();
    writePreset(m_presets, "classic", doc);
    LayoutManager::getInstance()->fillLayoutMenu(layout);
    // The menu lists the user's files only (the preset is not a menu
    // entry — it is taken as a copy from the dialog), the current
    // checked, then the management entries.
    QStringList layoutTitles;
    for (QAction* action : layout->actions())
        if (!action->text().isEmpty())
            layoutTitles.append(action->text());
    QCOMPARE(layoutTitles, (QStringList{QString("mine"),
                                        QString("Manage windows"),
                                        QString("Manage layouts")}));
    QList<QAction*> layoutActions = layout->actions();
    QVERIFY(layoutActions.at(0)->isChecked());

    // A fresh store (no user files, no current): only the management
    // entries.
    setDataDir(tmp, QString());
    LayoutManager::getInstance()->fillLayoutMenu(layout);
    QStringList freshTitles;
    for (QAction* action : layout->actions())
        if (!action->text().isEmpty())
            freshTitles.append(action->text());
    QCOMPARE(freshTitles, (QStringList{QString("Manage windows"),
                                       QString("Manage layouts")}));

    // The Styles menu mirrors the Layout one (re-built the same way):
    // the dynamic entries first, then the management entries — the
    // picker dialog and the sheet editor.
    QMenu* stylesMenu = view->actions().at(3)->menu();
    QVERIFY(stylesMenu);
    QVERIFY(stylesMenu->actions().isEmpty());
    StyleManager::getInstance()->fillStylesMenu(stylesMenu);
    QStringList styleTitles;
    for (QAction* action : stylesMenu->actions())
        if (!action->text().isEmpty())
            styleTitles.append(action->text());
    // No user styles: the dynamic list is empty.
    QCOMPARE(styleTitles, (QStringList{QString("Manage styles"),
                                       QString("Edit style sheet")}));
}

void LayoutManagerTest::testWindowGeometrySavedAndRestored()
{
    LayoutManager* lm = LayoutManager::getInstance();
    MainWindow* window = lm->window();
    QVERIFY(window);

    QTemporaryDir tmp;
    setDataDir(tmp, "test1");
    const QString layoutFile = Settings::instance()->layoutsDir() + "/test1.json";

    window->setGeometry(100, 50, 640, 480);
    QApplication::processEvents();
    QMetaObject::invokeMethod(lm, "saveLayout");
    QVERIFY(QFile::exists(layoutFile));

    // The window state sits at the root of the document, next to the
    // root container's own "container" key (no "type" at the top
    // level).
    QFile f(layoutFile);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject doc = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    QVERIFY(doc.contains("container"));
    QVERIFY(!doc.contains("type"));
    const QJsonObject win = doc.value("window").toObject();
    QCOMPARE(win.value("x").toInt(), 100);
    QCOMPARE(win.value("y").toInt(), 50);
    QCOMPARE(win.value("width").toInt(), 640);
    QCOMPARE(win.value("height").toInt(), 480);
    QCOMPARE(win.value("maximized").toBool(), false);

    // Move and resize the window; loading must bring the saved state back.
    window->setGeometry(0, 0, 200, 150);
    QApplication::processEvents();
    QMetaObject::invokeMethod(lm, "loadLayout");
    QApplication::processEvents();
    QCOMPARE(window->geometry(), QRect(100, 50, 640, 480));
}

void LayoutManagerTest::testWindowGeometryOffScreenIsCentered()
{
    LayoutManager* lm = LayoutManager::getInstance();
    MainWindow* window = lm->window();
    QVERIFY(window);

    QTemporaryDir tmp;
    setDataDir(tmp, "test1");
    const QString layoutFile = Settings::instance()->layoutsDir() + "/test1.json";

    // Save a position that no screen covers.
    window->setGeometry(-5000, -5000, 300, 200);
    QApplication::processEvents();
    QMetaObject::invokeMethod(lm, "saveLayout");
    QVERIFY(QFile::exists(layoutFile));

    // Move the window somewhere else so the restore is detectable.
    window->setGeometry(0, 0, 200, 150);
    QApplication::processEvents();
    QMetaObject::invokeMethod(lm, "loadLayout");
    QApplication::processEvents();

    const QRect geo = window->geometry();
    QCOMPARE(geo.size(), QSize(300, 200)); // the saved size is kept
    bool onScreen = false;
    const QList<QScreen*> screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        if (screen->geometry().intersects(geo))
            onScreen = true;
    }
    QVERIFY(onScreen);
}

void LayoutManagerTest::testWindowGeometryOversizedIsIgnored()
{
    // A size no screen can hold (a corrupt document, one written on a
    // much bigger display) is invalid state: the saved size AND
    // position are ignored, the window keeps its current geometry.
    LayoutManager* lm = LayoutManager::getInstance();
    MainWindow* window = lm->window();
    QVERIFY(window);

    QTemporaryDir dataDir;
    QTemporaryDir importDir;
    setDataDir(dataDir, "test1");

    window->setGeometry(100, 50, 640, 480);
    QApplication::processEvents();

    const QString path = importDir.filePath("oversized.json");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("{\"container\": {}, \"window\": {\"x\": -5000, \"y\": -5000, "
            "\"width\": 100000, \"height\": 100000}}");
    f.close();

    QVERIFY(lm->loadLayoutFrom(path));
    QApplication::processEvents();
    QCOMPARE(window->geometry(), QRect(100, 50, 640, 480));
}

// The child windows ride in the document under the "windows" key, next
// to the root container and the window state; the automatic save/load
// round-trips them.
void LayoutManagerTest::testWindowsSavedAndLoaded()
{
    LayoutManager* lm = LayoutManager::getInstance();
    MainWindow* window = lm->window();
    QVERIFY(window);
    WindowManager* wm = WindowManager::getInstance();
    wm->clear();
    qApp->processEvents();

    QTemporaryDir tmp;
    setDataDir(tmp, "test1");
    const QString layoutFile = Settings::instance()->layoutsDir() + "/test1.json";

    const QString id = wm->addWindow(WindowManager::Kind::Standalone, "Side");
    wm->containerOf(id)->setWidget(new SplitterWidget);
    wm->widgetOf(id)->setGeometry(300, 200, 400, 300);

    QMetaObject::invokeMethod(lm, "saveLayout");
    QFile f(layoutFile);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject doc = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    QVERIFY(doc.contains("windows"));
    QCOMPARE(doc.value("windows").toArray().size(), 1);
    const QJsonObject w = doc.value("windows").toArray().first().toObject();
    QCOMPARE(w.value("id").toString(), id);
    QCOMPARE(w.value("kind").toString(), QString("standalone"));
    QCOMPARE(w.value("title").toString(), QString("Side"));
    QCOMPARE(w.value("visible").toBool(), true);
    QCOMPARE(w.value("container").toObject().value("type").toString(),
             QString("splitter"));

    // Destroy the live windows; loading must bring them back.
    wm->clear();
    qApp->processEvents();
    QVERIFY(wm->windows().isEmpty());
    QMetaObject::invokeMethod(lm, "loadLayout");
    qApp->processEvents();

    QCOMPARE(wm->windows().size(), 1);
    const WindowManager::Info info = wm->window(id);
    QCOMPARE(info.kind, WindowManager::Kind::Standalone);
    QCOMPARE(info.title, QString("Side"));
    QCOMPARE(info.visible, true);
    QCOMPARE(wm->widgetOf(id)->geometry(), QRect(300, 200, 400, 300));
    QVERIFY(wm->containerOf(id)->getWidget() != nullptr);
    QCOMPARE(wm->containerOf(id)->getWidget()->widgetId(), QString("splitter"));

    wm->clear();
    qApp->processEvents();
}

// The app's exit: the main window's closeEvent saves the layout
// before the base class hides the window (the hide cascades to the
// docked children). A docked window that was open must be saved
// visible (and restored visible), not "closed by the cascade" —
// otherwise it vanishes on every restart.
void LayoutManagerTest::testDockedWindowSurvivesAppExit()
{
    LayoutManager* lm = LayoutManager::getInstance();
    MainWindow* window = lm->window();
    QVERIFY(window);
    WindowManager* wm = WindowManager::getInstance();
    wm->clear();
    qApp->processEvents();

    QTemporaryDir tmp;
    setDataDir(tmp, "test1");
    const QString layoutFile = Settings::instance()->layoutsDir() + "/test1.json";

    const QString id = wm->addWindow(WindowManager::Kind::Docked, "Side");
    wm->containerOf(id)->setWidget(new TabsWidget);
    QCOMPARE(wm->windowVisible(id), true);

    // The exit: close the main window (in the app this is where the
    // app quits and saves the layout). The test closes the window and
    // saves the layout the same way; the quit on last window closed is
    // suppressed so the test app keeps running.
    const bool quitOnLast = qApp->quitOnLastWindowClosed();
    qApp->setQuitOnLastWindowClosed(false);
    window->close();
    qApp->processEvents();
    qApp->setQuitOnLastWindowClosed(quitOnLast);

    QMetaObject::invokeMethod(lm, "saveLayout");
    QFile f(layoutFile);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject doc = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    QCOMPARE(doc.value("windows").toArray().size(), 1);
    const QJsonObject w = doc.value("windows").toArray().first().toObject();
    QCOMPARE(w.value("id").toString(), id);
    QCOMPARE(w.value("kind").toString(), QString("docked"));
    QCOMPARE(w.value("visible").toBool(), true);
    QCOMPARE(w.value("container").toObject().value("type").toString(),
             QString("tabs"));
    QVERIFY(doc.value("window").toObject().contains("dock_state"));

    // The restart: show the main window and load the layout.
    window->show();
    qApp->processEvents();
    QMetaObject::invokeMethod(lm, "loadLayout");
    qApp->processEvents();

    QCOMPARE(wm->windows().size(), 1);
    QCOMPARE(wm->windowVisible(id), true);
    QVERIFY(wm->widgetOf(id)->isVisible());
    QVERIFY(wm->containerOf(id)->getWidget() != nullptr);
    QCOMPARE(wm->containerOf(id)->getWidget()->widgetId(), QString("tabs"));

    wm->clear();
    qApp->processEvents();
}

// A docked window's edge in the main window (the dock arrangement, in
// the main window's state) survives a save/load cycle (the app's
// restart): a dock the user moved to the left comes back on the left,
// not at its default top position.
void LayoutManagerTest::testDockedPositionPersists()
{
    LayoutManager* lm = LayoutManager::getInstance();
    MainWindow* window = lm->window();
    QVERIFY(window);
    WindowManager* wm = WindowManager::getInstance();
    wm->clear();
    qApp->processEvents();

    QTemporaryDir tmp;
    setDataDir(tmp, "test1");

    const QString id = wm->addWindow(WindowManager::Kind::Docked, "Side");
    wm->containerOf(id)->setWidget(new TabsWidget);
    qApp->processEvents();
    QDockWidget* dock = qobject_cast<QDockWidget*>(wm->widgetOf(id));
    QVERIFY(dock);
    QCOMPARE(window->dockWidgetArea(dock), int(Qt::TopDockWidgetArea));

    // The user drags the dock to the left edge.
    window->addDockWidget(Qt::LeftDockWidgetArea, dock);
    qApp->processEvents();
    QCOMPARE(window->dockWidgetArea(dock), int(Qt::LeftDockWidgetArea));

    QMetaObject::invokeMethod(lm, "saveLayout");

    // The restart: destroy the live windows and load the layout.
    wm->clear();
    qApp->processEvents();
    QMetaObject::invokeMethod(lm, "loadLayout");
    qApp->processEvents();

    QCOMPARE(wm->windows().size(), 1);
    QDockWidget* dock2 = qobject_cast<QDockWidget*>(wm->widgetOf(id));
    QVERIFY(dock2);
    QCOMPARE(window->dockWidgetArea(dock2), int(Qt::LeftDockWidgetArea));

    wm->clear();
    qApp->processEvents();
}

// The windows editor (View → Layout → Windows...) is non-modal and a
// single instance: a window shown from it must be interactable at once
// (a modal dialog would lock the user out of a window big enough to
// cover it), a second trigger raises the open dialog, and a closed one
// opens fresh on the next trigger.
void LayoutManagerTest::testWindowsEditorIsNonModal()
{
    LayoutManager* lm = LayoutManager::getInstance();
    MainWindow* window = lm->window();
    QVERIFY(window);
    WindowManager* wm = WindowManager::getInstance();
    wm->clear();
    qApp->processEvents();

    QMetaObject::invokeMethod(lm, "editWindows");
    qApp->processEvents();

    WindowsDialog* dialog = window->findChild<WindowsDialog*>();
    if (dialog == nullptr)
        QFAIL("the editor dialog was not opened");
    QVERIFY(dialog->isVisible());
    QVERIFY(!dialog->isModal());

    // A second trigger raises the same dialog (not a second one).
    QMetaObject::invokeMethod(lm, "editWindows");
    qApp->processEvents();
    QCOMPARE(window->findChildren<WindowsDialog*>().size(), 1);
    QCOMPARE(window->findChild<WindowsDialog*>(), dialog);

    // Closing destroys the dialog (WA_DeleteOnClose) and the next
    // trigger opens a fresh one.
    dialog->close();
    qApp->processEvents();
    QVERIFY(window->findChild<WindowsDialog*>() == nullptr);

    QMetaObject::invokeMethod(lm, "editWindows");
    qApp->processEvents();
    WindowsDialog* dialog2 = window->findChild<WindowsDialog*>();
    if (dialog2 == nullptr)
        QFAIL("the reopened editor dialog was not found");
    QVERIFY(dialog2 != dialog);
    dialog2->close();
    qApp->processEvents();

    wm->clear();
    qApp->processEvents();
}

// The app's exit with a standalone window open: the main window is the
// app's anchor, so closing it quits the app and saves the layout while
// the standalone window is still open — it must be saved visible (and
// restored visible), like the docked case.
void LayoutManagerTest::testStandaloneWindowSurvivesAppExit()
{
    LayoutManager* lm = LayoutManager::getInstance();
    MainWindow* window = lm->window();
    QVERIFY(window);
    WindowManager* wm = WindowManager::getInstance();
    wm->clear();
    qApp->processEvents();

    QTemporaryDir tmp;
    setDataDir(tmp, "test1");
    const QString layoutFile = Settings::instance()->layoutsDir() + "/test1.json";

    const QString id = wm->addWindow(WindowManager::Kind::Standalone, "Side");
    wm->containerOf(id)->setWidget(new TabsWidget);
    wm->widgetOf(id)->setGeometry(100, 100, 400, 300);
    qApp->processEvents();
    QCOMPARE(wm->windowVisible(id), true);

    // The exit: close the main window (in the app this is where the
    // app quits and the closeEvent saves the layout). The test closes
    // the window and saves the layout the same way; the quit on last
    // window closed is suppressed so the test app keeps running.
    const bool quitOnLast = qApp->quitOnLastWindowClosed();
    qApp->setQuitOnLastWindowClosed(false);
    window->close();
    qApp->processEvents();
    qApp->setQuitOnLastWindowClosed(quitOnLast);

    QMetaObject::invokeMethod(lm, "saveLayout");
    QFile f(layoutFile);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject doc = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    QCOMPARE(doc.value("windows").toArray().size(), 1);
    const QJsonObject w = doc.value("windows").toArray().first().toObject();
    QCOMPARE(w.value("id").toString(), id);
    QCOMPARE(w.value("kind").toString(), QString("standalone"));
    QCOMPARE(w.value("visible").toBool(), true);
    QCOMPARE(w.value("container").toObject().value("type").toString(),
             QString("tabs"));

    window->show();
    qApp->processEvents();
    QMetaObject::invokeMethod(lm, "loadLayout");
    qApp->processEvents();

    QCOMPARE(wm->windows().size(), 1);
    QCOMPARE(wm->windowVisible(id), true);
    QVERIFY(wm->widgetOf(id)->isVisible());
    QCOMPARE(wm->widgetOf(id)->geometry(), QRect(100, 100, 400, 300));
    QVERIFY(wm->containerOf(id)->getWidget() != nullptr);
    QCOMPARE(wm->containerOf(id)->getWidget()->widgetId(), QString("tabs"));

    wm->clear();
    qApp->processEvents();
}

// The app's real exit path: the main window is closed from inside a
// running event loop, and its closeEvent saves the layout before the
// quit tears the windows down (the close cascades a hide to the
// docked children, and the quit handler closes the top-levels and
// this window a second time — all after the save). Both child windows
// (standalone and docked) were open: the save must record them as
// open, and the teardown's second close must not re-save the
// torn-down state over it.
void LayoutManagerTest::testRealQuitSavesWindowsAsTheyWere()
{
    LayoutManager* lm = LayoutManager::getInstance();
    MainWindow* window = lm->window();
    QVERIFY(window);
    WindowManager* wm = WindowManager::getInstance();
    wm->clear();
    qApp->processEvents();

    QTemporaryDir tmp;
    setDataDir(tmp, "test1");
    const QString layoutFile = Settings::instance()->layoutsDir() + "/test1.json";

    const QString solo = wm->addWindow(WindowManager::Kind::Standalone, "Solo");
    wm->containerOf(solo)->setWidget(new TabsWidget);
    const QString dock = wm->addWindow(WindowManager::Kind::Docked, "Dock");
    wm->containerOf(dock)->setWidget(new SplitterWidget);
    qApp->processEvents();

    // Hide the main windows leaked by the tests that re-show the
    // layout manager: while one is visible, the automatic quit of the
    // close below would not fire, and the test would fall back to the
    // failsafe timer.
    for (QWidget* w : qApp->topLevelWidgets())
        if (qobject_cast<QMainWindow*>(w) != nullptr && w != window && w->isVisible())
            w->hide();

    // The exit, the way the app does it: the user closes the main
    // window while the event loop runs; the closeEvent saves the
    // layout, then the quit tears the windows down. A failsafe timer
    // keeps the loop from hanging.
    QTimer::singleShot(0, window, &QWidget::close);
    QTimer::singleShot(3000, qApp, &QCoreApplication::quit);
    qApp->exec();

    QFile f(layoutFile);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject doc = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    const QJsonArray ws = doc.value("windows").toArray();
    QCOMPARE(ws.size(), 2);
    for (const QJsonValue v : ws) {
        const QJsonObject w = v.toObject();
        if (w.value("id").toString() == solo) {
            QCOMPARE(w.value("kind").toString(), QString("standalone"));
            QCOMPARE(w.value("visible").toBool(), true);
        } else {
            QCOMPARE(w.value("id").toString(), dock);
            QCOMPARE(w.value("kind").toString(), QString("docked"));
            QCOMPARE(w.value("visible").toBool(), true);
        }
    }

    // The restart: show the main window and load the layout.
    window->show();
    qApp->processEvents();
    QMetaObject::invokeMethod(lm, "loadLayout");
    qApp->processEvents();

    QCOMPARE(wm->windows().size(), 2);
    QCOMPARE(wm->windowVisible(solo), true);
    QVERIFY(wm->widgetOf(solo)->isVisible());
    QCOMPARE(wm->windowVisible(dock), true);
    QVERIFY(wm->widgetOf(dock)->isVisible());

    wm->clear();
    qApp->processEvents();
}

// A window the user moved (a drag — the carrier's move/resize events
// are what update the record) comes back at the moved position after a
// save/load cycle (the app's restart): the position rides in the
// document, and the restore applies it once the window is shown (a
// window manager may re-place a window on the first map).
void LayoutManagerTest::testMovedWindowPositionPersists()
{
    LayoutManager* lm = LayoutManager::getInstance();
    MainWindow* window = lm->window();
    QVERIFY(window);
    WindowManager* wm = WindowManager::getInstance();
    wm->clear();
    qApp->processEvents();

    QTemporaryDir tmp;
    setDataDir(tmp, "test1");
    const QString layoutFile = Settings::instance()->layoutsDir() + "/test1.json";

    const QString id = wm->addWindow(WindowManager::Kind::Standalone, "Moved");
    // The user drags the window; the carrier's move/resize events
    // update the record.
    wm->widgetOf(id)->setGeometry(400, 250, 420, 310);
    qApp->processEvents();

    QMetaObject::invokeMethod(lm, "saveLayout");
    QFile f(layoutFile);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject doc = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    const QJsonObject geo = doc.value("windows").toArray().first().toObject()
        .value("geometry").toObject();
    QCOMPARE(geo.value("x").toInt(), 400);
    QCOMPARE(geo.value("y").toInt(), 250);
    QCOMPARE(geo.value("width").toInt(), 420);
    QCOMPARE(geo.value("height").toInt(), 310);

    // The restart: destroy the live windows and load the layout.
    wm->clear();
    qApp->processEvents();
    QMetaObject::invokeMethod(lm, "loadLayout");
    qApp->processEvents();

    QCOMPARE(wm->windows().size(), 1);
    QCOMPARE(wm->widgetOf(id)->geometry(), QRect(400, 250, 420, 310));

    wm->clear();
    qApp->processEvents();
}

// The strict import walks the windows' container trees like the main
// one: a document whose window references a widget this build cannot
// create is refused whole.
void LayoutManagerTest::testImportRejectsUnknownTypeInWindow()
{
    LayoutManager* lm = LayoutManager::getInstance();
    MainWindow* window = lm->window();
    QVERIFY(window);
    WindowManager* wm = WindowManager::getInstance();
    wm->clear();
    qApp->processEvents();

    QTemporaryDir dataDir;
    QTemporaryDir importDir;
    setDataDir(dataDir, "test1");
    (void)importDir;

    window->mainContainer()->setWidget(new TabsWidget);

    const QString unknownPath = importDir.filePath("window-unknown.json");
    QFile f(unknownPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("{\"container\": {\"type\": \"tabs\"}, "
            "\"windows\": [{\"id\": \"window-1\", \"kind\": \"standalone\", "
            "\"title\": \"Ghost\", \"visible\": true, "
            "\"container\": {\"type\": \"ghostwidget\"}}]}");
    f.close();

    QVERIFY(!lm->loadLayoutFrom(unknownPath));
    // The current layout survived the failed import.
    QVERIFY(window->mainContainer()->getWidget());
    QCOMPARE(window->mainContainer()->getWidget()->widgetId(), QString("tabs"));
    QVERIFY(wm->windows().isEmpty());

    wm->clear();
    qApp->processEvents();
}

// The automatic load is lenient: a saved window whose widget tree
// references a missing plugin's widget is kept with an empty container
// (the subtree is skipped with a warning).
void LayoutManagerTest::testAutoLoadSkipsUnknownWindowSubtree()
{
    LayoutManager* lm = LayoutManager::getInstance();
    MainWindow* window = lm->window();
    QVERIFY(window);
    WindowManager* wm = WindowManager::getInstance();
    wm->clear();
    qApp->processEvents();

    QTemporaryDir dataDir;
    setDataDir(dataDir, "test1");

    QFile f(Settings::instance()->layoutsDir() + "/test1.json");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("{\"container\": {\"type\": \"tabs\"}, "
            "\"windows\": [{\"id\": \"window-7\", \"kind\": \"standalone\", "
            "\"title\": \"Ghost\", \"visible\": true, "
            "\"container\": {\"type\": \"ghostwidget\"}}]}");
    f.close();

    QMetaObject::invokeMethod(lm, "loadLayout");
    qApp->processEvents();

    // The window itself is restored; only its unknown subtree is
    // skipped (an empty container).
    QCOMPARE(wm->windows().size(), 1);
    const WindowManager::Info info = wm->window("window-7");
    QCOMPARE(info.kind, WindowManager::Kind::Standalone);
    QCOMPARE(info.title, QString("Ghost"));
    QVERIFY(wm->containerOf("window-7") != nullptr);
    QCOMPARE(wm->containerOf("window-7")->getWidget(), nullptr);
    // The main container is unaffected.
    QCOMPARE(window->mainContainer()->getWidget()->widgetId(), QString("tabs"));

    wm->clear();
    qApp->processEvents();
}

// The auto-name counter is a single, monotonically growing value (the
// "widgets.name_counter" setting; the numbers keep growing across
// launches); a name
// a live widget already carries (a copy-paste keeps its name) and a
// name the sheet references (a deleted widget's rule) are never handed
// out.
void LayoutManagerTest::testFreeInstanceName()
{
    LayoutManager* lm = LayoutManager::getInstance();
    SettingsManager* settings = SettingsManager::getInstance();
    StyleManager::getInstance()->setStyleSheet(QString());

    // A reset (or fresh) counter starts at 1.
    settings->setValue("widgets.name_counter", 0);
    QCOMPARE(lm->freeInstanceName("peakmeter"), QString("peakmeter-1"));
    QCOMPARE(lm->freeInstanceName("oscillogram"), QString("oscillogram-2"));
    QCOMPARE(settings->value("widgets.name_counter").toInt(), 3);

    // A name a live layout widget carries is skipped.
    WidgetContainer* main = lm->window()->mainContainer();
    main->setWidget(new PluginTestWidget);
    main->getWidget()->setObjectName("plugtest-3");
    QCOMPARE(lm->freeInstanceName("plugtest"), QString("plugtest-4"));
    main->setWidget(nullptr);

    // A name the sheet references is never handed out, even though the
    // counter has not reached it: a rule left behind by a deleted
    // widget must not be inherited by a fresh instance.
    StyleManager::getInstance()->setStyleSheet("#plugtest-5 { color: red; }");
    QCOMPARE(lm->freeInstanceName("plugtest"), QString("plugtest-6"));
    QCOMPARE(settings->value("widgets.name_counter").toInt(), 7);
    StyleManager::getInstance()->setStyleSheet(QString());
}

// The instance name rides in the node configuration: it survives the
// save/load round-trip and the copy/paste serialization (a pasted copy
// keeps the name, and with it the "#name" rule's look).
void LayoutManagerTest::testWidgetNameRoundTrips()
{
    LayoutManager* lm = LayoutManager::getInstance();
    WidgetRegistry* registry = WidgetRegistry::getInstance();
    // testPluginWidgetRegistration unregistered the id; it is free again.
    QVERIFY(registry->registerWidget("plugtest", "Plugtest",
                                     [](WidgetConfiguration &) {
                                         return new PluginTestWidget;
                                     }));

    MainWindow* window = lm->window();
    WidgetContainer* main = window->mainContainer();
    main->setWidget(new PluginTestWidget);
    main->getWidget()->setObjectName("plugtest-42");

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath("named.json");
    QVERIFY(lm->saveLayoutTo(path));

    // A fresh, unnamed widget; loading must bring the name back.
    main->setWidget(new PluginTestWidget);
    QCOMPARE(main->getWidget()->objectName(), QString());
    QVERIFY(lm->loadLayoutFrom(path));
    QVERIFY(main->getWidget());
    QCOMPARE(main->getWidget()->objectName(), QString("plugtest-42"));

    // The copy/paste path carries the name too.
    const QString text = lm->serializeContainer(main);
    BaseWidget* pasted = lm->createWidgetFromSerializedText(text);
    QVERIFY(pasted);
    QCOMPARE(pasted->objectName(), QString("plugtest-42"));
    delete pasted;

    registry->unregisterWidget("plugtest");
    main->setWidget(nullptr);
}

// The overlay's grayed veil (edit mode: the context menu is called on
// an empty container) follows the theme — the palette's shadow color,
// translucent — and the overlay paints nothing when idle.
void LayoutManagerTest::testOverlayGrayedVeil()
{
    LayoutManager* lm = LayoutManager::getInstance();
    WidgetContainer* main = lm->window()->mainContainer();
    WidgetContainerOverlay* overlay = main->getOverlayWidget();
    QVERIFY(overlay != nullptr);

    overlay->resize(40, 40);

    // (The render target is a transparent image — a default QPixmap
    // holds undefined contents where the widget paints nothing.)

    // Idle: fully transparent.
    overlay->grayed(false);
    QImage idle(40, 40, QImage::Format_ARGB32);
    idle.fill(Qt::transparent);
    overlay->render(&idle);
    QCOMPARE(idle.pixelColor(20, 20).alpha(), 0);

    // Grayed: the theme's shadow color, translucent.
    overlay->grayed(true);
    QImage grayed(40, 40, QImage::Format_ARGB32);
    grayed.fill(Qt::transparent);
    overlay->render(&grayed);
    const QColor expected = overlay->palette().color(QPalette::Shadow);
    QCOMPARE(grayed.pixelColor(20, 20).alpha(), 100);
    // (The ARGB image stores the color premultiplied, so the un-
    // premultiplied pixel can differ from the brush color by one
    // channel unit.)
    const QColor pixel = grayed.pixelColor(20, 20);
    QVERIFY(qAbs(pixel.red() - expected.red()) <= 1);
    QVERIFY(qAbs(pixel.green() - expected.green()) <= 1);
    QVERIFY(qAbs(pixel.blue() - expected.blue()) <= 1);
    overlay->grayed(false);
}

void LayoutManagerTest::testAutoSaveOnQuit()
{
    LayoutManager* lm = LayoutManager::getInstance();
    MainWindow* window = lm->window();
    QVERIFY(window);
    WidgetContainer* main = window->mainContainer();
    QVERIFY(main);

    main->setWidget(new SplitterWidget);

    QTemporaryDir tmp;
    setDataDir(tmp, "test1");
    const QString layoutFile = Settings::instance()->layoutsDir() + "/test1.json";
    QVERIFY(!QFile::exists(layoutFile));

    // Hide the main windows leaked by the tests that re-show the
    // layout manager: while one is visible, the automatic quit of the
    // close below would not fire, and the test would fall back to the
    // failsafe timer.
    for (QWidget* w : qApp->topLevelWidgets())
        if (qobject_cast<QMainWindow*>(w) != nullptr && w != window && w->isVisible())
            w->hide();

    // The app's real exit: the main window is closed from inside a
    // running event loop. Its closeEvent saves the layout (the save
    // must happen before the quit tears the windows down, so a plain
    // quit() with no window close would never save). The second timer
    // is a failsafe so the loop can never hang the test run.
    QTimer::singleShot(0, window, &QWidget::close);
    QTimer::singleShot(2000, qApp, &QCoreApplication::quit);
    qApp->exec();

    QVERIFY(QFile::exists(layoutFile));
    QVERIFY(QFile(layoutFile).size() > 0);

    // The quit's teardown hid the window: show it again for the tests
    // that run after this one.
    window->show();
    qApp->processEvents();
}

void LayoutManagerTest::testAutoLoadOnShow()
{
    LayoutManager* lm = LayoutManager::getInstance();

    QTemporaryDir tmp;
    setDataDir(tmp, "test1");
    const QString layoutFile = Settings::instance()->layoutsDir() + "/test1.json";

    // Save a known layout (tabs) into the temp data dir.
    WidgetContainer* main = lm->window()->mainContainer();
    main->setWidget(new TabsWidget);
    QMetaObject::invokeMethod(lm, "saveLayout");
    QVERIFY(QFile::exists(layoutFile));

    // Overwrite the live layout so a no-op load would be detectable.
    main->setWidget(new SplitterWidget);

    // Showing again must automatically restore the saved (tabs) layout.
    lm->show();

    WidgetContainer* freshMain = lm->window()->mainContainer();
    QVERIFY(freshMain);
    QVERIFY(freshMain->getWidget());
    QCOMPARE(freshMain->getWidget()->widgetId(), QString("tabs"));
}

// The listing: the user's files and the read-only presets (a user file
// shadows a same-named preset), sorted by title.
void LayoutManagerTest::testLayoutsListing()
{
    LayoutManager* lm = LayoutManager::getInstance();

    QTemporaryDir data;
    QTemporaryDir presets;
    setDataDir(data, "test1");
    setPresetsDir(presets);

    QJsonObject a;
    a["container"] = QJsonObject{{"type", QString("tabs")}};
    writePreset(presets, "alpha", a);
    QJsonObject b;
    b["container"] = QJsonObject{{"type", QString("splitter")}};
    b["title"] = "Beta preset";
    writePreset(presets, "beta", b);

    const QString userFile = Settings::instance()->layoutsDir() + "/gamma.json";
    QFile f(userFile);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(a).toJson());
    f.close();

    const QList<LayoutManager::LayoutInfo> layouts = lm->layouts();
    // The user's files and the presets, sorted by title
    // (case-insensitive: "alpha" < "Beta preset" < "gamma").
    QCOMPARE(layouts.size(), 3);
    QStringList names;
    for (const LayoutManager::LayoutInfo &info : layouts)
        names.append(info.name);
    const QStringList expected = {QString("alpha"), QString("beta"),
                                  QString("gamma"),};
    QCOMPARE(names, expected);
    for (const LayoutManager::LayoutInfo &info : layouts)
        QCOMPARE(info.readOnly, info.name == "alpha" || info.name == "beta");
    // The title comes from the document's "title" key, else the name.
    for (const LayoutManager::LayoutInfo &info : layouts) {
        if (info.name == "beta")
            QCOMPARE(info.title, QString("Beta preset"));
        else
            QCOMPARE(info.title, info.name);
    }
}

// A user file with a preset's name shadows the preset: one entry, the
// user's text wins on read, and the name is not read-only.
void LayoutManagerTest::testUserFileShadowsPreset()
{
    LayoutManager* lm = LayoutManager::getInstance();

    QTemporaryDir data;
    QTemporaryDir presets;
    setDataDir(data, "test1");
    setPresetsDir(presets);

    QJsonObject preset;
    preset["container"] = QJsonObject{{"type", QString("tabs")}};
    writePreset(presets, "alpha", preset);

    const QString userFile = Settings::instance()->layoutsDir() + "/alpha.json";
    QFile f(userFile);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(preset).toJson());
    f.close();

    int alphas = 0;
    for (const LayoutManager::LayoutInfo &info : lm->layouts())
        if (info.name == "alpha") {
            ++alphas;
            QCOMPARE(info.readOnly, false);
        }
    QCOMPARE(alphas, 1);
    QVERIFY(!lm->isPreset("alpha"));
}

// The active layout rides in the "layout.current" setting (it survives
// a restart, which is what loadCurrentLayout reads); an empty current
// unsets the
// key (the first-run signal).
void LayoutManagerTest::testCurrentLayoutPersistence()
{
    LayoutManager* lm = LayoutManager::getInstance();
    QTemporaryDir data;
    setDataDir(data, QString());

    QCOMPARE(lm->currentLayout(), QString());

    lm->setCurrentLayout("test1");
    QCOMPARE(lm->currentLayout(), QString("test1"));
    QCOMPARE(SettingsManager::getInstance()->value("layout.current").toString(),
             QString("test1"));

    lm->setCurrentLayout(QString());
    QCOMPARE(lm->currentLayout(), QString());
}

// First run: no "layout.current" and no file in the user's layouts
// directory.
void LayoutManagerTest::testFirstRun()
{
    LayoutManager* lm = LayoutManager::getInstance();

    QTemporaryDir data;
    setDataDir(data, QString());
    QVERIFY(lm->firstRun());

    // A file in the layouts directory is not a first run (something was
    // saved before).
    const QString userFile = Settings::instance()->layoutsDir() + "/x.json";
    QFile f(userFile);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("{}");
    f.close();
    QVERIFY(!lm->firstRun());
    QFile::remove(userFile);

    // A current layout is not a first run, either.
    lm->setCurrentLayout("test1");
    QVERIFY(!lm->firstRun());
}

// A switch saves the previous layout (the user's file gets the modified
// live state) and applies the target.
void LayoutManagerTest::testSwitchLayoutSavesCurrent()
{
    LayoutManager* lm = LayoutManager::getInstance();
    WidgetContainer* main = lm->window()->mainContainer();

    QTemporaryDir data;
    setDataDir(data, "a");
    const QString fileA = Settings::instance()->layoutsDir() + "/a.json";
    const QString fileB = Settings::instance()->layoutsDir() + "/b.json";

    // Two user layouts.
    main->setWidget(new TabsWidget);
    QFile f(fileA);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(lm->buildLayoutDocument()).toJson());
    f.close();
    main->setWidget(new SplitterWidget);
    QFile fb(fileB);
    QVERIFY(fb.open(QIODevice::WriteOnly));
    fb.write(QJsonDocument(lm->buildLayoutDocument()).toJson());
    fb.close();

    // The current is "a"; the live state is "a" (tabs) — now modified.
    main->setWidget(new TabsWidget);
    main->setWidget(new PluginTestWidget); // the modification

    QVERIFY(lm->switchLayout("b"));
    QCOMPARE(lm->currentLayout(), QString("b"));
    // The live state is "b" (splitter).
    QCOMPARE(main->getWidget()->widgetId(), QString("splitter"));
    // "a"'s file holds the modification (plugtest), not the original.
    QFile g(fileA);
    QVERIFY(g.open(QIODevice::ReadOnly));
    const QJsonObject docA = QJsonDocument::fromJson(g.readAll()).object();
    QCOMPARE(docA.value("container").toObject().value("type").toString(),
             QString("plugtest"));
}

// A switch to a preset takes a copy: the fork "<name>.modified" is
// written (origin recorded) and made current, the preset itself is
// untouched.
void LayoutManagerTest::testSwitchLayoutToPreset()
{
    LayoutManager* lm = LayoutManager::getInstance();
    WidgetContainer* main = lm->window()->mainContainer();

    QTemporaryDir data;
    QTemporaryDir presets;
    setDataDir(data, QString());
    setPresetsDir(presets);

    QJsonObject preset;
    preset["container"] = QJsonObject{{"type", QString("tabs")}};
    writePreset(presets, "classic", preset);

    main->setWidget(new SplitterWidget);
    QVERIFY(lm->switchLayout("classic"));
    QCOMPARE(lm->currentLayout(), QString("classic.modified"));
    QCOMPARE(main->getWidget()->widgetId(), QString("tabs"));
    // The copy is a user file with the preset's provenance.
    const QString fork = Settings::instance()->layoutsDir()
        + "/classic.modified.json";
    QVERIFY(QFile::exists(fork));
    QCOMPARE(lm->originOf("classic.modified"), QString("preset:classic"));
    QCOMPARE(lm->layoutDocument("classic.modified").value("container")
                 .toObject().value("type").toString(),
             QString("tabs"));
    // The preset is untouched (and not current).
    QVERIFY(lm->isPreset("classic"));

    // An unknown target is refused: nothing is saved, nothing applied.
    main->setWidget(new PluginTestWidget);
    QVERIFY(!lm->switchLayout("nosuchlayout"));
    QCOMPARE(lm->currentLayout(), QString("classic.modified"));
    QCOMPARE(main->getWidget()->widgetId(), QString("plugtest"));
    // The current's file was not rewritten by the refused switch.
    QFile f(fork);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject doc = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    QCOMPARE(doc.value("container").toObject().value("type").toString(),
             QString("tabs"));
}

// A preset's copy is adopted, not duplicated: switching to a preset
// that already has a "<name>.modified" user copy switches to that copy
// (as it stands), no second copy is created.
void LayoutManagerTest::testSwitchToPresetAdoptsExistingCopy()
{
    LayoutManager* lm = LayoutManager::getInstance();
    WidgetContainer* main = lm->window()->mainContainer();

    QTemporaryDir data;
    QTemporaryDir presets;
    setDataDir(data, QString());
    setPresetsDir(presets);

    QJsonObject preset;
    preset["container"] = QJsonObject{{"type", QString("tabs")}};
    writePreset(presets, "classic", preset);

    // The user's copy of the preset, diverged from it (a splitter).
    QJsonObject fork;
    fork["container"] = QJsonObject{{"type", QString("splitter")}};
    fork["origin"] = QString("preset:classic");
    const QString forkFile = Settings::instance()->layoutsDir()
        + "/classic.modified.json";
    QFile f(forkFile);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(fork).toJson());
    f.close();

    // The current is a user layout; the switch to the preset adopts the
    // existing copy.
    QJsonObject user;
    user["container"] = QJsonObject{{"type", QString("currenttrack")}};
    QFile uf(Settings::instance()->layoutsDir() + "/mine.json");
    QVERIFY(uf.open(QIODevice::WriteOnly));
    uf.write(QJsonDocument(user).toJson());
    uf.close();
    lm->setCurrentLayout("mine");

    QVERIFY(lm->switchLayout("classic"));
    QCOMPARE(lm->currentLayout(), QString("classic.modified"));
    // The copy's own (diverged) content is loaded, the preset's is not.
    QCOMPARE(main->getWidget()->widgetId(), QString("splitter"));
    // No second copy was created.
    QVERIFY(!QFile::exists(Settings::instance()->layoutsDir()
                               + "/classic.modified-2.json"));
    QVERIFY(!QFile::exists(Settings::instance()->layoutsDir()
                               + "/classic.json"));
}

// "default" is a plain user file: switching to it loads its document
// (the reported bug: the virtual default used to clear the widgets
// instead), the previous layout is saved, and the round trip back
// keeps working.
void LayoutManagerTest::testSwitchToDefaultFileLoadsIt()
{
    LayoutManager* lm = LayoutManager::getInstance();
    WidgetContainer* main = lm->window()->mainContainer();

    QTemporaryDir data;
    setDataDir(data, "mine");

    // Two user layouts: "default" (tabs) and "mine" (splitter).
    QJsonObject defaultDoc;
    defaultDoc["container"] = QJsonObject{{"type", QString("tabs")}};
    QFile df(Settings::instance()->layoutsDir() + "/default.json");
    QVERIFY(df.open(QIODevice::WriteOnly));
    df.write(QJsonDocument(defaultDoc).toJson());
    df.close();
    QJsonObject mineDoc;
    mineDoc["container"] = QJsonObject{{"type", QString("splitter")}};
    const QString file = Settings::instance()->layoutsDir() + "/mine.json";
    QFile mf(file);
    QVERIFY(mf.open(QIODevice::WriteOnly));
    mf.write(QJsonDocument(mineDoc).toJson());
    mf.close();

    // The current is "mine"; the live state matches its file.
    main->setWidget(new SplitterWidget);
    // Switching to "default" loads its document (the bug: the virtual
    // default cleared the widgets instead).
    QVERIFY(lm->switchLayout("default"));
    QCOMPARE(lm->currentLayout(), QString("default"));
    QCOMPARE(main->getWidget()->widgetId(), QString("tabs"));

    // The round trip back: each file reloads its own content.
    QVERIFY(lm->switchLayout("mine"));
    QCOMPARE(main->getWidget()->widgetId(), QString("splitter"));
    QVERIFY(lm->switchLayout("default"));
    QCOMPARE(lm->currentLayout(), QString("default"));
    QCOMPARE(main->getWidget()->widgetId(), QString("tabs"));
}

// New layout: the current layout is saved as-is first (a no-op for an
// unset current — there is no file to write to) — then the fresh empty
// layout is created and made current.
void LayoutManagerTest::testNewLayoutSavesCurrentFirst()
{
    LayoutManager* lm = LayoutManager::getInstance();
    WidgetContainer* main = lm->window()->mainContainer();

    QTemporaryDir data;
    setDataDir(data, QString());

    // The current is unset; the live state has no file to save to.
    main->setWidget(new TabsWidget);
    QVERIFY(lm->newLayout("fresh"));
    QCOMPARE(lm->currentLayout(), QString("fresh"));
    // Nothing was materialized for the unset current.
    QVERIFY(!QFile::exists(Settings::instance()->layoutsDir()
                               + "/default.json"));
    // The new layout is empty and current.
    const QString freshFile = Settings::instance()->layoutsDir()
        + "/fresh.json";
    QVERIFY(QFile::exists(freshFile));
    QFile g(freshFile);
    QVERIFY(g.open(QIODevice::ReadOnly));
    const QJsonObject fresh = QJsonDocument::fromJson(g.readAll()).object();
    g.close();
    QVERIFY(fresh.value("container").toObject().isEmpty());
    QCOMPARE(main->getWidget(), nullptr);
}

// A stale preset current (a reference left by an older build — presets
// are taken as copies now) whose live state was changed: the save on
// the switch forks it to "<name>.modified.json" (with the origin); the
// read-only preset is untouched. The target — a preset — is taken as a
// copy too.
void LayoutManagerTest::testCowForksEditedPreset()
{
    LayoutManager* lm = LayoutManager::getInstance();
    WidgetContainer* main = lm->window()->mainContainer();

    QTemporaryDir data;
    QTemporaryDir presets;
    setDataDir(data, QString());
    setPresetsDir(presets);

    // The presets, the way the app saves them (the window state and the
    // version key ride in the document too — the unmodified check
    // compares the whole document).
    main->setWidget(new TabsWidget);
    writePreset(presets, "classic", lm->buildLayoutDocument());
    main->setWidget(new SplitterWidget);
    writePreset(presets, "minimal", lm->buildLayoutDocument());
    main->setWidget(new TabsWidget);

    // The current is the (stale) preset; the live state diverges from
    // it.
    lm->setCurrentLayout("classic");
    main->setWidget(new TabsWidget);
    main->setWidget(new SplitterWidget); // the edit
    QVERIFY(lm->switchLayout("minimal"));
    QCOMPARE(lm->currentLayout(), QString("minimal.modified"));

    const QString fork = Settings::instance()->layoutsDir() + "/classic.modified.json";
    QVERIFY(QFile::exists(fork));
    QFile f(fork);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject doc = QJsonDocument::fromJson(f.readAll()).object();
    QCOMPARE(doc.value("origin").toString(), QString("preset:classic"));
    QCOMPARE(doc.value("container").toObject().value("type").toString(),
             QString("splitter"));

    // The target preset was taken as a copy too.
    QVERIFY(QFile::exists(Settings::instance()->layoutsDir()
                              + "/minimal.modified.json"));

    // The presets themselves were not rewritten.
    QCOMPARE(lm->isPreset("classic"), true);
    QCOMPARE(lm->layoutDocument("classic").value("container").toObject()
                 .value("type").toString(), QString("tabs"));
}

// An unedited (stale) preset current is never written: the save on the
// switch away is a no-op for it; the target — a preset — is taken as a
// copy.
void LayoutManagerTest::testCowLeavesUneditedPresetAlone()
{
    LayoutManager* lm = LayoutManager::getInstance();
    WidgetContainer* main = lm->window()->mainContainer();

    QTemporaryDir data;
    QTemporaryDir presets;
    setDataDir(data, QString());
    setPresetsDir(presets);

    // The presets, the way the app saves them (the unmodified check
    // compares the whole document).
    main->setWidget(new TabsWidget);
    writePreset(presets, "classic", lm->buildLayoutDocument());
    main->setWidget(new SplitterWidget);
    writePreset(presets, "minimal", lm->buildLayoutDocument());
    main->setWidget(new TabsWidget);

    // The current is the (stale) preset, applied as-is (no edit).
    lm->setCurrentLayout("classic");
    main->setWidget(new TabsWidget);

    QVERIFY(lm->switchLayout("minimal"));
    QCOMPARE(lm->currentLayout(), QString("minimal.modified"));
    // No fork for the unedited preset; the target was taken as a copy.
    QVERIFY(!QFile::exists(Settings::instance()->layoutsDir() + "/classic.modified.json"));
    QVERIFY(!QFile::exists(Settings::instance()->layoutsDir() + "/classic.json"));
    QVERIFY(QFile::exists(Settings::instance()->layoutsDir() + "/minimal.modified.json"));
}

// Save-as: the live layout under a new name (an existing file is
// overwritten), the provenance carries over, the copy becomes current.
void LayoutManagerTest::testSaveLayoutAs()
{
    LayoutManager* lm = LayoutManager::getInstance();
    WidgetContainer* main = lm->window()->mainContainer();

    QTemporaryDir data;
    QTemporaryDir presets;
    setDataDir(data, QString());
    setPresetsDir(presets);

    QJsonObject preset;
    preset["container"] = QJsonObject{{"type", QString("tabs")}};
    writePreset(presets, "classic", preset);
    lm->setCurrentLayout("classic");
    main->setWidget(new TabsWidget);
    main->setWidget(new SplitterWidget); // the edit

    QVERIFY(lm->saveLayoutAs("myfork"));
    QCOMPARE(lm->currentLayout(), QString("myfork"));
    const QString fork = Settings::instance()->layoutsDir() + "/myfork.json";
    QVERIFY(QFile::exists(fork));
    // The provenance names the preset it was forked from.
    QCOMPARE(lm->originOf("myfork"), QString("preset:classic"));

    // A bad name (a path separator) is refused.
    QVERIFY(!lm->saveLayoutAs("../escape"));
}

// Delete: user files only; deleting the current falls back to the
// first remaining user file, or to no current (the empty window) when
// there are none.
void LayoutManagerTest::testDeleteLayout()
{
    LayoutManager* lm = LayoutManager::getInstance();
    WidgetContainer* main = lm->window()->mainContainer();

    QTemporaryDir data;
    QTemporaryDir presets;
    setDataDir(data, "mine");
    setPresetsDir(presets);

    QJsonObject preset;
    preset["container"] = QJsonObject{{"type", QString("tabs")}};
    writePreset(presets, "classic", preset);

    // Two user layouts: "mine" (the current, tabs) and "other"
    // (splitter).
    QJsonObject mine;
    mine["container"] = QJsonObject{{"type", QString("tabs")}};
    const QString userFile = Settings::instance()->layoutsDir() + "/mine.json";
    QFile f(userFile);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(mine).toJson());
    f.close();
    QJsonObject other;
    other["container"] = QJsonObject{{"type", QString("splitter")}};
    QFile fo(Settings::instance()->layoutsDir() + "/other.json");
    QVERIFY(fo.open(QIODevice::WriteOnly));
    fo.write(QJsonDocument(other).toJson());
    fo.close();
    main->setWidget(new TabsWidget);

    // Presets and names without a file are refused.
    QVERIFY(!lm->deleteLayout("classic"));
    QVERIFY(!lm->deleteLayout("default"));

    QVERIFY(lm->deleteLayout("mine"));
    QVERIFY(!QFile::exists(userFile));
    // The current fell back to the first remaining user file.
    QCOMPARE(lm->currentLayout(), QString("other"));
    QCOMPARE(main->getWidget()->widgetId(), QString("splitter"));

    // The last one: the current is unset (the empty window).
    QVERIFY(lm->deleteLayout("other"));
    QCOMPARE(lm->currentLayout(), QString());
    QCOMPARE(main->getWidget(), nullptr);
}

// A current that names no file anywhere (deleted behind the app's back)
// falls back to the first user file on the automatic load, or to no
// current (the empty window) when there are none.
void LayoutManagerTest::testStaleCurrentFallsBackToFirstUserFile()
{
    LayoutManager* lm = LayoutManager::getInstance();
    WidgetContainer* main = lm->window()->mainContainer();

    QTemporaryDir data;
    setDataDir(data, "vanished");

    main->setWidget(new TabsWidget);
    QMetaObject::invokeMethod(lm, "loadLayout");
    qApp->processEvents();

    QCOMPARE(lm->currentLayout(), QString());
    // The stale state was not applied (there was none to apply): the
    // live layout is kept.
    QCOMPARE(main->getWidget()->widgetId(), QString("tabs"));

    // With a user file around, the fallback is that file.
    QJsonObject doc;
    doc["container"] = QJsonObject{{"type", QString("splitter")}};
    QFile f(Settings::instance()->layoutsDir() + "/only.json");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(doc).toJson());
    f.close();
    lm->setCurrentLayout("vanished");
    QMetaObject::invokeMethod(lm, "loadLayout");
    qApp->processEvents();
    QCOMPARE(lm->currentLayout(), QString("only"));
    QCOMPARE(main->getWidget()->widgetId(), QString("splitter"));
}

// The import into the user's layouts directory (the menu slot's work,
// minus the dialog): the document is validated strictly, written under
// the picked file's name into the user's layouts directory (it becomes
// the current layout), and the document's provenance is kept.
void LayoutManagerTest::testImportLayoutIntoUserDir()
{
    LayoutManager* lm = LayoutManager::getInstance();
    WidgetContainer* main = lm->window()->mainContainer();

    QTemporaryDir data;
    QTemporaryDir importDir;
    setDataDir(data, QString());

    main->setWidget(new SplitterWidget);

    QJsonObject doc;
    doc["container"] = QJsonObject{{"type", QString("tabs")}};
    doc["origin"] = QString("preset:classic");
    const QString path = importDir.filePath("imported.json");
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(doc).toJson());
        f.close();
    }

    // The slot: strict validate (the document is applied live), then the
    // file is written into the user's layouts directory under the picked
    // name and made current.
    QVERIFY(lm->loadLayoutFrom(path, true));
    const QString name = QFileInfo(path).completeBaseName();
    QCOMPARE(name, QString("imported"));
    const QString userFile = Settings::instance()->layoutsDir() + "/" + name + ".json";
    QFile::copy(path, userFile);
    lm->setCurrentLayout(name);

    QCOMPARE(lm->currentLayout(), QString("imported"));
    QCOMPARE(main->getWidget()->widgetId(), QString("tabs"));
    // The user's file carries the document as imported (provenance kept).
    QCOMPARE(lm->originOf(name), QString("preset:classic"));
}

}


QTEST_MAIN(LayoutManagerTest)
#include "layoutmanager_test.moc"
