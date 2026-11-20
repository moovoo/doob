#include <QtTest/QtTest>
#include <QApplication>
#include <QContextMenuEvent>
#include <QFile>
#include <QMenu>
#include <QListWidget>
#include <QDialog>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <memory>

#include "layoutmanager.h"
#include "widgetcontainer.h"
#include "basewidget.h"
#include "currentplaylistwidget.h"
#include "mediaitemstore.h"
#include "medialistview.h"
#include "playlistmanager.h"
#include "splitterwidget.h"
#include "tabswidget.h"
#include "testsupport.h"
#include "tracklabel.h"

namespace {

class WidgetContainerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testSetAndGetWidget();
    void testStoreRestoreEmpty();
    void testStoreRestoreWithChild();
    void testDblClickInsertsWidget();
    void testRightClickShowsWidgetContextMenu();
    void testDeleteWidgetFromMenu();
    void testDeleteWidgetDisabledWhenEmpty();
    void testWidgetSubmenuIsTitledEdit();
    void testEditRightClickTogglesBetweenEditAndNativeMenu();
    void testPassThroughIsNoOpWithoutNativeMenu();
    void testFlipFlopResetsWhenEditModeToggles();
    void testFlipFlopTogglesEvenWithoutContextMenuEvents();
    void testDismissClickDoesNotPopNativeMenu();
    void testEnteringNewWidgetResetsFlipFlop();
    void testMenuCloseEnterCascadeDoesNotResetFlipFlop();

private:
    static QMenu* findOpenMenu();
    static QStringList menuTexts(const QMenu* menu);
    static QString submenuTitle(const QMenu* menu);
    // A right-click on the container, plus the synthesized context-menu
    // event the platform delivers after the release (QTest never
    // synthesizes it — the gotcha): `target` is the widget under the
    // cursor (the event's receiver in a real app).
    static void rightClick(WidgetContainer* c, QWidget* target);
};

void WidgetContainerTest::initTestCase()
{
    LayoutManager::getInstance();
}

QMenu* WidgetContainerTest::findOpenMenu()
{
    for (QWidget* w : QApplication::topLevelWidgets()) {
        QMenu* menu = qobject_cast<QMenu*>(w);
        if (menu != nullptr && menu->isVisible())
            return menu;
    }
    return nullptr;
}

QStringList WidgetContainerTest::menuTexts(const QMenu* menu)
{
    QStringList texts;
    for (QAction* a : menu->actions())
        if (!a->isSeparator())
            texts << a->text();
    return texts;
}

QString WidgetContainerTest::submenuTitle(const QMenu* menu)
{
    for (QAction* a : menu->actions())
        if (a->menu() != nullptr)
            return a->menu()->title();
    return {};
}

void WidgetContainerTest::rightClick(WidgetContainer* c, QWidget* target)
{
    const QPoint pos = c->rect().center();
    QTest::mouseClick(c, Qt::RightButton, Qt::NoModifier, pos);
    QContextMenuEvent cme(QContextMenuEvent::Mouse,
                          target->mapFrom(c, pos),
                          c->mapToGlobal(pos),
                          Qt::NoModifier);
    qApp->sendEvent(target, &cme);
}

void WidgetContainerTest::testSetAndGetWidget()
{
    WidgetContainer c;
    QVERIFY(!c.getWidget());

    TabsWidget* t = new TabsWidget;
    c.setWidget(t);
    QCOMPARE(c.getWidget(), static_cast<BaseWidget*>(t));
}

void WidgetContainerTest::testStoreRestoreEmpty()
{
    WidgetContainer c;
    WidgetConfiguration cfg = c.storeContainer();
    QVERIFY(cfg.isEmpty());

    WidgetContainer c2;
    c2.restoreContainer(cfg);
    QVERIFY(!c2.getWidget());
}

void WidgetContainerTest::testStoreRestoreWithChild()
{
    WidgetContainer c;
    c.setWidget(new TabsWidget);
    WidgetConfiguration cfg = c.storeContainer();
    QCOMPARE(cfg.value("type").toString(), QString("tabs"));

    WidgetContainer c2;
    c2.restoreContainer(cfg);
    QVERIFY(c2.getWidget());
    QCOMPARE(c2.getWidget()->widgetId(), QString("tabs"));
}

