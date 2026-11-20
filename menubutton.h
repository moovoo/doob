#ifndef MENUBUTTON_H
#define MENUBUTTON_H

#include "basewidget.h"

class QPushButton;

// The hamburger: a single button that pops the main menu (the menu
// bar's File and View submenus as-is, plus the checkable "Show menu
// bar" entry where the menu bar can be hidden). The recovery's second
// layer (the F10 shortcut is the first, the tray entry the third) and
// the only menu access in the child standalone windows (they have no
// menu bar at all). Stateless — the base persists the instance name
// only; a click pops the main menu at the button's position through a
// protected virtual seam (a modal popup is untestable offscreen — the
// chooseFiles pattern).
class MenuButtonWidget : public BaseWidget
{
    Q_OBJECT
public:
    inline static const QString id = "menubutton";
    inline static const QString title = "Menu button";
    inline static const QString tag = widgetTags::layout;

    explicit MenuButtonWidget(QWidget *parent = nullptr);
    QString widgetId() const override { return id; }
    QString widgetTitle() const override { return title; }

protected:
    // The click's menu popup (the test seam: the tests count the call
    // instead of driving the modal popup).
    virtual void showMenu();

private:
    QPushButton* m_button;
};

#endif // MENUBUTTON_H
