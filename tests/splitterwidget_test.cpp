#include <QtTest/QtTest>
#include <QApplication>
#include <QSplitter>
#include <QSpinBox>
#include <QDialog>
#include <QTimer>
#include <QAction>
#include <QMenu>

#include "layoutmanager.h"
#include "splitterwidget.h"
#include "widgetcontainer.h"

namespace {

class SplitterWidgetTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testDefaultTwoSections();
    void testToggleOrientation();
    void testSetSectionsDialog();
    void testRestoreFiveSections();
    void testStoreRestoreRoundtrip();
    void testContextMenuCarriesStyleAction();
};

void SplitterWidgetTest::initTestCase()
{
    LayoutManager::getInstance();
}

void SplitterWidgetTest::testDefaultTwoSections()
{
    SplitterWidget splitter;
    splitter.show();
    qApp->processEvents();
    QSplitter* sp = splitter.findChild<QSplitter*>();
    QVERIFY(sp);
    QCOMPARE(sp->count(), 2);
    for (int i = 0; i < sp->count(); ++i) {
        QWidget* w = sp->widget(i);
        QVERIFY(w);
        QVERIFY(w->isVisible());
    }
}

void SplitterWidgetTest::testToggleOrientation()
{
    SplitterWidget splitter;
    QSplitter* sp = splitter.findChild<QSplitter*>();
    Qt::Orientation before = sp->orientation();
    QMetaObject::invokeMethod(&splitter, "onToggleOrientation", Qt::DirectConnection, Q_ARG(bool, false));
    QVERIFY(sp->orientation() != before);
}

void SplitterWidgetTest::testSetSectionsDialog()
{
    SplitterWidget splitter;
    splitter.resize(600, 400);
    splitter.show();
    qApp->processEvents();
    QSplitter* sp = splitter.findChild<QSplitter*>();

    bool dialogHandled = false;
    QTimer::singleShot(100, &splitter, [&dialogHandled] {
        QWidget* dlg = QApplication::activeModalWidget();
        if (!dlg)
            return;
        QSpinBox* spin = dlg->findChild<QSpinBox*>();
        QVERIFY(spin);
        spin->setValue(5);
        dialogHandled = true;
        qobject_cast<QDialog*>(dlg)->accept();
    });

    QMetaObject::invokeMethod(&splitter, "onSetSections", Qt::DirectConnection, Q_ARG(bool, false));

    QVERIFY(dialogHandled);
    QCOMPARE(sp->count(), 5);
}

void SplitterWidgetTest::testRestoreFiveSections()
{
    WidgetConfiguration cfg;
    cfg["orientation"] = static_cast<int>(Qt::Horizontal);
    QVariantList containers;
    for (int i = 0; i < 5; ++i)
        containers.append(QVariantMap());
    cfg["containers"] = containers;

    SplitterWidget splitter;
    splitter.restoreWidget(cfg);
    QSplitter* sp = splitter.findChild<QSplitter*>();
    QCOMPARE(sp->count(), 5);
    QCOMPARE(sp->orientation(), Qt::Horizontal);

    WidgetConfiguration stored = splitter.storeWidget();
    QCOMPARE(stored.value("containers").toList().size(), 5);
    QCOMPARE(stored.value("orientation").toInt(), static_cast<int>(Qt::Horizontal));
}

void SplitterWidgetTest::testStoreRestoreRoundtrip()
{
    WidgetConfiguration cfg;
    cfg["orientation"] = static_cast<int>(Qt::Vertical);
    QVariantList containers;
    for (int i = 0; i < 3; ++i)
        containers.append(QVariantMap());
    cfg["containers"] = containers;

    SplitterWidget s1;
    s1.restoreWidget(cfg);
    QCOMPARE(s1.findChild<QSplitter*>()->orientation(), Qt::Vertical);
    WidgetConfiguration stored = s1.storeWidget();

    SplitterWidget s2;
    s2.restoreWidget(stored);
    QCOMPARE(s2.findChild<QSplitter*>()->orientation(), Qt::Vertical);
    WidgetConfiguration stored2 = s2.storeWidget();

    QCOMPARE(stored2.value("orientation").toInt(), static_cast<int>(Qt::Vertical));
    QCOMPARE(stored2.value("containers").toList().size(), 3);
    QCOMPARE(stored2.value("containers").toList(), stored.value("containers").toList());
}

// The menu chains the base's entries: like every other widget, the
// splitter gets the "Style this widget..." action (the splitter's own
// menu used to start from scratch and leave it out).
void SplitterWidgetTest::testContextMenuCarriesStyleAction()
{
    SplitterWidget splitter;
    QMenu* menu = splitter.widgetContextMenu();
    QVERIFY(menu != nullptr);
    bool styleFound = false;
    for (QAction* action : menu->actions())
        if (action->text() == QStringLiteral("Style this widget..."))
            styleFound = true;
    QVERIFY(styleFound);
    delete menu;
}
}


QTEST_MAIN(SplitterWidgetTest)
#include "splitterwidget_test.moc"
