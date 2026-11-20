#include <QtTest/QtTest>
#include <QApplication>
#include <QDockWidget>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "layoutmanager.h"
#include "mainwindow.h"
#include "settings.h"
#include "settingsmanager.h"
#include "splitterwidget.h"
#include "tabswidget.h"
#include "windowmanager.h"
#include "widgetcontainer.h"

#include <memory>

namespace {

class WindowManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void addStandaloneWindow();
    void addDockedWindow();
    void idsAreUniqueAndReuseFreedNumbers();
    void closeHidesWindow();
    void mainWindowHideDoesNotRecordDockClosed();
    void setWindowVisible();
    void showRestoresStandaloneGeometry();
    void setWindowTitle();
    void removeWindowDestroysCarrier();
    void convertStandaloneToDockedKeepsContainer();
    void convertDockedToStandaloneKeepsContainerAndGeometry();
    void storeRestoreRoundtrip();
    void restoreOversizedGeometryFallsBackToDefault();
    void restoreWithDockState();
    void restoreSkipsEntriesWithoutId();
    void containersAreListed();

private:
    std::unique_ptr<QTemporaryDir> m_dataDir;
    WindowManager* m_wm = nullptr;
};

void WindowManagerTest::initTestCase()
{
    m_dataDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dataDir->isValid());
    qputenv("DOOB_DATA_DIR", m_dataDir->path().toUtf8());
    Settings::reset();
    LayoutManager::getInstance()->show();
}

void WindowManagerTest::init()
{
    m_wm = WindowManager::getInstance();
    m_wm->clear();
}

void WindowManagerTest::cleanup()
{
    m_wm->clear();
    qApp->processEvents();
}

void WindowManagerTest::addStandaloneWindow()
{
    QSignalSpy changed(m_wm, &WindowManager::windowsChanged);

    const QString id = m_wm->addWindow(WindowManager::Kind::Standalone, "Solo");

    QCOMPARE(id, QString("window-1"));
    QCOMPARE(changed.count(), 1);
    QVERIFY(m_wm->hasWindow(id));

    const WindowManager::Info info = m_wm->window(id);
    QCOMPARE(info.id, id);
    QCOMPARE(info.kind, WindowManager::Kind::Standalone);
    QCOMPARE(info.title, QString("Solo"));
    QCOMPARE(info.visible, true);

    // The carrier checks ride in one expression: the analyzer does not
    // trust the QVERIFY macro's failure return, so a bare dereference
    // on the following line reads a pointer it assumes is null. The
    // carrier does not count as an app-anchor window (WA_QuitOnClose):
    // closing the main window quits the app, and the layout is saved
    // with the windows in the state they were in (not "closed by the
    // user's final click on the orphaned window").
    QWidget* w = m_wm->widgetOf(id);
    QVERIFY(w != nullptr && w->parentWidget() == nullptr // top-level
            && !w->testAttribute(Qt::WA_QuitOnClose)
            && w->isVisible()
            && w->windowTitle() == QStringLiteral("Solo"));
    QVERIFY(m_wm->containerOf(id) != nullptr);
}

void WindowManagerTest::addDockedWindow()
{
    MainWindow* window = LayoutManager::getInstance()->window();
    QVERIFY(window);

    const QString id = m_wm->addWindow(WindowManager::Kind::Docked, "Dock");

    QCOMPARE(id, QString("window-1"));
    QCOMPARE(m_wm->windowKind(id), WindowManager::Kind::Docked);
    QCOMPARE(m_wm->windowVisible(id), true);

    QDockWidget* dock = qobject_cast<QDockWidget*>(m_wm->widgetOf(id));
    QVERIFY(dock != nullptr);
    QCOMPARE(dock->parentWidget(), static_cast<QWidget*>(window));
    QCOMPARE(dock->windowTitle(), QString("Dock"));
    // saveState keys the dock by its objectName (the window's id).
    QCOMPARE(dock->objectName(), id);
    QVERIFY(m_wm->containerOf(id) != nullptr);
}

void WindowManagerTest::idsAreUniqueAndReuseFreedNumbers()
{
    const QString a = m_wm->addWindow(WindowManager::Kind::Standalone, "A");
    const QString b = m_wm->addWindow(WindowManager::Kind::Standalone, "B");
    const QString c = m_wm->addWindow(WindowManager::Kind::Standalone, "C");
    QCOMPARE(a, QString("window-1"));
    QCOMPARE(b, QString("window-2"));
    QCOMPARE(c, QString("window-3"));

    m_wm->removeWindow(b);
    const QString d = m_wm->addWindow(WindowManager::Kind::Standalone, "D");
    QCOMPARE(d, QString("window-2"));
}