void WidgetContainerTest::testDblClickInsertsWidget()
{
    WidgetContainer c;
    c.resize(400, 300);
    c.show();
    qApp->processEvents();
    LayoutManager::getInstance()->setEditMode(true);

    QTimer::singleShot(100, &c, [] {
        QMenu* menu = findOpenMenu();
        if (!menu)
            return;
        for (QAction* a : menu->actions()) {
            if (a->text().startsWith("Set widget")) {
                a->trigger();
                return;
            }
        }
    });

    bool dialogAccepted = false;
    QTimer::singleShot(300, &c, [&dialogAccepted] {
        QWidget* dlg = QApplication::activeModalWidget();
        if (!dlg)
            return;
        QListWidget* list = dlg->findChild<QListWidget*>();
        // The first rows are group headings (not selectable): pick the
        // first widget row.
        if (list && list->count() > 0) {
            for (int i = 0; i < list->count(); ++i) {
                if (list->item(i)->flags() & Qt::ItemIsSelectable) {
                    list->setCurrentRow(i);
                    break;
                }
            }
        }
        dialogAccepted = true;
        qobject_cast<QDialog*>(dlg)->accept();
    });

    QTest::mouseDClick(&c, Qt::LeftButton, Qt::NoModifier, c.rect().center());

    QTRY_VERIFY_WITH_TIMEOUT(dialogAccepted, 2000);
    QVERIFY(c.getWidget());
    QCOMPARE(c.getWidget()->widgetId(), QString("splitter"));

    if (QMenu* m = findOpenMenu())
        m->hide();
}

void WidgetContainerTest::testRightClickShowsWidgetContextMenu()
{
    WidgetContainer c;
    c.setWidget(new SplitterWidget);
    c.resize(400, 300);
    c.show();
    qApp->processEvents();
    LayoutManager::getInstance()->setEditMode(true);

    QTest::mouseClick(&c, Qt::RightButton, Qt::NoModifier, c.rect().center());

    QMenu* menu = findOpenMenu();
    QVERIFY(menu);

    QStringList topTexts;
    QMenu* submenu = nullptr;
    for (QAction* a : menu->actions()) {
        if (a->isSeparator())
            continue;
        if (a->menu() != nullptr)
            submenu = a->menu();
        topTexts << a->text();
    }
    // QVERIFY's early return goes through the opaque QTest::qVerify,
    // so the analyzer keeps the "submenu is null" path alive — the
    // NOLINTs below silence that false positive.
    QVERIFY(topTexts.contains("Set widget..."));
    QVERIFY(submenu);

    QStringList subTexts;
    for (QAction* a : submenu->actions()) // NOLINT(clang-analyzer-core.CallAndMessage)
        subTexts << a->text();
    QVERIFY(subTexts.contains("Toggle mode"));
    QVERIFY(subTexts.contains("Sections..."));

    menu->hide();
}

void WidgetContainerTest::testDeleteWidgetFromMenu()
{
    WidgetContainer c;
    c.setWidget(new SplitterWidget);
    QVERIFY(c.getWidget());
    c.resize(400, 300);
    c.show();
    qApp->processEvents();
    LayoutManager::getInstance()->setEditMode(true);

    QTest::mouseClick(&c, Qt::RightButton, Qt::NoModifier, c.rect().center());

    QMenu* menu = findOpenMenu();
    QVERIFY(menu);

    QAction* deleteAction = nullptr;
    for (QAction* a : menu->actions()) {
        if (a->text().startsWith("Delete widget"))
            deleteAction = a;
    }
    QVERIFY(deleteAction);
    QVERIFY(deleteAction->isEnabled()); // NOLINT(clang-analyzer-core.CallAndMessage)

    deleteAction->trigger();
    menu->hide();
    qApp->processEvents();

    QVERIFY(!c.getWidget());
}

void WidgetContainerTest::testDeleteWidgetDisabledWhenEmpty()
{
    WidgetContainer c;
    QVERIFY(!c.getWidget());
    c.resize(400, 300);
    c.show();
    qApp->processEvents();
    LayoutManager::getInstance()->setEditMode(true);

    QTest::mouseClick(&c, Qt::RightButton, Qt::NoModifier, c.rect().center());

    QMenu* menu = findOpenMenu();
    QVERIFY(menu);

    bool found = false;
    for (QAction* a : menu->actions()) {
        if (a->text().startsWith("Delete widget")) {
            found = true;
            QVERIFY(!a->isEnabled());
        }
    }
    QVERIFY(found);

    menu->hide();
}

