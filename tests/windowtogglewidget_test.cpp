#include <QtTest/QtTest>
#include <QApplication>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>

#include "layoutmanager.h"
#include "windowmanager.h"
#include "windowtogglewidget.h"

namespace {

// The modal dialogs are seams: recorded, not driven (the QFileDialog is
// untestable offscreen — the chooseFiles() pattern).
class TestWindowToggleWidget : public WindowToggleWidget
{
public:
    explicit TestWindowToggleWidget(QWidget *parent = nullptr)
        : WindowToggleWidget(parent) {}

    void openWindowPicker(const QString &selectedId) override
    {
        pickerSelected = selectedId;
        ++pickerCount;
        if (!pickResult.isEmpty())
            setWindowId(pickResult);
    }
    QString promptCaption(const QString &current) override
    {
        ++promptCount;
        return promptResult;
    }

    int pickerCount = 0;
    QString pickerSelected;
    QString pickResult;
    int promptCount = 0;
    QString promptResult;
};

class WindowToggleWidgetTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void registeredInLayoutManager();
    void unboundShowsQuestionMark();
    void bindWindowSeedsCaptionAndState();
    void clickTogglesWindow();
    void externalVisibilityChangeSyncsButton();
    void deletedWindowShowsQuestionMark();
    void clickUnboundOpensPicker();
    void stateRoundTripsThroughConfiguration();
    void contextMenuOffersCaptionAndPicker();

private:
    WindowManager* m_wm = nullptr;
    TestWindowToggleWidget* m_widget = nullptr;
    QPushButton* m_button = nullptr;
};

void WindowToggleWidgetTest::init()
{
    m_wm = WindowManager::getInstance();
    m_wm->clear();

    m_widget = new TestWindowToggleWidget();
    m_widget->show();
    qApp->processEvents();

    m_button = m_widget->findChild<QPushButton*>("toggleButton");
    QVERIFY(m_button != nullptr);
}

void WindowToggleWidgetTest::cleanup()
{
    delete m_widget;
    m_wm->clear();
    qApp->processEvents();
}

void WindowToggleWidgetTest::registeredInLayoutManager()
{
    WidgetConfiguration cfg;
    BaseWidget* w = LayoutManager::getInstance()->createWidget(WindowToggleWidget::id, cfg);

    QVERIFY(w != nullptr);
    QCOMPARE(w->widgetId(), WindowToggleWidget::id);
    QCOMPARE(w->widgetTitle(), WindowToggleWidget::title);
    delete w;
}

void WindowToggleWidgetTest::unboundShowsQuestionMark()
{
    // No window bound: the button shows "?" and is not checked.
    QCOMPARE(m_button->text(), QString("?"));
    QCOMPARE(m_button->isChecked(), false);
    QCOMPARE(m_widget->windowId(), QString());
}

void WindowToggleWidgetTest::bindWindowSeedsCaptionAndState()
{
    const QString id = m_wm->addWindow(WindowManager::Kind::Standalone, "Side");

    m_widget->setWindowId(id);

    QCOMPARE(m_widget->windowId(), id);
    QCOMPARE(m_widget->caption(), QString("Side"));
    QCOMPARE(m_button->text(), QString("Side"));
    QCOMPARE(m_button->isChecked(), true);

    // A rename of the window does not rewrite the caption.
    m_wm->setWindowTitle(id, "Renamed");
    QCOMPARE(m_button->text(), QString("Side"));

    // A re-selection re-seeds the caption from the current title.
    m_widget->setWindowId(id);
    QCOMPARE(m_button->text(), QString("Renamed"));
}

void WindowToggleWidgetTest::clickTogglesWindow()
{
    const QString id = m_wm->addWindow(WindowManager::Kind::Standalone, "W");
    m_widget->setWindowId(id);
    QSignalSpy vis(m_wm, &WindowManager::windowVisibleChanged);

    m_button->click();

    QCOMPARE(vis.count(), 1);
    QCOMPARE(m_wm->windowVisible(id), false);
    QCOMPARE(m_button->isChecked(), false);

    m_button->click();
    QCOMPARE(m_wm->windowVisible(id), true);
    QCOMPARE(m_button->isChecked(), true);
}

void WindowToggleWidgetTest::externalVisibilityChangeSyncsButton()
{
    const QString id = m_wm->addWindow(WindowManager::Kind::Standalone, "S");
    m_widget->setWindowId(id);

    m_wm->setWindowVisible(id, false);
    QCOMPARE(m_button->isChecked(), false);

    m_wm->setWindowVisible(id, true);
    QCOMPARE(m_button->isChecked(), true);
}

