#include <QtTest>

#include <QAction>
#include <QMenu>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include "basewidget.h"
#include "stylemanager.h"

namespace {

// A minimal widget with a toolbar, to test the base-class machinery
// without any concrete player widget.
class ToolbarWidget : public BaseWidget
{
    Q_OBJECT
public:
    explicit ToolbarWidget(bool visibleByDefault = false)
        : BaseWidget(nullptr)
    {
        delete layout();
        QVBoxLayout* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        m_toolbar = createToolbar(visibleByDefault);
        m_button = new QToolButton(m_toolbar);
        m_button->setObjectName("toolButton");
        m_button->setText("T");
        m_toolbar->addWidget(m_button);
        m_body = new QWidget(this);
        m_body->setObjectName("body");
        layout->addWidget(m_body, 1);
    }

    QString widgetId() const override { return "toolbar-test"; }
    QString widgetTitle() const override { return "Toolbar Test"; }

    WidgetConfiguration storeWidget() override
    {
        WidgetConfiguration config;
        storeToolbar(config);
        return config;
    }

    void restoreWidget(WidgetConfiguration &config) override { restoreToolbar(config); }

    // Forward the protected base-class setters for the tests.
    void showToolbar(bool visible) { BaseWidget::setToolbarVisible(visible); }
    void moveToolbar(ToolbarPosition position) { BaseWidget::setToolbarPosition(position); }

    QToolBar* m_toolbar;
    QToolButton* m_button;
    QWidget* m_body;
};

class NoToolbarWidget : public BaseWidget
{
    Q_OBJECT
public:
    using BaseWidget::BaseWidget;

    QString widgetId() const override { return "no-toolbar-test"; }
    QString widgetTitle() const override { return "No Toolbar Test"; }

    // Forward the protected base-class resolvers for the tests.
    QColor resolveStyleColor(QPalette::ColorRole styled, QPalette::ColorRole fallback) const
    {
        return BaseWidget::styleColor(styled, fallback);
    }

    QString readStyleValue(const QString &name, const QString &fallback) const
    {
        return BaseWidget::styleValue(name, fallback);
    }

    QColor readStyleColorValue(const QString &name, const QColor &fallback) const
    {
        return BaseWidget::styleColorValue(name, fallback);
    }

    int readStyleInt(const QString &name, int fallback, int min, int max) const
    {
        return BaseWidget::styleInt(name, fallback, min, max);
    }

    int readStyleLength(const QString &name, int fallback, int reference) const
    {
        return BaseWidget::styleLength(name, fallback, reference);
    }
};

// Records the style editor the "Style this widget..." action opens
// instead of driving the modal (the chooseFiles() pattern).
class StyleRecordingWidget : public BaseWidget
{
    Q_OBJECT
public:
    using BaseWidget::BaseWidget;

    QString widgetId() const override { return "style-rec-test"; }
    QString widgetTitle() const override { return "Style Rec Test"; }

    // Public here (protected in the base) so the test can read it.
    void openStyleEditor(const QString &selector, const QString &revertTo) override
    {
        m_selector = selector;
        m_revertTo = revertTo;
    }

    QString m_selector;
    QString m_revertTo;
};

// A self-painted variant of the recorder (like the oscillogram): the
// scaffold is the bare "#name" rule only, no "#name *" for children it
// does not have.
class SelfPaintedRecordingWidget : public StyleRecordingWidget
{
    Q_OBJECT
public:
    using StyleRecordingWidget::StyleRecordingWidget;

    QString widgetId() const override { return "self-rec-test"; }

