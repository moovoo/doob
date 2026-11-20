#ifndef ROWMENU_H
#define ROWMENU_H

#include <QList>

class QMenu;
class QObject;

// The standard row actions the media views' context menus share:
// "Play now", "Play next", "Add to library", "Copy URL". They are
// appended to the menu after a separator. The selection is captured as
// values (the menu is modal, so the selection cannot change between
// popup and trigger).
//
// playlistId names the rows' playlist. 0 is the library view, whose
// rows are not playlist rows: "Play now" appends to the current
// playlist (like a double-click) and plays the first appended row,
// "Play next" inserts into the current playlist after its current row,
// and "Add to library" is not offered (the rows are library items).
void addRowActions(QMenu *menu, QObject *context,
                   const QList<quint64> &ids, const QList<int> &rows,
                   quint64 playlistId);

#endif // ROWMENU_H