// The widget's own submenu of the edit menu (7d): it is titled
// "Edit {widget type name}" at its insertion point.
void WidgetContainerTest::testWidgetSubmenuIsTitledEdit()
{
    WidgetContainer c;
    c.setWidget(new SplitterWidget);
    c.resize(400, 300);
    c.show();
    qApp->processEvents();
    LayoutManager::getInstance()->setEditMode(true);

    QTest::mouseClick(&c, Qt::RightButton, Qt::NoModifier, c.rect().center());

    QMenu* menu = findOpenMenu();
    QVERIFY(menu);

    QMenu* submenu = nullptr;
    for (QAction* a : menu->actions())
        if (a->menu() != nullptr)
            submenu = a->menu();
    QVERIFY(submenu);
    // The analyzer keeps the "submenu is null" path alive (QVERIFY's
    // early return goes through the opaque QTest::qVerify).
    QCOMPARE(submenu->title(), QString("Edit Splitter")); // NOLINT(clang-analyzer-core.CallAndMessage)

    menu->hide();
    qApp->processEvents();
}

// The edit-mode right-click flip-flop (7d): the first right-click
// shows the edit menu, the second passes through to the widget's
// native context menu (the synthesized context-menu event reaching the
// widget's Qt::CustomContextMenu slot, as in normal mode), the third
// goes back to the edit menu.
void WidgetContainerTest::testEditRightClickTogglesBetweenEditAndNativeMenu()
{
    // A row in the current playlist (the row menu's entries read the
    // selection): the write-through managers stay on a private database.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    test::usePrivateDatabase(dir.path());
    PlaylistManager* pl = PlaylistManager::getInstance();
    MediaItemStore* store = MediaItemStore::getInstance();
    pl->reset();
    store->clear();

    const QString path = dir.path() + "/row.mp3";
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("\x00\x01\x02");
    }
    MediaItem item;
    item.title = "Song";
    item.source = QUrl::fromLocalFile(path);
    QVERIFY(store->insert(item));
    pl->addItems({ item.id });

    WidgetContainer c;
    CurrentPlaylistWidget* view = new CurrentPlaylistWidget;
    c.setWidget(view);
    c.resize(400, 300);
    c.show();
    qApp->processEvents();
    view->listView()->selectRow(0);
    LayoutManager::getInstance()->setEditMode(true);

    // The first right-click: the edit menu (the "Edit {type}" submenu).
    rightClick(&c, view->listView()->viewport());
    QMenu* menu = findOpenMenu();
    QVERIFY(menu);
    QCOMPARE(submenuTitle(menu), QString("Edit Current playlist"));
    menu->hide();
    qApp->processEvents();

    // The second passes through: the release goes to the widget, the
    // context-menu event reaches its native slot — the row menu.
    rightClick(&c, view->listView()->viewport());
    QMenu* native = findOpenMenu();
    QVERIFY(native);
    const QStringList texts = menuTexts(native);
    QVERIFY(texts.contains("Remove selection"));
    QVERIFY(texts.contains("Edit metadata..."));
    QVERIFY(texts.contains("Play now"));
    native->hide();
    qApp->processEvents();

    // The third: the edit menu again.
    rightClick(&c, view->listView()->viewport());
    QMenu* menu2 = findOpenMenu();
    QVERIFY(menu2);
    QCOMPARE(submenuTitle(menu2), QString("Edit Current playlist"));
    menu2->hide();
    qApp->processEvents();

    pl->reset();
    store->clear();
}

// The flip-flop's pass-through on a widget without a native menu (a
// tracklabel): a harmless no-op — nothing pops up, and the next
// right-click goes back to the edit menu.
void WidgetContainerTest::testPassThroughIsNoOpWithoutNativeMenu()
{
    WidgetContainer c;
    c.setWidget(new TrackLabelWidget);
    c.resize(400, 300);
    c.show();
    qApp->processEvents();
    LayoutManager::getInstance()->setEditMode(true);

    rightClick(&c, &c);
    QMenu* menu = findOpenMenu();
    QVERIFY(menu);
    menu->hide();
    qApp->processEvents();

    // The second passes through: no native menu to show, nothing pops
    // up.
    rightClick(&c, &c);
    QVERIFY(findOpenMenu() == nullptr);

    // The third: the edit menu again.
    rightClick(&c, &c);
    QMenu* menu2 = findOpenMenu();
    QVERIFY(menu2);
    menu2->hide();
    qApp->processEvents();
}