    bool selfPainted() const override { return true; }
};

QAction* actionByText(QMenu* menu, const QString &text);

class BaseWidgetToolbarTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void noToolbarByDefault();
    void createToolbarInstallsAtTop();
    void createToolbarVisibleByDefault();
    void setToolbarVisibleToggles();
    void setToolbarPositionMovesWithinLayout();
    void storeToolbarOnlyWhenDifferentFromDefault();
    void restoreToolbarAppliesState();
    void contextMenuTogglesToolbarVisibility();
    void contextMenuMovesToolbar();
    void styleActionNamesAndSeedsEditor();
    void styleActionSkipsChildRuleForSelfPainted();
    void nameRoundTripsThroughConfiguration();
    void styleColorFallsBackToThemeRole();
    void styleValueFallsBackWithoutSheet();
    void styleValueHelpersResolve();
};

void BaseWidgetToolbarTest::init()
{
    StyleManager::getInstance()->setStyleSheet(QString());
}

void BaseWidgetToolbarTest::cleanup()
{
    StyleManager::getInstance()->setStyleSheet(QString());
}

void BaseWidgetToolbarTest::noToolbarByDefault()
{
    NoToolbarWidget widget;

    QVERIFY(widget.toolbar() == nullptr);
    // No toolbar, but the edit-mode menu still exists: it carries the
    // style action every widget gets.
    QMenu* menu = widget.widgetContextMenu();
    QVERIFY(menu != nullptr);
    QVERIFY(actionByText(menu, "Style this widget...") != nullptr);
    menu->deleteLater();
    QVERIFY(!widget.storeWidget().contains("toolbar"));
    QVERIFY(!widget.storeWidget().contains("name"));
}

void BaseWidgetToolbarTest::createToolbarInstallsAtTop()
{
    ToolbarWidget widget;
    widget.show();
    QCoreApplication::processEvents();

    QVERIFY(widget.toolbar() != nullptr);
    QVERIFY(widget.toolbar()->isHidden());
    QVERIFY(!widget.toolbarVisible());
    QCOMPARE(widget.toolbarPosition(), BaseWidget::ToolbarPosition::Top);

    QLayout* layout = widget.layout();
    QCOMPARE(layout->indexOf(widget.m_toolbar), 0);
    QCOMPARE(layout->indexOf(widget.m_body), 1);
}

void BaseWidgetToolbarTest::createToolbarVisibleByDefault()
{
    ToolbarWidget widget(true);
    widget.show();
    QCoreApplication::processEvents();

    QVERIFY(!widget.toolbar()->isHidden());
    QVERIFY(widget.toolbarVisible());
}

void BaseWidgetToolbarTest::setToolbarVisibleToggles()
{
    ToolbarWidget widget(true);
    widget.show();
    QCoreApplication::processEvents();

    widget.showToolbar(false);
    QVERIFY(!widget.toolbarVisible());
    QVERIFY(widget.toolbar()->isHidden());
    // Hiding keeps the toolbar in the layout.
    QCOMPARE(widget.layout()->indexOf(widget.m_toolbar), 0);

    widget.showToolbar(true);
    QVERIFY(widget.toolbarVisible());
    QVERIFY(!widget.toolbar()->isHidden());
}

void BaseWidgetToolbarTest::setToolbarPositionMovesWithinLayout()
{
    ToolbarWidget widget;
    widget.show();
    QCoreApplication::processEvents();

    QLayout* layout = widget.layout();
    QCOMPARE(layout->indexOf(widget.m_toolbar), 0);

    widget.moveToolbar(BaseWidget::ToolbarPosition::Bottom);
    QCOMPARE(widget.toolbarPosition(), BaseWidget::ToolbarPosition::Bottom);
    QCOMPARE(layout->indexOf(widget.m_toolbar), layout->count() - 1);
    // The rest of the layout keeps its relative order.
    QCOMPARE(layout->indexOf(widget.m_body), 0);

    widget.moveToolbar(BaseWidget::ToolbarPosition::Top);
    QCOMPARE(layout->indexOf(widget.m_toolbar), 0);
}

