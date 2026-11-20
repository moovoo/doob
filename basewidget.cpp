#include "basewidget.h"
#include "layoutmanager.h"
#include "stylesheetdialog.h"
#include "stylemanager.h"
#include "widgetcontainer.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBoxLayout>
#include <QPainter>
#include <QToolBar>

BaseWidget::BaseWidget(QWidget *parent)
    : QWidget{parent}
{
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    setLayout(layout);
}

QMenu *transientMenu(QWidget *parent)
{
    QMenu *menu = new QMenu(parent);
    QObject::connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
    return menu;
}


QMenu* BaseWidget::widgetContextMenu() {
    QMenu* menu = new QMenu(widgetTitle(), this);

    // The style action belongs to every widget (leaves and containers
    // alike): the toolbar entries, when a toolbar exists, follow it.
    QAction* style = menu->addAction("Style this widget...");
    connect(style, &QAction::triggered, this, [this] {
        editOwnStyle();
    });

    if (m_toolbar == nullptr)
        return menu;

    menu->addSeparator();

    QAction* show = menu->addAction("Show toolbar");
    show->setCheckable(true);
    show->setChecked(m_toolbarVisible);
    connect(show, &QAction::toggled, this,
            [this](bool visible) { setToolbarVisible(visible); });

    menu->addSeparator();

    QAction* top = menu->addAction("Toolbar at top");
    QAction* bottom = menu->addAction("Toolbar at bottom");
    top->setCheckable(true);
    bottom->setCheckable(true);
    top->setChecked(m_toolbarPosition == ToolbarPosition::Top);
    bottom->setChecked(m_toolbarPosition == ToolbarPosition::Bottom);
    // An exclusive group: the two positions are options, not toggles —
    // Qt draws them as radio buttons and unchecks the previous one on a
    // choice (as do the widgets' own option menus, e.g. the oscillogram's).
    QActionGroup* positionGroup = new QActionGroup(menu);
    positionGroup->setExclusive(true);
    positionGroup->addAction(top);
    positionGroup->addAction(bottom);
    connect(top, &QAction::triggered, this,
            [this, top, bottom] {
                top->setChecked(true);
                bottom->setChecked(false);
                setToolbarPosition(ToolbarPosition::Top);
            });
    connect(bottom, &QAction::triggered, this,
            [this, top, bottom] {
                bottom->setChecked(true);
                top->setChecked(false);
                setToolbarPosition(ToolbarPosition::Bottom);
            });

    return menu;
}

QToolBar* BaseWidget::createToolbar(bool visibleByDefault)
{
    if (m_toolbar != nullptr)
        return m_toolbar;

    QToolBar* toolbar = new QToolBar(this);
    toolbar->setObjectName("toolbar");
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    // Right-clicks are handled by the container's edit-mode filter; the
    // toolbar offers no menu of its own.
    toolbar->setContextMenuPolicy(Qt::NoContextMenu);

    m_toolbar = toolbar;
    m_toolbarDefaultVisible = visibleByDefault;
    // Do not go through setToolbarVisible(): the member already holds the
    // initial value and the call would be a no-op, leaving the fresh
    // QToolBar (visible by default) unhidden.
    m_toolbarVisible = visibleByDefault;
    toolbar->setVisible(visibleByDefault);

    // The toolbar is a strip: first layout item (top) by default,
    // repositioned by setToolbarPosition(). The widget's layout must be
    // a QBoxLayout for the strip to take part in it.
    if (QBoxLayout* box = qobject_cast<QBoxLayout*>(layout()))
        box->insertWidget(0, toolbar);
    return toolbar;
}

void BaseWidget::setToolbarVisible(bool visible)
{
    if (m_toolbar == nullptr || m_toolbarVisible == visible)
        return;
    m_toolbarVisible = visible;
    m_toolbar->setVisible(visible);
}

void BaseWidget::setToolbarPosition(ToolbarPosition position)
{
    if (m_toolbar == nullptr || m_toolbarPosition == position)
        return;
    m_toolbarPosition = position;

    // Moving the strip is just reinserting it: first item for top, last
    // for bottom, whatever the widget's own box layout contains.
    QBoxLayout* box = qobject_cast<QBoxLayout*>(layout());
    if (box == nullptr)
        return;
    box->removeWidget(m_toolbar);
    if (position == ToolbarPosition::Top)
        box->insertWidget(0, m_toolbar);
    else
        box->addWidget(m_toolbar);
}

void BaseWidget::storeToolbar(WidgetConfiguration &config) const
{
    if (m_toolbar == nullptr)
        return;

    const bool nonDefault = m_toolbarVisible != m_toolbarDefaultVisible
                          || m_toolbarPosition != ToolbarPosition::Top;
    if (!nonDefault)
        return;

    QVariantMap toolbar;
    toolbar.insert("visible", m_toolbarVisible);
    toolbar.insert("position", m_toolbarPosition == ToolbarPosition::Top ? "top" : "bottom");
    config.insert("toolbar", toolbar);
}