// The flip-flop resets when edit mode is toggled: a stale "on" must
// not eat the first right-click after re-entering edit mode. (A
// widget without nested containers — a splitter's inner container is
// a different widget for the enter-reset, so the post-menu-close Enter
// would land in it and reset the just-set "native next".)
void WidgetContainerTest::testFlipFlopResetsWhenEditModeToggles()
{
    WidgetContainer c;
    c.setWidget(new TrackLabelWidget);
    c.resize(400, 300);
    c.show();
    qApp->processEvents();
    LayoutManager* lm = LayoutManager::getInstance();
    lm->setEditMode(true);

    // One right-click: the edit menu (the flip-flop goes on).
    rightClick(&c, &c);
    QMenu* menu = findOpenMenu();
    QVERIFY(menu);
    menu->hide();
    qApp->processEvents();
    QVERIFY(lm->editRightClickPassThrough());

    // Toggle edit mode off and on: the flip-flop resets.
    lm->setEditMode(false);
    lm->setEditMode(true);
    QVERIFY(!lm->editRightClickPassThrough());

    // The first right-click after re-entering: the edit menu again (a
    // stale "on" would have passed it through and shown nothing).
    rightClick(&c, &c);
    QMenu* menu2 = findOpenMenu();
    QVERIFY(menu2);
    menu2->hide();
    qApp->processEvents();
}

// The flip-flop toggles on the release (every edit-mode right-click),
// not on the synthesized context-menu event: clicks that never produce
// one (a right-drag, a click that dismisses the menu's popup) must not
// leave it stuck — the alternation keeps going. (A widget without
// nested containers — see testFlipFlopResetsWhenEditModeToggles.)
void WidgetContainerTest::testFlipFlopTogglesEvenWithoutContextMenuEvents()
{
    WidgetContainer c;
    c.setWidget(new TrackLabelWidget);
    c.resize(400, 300);
    c.show();
    qApp->processEvents();
    LayoutManager* lm = LayoutManager::getInstance();
    lm->setEditMode(true);

    // Click 1: the edit menu (the flip-flop goes on). No context-menu
    // event follows (the QTest click sends press + release only).
    QTest::mouseClick(&c, Qt::RightButton, Qt::NoModifier, c.rect().center());
    QMenu* menu = findOpenMenu();
    QVERIFY(menu);
    menu->hide();
    qApp->processEvents();
    QVERIFY(lm->editRightClickPassThrough());

    // Click 2 (again no context-menu event): the pass-through — nothing
    // pops up, the flip-flop goes back off.
    QTest::mouseClick(&c, Qt::RightButton, Qt::NoModifier, c.rect().center());
    QVERIFY(findOpenMenu() == nullptr);
    QVERIFY(!lm->editRightClickPassThrough());

    // Click 3: the edit menu again (the flip-flop is not stuck).
    QTest::mouseClick(&c, Qt::RightButton, Qt::NoModifier, c.rect().center());
    QMenu* menu2 = findOpenMenu();
    QVERIFY(menu2);
    menu2->hide();
    qApp->processEvents();
}