void BaseWidgetToolbarTest::storeToolbarOnlyWhenDifferentFromDefault()
{
    ToolbarWidget widget(true); // visible by default, at the top
    widget.show();
    QCoreApplication::processEvents();

    // The default state is not persisted.
    QVERIFY(!widget.storeWidget().contains("toolbar"));

    // Hidden (differs from the default) is.
    widget.showToolbar(false);
    const QVariantMap hidden = widget.storeWidget().value("toolbar").toMap();
    QCOMPARE(hidden.value("visible").toBool(), false);
    QCOMPARE(hidden.value("position").toString(), QString("top"));

    // A moved toolbar is persisted even when the visibility matches.
    ToolbarWidget moved(true);
    moved.show();
    moved.moveToolbar(BaseWidget::ToolbarPosition::Bottom);
    const QVariantMap state = moved.storeWidget().value("toolbar").toMap();
    QCOMPARE(state.value("visible").toBool(), true);
    QCOMPARE(state.value("position").toString(), QString("bottom"));
}

void BaseWidgetToolbarTest::restoreToolbarAppliesState()
{
    ToolbarWidget widget(true);
    widget.show();
    QCoreApplication::processEvents();

    WidgetConfiguration config;
    config.insert("toolbar", QVariantMap({
        { "visible", false },
        { "position", "bottom" },
    }));
    widget.restoreWidget(config);

    QVERIFY(!widget.toolbarVisible());
    QVERIFY(widget.toolbar()->isHidden());
    QCOMPARE(widget.layout()->indexOf(widget.m_toolbar),
             widget.layout()->count() - 1);

    // A config without the key leaves the state untouched.
    widget.moveToolbar(BaseWidget::ToolbarPosition::Top);
    WidgetConfiguration empty;
    widget.restoreWidget(empty);
    QVERIFY(widget.toolbar()->isHidden());
    QCOMPARE(widget.layout()->indexOf(widget.m_toolbar), 0);
}

QAction* actionByText(QMenu* menu, const QString &text)
{
    for (QAction* a : menu->actions())
        if (a->text() == text)
            return a;
    return nullptr;
}

void BaseWidgetToolbarTest::contextMenuTogglesToolbarVisibility()
{
    ToolbarWidget widget(true);
    widget.show();
    QCoreApplication::processEvents();

    QMenu* menu = widget.widgetContextMenu();
    if (menu == nullptr) {
        QFAIL("no context menu");
        return;
    }
    QAction* show = actionByText(menu, "Show toolbar");
    if (show == nullptr) {
        QFAIL("no \"Show toolbar\" action");
        return;
    }
    QVERIFY(show->isCheckable());
    QVERIFY(show->isChecked());

    show->trigger(); // uncheck -> hide
    QVERIFY(!widget.toolbarVisible());
    QVERIFY(widget.toolbar()->isHidden());

    show->trigger(); // re-check -> show
    QVERIFY(widget.toolbarVisible());
    QVERIFY(!widget.toolbar()->isHidden());
}

void BaseWidgetToolbarTest::contextMenuMovesToolbar()
{
    ToolbarWidget widget;
    widget.show();
    QCoreApplication::processEvents();

    QMenu* menu = widget.widgetContextMenu();
    if (menu == nullptr) {
        QFAIL("no context menu");
        return;
    }
    QAction* top = actionByText(menu, "Toolbar at top");
    QAction* bottom = actionByText(menu, "Toolbar at bottom");
    if (top == nullptr || bottom == nullptr) {
        QFAIL("no position actions");
        return;
    }
    QVERIFY(top->isChecked());
    QVERIFY(!bottom->isChecked());

    bottom->trigger();
    QCOMPARE(widget.toolbarPosition(), BaseWidget::ToolbarPosition::Bottom);
    QCOMPARE(widget.layout()->indexOf(widget.m_toolbar),
             widget.layout()->count() - 1);

    // A fresh menu reflects the current position.
    menu->deleteLater();
    QMenu* menu2 = widget.widgetContextMenu();
    if (menu2 == nullptr) {
        QFAIL("no context menu");
        return;
    }
    QAction* top2 = actionByText(menu2, "Toolbar at top");
    if (top2 == nullptr) {
        QFAIL("no \"Toolbar at top\" action");
        return;
    }
    QVERIFY(!top2->isChecked());
    menu2->deleteLater();
}

