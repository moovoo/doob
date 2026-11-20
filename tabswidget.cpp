#include "tabswidget.h"
#include "widgetcontainer.h"
#include "layoutmanager.h"
#include "tabposition.h"

#include <QTabBar>
#include <QInputDialog>
#include <QLineEdit>

TabsWidget::TabsWidget(QWidget *parent) : BaseWidget(parent)
{
    m_tabs = new QTabWidget(this);
    m_tabs->tabBar()->setExpanding(false);
    m_tabs->tabBar()->setDrawBase(false);

    layout()->addWidget(m_tabs);

    connect(m_tabs, SIGNAL(tabBarDoubleClicked(int)), this, SLOT(onTabDoubleClicked(int)));
    // A movable tab bar reorders its tabs; keep m_containers in the same
    // order so the saved container always belongs to the tab that shows it.
    connect(m_tabs->tabBar(), SIGNAL(tabMoved(int,int)), this, SLOT(onTabMoved(int,int)));

    addTab("Tab 1");
    addTab("Tab 2");
}

void TabsWidget::addTab(const QString& name)
{
    WidgetContainer* w = new WidgetContainer(this);
    m_containers.append(w);

    m_tabs->addTab(w, name);

    WidgetContainer *c = parentWidgetContainer();
    if (c != nullptr) {
        c->forceInstallEventFilter();
    }
}

void TabsWidget::removeTab(int index)
{
    if (index < 0 || index >= m_containers.size()) {
        return;
    }
    if (m_containers.size() <= 1) {
        return;
    }

    WidgetContainer* w = m_containers.at(index);
    m_containers.removeAt(index);
    m_tabs->removeTab(index);
    w->deleteLater();
}

void TabsWidget::renameTabAt(int index)
{
    if (index < 0 || index >= m_containers.size()) {
        return;
    }
    bool ok = false;
    QString name = QInputDialog::getText(this, "Rename tab", "Tab name:",
                                         QLineEdit::Normal, m_tabs->tabText(index), &ok);
    if (ok && !name.isEmpty()) {
        m_tabs->setTabText(index, name);
    }
}

QMenu* TabsWidget::widgetContextMenu()
{
    // Start from the base menu so this widget gets the "Style this
    // widget..." action (and the toolbar entries, should a toolbar ever
    // exist) like every other widget; its own entries follow below a
    // separator.
    QMenu* menu = BaseWidget::widgetContextMenu();
    menu->addSeparator();

    QAction* add = menu->addAction("Add tab");
    QAction* remove = menu->addAction("Remove tab");
    QAction* rename = menu->addAction("Rename tab...");

    remove->setEnabled(m_containers.size() > 1);

    connect(add, SIGNAL(triggered(bool)), this, SLOT(onAddTab(bool)));
    connect(remove, SIGNAL(triggered(bool)), this, SLOT(onRemoveTab(bool)));
    connect(rename, SIGNAL(triggered(bool)), this, SLOT(onRenameTab(bool)));

    menu->addSeparator();

    TabPosition::positionMenu(menu, m_tabs, [this](QTabWidget::TabPosition p) {
        m_tabs->setTabPosition(p);
    });

    QAction* movable = menu->addAction("Movable tabs");
    movable->setCheckable(true);
    movable->setChecked(m_tabs->isMovable());
    connect(movable, SIGNAL(triggered(bool)), this, SLOT(onToggleMovable(bool)));

    return menu;
}

void TabsWidget::onAddTab(bool checked)
{
    addTab(QString("Tab %1").arg(m_containers.size() + 1));
}

void TabsWidget::onRemoveTab(bool checked)
{
    removeTab(m_tabs->currentIndex());
}

void TabsWidget::onRenameTab(bool checked)
{
    renameTabAt(m_tabs->currentIndex());
}

void TabsWidget::onToggleMovable(bool checked)
{
    m_tabs->setMovable(checked);
}

void TabsWidget::onTabMoved(int from, int to)
{
    m_containers.move(from, to);
}

void TabsWidget::onTabDoubleClicked(int index)
{
    if (!LayoutManager::getInstance()->isEditMode()) {
        return;
    }
    renameTabAt(index);
}

WidgetConfiguration TabsWidget::storeWidget()
{
    WidgetConfiguration config = BaseWidget::storeWidget();
    config["current"] = m_tabs->currentIndex();

    // Only states different from the defaults (top position, not movable)
    // are stored, so a stale layout document can't reintroduce them.
    if (m_tabs->tabPosition() != QTabWidget::North) {
        config["tabPosition"] = TabPosition::name(m_tabs->tabPosition());
    }
    if (m_tabs->isMovable()) {
        config["movableTabs"] = true;
    }

    QVariantList tabs;
    for (int i = 0; i < m_containers.size(); ++i) {
        QVariantMap tab;
        tab["name"] = m_tabs->tabText(i);
        tab["container"] = m_containers.at(i)->storeContainer();
        tabs.append(tab);
    }
    config["tabs"] = tabs;
    return config;
}

void TabsWidget::restoreWidget(WidgetConfiguration &config)
{
    BaseWidget::restoreWidget(config);
    QVariantList tabs = config.value("tabs").toList();
    if (tabs.isEmpty()) {
        return;
    }

    while (m_containers.size() > tabs.size()) {
        removeTab(static_cast<int>(m_containers.size()) - 1);
    }
    while (m_containers.size() < tabs.size()) {
        addTab(QString("Tab %1").arg(m_containers.size() + 1));
    }

    for (int i = 0; i < tabs.size(); ++i) {
        QVariantMap tab = tabs.at(i).toMap();
        m_tabs->setTabText(i, tab.value("name", QString("Tab %1").arg(i + 1)).toString());
        WidgetConfiguration wc = tab.value("container").toMap();
        m_containers.at(i)->restoreContainer(wc);
    }

    int current = config.value("current", 0).toInt();
    if (current >= 0 && current < m_tabs->count()) {
        m_tabs->setCurrentIndex(current);
    }

    // The position is applied only when present (a missing key keeps
    // the default, as before).
    if (config.contains("tabPosition"))
        m_tabs->setTabPosition(TabPosition::fromName(config.value("tabPosition").toString()));
    m_tabs->setMovable(config.value("movableTabs", false).toBool());
}