// A right-click whose press landed on an open popup (the edit menu, a
// native menu a previous pass-through showed) is a dismissal, not a new
// command (7d): the release is swallowed whole — no flip-flop toggle,
// no pass-through. Without this, dismissing over a media view pops the
// widget's native menu on the very click that closed the other one:
// the release passes through, QWindow synthesizes the context-menu
// event after the unaccepted release, and it passes through too.
void WidgetContainerTest::testDismissClickDoesNotPopNativeMenu()
{
    // A row in the current playlist (the row menu's entries read the
    // selection): the write-through managers stay on a private database.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    test::usePrivateDatabase(dir.path());
    PlaylistManager* pl = PlaylistManager::getInstance();
    MediaItemStore* store = MediaItemStore::getInstance();
    pl->reset();
    store->clear();

    const QString path = dir.path() + "/row.mp3";
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("\x00\x01\x02");
    }
    MediaItem item;
    item.title = "Song";
    item.source = QUrl::fromLocalFile(path);
    QVERIFY(store->insert(item));
    pl->addItems({ item.id });

    WidgetContainer c;
    CurrentPlaylistWidget* view = new CurrentPlaylistWidget;
    c.setWidget(view);
    c.resize(400, 300);
    c.show();
    qApp->processEvents();
    view->listView()->selectRow(0);
    LayoutManager* lm = LayoutManager::getInstance();
    lm->setEditMode(true);

    QWidget* vp = view->listView()->viewport();
    const QPoint pos = vp->rect().center();

    // A real-app right-click: press + release on the viewport under the
    // cursor (QTest delivers directly to the named widget — no childAt
    // routing). QWindow then synthesizes the context-menu event — but
    // only after an UNaccepted release (the edit menu's is swallowed,
    // so no synthesis; the pass-through's is not).
    auto realClick = [vp, pos, lm] {
        QTest::mousePress(vp, Qt::RightButton, Qt::NoModifier, pos);
        QTest::mouseRelease(vp, Qt::RightButton, Qt::NoModifier, pos);
        if (lm->editRightClickPassThrough())
            return; // the release was swallowed (the edit menu)
        QContextMenuEvent cme(QContextMenuEvent::Mouse, pos,
                              vp->mapToGlobal(pos), Qt::NoModifier);
        qApp->sendEvent(vp, &cme);
    };
    // A dismissal: the press goes to the open popup (its mouse grab —
    // outside its rect, so it closes, as in the real app), the release
    // lands on the widget under the cursor.
    auto dismiss = [vp, pos] {
        QMenu* menu = findOpenMenu();
        QVERIFY(menu != nullptr);
        QTest::mousePress(menu, Qt::RightButton, Qt::NoModifier,
                          QPoint(10000, 10000));
        menu->hide();
        qApp->processEvents();
        QTest::mouseRelease(vp, Qt::RightButton, Qt::NoModifier, pos);
        qApp->processEvents();
    };

    // Click 1: the edit menu. The flip-flop is "edit" while the menu is
    // open; it turns to "native next" only when the menu closes.
    realClick();
    QMenu* menu = findOpenMenu();
    QVERIFY(menu);
    QCOMPARE(submenuTitle(menu), QString("Edit Current playlist"));
    QVERIFY(!lm->editRightClickPassThrough());

    // The dismissal: the press on the popup closes the edit menu (the
    // flip-flop goes to "native next"), the release is swallowed whole —
    // no native menu pops on the very click that closed the other one.
    dismiss();
    QVERIFY(findOpenMenu() == nullptr);
    QVERIFY(lm->editRightClickPassThrough());

    // Click 2: the deliberate second right-click — the pass-through,
    // the widget's native row menu (the flip goes off).
    realClick();
    QMenu* native = findOpenMenu();
    QVERIFY(native);
    const QStringList texts = menuTexts(native);
    QVERIFY(texts.contains("Remove selection"));
    QVERIFY(!lm->editRightClickPassThrough());

    // Its dismissal: swallowed too (the flip stays off).
    dismiss();
    QVERIFY(findOpenMenu() == nullptr);
    QVERIFY(!lm->editRightClickPassThrough());

    // Click 3: the edit menu again.
    realClick();
    QMenu* menu2 = findOpenMenu();
    QVERIFY(menu2);
    QCOMPARE(submenuTitle(menu2), QString("Edit Current playlist"));
    menu2->hide();
    qApp->processEvents();

    pl->reset();
    store->clear();
}