void WindowManagerTest::closeHidesWindow()
{
    // Standalone: the window's own close button hides it (the state
    // persists, the window is not destroyed).
    const QString id = m_wm->addWindow(WindowManager::Kind::Standalone, "Solo");
    QSignalSpy vis(m_wm, &WindowManager::windowVisibleChanged);
    m_wm->widgetOf(id)->close();
    qApp->processEvents();
    QCOMPARE(vis.count(), 1);
    QCOMPARE(vis.at(0).at(0).toString(), id);
    QCOMPARE(vis.at(0).at(1).toBool(), false);
    QCOMPARE(m_wm->windowVisible(id), false);
    QVERIFY(m_wm->hasWindow(id));
    QVERIFY(m_wm->widgetOf(id) != nullptr);

    // Docked: the dock's close button behaves the same.
    const QString dock = m_wm->addWindow(WindowManager::Kind::Docked, "Dock");
    QSignalSpy visDock(m_wm, &WindowManager::windowVisibleChanged);
    m_wm->widgetOf(dock)->close();
    qApp->processEvents();
    QCOMPARE(visDock.count(), 1);
    QCOMPARE(visDock.at(0).at(0).toString(), dock);
    QCOMPARE(m_wm->windowVisible(dock), false);
    QVERIFY(m_wm->hasWindow(dock));
}

void WindowManagerTest::mainWindowHideDoesNotRecordDockClosed()
{
    // Hiding the main window cascades a hide to its docked child
    // windows (the app's exit does exactly that, and the layout is
    // saved afterwards). That is the app going away, not the window
    // being closed: the record keeps the state the dock was in, so a
    // restart does not restore it hidden.
    MainWindow* window = LayoutManager::getInstance()->window();
    QVERIFY(window);

    const QString id = m_wm->addWindow(WindowManager::Kind::Docked, "Dock");
    QCOMPARE(m_wm->windowVisible(id), true);

    QSignalSpy vis(m_wm, &WindowManager::windowVisibleChanged);
    window->hide();
    qApp->processEvents();
    QCOMPARE(m_wm->windowVisible(id), true); // not recorded as closed
    QCOMPARE(vis.count(), 0);

    // A show (a restore) applies: the dock comes back and the record
    // follows it.
    window->show();
    qApp->processEvents();
    QVERIFY(m_wm->widgetOf(id)->isVisible());
    QCOMPARE(m_wm->windowVisible(id), true);

    // A user-initiated dock close (the main window visible) is recorded
    // as usual.
    m_wm->widgetOf(id)->close();
    qApp->processEvents();
    QCOMPARE(m_wm->windowVisible(id), false);
}

void WindowManagerTest::setWindowVisible()
{
    const QString id = m_wm->addWindow(WindowManager::Kind::Standalone, "V");

    m_wm->setWindowVisible(id, false);
    QCOMPARE(m_wm->windowVisible(id), false);
    QVERIFY(!m_wm->widgetOf(id)->isVisible());

    m_wm->setWindowVisible(id, true);
    QCOMPARE(m_wm->windowVisible(id), true);
    QVERIFY(m_wm->widgetOf(id)->isVisible());

    // A no-op set (the same state) emits nothing.
    QSignalSpy vis(m_wm, &WindowManager::windowVisibleChanged);
    m_wm->setWindowVisible(id, true);
    QCOMPARE(vis.count(), 0);
}

void WindowManagerTest::showRestoresStandaloneGeometry()
{
    // A re-shown standalone window comes back where it was (the window
    // manager would re-place it, often centered, on a re-map).
    const QString id = m_wm->addWindow(WindowManager::Kind::Standalone, "P");
    m_wm->widgetOf(id)->setGeometry(111, 222, 333, 444);
    qApp->processEvents(); // the move/resize events update the record
    m_wm->setWindowVisible(id, false);
    m_wm->setWindowVisible(id, true);
    QCOMPARE(m_wm->widgetOf(id)->geometry(), QRect(111, 222, 333, 444));
}