void WindowToggleWidgetTest::deletedWindowShowsQuestionMark()
{
    const QString id = m_wm->addWindow(WindowManager::Kind::Standalone, "Gone");
    m_widget->setWindowId(id);
    QCOMPARE(m_button->text(), QString("Gone"));

    m_wm->removeWindow(id);
    qApp->processEvents();

    QCOMPARE(m_button->text(), QString("?"));
    QCOMPARE(m_button->isChecked(), false);
    // The stale id is kept: re-adding a window with the same id
    // rebinds the button automatically (the caption, being
    // per-widget state, survives the round trip).
    const QString again = m_wm->addWindow(WindowManager::Kind::Standalone, "Again");
    QCOMPARE(again, id);
    QCOMPARE(m_button->text(), QString("Gone"));
    QCOMPARE(m_button->isChecked(), true);
}

void WindowToggleWidgetTest::clickUnboundOpensPicker()
{
    const QString id = m_wm->addWindow(WindowManager::Kind::Standalone, "Pick");
    m_widget->pickResult = id; // the (simulated) picker's acceptance

    m_button->click();

    QCOMPARE(m_widget->pickerCount, 1);
    QCOMPARE(m_widget->pickerSelected, QString()); // nothing was bound
    QCOMPARE(m_widget->windowId(), id);
    QCOMPARE(m_button->text(), QString("Pick"));
}

void WindowToggleWidgetTest::stateRoundTripsThroughConfiguration()
{
    const QString id = m_wm->addWindow(WindowManager::Kind::Standalone, "RT");
    m_widget->setWindowId(id);
    m_widget->setCaption("Custom");

    const WidgetConfiguration cfg = m_widget->storeWidget();
    QCOMPARE(cfg.value("window_id").toString(), id);
    QCOMPARE(cfg.value("caption").toString(), QString("Custom"));

    // A fresh widget (created the way the "Set widget..." dialog does)
    // restores both.
    WidgetConfiguration restore = cfg;
    WindowToggleWidget* fresh = new WindowToggleWidget();
    fresh->restoreWidget(restore);
    QCOMPARE(fresh->windowId(), id);
    QCOMPARE(fresh->caption(), QString("Custom"));
    QPushButton* button = fresh->findChild<QPushButton*>("toggleButton");
    QVERIFY(button != nullptr);
    QCOMPARE(button->text(), QString("Custom"));
    QCOMPARE(button->isChecked(), true);
    delete fresh;
}

void WindowToggleWidgetTest::contextMenuOffersCaptionAndPicker()
{
    const QString id = m_wm->addWindow(WindowManager::Kind::Standalone, "M");
    m_widget->setWindowId(id);

    QMenu* menu = m_widget->widgetContextMenu();
    QVERIFY(menu != nullptr);

    QStringList texts;
    for (QAction* a : menu->actions())
        if (!a->isSeparator())
            texts << a->text();
    // The base's style action first, the widget's own actions after.
    QVERIFY(texts.contains("Style this widget..."));
    QVERIFY(texts.contains("Edit caption..."));
    QVERIFY(texts.contains("Choose window..."));

    // Edit caption... commits the prompted text.
    m_widget->promptResult = "Edited";
    for (QAction* a : menu->actions())
        if (a->text() == "Edit caption...")
            a->trigger();
    QCOMPARE(m_widget->promptCount, 1);
    QCOMPARE(m_widget->caption(), QString("Edited"));
    QCOMPARE(m_button->text(), QString("Edited"));

    // Choose window... opens the picker on the bound window and binds
    // the (simulated) choice.
    m_widget->pickResult = QString();
    const QString other = m_wm->addWindow(WindowManager::Kind::Standalone, "Other");
    m_widget->pickResult = other;
    for (QAction* a : menu->actions())
        if (a->text() == "Choose window...")
            a->trigger();
    QCOMPARE(m_widget->pickerCount, 1);
    QCOMPARE(m_widget->pickerSelected, id);
    QCOMPARE(m_widget->windowId(), other);
    QCOMPARE(m_button->text(), QString("Other"));

    menu->deleteLater();
}
}

QTEST_MAIN(WindowToggleWidgetTest)
#include "windowtogglewidget_test.moc"