// The flip-flop is global (a LayoutManager member), so without a reset it
// would carry a "native next" left by a right-click on one widget into the
// first click on another. Entering a DIFFERENT container resets it, so the
// first right-click on a widget is always the edit menu.
void WidgetContainerTest::testEnteringNewWidgetResetsFlipFlop()
{
    // Two containers, each with a CurrentPlaylistWidget (a native row
    // menu exists, so the edit menu and the native menu are
    // distinguishable): the write-through managers stay private.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    test::usePrivateDatabase(dir.path());
    PlaylistManager* pl = PlaylistManager::getInstance();
    MediaItemStore* store = MediaItemStore::getInstance();
    pl->reset();
    store->clear();

    const QString path = dir.path() + "/row.mp3";
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("\x00\x01\x02");
    }
    MediaItem item;
    item.title = "Song";
    item.source = QUrl::fromLocalFile(path);
    QVERIFY(store->insert(item));
    pl->addItems({ item.id });

    LayoutManager* lm = LayoutManager::getInstance();
    lm->setEditMode(true);

    WidgetContainer a;
    CurrentPlaylistWidget* viewA = new CurrentPlaylistWidget;
    a.setWidget(viewA);
    a.resize(200, 200);
    a.move(0, 0);
    a.show();

    WidgetContainer b;
    CurrentPlaylistWidget* viewB = new CurrentPlaylistWidget;
    b.setWidget(viewB);
    b.resize(200, 200);
    b.move(300, 0);
    b.show();
    qApp->processEvents();
    viewA->listView()->selectRow(0);
    viewB->listView()->selectRow(0);

    QWidget* vpA = viewA->listView()->viewport();
    QWidget* vpB = viewB->listView()->viewport();

    // Container A: right-click → the edit menu; close it → "native next".
    rightClick(&a, vpA);
    QMenu* menuA = findOpenMenu();
    QVERIFY(menuA);
    QCOMPARE(submenuTitle(menuA), QString("Edit Current playlist"));
    menuA->hide();
    qApp->processEvents();
    QVERIFY(lm->editRightClickPassThrough());

    // The mouse enters container B (the Enter event on its viewport —
    // QTest does not synthesize one, deliver it like the platform would).
    // The flip-flop resets to "edit next".
    QEvent enterEvent(QEvent::Enter);
    qApp->sendEvent(vpB, &enterEvent);
    qApp->processEvents();
    QVERIFY(!lm->editRightClickPassThrough());

    // Container B: the first right-click is the edit menu — not the
    // native menu container A left behind.
    rightClick(&b, vpB);
    QMenu* menuB = findOpenMenu();
    QVERIFY(menuB);
    QCOMPARE(submenuTitle(menuB), QString("Edit Current playlist"));
    menuB->hide();
    qApp->processEvents();

    // Moving back to A resets it again (A had left "native next").
    QEvent enterEventA(QEvent::Enter);
    qApp->sendEvent(vpA, &enterEventA);
    qApp->processEvents();
    QVERIFY(!lm->editRightClickPassThrough());

    pl->reset();
    store->clear();
}

// When a popup (the edit menu) closes, Qt re-enters the whole chain from
// the outermost container down to the widget under the cursor — every
// container in the chain gets its QEvent::Enter. The mouse is in the
// LEAF (the last Enter), so the ancestor Enters must not reset the
// flip-flop: without that, the "native next" the just-closed menu set is
// undone by the cascade and the native menu is unreachable (every
// right-click shows the edit menu). (A raw container-in-container — the
// shape the splitter/tab composites produce, without their sibling panes,
// which the offscreen QPA's cascade would Enter too.)
void WidgetContainerTest::testMenuCloseEnterCascadeDoesNotResetFlipFlop()
{
    WidgetContainer outer;
    WidgetContainer* inner = new WidgetContainer(&outer);
    outer.layout()->addWidget(inner);
    outer.resize(400, 300);
    outer.show();
    qApp->processEvents();
    LayoutManager* lm = LayoutManager::getInstance();
    lm->setEditMode(true);

    // The mouse is in the inner container: right-click → the edit menu;
    // close it → "native next".
    rightClick(inner, inner);
    QMenu* menu = findOpenMenu();
    QVERIFY(menu);
    menu->hide();
    qApp->processEvents();
    QVERIFY(lm->editRightClickPassThrough());

    // The platform's re-entry cascade (outermost first, the leaf last) —
    // the ancestor Enters must not reset the "native next".
    QEvent cascade(QEvent::Enter);
    qApp->sendEvent(&outer, &cascade);
    qApp->sendEvent(inner, &cascade);
    qApp->processEvents();
    QVERIFY(lm->editRightClickPassThrough());

    // The next right-click on the inner container is the pass-through
    // (an empty container has no native menu — nothing pops up), the
    // flip-flop flips back.
    rightClick(inner, inner);
    QVERIFY(findOpenMenu() == nullptr);
    QVERIFY(!lm->editRightClickPassThrough());
}
}


QTEST_MAIN(WidgetContainerTest)
#include "widgetcontainer_test.moc"