void WindowManagerTest::setWindowTitle()
{
    const QString id = m_wm->addWindow(WindowManager::Kind::Standalone, "Old");

    m_wm->setWindowTitle(id, "New");

    QCOMPARE(m_wm->windowTitle(id), QString("New"));
    QCOMPARE(m_wm->widgetOf(id)->windowTitle(), QString("New"));

    // A rename to the same title is a no-op (no signal).
    QSignalSpy changed(m_wm, &WindowManager::windowsChanged);
    m_wm->setWindowTitle(id, "New");
    QCOMPARE(changed.count(), 0);
}

void WindowManagerTest::removeWindowDestroysCarrier()
{
    const QString id = m_wm->addWindow(WindowManager::Kind::Standalone, "R");
    QPointer<QWidget> carrier = m_wm->widgetOf(id);
    QVERIFY(carrier != nullptr);

    QSignalSpy changed(m_wm, &WindowManager::windowsChanged);
    m_wm->removeWindow(id);

    QCOMPARE(changed.count(), 1);
    QVERIFY(!m_wm->hasWindow(id));
    qApp->processEvents();
    QVERIFY(carrier.isNull());
}

void WindowManagerTest::convertStandaloneToDockedKeepsContainer()
{
    const QString id = m_wm->addWindow(WindowManager::Kind::Standalone, "C");
    WidgetContainer* container = m_wm->containerOf(id);
    QVERIFY(container != nullptr);

    m_wm->setWindowKind(id, WindowManager::Kind::Docked);

    QCOMPARE(m_wm->windowKind(id), WindowManager::Kind::Docked);
    QVERIFY(qobject_cast<QDockWidget*>(m_wm->widgetOf(id)) != nullptr);
    QCOMPARE(m_wm->containerOf(id), container); // the same container
    QCOMPARE(m_wm->windowVisible(id), true);

    // A hidden window stays hidden across the conversion.
    const QString hidden = m_wm->addWindow(WindowManager::Kind::Standalone, "H");
    m_wm->setWindowVisible(hidden, false);
    m_wm->setWindowKind(hidden, WindowManager::Kind::Docked);
    QCOMPARE(m_wm->windowVisible(hidden), false);
    QVERIFY(!m_wm->widgetOf(hidden)->isVisible());

    // Converting to the same kind is a no-op.
    QSignalSpy changed(m_wm, &WindowManager::windowsChanged);
    m_wm->setWindowKind(id, WindowManager::Kind::Docked);
    QCOMPARE(changed.count(), 0);
}

void WindowManagerTest::convertDockedToStandaloneKeepsContainerAndGeometry()
{
    const QString id = m_wm->addWindow(WindowManager::Kind::Docked, "D");
    WidgetContainer* container = m_wm->containerOf(id);
    QVERIFY(container != nullptr);

    m_wm->setWindowKind(id, WindowManager::Kind::Standalone);

    QCOMPARE(m_wm->windowKind(id), WindowManager::Kind::Standalone);
    QWidget* top = m_wm->widgetOf(id);
    QVERIFY(qobject_cast<QDockWidget*>(top) == nullptr);
    QVERIFY(top->parentWidget() == nullptr);
    QCOMPARE(m_wm->containerOf(id), container);
    QCOMPARE(m_wm->windowVisible(id), true);

    // The standalone's geometry survives a round trip back to docked.
    m_wm->widgetOf(id)->setGeometry(123, 45, 400, 300);
    m_wm->setWindowKind(id, WindowManager::Kind::Docked);
    m_wm->setWindowKind(id, WindowManager::Kind::Standalone);
    QCOMPARE(m_wm->widgetOf(id)->geometry(), QRect(123, 45, 400, 300));
}