// "Style this widget..." names the widget lazily (the SettingsManager's
// "widgets.name_counter" setting), starts an empty "#name" rule when
// the sheet has none, and opens the editor seeded on the "#name" rule
// — with the pre-action sheet as the cancel target, so an abandoned
// rule leaves nothing behind.
void BaseWidgetToolbarTest::styleActionNamesAndSeedsEditor()
{
    StyleRecordingWidget widget;

    QMenu* menu = widget.widgetContextMenu();
    QVERIFY(menu != nullptr);
    QAction* style = actionByText(menu, "Style this widget...");
    QVERIFY(style != nullptr);

    style->trigger();

    // The name is assigned and the sheet carries an empty "#name" rule.
    QCOMPARE(widget.objectName(), QString("style-rec-test-1"));
    const QString sheet = StyleManager::getInstance()->stylesheet();
    QVERIFY(sheet.contains(QStringLiteral("#style-rec-test-1 {")));

    // The editor is seeded on the "#name" rule; Cancel would revert to
    // the pre-action sheet (empty here).
    QCOMPARE(widget.m_selector, QString("#style-rec-test-1"));
    QCOMPARE(widget.m_revertTo, QString());

    // A second run reuses the name and does not duplicate the rules.
    QMenu* menu2 = widget.widgetContextMenu();
    QVERIFY(menu2 != nullptr);
    QAction* style2 = actionByText(menu2, "Style this widget...");
    QVERIFY(style2 != nullptr);
    style2->trigger();
    QCOMPARE(widget.objectName(), QString("style-rec-test-1"));
    const QString sheet2 = StyleManager::getInstance()->stylesheet();
    QCOMPARE(sheet2.count(QStringLiteral("#style-rec-test-1 {")), 1);
    menu->deleteLater();
    menu2->deleteLater();
}

// A self-painted widget (like the oscillogram) paints its own surface:
// the scaffold is the bare "#name" rule only, no "#name *" rule for
// children it does not have.
void BaseWidgetToolbarTest::styleActionSkipsChildRuleForSelfPainted()
{
    SelfPaintedRecordingWidget widget;

    QMenu* menu = widget.widgetContextMenu();
    QVERIFY(menu != nullptr);
    QAction* style = actionByText(menu, "Style this widget...");
    QVERIFY(style != nullptr);
    style->trigger();

    const QString name = widget.objectName();
    QVERIFY(name.startsWith(QStringLiteral("self-rec-test-")));
    const QString sheet = StyleManager::getInstance()->stylesheet();
    QVERIFY(sheet.contains(QStringLiteral("#%1 {").arg(name)));
    QVERIFY(!sheet.contains(QStringLiteral("#%1 *").arg(name)));
    QCOMPARE(widget.m_selector, QStringLiteral("#") + name);
    menu->deleteLater();
}

// The instance name persists with the widget configuration (only when
// set) and is applied on restore.
void BaseWidgetToolbarTest::nameRoundTripsThroughConfiguration()
{
    StyleRecordingWidget widget;
    WidgetConfiguration stored = widget.storeWidget();
    QVERIFY(!stored.contains("name"));

    widget.setObjectName("meter-1");
    stored = widget.storeWidget();
    QCOMPARE(stored.value("name").toString(), QString("meter-1"));

    StyleRecordingWidget restored;
    restored.restoreWidget(stored);
    QCOMPARE(restored.objectName(), QString("meter-1"));

    // A config without the key leaves the name untouched.
    StyleRecordingWidget bare;
    bare.setObjectName("other");
    WidgetConfiguration empty;
    bare.restoreWidget(empty);
    QCOMPARE(bare.objectName(), QString("other"));
}

