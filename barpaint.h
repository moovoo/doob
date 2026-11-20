#ifndef BARPAINT_H
#define BARPAINT_H

#include <QColor>
#include <QList>
#include <QRect>
#include <QSize>
#include <QString>

#include "styleproperty.h"

class QPainter;

// The bar rendering shared by the visualization's bar displays (the
// peak meter's, the spectrum's): the fill direction and, for one bar's
// slot, the solid fill and the segmented rendering. The direction is a
// shared enum so both widgets (and the edit menus, and the tests) use
// one type.
namespace barpaint {

// The direction the bars fill: Up/Down draw vertical bars (from the
// bottom / from the top), LeftToRight/RightToLeft draw horizontal bars
// (from the left / from the right). Up is the default.
enum class Orientation { Up, Down, LeftToRight, RightToLeft };

// The display label of a direction (the edit menus' entries).
QString orientationName(Orientation orientation);

// True for the vertical directions (Up/Down).
bool isVertical(Orientation orientation);

// One bar's slot: `bars` bars stacked along the axis orthogonal to the
// fill (vertical bars side by side along x, horizontal bars one over
// the other along y), a margin between and around them — `marginPx`
// explicit, a fixed share of the across-axis (at least 2 px) when it
// is 0. An invalid rect when the bars do not fit (the across-axis too
// small for the margins) or `index` is out of range.
QRect barSlot(int index, int bars, const QSize &size, Orientation orientation,
              int marginPx = 0);

// The "x-" properties the bar rendering reads (the margin, the track
// color, the segment count): the style sheet help's docs, `owner`
// (the reading widget's title) supplied by the caller — the two bar
// widgets share the rendering, so they share the docs.
QList<StyleProperty> styleProperties(const QString &owner);

// The solid rendering of a bar's slot: the track color, then the filled
// part (level, 0..1, from the fill origin toward the far edge) in the
// bar's color.
void paintSolid(QPainter &p, const QRect &slot, float level,
                Orientation orientation, const QColor &track, const QColor &bar);

// The segmented rendering of a bar's slot: `segments` discrete blocks
// along the fill axis, a 1px gap between them (when the blocks are
// wide enough for one); the blocks the level reaches are lit, the rest
// carry the track color.
void paintSegments(QPainter &p, const QRect &slot, float level, int segments,
                   Orientation orientation, const QColor &track, const QColor &bar);

}  // namespace barpaint

#endif  // BARPAINT_H
