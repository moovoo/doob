#include <QtTest/QtTest>
#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QMenu>
#include <QAction>
#include <QTabBar>
#include <QSplitter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "layoutmanager.h"
#include "mainwindow.h"
#include "widgetcontainer.h"
#include "splitterwidget.h"
#include "tabswidget.h"

namespace {

class LayoutManagerCopyPasteTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void serializeContainerProducesValidText();
    void serializeEmptyContainerProducesInvalidText();
    void isSerializedWidgetRejectsInvalidText();
    void isSerializedWidgetAcceptsKnownTypes();
    void copyActionEnabledOnlyWhenContainerHasWidget();
    void cutActionEnabledOnlyWhenContainerHasWidget();
    void pasteActionEnabledOnlyWhenClipboardHoldsValidWidget();
    void copySlotWritesSerializedWidgetToClipboard();
    void cutSlotSerializesAndRemovesWidget();
    void cutThenPasteRestoresWidget();
    void pasteSlotRestoresWidgetWithState();
    void pasteSlotReplacesExistingWidget();
    void pasteSlotIgnoresInvalidClipboard();

private:
    QMenu* popupMenuFor(WidgetContainer* container);
    void closeMenu(QMenu* menu);
    QAction* menuAction(QMenu* menu, const QString& text) const;

    LayoutManager* m_lm = nullptr;
    WidgetContainer* m_main = nullptr;
    QClipboard* m_clipboard = nullptr;
};

void LayoutManagerCopyPasteTest::initTestCase()
{
    m_lm = LayoutManager::getInstance();
    m_lm->show();

    m_main = m_lm->window()->mainContainer();
    QVERIFY(m_main);

    m_clipboard = QGuiApplication::clipboard();
    QVERIFY(m_clipboard);
}

void LayoutManagerCopyPasteTest::init()
{
    m_main->setWidget(nullptr);
    m_clipboard->clear();
}

void LayoutManagerCopyPasteTest::cleanup()
{
    m_main->setWidget(nullptr);
    m_clipboard->clear();
    qApp->processEvents();
}

QMenu* LayoutManagerCopyPasteTest::popupMenuFor(WidgetContainer* container)
{
    m_lm->popupContainerMenu(container);

    return qobject_cast<QMenu*>(qApp->activePopupWidget());
}

void LayoutManagerCopyPasteTest::closeMenu(QMenu* menu)
{
    if (menu != nullptr)
        menu->hide();
    qApp->processEvents();
}

QAction* LayoutManagerCopyPasteTest::menuAction(QMenu* menu, const QString& text) const
{
    for (QAction* action : menu->actions()) {
        if (action->text() == text)
            return action;
    }
    return nullptr;
}

void LayoutManagerCopyPasteTest::serializeContainerProducesValidText()
{
    m_main->setWidget(new SplitterWidget);

    const QString text = m_lm->serializeContainer(m_main);

    QVERIFY(m_lm->isSerializedWidget(text));
    const QJsonObject obj = QJsonDocument::fromJson(text.toUtf8()).object();
    QCOMPARE(obj.value("type").toString(), QString("splitter"));
}

void LayoutManagerCopyPasteTest::serializeEmptyContainerProducesInvalidText()
{
    m_main->setWidget(nullptr);

    const QString text = m_lm->serializeContainer(m_main);

    QVERIFY(!m_lm->isSerializedWidget(text));
}

void LayoutManagerCopyPasteTest::isSerializedWidgetRejectsInvalidText()
{
    QVERIFY(!m_lm->isSerializedWidget(""));
    QVERIFY(!m_lm->isSerializedWidget("   "));
    QVERIFY(!m_lm->isSerializedWidget("hello"));
    QVERIFY(!m_lm->isSerializedWidget("{not json"));
    QVERIFY(!m_lm->isSerializedWidget("[1, 2, 3]"));
    QVERIFY(!m_lm->isSerializedWidget("{\"a\": 1}"));
    QVERIFY(!m_lm->isSerializedWidget("{\"type\": 42}"));
    QVERIFY(!m_lm->isSerializedWidget("{\"type\": \"nonexistent\"}"));
}

void LayoutManagerCopyPasteTest::isSerializedWidgetAcceptsKnownTypes()
{
    QVERIFY(m_lm->isSerializedWidget(R"({"type":"splitter"})"));
    QVERIFY(m_lm->isSerializedWidget(R"({"type":"tabs","current":1})"));
    QVERIFY(m_lm->isSerializedWidget(R"({"type":"position"})"));
}

void LayoutManagerCopyPasteTest::copyActionEnabledOnlyWhenContainerHasWidget()
{
    m_main->setWidget(nullptr);

    QMenu* menu = popupMenuFor(m_main);
    QVERIFY(menu);
    QVERIFY(menuAction(menu, "Set widget...") != nullptr);
    QVERIFY(menuAction(menu, "Copy widget") != nullptr);
    QVERIFY(menuAction(menu, "Paste widget") != nullptr);
    QVERIFY(!menuAction(menu, "Copy widget")->isEnabled());
    closeMenu(menu);

    m_main->setWidget(new SplitterWidget);

    menu = popupMenuFor(m_main);
    QVERIFY(menu);
    QVERIFY(menuAction(menu, "Copy widget")->isEnabled());
    closeMenu(menu);
}

