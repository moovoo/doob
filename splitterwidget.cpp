#include "splitterwidget.h"
#include "widgetcontainer.h"

#include <QInputDialog>

SplitterWidget::SplitterWidget(QWidget *parent) : BaseWidget(parent)
{
    m_splitter = new QSplitter(this);
    m_splitter->setChildrenCollapsible(false);
    layout()->addWidget(m_splitter);

    for (int i=0; i<2; i++) {
        appendSlot();
    }
}

void SplitterWidget::appendSlot() {
    WidgetContainer* w = new WidgetContainer(this);
    m_containers.append(w);

    m_splitter->addWidget(w);

    WidgetContainer *c = parentWidgetContainer();
    if (c != nullptr) {
        c->forceInstallEventFilter();
    }
}

void SplitterWidget::removeSlot() {
    if (m_containers.size() > 2) {
        WidgetContainer* w = m_containers.last();
        w->deleteLater();
        m_containers.removeLast();
    }
}

QMenu* SplitterWidget::widgetContextMenu()
{
    // Start from the base menu so this widget gets the "Style this
    // widget..." action like every other widget; its own entries follow
    // below a separator.
    QMenu* menu = BaseWidget::widgetContextMenu();
    menu->addSeparator();

    QAction* act = menu->addAction("Toggle mode");
    QAction* sections = menu->addAction("Sections...");

    connect(act, SIGNAL(triggered(bool)), this, SLOT(onToggleOrientation(bool)));
    connect(sections, SIGNAL(triggered(bool)), this, SLOT(onSetSections(bool)));

    return menu;
}

void SplitterWidget::onToggleOrientation(bool checked)
{
    Qt::Orientation o = m_splitter->orientation() == Qt::Vertical ? Qt::Horizontal : Qt::Vertical;
    m_splitter->setOrientation(o);

}

void SplitterWidget::onSetSections(bool checked)
{
    bool ok = false;
    int n = QInputDialog::getInt(this, "Sections", "Number of sections:",
                                 static_cast<int>(m_containers.size()), 2, 8, 1, &ok);
    if (!ok) {
        return;
    }
    while (m_containers.size() > n) {
        removeSlot();
    }
    while (m_containers.size() < n) {
        appendSlot();
    }
}

WidgetConfiguration SplitterWidget::storeWidget() {
    WidgetConfiguration config = BaseWidget::storeWidget();
    config["orientation"] = static_cast<int>(m_splitter->orientation());
    config["state"] = m_splitter->saveState().toBase64();
    QVariantList children;
    foreach (WidgetContainer* c, m_containers) {
        WidgetConfiguration childConfig = c->storeContainer();
        children.append(childConfig);
    }
    config["containers"] = children;
    return config;
}

void SplitterWidget::restoreWidget(WidgetConfiguration &config) {
    BaseWidget::restoreWidget(config);
    int o = config.value("orientation", static_cast<int>(Qt::Vertical)).toInt();
    m_splitter->setOrientation(o == static_cast<int>(Qt::Vertical) ? Qt::Vertical : Qt::Horizontal);

    while (m_containers.size() > 2) {
        removeSlot();
    }

    QVariant containers = config.value("containers");
    int itemIndex = 0;
    foreach (QVariant item, containers.toList()) {
        if (m_containers.size() < itemIndex+1) {
            appendSlot();
        }
        WidgetConfiguration wc = item.toMap();
        m_containers.at(itemIndex++)->restoreContainer(wc);
    }
    QByteArray state = QByteArray::fromBase64(config.value("state", "").toByteArray());
    if (!state.isEmpty()) {
        m_splitter->restoreState(state);
    }
}

