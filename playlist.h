#ifndef PLAYLIST_H
#define PLAYLIST_H

#include <QString>
#include <QList>

// A playlist owns order, not data: items are referenced by id (the metadata
// lives in the single item store and is resolved through
// MediaItemStore::resolve()). Keeping only the ids is what lets many
// playlists share one large collection without duplicating it.
struct Playlist
{
    quint64 id = 0;
    QString name;
    QList<quint64> items;
    int currentIndex = -1;
    // Imported from an external file: the file it came from (empty when the
    // playlist was created by hand) and whether it was modified since —
    // items added, removed or reordered at least once — so the views can
    // mark it (a * in the name) until it is saved back.
    bool imported = false;
    QString sourceFile;
    bool dirty = false;

    bool isValid() const { return id != 0; }

    // The display name: an imported playlist that has been modified since
    // the import (dirty) carries a * marker until it is saved back.
    // Plain playlists are never marked — their content changes are not
    // drift from a file.
    QString displayName() const
    {
        return (imported && dirty) ? name + QLatin1Char('*') : name;
    }
};

#endif // PLAYLIST_H
