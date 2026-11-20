#ifndef TABPOSITION_H
#define TABPOSITION_H

#include <QMenu>
#include <QTabWidget>

#include <functional>

// The tab-position machinery shared by the tab widgets (TabsWidget,
// PlaylistsWidget): the name stored in the layout document ("top" is the
// default and is not stored), the name-to-position mapping, and the
// "Tab position" context-menu entry — one checkable action per
// position, the current one checked, each applying its position
// through onChange.
namespace TabPosition
{
QString name(QTabWidget::TabPosition position);
QTabWidget::TabPosition fromName(const QString &name);
QMenu* positionMenu(QMenu* parent, QTabWidget* tabs,
                    const std::function<void(QTabWidget::TabPosition)> &onChange);
}

#endif // TABPOSITION_H