void BaseWidget::restoreToolbar(const WidgetConfiguration &config)
{
    if (m_toolbar == nullptr)
        return;

    const QVariantMap toolbar = config.value("toolbar").toMap();
    if (toolbar.isEmpty())
        return;

    if (toolbar.contains("visible"))
        setToolbarVisible(toolbar.value("visible").toBool());
    if (toolbar.value("position").toString() == "bottom")
        setToolbarPosition(ToolbarPosition::Bottom);
}

WidgetContainer* BaseWidget::parentWidgetContainer() {
    return qobject_cast<WidgetContainer*>(parentWidget());
}

void BaseWidget::editOwnStyle()
{
    // The sheet as it was before this action touched it: the dialog's
    // Cancel reverts to it, so a rule appended for a freshly named
    // widget that the user abandons leaves nothing behind.
    StyleManager* styles = StyleManager::getInstance();
    const QString previous = styles->stylesheet();

    // A name is what a "#name" rule targets: assign one lazily (the
    // LayoutManager owns the counter and the collision checks).
    if (objectName().isEmpty())
        setObjectName(LayoutManager::getInstance()->freeInstanceName(widgetId()));

    QString rules = QStringLiteral("\n#%1 {\n}\n").arg(objectName());
    //if (!selfPainted())
    //    rules += QStringLiteral("\n#%1 * {\n}\n").arg(objectName());

    if (!styles->ruleReferences(objectName()))
        styles->setStyleSheet(previous + rules);

    openStyleEditor(QStringLiteral("#") + objectName(), previous);
}

void BaseWidget::storeName(WidgetConfiguration &config) const
{
    if (!objectName().isEmpty())
        config.insert("name", objectName());
}

void BaseWidget::restoreName(const WidgetConfiguration &config)
{
    const QString name = config.value("name").toString();
    if (!name.isEmpty())
        setObjectName(name);
}

void BaseWidget::openStyleEditor(const QString &selector, const QString &revertTo)
{
    StyleSheetDialog dialog(this, selector, revertTo);
    dialog.exec();
}


QColor BaseWidget::styleColor(QPalette::ColorRole styled, QPalette::ColorRole fallback) const
{
    // Compare by rgb(): QColor's operator== is sensitive to the internal
    // representation, two equal colors can fail it, and the question here
    // is purely "does the color value differ from the theme's".
    const QPalette pal = palette();
    const QColor styledColor = pal.color(QPalette::Active, styled);
    if (styledColor.rgb() != qApp->palette().color(QPalette::Active, styled).rgb())
        return styledColor;
    return pal.color(QPalette::Active, fallback);
}

QString BaseWidget::styleValue(const QString &name, const QString &fallback) const
{
    return StyleManager::getInstance()->styleValue(this, name, fallback);
}

QColor BaseWidget::styleColorValue(const QString &name, const QColor &fallback) const
{
    const QString value = styleValue(name, QString());
    if (value.isEmpty())
        return fallback;
    const QColor color(value);
    return color.isValid() ? color : fallback;
}

int BaseWidget::styleInt(const QString &name, int fallback, int min, int max) const
{
    const QString value = styleValue(name, QString());
    if (value.isEmpty())
        return fallback;
    // A trailing "px" is a unit, not part of the number (sheets write
    // lengths with it — the same leniency styleLength gives).
    QString digits = value.trimmed();
    if (digits.endsWith(QLatin1String("px")))
        digits.chop(2);
    bool ok = false;
    const int parsed = digits.trimmed().toInt(&ok);
    return ok ? qBound(min, parsed, max) : fallback;
}

int BaseWidget::styleLength(const QString &name, int fallback, int reference) const
{
    const QString value = styleValue(name, QString());
    if (value.isEmpty())
        return fallback;
    const QString trimmed = value.trimmed();
    bool ok = false;
    if (trimmed.endsWith(QLatin1Char('%'))) {
        const double percent = trimmed.left(trimmed.size() - 1).trimmed().toDouble(&ok);
        return ok ? qMax(1, qRound(reference * percent / 100.0)) : fallback;
    }
    QString digits = trimmed;
    if (digits.endsWith(QLatin1String("px")))
        digits.chop(2);
    const int parsed = digits.trimmed().toInt(&ok);
    return ok ? qMax(1, parsed) : fallback;
}

BarSurface BaseWidget::barSurface(QPainter &p) const
{
    BarSurface surface;
    const QColor bg = palette().color(QPalette::Base);
    surface.track =
        styleColorValue(QStringLiteral("x-track-color"), bg.lighter(120));
    surface.bar = styleColor(QPalette::WindowText, QPalette::Highlight);
    p.fillRect(rect(), bg);
    surface.segments = styleInt(QStringLiteral("x-segments"), 16, 2, 64);
    surface.margin = styleInt(QStringLiteral("x-bar-margin"), 0, 1, 100);
    return surface;
}

WidgetConfiguration BaseWidget::storeWidget() {
    WidgetConfiguration config;
    storeName(config);
    return config;
}

void BaseWidget::restoreWidget(WidgetConfiguration& config) {
    restoreName(config);
}
