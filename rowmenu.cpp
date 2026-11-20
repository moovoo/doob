#include "rowmenu.h"

#include "librarymanager.h"
#include "mediaitemstore.h"
#include "playbackmanager.h"
#include "playlistmanager.h"
#include "sourcemanager.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QObject>

#include <QDebug>

void addRowActions(QMenu *menu, QObject *context,
                   const QList<quint64> &ids, const QList<int> &rows,
                   quint64 playlistId)
{
    PlaylistManager *pl = PlaylistManager::getInstance();
    PlaybackManager *pm = PlaybackManager::getInstance();

    menu->addSeparator();

    // "Play now": a playlist row is played where it is; a library row is
    // appended to the current playlist first (like a double-click) and
    // the first appended row is played (the append lands at the end, so
    // that row is the playlist's old size).
    QAction *playNow = menu->addAction("Play now");
    playNow->setEnabled(!ids.isEmpty());
    QObject::connect(playNow, &QAction::triggered, context, [pl, pm, ids, rows,
                                                    playlistId] {
        if (playlistId != 0 && !rows.isEmpty()) {
            pm->playFromPlaylist(playlistId, rows.first());
            return;
        }
        QList<quint64> added = ids;
        added.removeAll(0);
        if (added.isEmpty())
            return;
        const int firstNew = static_cast<int>(
            pl->playlist(pl->currentPlaylistId()).items.size());
        pl->addItems(added);
        pm->playFromPlaylist(pl->currentPlaylistId(), firstNew);
    });

    // "Play next": the rows land after the target playlist's current
    // row (at the top when it has none) without starting playback.
    QAction *playNext = menu->addAction("Play next");
    playNext->setEnabled(!ids.isEmpty());
    QObject::connect(playNext, &QAction::triggered, context, [pl, ids, playlistId] {
        const quint64 target =
            playlistId != 0 ? playlistId : pl->currentPlaylistId();
        const Playlist targetPl = pl->playlist(target);
        const int at = targetPl.currentIndex < 0
                           ? 0
                           : targetPl.currentIndex + 1;
        pl->insertItemsInto(target, at, ids);
    });

    // "Add to library": playlist rows only (the library's rows are
    // already library items).
    if (playlistId != 0) {
        QAction *addToLibrary = menu->addAction("Add to library");
        addToLibrary->setEnabled(!ids.isEmpty());
        QObject::connect(addToLibrary, &QAction::triggered, context, [ids] {
            LibraryManager::getInstance()->addItems(ids);
        });
    }

    // "Copy URL": the owning provider's shareable form (a vanished
    // provider degrades to the source URL), one line per row.
    QAction *copyUrl = menu->addAction("Copy URL");
    copyUrl->setEnabled(!ids.isEmpty());
    QObject::connect(copyUrl, &QAction::triggered, context, [ids] {
        QStringList lines;
        for (quint64 id : ids) {
            const MediaItem item = MediaItemStore::resolve(id);
            if (!item.isValid())
                continue;
            SourceProvider *provider =
                SourceManager::getInstance()->providerById(item.provider);
            lines << (provider != nullptr ? provider->shareableUrl(item)
                                          : item.source.toString());
        }
        QApplication::clipboard()->setText(lines.join(QStringLiteral("\n")));
    });
}
