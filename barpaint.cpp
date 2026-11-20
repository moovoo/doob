#include "barpaint.h"

#include <QPainter>

namespace barpaint {

QString orientationName(Orientation orientation)
{
    if (orientation == Orientation::Down)
        return QStringLiteral("Fill down");
    if (orientation == Orientation::LeftToRight)
        return QStringLiteral("Fill left to right");
    if (orientation == Orientation::RightToLeft)
        return QStringLiteral("Fill right to left");
    return QStringLiteral("Fill up");
}

bool isVertical(Orientation orientation)
{
    return orientation == Orientation::Up
        || orientation == Orientation::Down;
}

QRect barSlot(int index, int bars, const QSize &size,
              Orientation orientation, int marginPx)
{
    if (bars <= 0 || index < 0 || index >= bars)
        return {};
    const int w = size.width();
    const int h = size.height();
    const bool vertical = isVertical(orientation);
    const int margin = marginPx > 0
        ? marginPx : qMax(2, (vertical ? w : h) / 40);
    const int across = vertical ? w : h;
    const int barThick = (across - ((bars + 1) * margin)) / bars;
    if (barThick <= 0)
        return {};
    const int inset = 2 * margin;
    const int offset = margin + (index * (barThick + margin));
    return vertical
        ? QRect(offset, 0, barThick, h)
        : QRect(margin, offset, w - inset, barThick);
}

void paintSolid(QPainter &p, const QRect &slot, float level,
                Orientation orientation, const QColor &track, const QColor &bar)
{
    p.fillRect(slot, track);
    const float spanF =
        static_cast<float>(isVertical(orientation) ? slot.height() : slot.width());
    const int fill = qRound(qBound(0.0F, level, 1.0F) * spanF);
    QRect filled = slot;
    if (isVertical(orientation)) {
        if (orientation == Orientation::Up)
            filled = QRect(slot.x(), slot.y() + slot.height() - fill,
                           slot.width(), fill);
        else  // Down
            filled = QRect(slot.x(), slot.y(), slot.width(), fill);
    } else if (orientation == Orientation::LeftToRight) {
        filled = QRect(slot.x(), slot.y(), fill, slot.height());
    } else {  // RightToLeft
        filled = QRect(slot.x() + slot.width() - fill, slot.y(), fill,
                       slot.height());
    }
    p.fillRect(filled, bar);
}

void paintSegments(QPainter &p, const QRect &slot, float level, int segments,
                   Orientation orientation, const QColor &track,
                   const QColor &bar)
{
    // The slot is split into `segments` discrete blocks along the fill
    // axis, a 1px gap between them (when the blocks are wide enough
    // for one); the blocks the level reaches are lit, the rest carry
    // the track color.
    const bool vertical = isVertical(orientation);
    const int span = vertical ? slot.height() : slot.width();
    const float segSize =
        static_cast<float>(span) / static_cast<float>(qMax(1, segments));
    const int gap = segSize >= 3 ? 1 : 0;
    const int lit = qRound(qBound(0.0F, level, 1.0F)
                           * static_cast<float>(qMax(1, segments)));
    for (int i = 0; i < qMax(1, segments); ++i) {
        const int t0 = qRound(static_cast<float>(i) * segSize);
        const int t1 = qRound((static_cast<float>(i) + 1.0F) * segSize);
        // The gap sits on the block's origin side (the block at the
        // fill origin stays flush with it).
        const int inset = (i == 0) ? 0 : gap;
        const int len = qMax(1, t1 - t0 - inset);
        // In fill-origin coordinates the block runs [t0 + inset, t1);
        // Up and RightToLeft fill toward the far edge, so measure the
        // block's start from there.
        const int pos = (orientation == Orientation::Up
                         || orientation == Orientation::RightToLeft)
            ? span - t1 : t0 + inset;
        const QRect seg = vertical
            ? QRect(slot.x(), slot.y() + pos, slot.width(), len)
            : QRect(slot.x() + pos, slot.y(), len, slot.height());
        p.fillRect(seg, i < lit ? bar : track);
    }
}

QList<StyleProperty> styleProperties(const QString &owner)
{
    return {
        {
            QStringLiteral("x-bar-margin"),
            owner,
            QStringLiteral("The space between the bars and around the "
                           "first and last one, in pixels."),
            QStringLiteral("auto"),
            QStringLiteral("px"),
            1,
            100,
        },
        {
            QStringLiteral("x-track-color"),
            owner,
            QStringLiteral("The color of a bar's unfilled part. The "
                           "default is the widget's background lightened "
                           "by 20%."),
            QStringLiteral("auto"),
            QStringLiteral("color"),
            0,
            0,
        },
        {
            QStringLiteral("x-segments"),
            owner,
            QStringLiteral("How many discrete blocks the Segments "
                           "display style splits each bar into along the "
                           "fill axis."),
            QStringLiteral("16"),
            QStringLiteral(""),
            2,
            64,
        },
    };
}

}  // namespace barpaint