void LayoutManagerCopyPasteTest::cutActionEnabledOnlyWhenContainerHasWidget()
{
    m_main->setWidget(nullptr);

    QMenu* menu = popupMenuFor(m_main);
    QVERIFY(menu);
    QVERIFY(!menuAction(menu, "Cut widget")->isEnabled());
    closeMenu(menu);

    m_main->setWidget(new SplitterWidget);

    menu = popupMenuFor(m_main);
    QVERIFY(menu);
    QVERIFY(menuAction(menu, "Cut widget")->isEnabled());
    closeMenu(menu);
}

void LayoutManagerCopyPasteTest::pasteActionEnabledOnlyWhenClipboardHoldsValidWidget()
{
    m_clipboard->setText("not a widget");
    QMenu* menu = popupMenuFor(m_main);
    QVERIFY(menu);
    QVERIFY(!menuAction(menu, "Paste widget")->isEnabled());
    closeMenu(menu);

    m_clipboard->setText(R"({"type":"splitter"})");
    menu = popupMenuFor(m_main);
    QVERIFY(menu);
    QVERIFY(menuAction(menu, "Paste widget")->isEnabled());
    closeMenu(menu);

    m_clipboard->clear();
    menu = popupMenuFor(m_main);
    QVERIFY(menu);
    QVERIFY(!menuAction(menu, "Paste widget")->isEnabled());
    closeMenu(menu);
}

void LayoutManagerCopyPasteTest::copySlotWritesSerializedWidgetToClipboard()
{
    m_main->setWidget(new SplitterWidget);

    // Pop up the menu so the slot's target container is the main one.
    closeMenu(popupMenuFor(m_main));

    m_clipboard->setText("stale");
    QMetaObject::invokeMethod(m_lm, "onMenuCopyWidget");

    const QString text = m_clipboard->text();
    QVERIFY(m_lm->isSerializedWidget(text));
    const QJsonObject obj = QJsonDocument::fromJson(text.toUtf8()).object();
    QCOMPARE(obj.value("type").toString(), QString("splitter"));
}

void LayoutManagerCopyPasteTest::cutSlotSerializesAndRemovesWidget()
{
    m_main->setWidget(new SplitterWidget);

    // Pop up the menu so the slot's target container is the main one.
    closeMenu(popupMenuFor(m_main));

    QMetaObject::invokeMethod(m_lm, "onMenuCutWidget");

    const QString text = m_clipboard->text();
    QVERIFY(m_lm->isSerializedWidget(text));
    const QJsonObject obj = QJsonDocument::fromJson(text.toUtf8()).object();
    QCOMPARE(obj.value("type").toString(), QString("splitter"));

    QVERIFY(!m_main->getWidget());
}

void LayoutManagerCopyPasteTest::cutThenPasteRestoresWidget()
{
    m_main->setWidget(new SplitterWidget);

    closeMenu(popupMenuFor(m_main));
    QMetaObject::invokeMethod(m_lm, "onMenuCutWidget");
    QVERIFY(!m_main->getWidget());

    QMetaObject::invokeMethod(m_lm, "onMenuPasteWidget");

    QVERIFY(m_main->getWidget());
    QCOMPARE(m_main->getWidget()->widgetId(), QString("splitter"));
}

void LayoutManagerCopyPasteTest::pasteSlotRestoresWidgetWithState()
{
    m_main->setWidget(new TabsWidget);

    // Rename the first tab and nest a splitter inside it, then point the
    // clipboard at the modified serialization.
    QJsonObject obj = QJsonDocument::fromJson(m_lm->serializeContainer(m_main).toUtf8()).object();
    QJsonArray tabs = obj.value("tabs").toArray();
    QCOMPARE(tabs.size(), 2);
    QJsonObject first = tabs.at(0).toObject();
    first["name"] = "Alpha";
    QJsonObject nested;
    nested["type"] = "splitter";
    first["container"] = nested;
    tabs[0] = first;
    obj["tabs"] = tabs;
    obj["current"] = 1;
    m_clipboard->setText(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));

    closeMenu(popupMenuFor(m_main));
    QMetaObject::invokeMethod(m_lm, "onMenuPasteWidget");

    TabsWidget* pasted = qobject_cast<TabsWidget*>(m_main->getWidget());
    QVERIFY(pasted);

    QTabBar* bar = pasted->findChild<QTabBar*>();
    QVERIFY(bar);
    QCOMPARE(bar->tabText(0), QString("Alpha"));
    QCOMPARE(bar->currentIndex(), 1);

    // The nested container was recreated from the serialization.
    QVERIFY(pasted->findChild<QSplitter*>() != nullptr);
}

void LayoutManagerCopyPasteTest::pasteSlotReplacesExistingWidget()
{
    m_main->setWidget(new TabsWidget);

    m_clipboard->setText(R"({"type":"splitter"})");
    closeMenu(popupMenuFor(m_main));
    QMetaObject::invokeMethod(m_lm, "onMenuPasteWidget");

    QVERIFY(m_main->getWidget());
    QCOMPARE(m_main->getWidget()->widgetId(), QString("splitter"));
}

void LayoutManagerCopyPasteTest::pasteSlotIgnoresInvalidClipboard()
{
    m_main->setWidget(new TabsWidget);

    m_clipboard->setText("not a widget");
    closeMenu(popupMenuFor(m_main));
    QMetaObject::invokeMethod(m_lm, "onMenuPasteWidget");

    // Invalid clipboard content must never wipe the container's widget.
    QVERIFY(m_main->getWidget());
    QCOMPARE(m_main->getWidget()->widgetId(), QString("tabs"));
}
}


QTEST_MAIN(LayoutManagerCopyPasteTest)
#include "layoutmanager_copy_paste_test.moc"