void WindowManagerTest::storeRestoreRoundtrip()
{
    const QString dock = m_wm->addWindow(WindowManager::Kind::Docked, "Docked");
    m_wm->containerOf(dock)->setWidget(new TabsWidget);
    const QString solo = m_wm->addWindow(WindowManager::Kind::Standalone, "Standalone");
    m_wm->containerOf(solo)->setWidget(new SplitterWidget);
    m_wm->widgetOf(solo)->setGeometry(111, 222, 431, 321);
    m_wm->setWindowVisible(solo, false);

    const QVariantList stored = m_wm->storeWindows();
    const QByteArray dockState = m_wm->dockState();
    QCOMPARE(stored.size(), 2);

    m_wm->clear();
    qApp->processEvents();
    QVERIFY(m_wm->windows().isEmpty());

    m_wm->restoreWindows(stored, dockState);

    QCOMPARE(m_wm->windows().size(), 2);

    const WindowManager::Info di = m_wm->window(dock);
    QCOMPARE(di.kind, WindowManager::Kind::Docked);
    QCOMPARE(di.title, QString("Docked"));
    QCOMPARE(di.visible, true);
    QVERIFY(qobject_cast<QDockWidget*>(m_wm->widgetOf(dock)) != nullptr);
    QVERIFY(m_wm->containerOf(dock)->getWidget() != nullptr);
    QCOMPARE(m_wm->containerOf(dock)->getWidget()->widgetId(), QString("tabs"));

    const WindowManager::Info si = m_wm->window(solo);
    QCOMPARE(si.kind, WindowManager::Kind::Standalone);
    QCOMPARE(si.title, QString("Standalone"));
    QCOMPARE(si.visible, false);
    QVERIFY(!m_wm->widgetOf(solo)->isVisible());
    QCOMPARE(m_wm->widgetOf(solo)->geometry(), QRect(111, 222, 431, 321));
    QVERIFY(m_wm->containerOf(solo)->getWidget() != nullptr);
    QCOMPARE(m_wm->containerOf(solo)->getWidget()->widgetId(), QString("splitter"));
}

void WindowManagerTest::restoreOversizedGeometryFallsBackToDefault()
{
    // A saved size no screen can hold (a corrupt document, one written
    // on a much bigger display) is invalid state: the fresh default
    // size is used, the window does not outgrow every screen.
    const QString solo = m_wm->addWindow(WindowManager::Kind::Standalone, "Solo");
    m_wm->widgetOf(solo)->setGeometry(11, 22, 431, 321);

    QVariantMap entry = m_wm->storeWindows().first().toMap();
    QVariantMap geo = entry.value("geometry").toMap();
    geo.insert("width", 100000);
    geo.insert("height", 100000);
    entry.insert("geometry", geo);

    m_wm->clear();
    qApp->processEvents();

    QVariantList stored;
    stored.append(entry);
    m_wm->restoreWindows(stored, {});

    QCOMPARE(m_wm->windows().size(), 1);
    QCOMPARE(m_wm->widgetOf(solo)->size(), QSize(480, 360));
}

void WindowManagerTest::restoreWithDockState()
{
    // A dock that was hidden stays hidden across the save/restore (the
    // main window's state carries the dock's visibility).
    const QString id = m_wm->addWindow(WindowManager::Kind::Docked, "Dock");
    m_wm->setWindowVisible(id, false);

    const QVariantList stored = m_wm->storeWindows();
    const QByteArray dockState = m_wm->dockState();
    QVERIFY(!dockState.isEmpty());

    m_wm->restoreWindows(stored, dockState);

    QCOMPARE(m_wm->windows().size(), 1);
    QVERIFY(qobject_cast<QDockWidget*>(m_wm->widgetOf(id)) != nullptr);
    QCOMPARE(m_wm->windowVisible(id), false);
    QVERIFY(!m_wm->widgetOf(id)->isVisible());

    // No docked window: the dock state is empty.
    m_wm->setWindowKind(id, WindowManager::Kind::Standalone);
    QVERIFY(m_wm->dockState().isEmpty());
}

void WindowManagerTest::restoreSkipsEntriesWithoutId()
{
    QVariantList entries;
    entries.append(QVariantMap{{"title", "NoId"}});
    entries.append(QVariantMap{{"id", "window-9"},
                               {"kind", "weird-kind"},
                               {"title", "Weird"},
                              });

    m_wm->restoreWindows(entries, {});

    QCOMPARE(m_wm->windows().size(), 1);
    const WindowManager::Info info = m_wm->window("window-9");
    QCOMPARE(info.title, QString("Weird"));
    QCOMPARE(info.kind, WindowManager::Kind::Standalone); // unknown kind
}

void WindowManagerTest::containersAreListed()
{
    const QString id = m_wm->addWindow(WindowManager::Kind::Standalone, "C");

    const QList<WidgetContainer*> containers = m_wm->containers();

    QCOMPARE(containers.size(), 1);
    QCOMPARE(containers.first(), m_wm->containerOf(id));
}
}

QTEST_MAIN(WindowManagerTest)
#include "windowmanager_test.moc"
