#include "tabposition.h"

#include <QAction>
#include <QActionGroup>
#include <QObject>

#include <array>

namespace TabPosition
{
namespace {
// The positions in menu order (the labels match).
const QList<QTabWidget::TabPosition> kPositions = {
    QTabWidget::North,
    QTabWidget::South,
    QTabWidget::West,
    QTabWidget::East,
};
const std::array<const char*, 4> kLabels = { "Top", "Bottom", "Left", "Right" };
}

QString name(QTabWidget::TabPosition position)
{
    switch (position) {
    case QTabWidget::South:
        return "bottom";
    case QTabWidget::West:
        return "left";
    case QTabWidget::East:
        return "right";
    default:
        return "top";
    }
}

QTabWidget::TabPosition fromName(const QString &name)
{
    if (name == "bottom")
        return QTabWidget::South;
    if (name == "left")
        return QTabWidget::West;
    if (name == "right")
        return QTabWidget::East;
    return QTabWidget::North;
}

QMenu* positionMenu(QMenu* parent, QTabWidget* tabs,
                    const std::function<void(QTabWidget::TabPosition)> &onChange)
{
    QMenu* position = parent->addMenu("Tab position");
    QList<QAction*> options;
    for (int i = 0; i < kPositions.size(); ++i) {
        const QTabWidget::TabPosition p = kPositions.at(i);
        QAction* action = position->addAction(QString::fromLatin1(kLabels.at(i)));
        action->setCheckable(true);
        action->setChecked(tabs->tabPosition() == p);
        QObject::connect(action, &QAction::triggered, [onChange, p] { onChange(p); });
        options.append(action);
    }
    // An exclusive group: the positions are options, not toggles — Qt
    // draws them as radio buttons and unchecks the previous one on a
    // choice (as do the widgets' own option menus, e.g. the
    // oscillogram's).
    QActionGroup* group = new QActionGroup(position);
    group->setExclusive(true);
    for (QAction* action : options)
        group->addAction(action);
    return position;
}
}