// Without a stylesheet the styled role matches the application's palette
// and the fallback role stands in; a rule that names the widget's role
// wins; clearing the sheet restores the fallback.
void BaseWidgetToolbarTest::styleColorFallsBackToThemeRole()
{
    NoToolbarWidget widget;
    widget.show();
    QCoreApplication::processEvents();

    // (Compared by rgb(): QColor's operator== is sensitive to the
    // internal representation, equal colors can fail it.)
    QCOMPARE(widget.resolveStyleColor(QPalette::WindowText, QPalette::Highlight).rgb(),
             widget.palette().color(QPalette::Active, QPalette::Highlight).rgb());

    qApp->setStyleSheet("NoToolbarWidget { color: rgb(1, 2, 3); }");
    QCoreApplication::processEvents();
    QCOMPARE(widget.resolveStyleColor(QPalette::WindowText, QPalette::Highlight).rgb(),
             QColor(1, 2, 3).rgb());

    qApp->setStyleSheet(QString());
    QCoreApplication::processEvents();
    QCOMPARE(widget.resolveStyleColor(QPalette::WindowText, QPalette::Highlight).rgb(),
             widget.palette().color(QPalette::Active, QPalette::Highlight).rgb());
}

// Without a sheet (or a matching rule) every helper returns the coded
// default.
void BaseWidgetToolbarTest::styleValueFallsBackWithoutSheet()
{
    NoToolbarWidget widget;

    QCOMPARE(widget.readStyleValue("x-mode", "default"), "default");
    QCOMPARE(widget.readStyleColorValue("x-color", QColor(9, 8, 7)).rgb(),
             QColor(9, 8, 7).rgb());
    QCOMPARE(widget.readStyleInt("x-count", 5, 0, 10), 5);
    QCOMPARE(widget.readStyleLength("x-size", 7, 100), 7);
}

// The typed helpers interpret the sheet's raw values: colors parse,
// ints clamp into range, lengths take px or % of the reference;
// unparseable values fall back.
void BaseWidgetToolbarTest::styleValueHelpersResolve()
{
    StyleManager::getInstance()->setStyleSheet(QStringLiteral(
        "NoToolbarWidget {\n"
        "    x-mode: boxes;\n"
        "    x-color: #123456;\n"
        "    x-count: 7;\n"
        "    x-big: 99;\n"
        "    x-pct: 50%;\n"
        "    x-px: 12px;\n"
        "    x-pxcount: 9px;\n"
        "    x-junk: hello\n"
        "}"));

    NoToolbarWidget widget;
    QCOMPARE(widget.readStyleValue("x-mode", "none"), "boxes");
    QCOMPARE(widget.readStyleColorValue("x-color", QColor(1, 1, 1)).rgb(),
             QColor("#123456").rgb());
    QCOMPARE(widget.readStyleInt("x-count", 0, 0, 16), 7);
    QCOMPARE(widget.readStyleInt("x-big", 0, 0, 16), 16);  // clamped
    QCOMPARE(widget.readStyleInt("x-pxcount", 0, 0, 16), 9);  // the px is a unit
    QCOMPARE(widget.readStyleLength("x-pct", 1, 100), 50);
    QCOMPARE(widget.readStyleLength("x-px", 1, 100), 12);
    // (Compared by rgb(): QColor's operator== is sensitive to the
    // internal representation, equal colors can fail it.)
    QCOMPARE(widget.readStyleColorValue("x-junk", QColor(2, 2, 2)).rgb(),
             QColor(2, 2, 2).rgb());
    QCOMPARE(widget.readStyleInt("x-junk", 3, 0, 16), 3);
    QCOMPARE(widget.readStyleLength("x-junk", 4, 100), 4);
}

} // namespace

QTEST_MAIN(BaseWidgetToolbarTest)

#include "basewidget_test.moc"
