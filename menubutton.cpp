#include "menubutton.h"

#include "layoutmanager.h"
#include "mainwindow.h"

#include <QBoxLayout>
#include <QPushButton>

MenuButtonWidget::MenuButtonWidget(QWidget *parent)
    : BaseWidget(parent)
{
    m_button = new QPushButton(this);
    m_button->setObjectName("menuButton");
    m_button->setText(QStringLiteral("☰"));
    m_button->setToolTip(QStringLiteral("Menu"));

    // The base's box layout: the button sits centered between stretches.
    QBoxLayout* box = qobject_cast<QBoxLayout*>(layout());
    box->addStretch(1);
    box->addWidget(m_button);
    box->addStretch(1);

    connect(m_button, &QPushButton::clicked, this, &MenuButtonWidget::showMenu);
}

void MenuButtonWidget::showMenu()
{
    // The main menu (the menu bar's File and View submenus as-is, the
    // checkable "Show menu bar" entry where the menu bar can be hidden),
    // popped at the button's lower edge.
    MainWindow* window = LayoutManager::getInstance()->window();
    if (window == nullptr)
        return;
    window->mainMenu()->popup(mapToGlobal(QPoint(0, height())));
}
