#include "widgetcontainer.h"
#include "widgetcontaineroverlay.h"

#include <QPainter>

WidgetContainerOverlay::WidgetContainerOverlay(WidgetContainer *parent)
    : QWidget{parent}
{
    m_parentContainer = parent;

    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);

    m_grayed = false;
}

void WidgetContainerOverlay::paintEvent(QPaintEvent *event)
{
    if (m_grayed)
    {
        // The gray-out follows the theme: the palette's shadow color
        // (dark in light and dark styles alike), made translucent —
        // a hard-coded black fought styled and dark layouts.
        QColor veil = palette().color(QPalette::Shadow);
        veil.setAlpha(100);

        QPainter painter(this);
        painter.setBrush(QBrush(veil));

        painter.setPen(QPen(Qt::NoPen));
        painter.drawRect(rect());
    }
}

void WidgetContainerOverlay::grayed(bool val)
{
    m_grayed = val;
    update();
}
