#ifndef PLAYLISTLISTWIDGET_H
#define PLAYLISTLISTWIDGET_H

#include "basewidget.h"
#include "playbackmanager.h"

#include <QIcon>

class QEvent;
class QMimeData;
class QMenu;
class QObject;
class QTreeWidgetItem;
class PlaylistListView;
class PlaylistManager;

// Flat list of every playlist the manager knows: one row per playlist with
// a playing marker, the name and the number of tracks. Double-clicking a
// row switches to that playlist and starts playing it.
class PlaylistListWidget : public BaseWidget
{
    Q_OBJECT
public:
    enum Column {
        Playing = 0,
        Name,
        Tracks,
        ColumnCount,
    };

    inline static const QString id = "playlistlist";
    inline static const QString title = "Playlist list";
    inline static const QString tag = widgetTags::library;

    explicit PlaylistListWidget(QWidget *parent = nullptr);
    QString widgetId() const override { return id; }
    QString widgetTitle() const override { return title; }

    PlaylistListView* list() const { return m_list; }

    // Moves a playlist to the given row of the list (a dropped row lands
    // here; the manager persists the new order).
    void moveRowTo(quint64 playlistId, int index);

    // The rows' drops: a row drag reorders the list, an item drag or a
    // file drop appends to the playlist of the row under the pointer.
    // Public to match QObject::eventFilter() (it is public in Qt 6).
    bool eventFilter(QObject *obj, QEvent *event) override;

    // The name the user entered for a new playlist (empty on cancel).
    // Protected virtual as a test seam: QInputDialog is untestable offscreen
    // (the same reason LibraryWidget::chooseFiles is).
    virtual QString askPlaylistName();
    // The name the user entered for the rename (empty on cancel); a test
    // seam, like askPlaylistName.
    virtual QString askRenameName(const QString &current);
    // The path the user chose for the export (empty on cancel); a test
    // seam, like askPlaylistName.
    virtual QString chooseExportPath(const QString &suggested);

private slots:
    void onPlaylistsChanged();
    void onPlaylistChanged(quint64 playlistId);
    void onCurrentPlaylistChanged(quint64 id);
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);
    void onPlayingItemChanged(const MediaItem &item);
    void onStateChanged(PlaybackManager::State state);
    // Right-click on the rows (outside edit mode): "New playlist" and, when
    // a row is selected, "Rename...", "Export to file...", "Save" (an
    // imported playlist only) and "Delete playlist".
    void onContextMenu(const QPoint &pos);

private:
    void rebuild();
    void updatePlayingMarker();
    // The "Rename..."/"Export to file..."/"Save"/"Delete playlist" entries
    // for the selected row.
    void addSelectedActions(QMenu *menu, quint64 selectedId);
    // The list's drop policy (the drag enter): a row drag (a reorder)
    // lands anywhere; an item drag or a file drop needs the row of the
    // target playlist under the pointer.
    bool listDropAccepted(const QMimeData *mime, bool overRow);
    // A drop on the list (pos in the viewport's coordinates): a row drag
    // reorders the list, an item drag or a file drop appends to the
    // playlist of the row under the pointer.
    void onListDrop(const QPoint &pos, const QMimeData *mime);
    // The landing row of a dropped row: the hovered row, with the row's
    // lower half meaning "after it"; the empty area below the last row
    // appends to the end.
    int dropTargetRow(const QPoint &pos) const;
    QTreeWidgetItem* row(quint64 playlistId) const;
    quint64 playlistIdAtRow(int row) const;
    quint64 selectedPlaylistId() const;

    PlaylistListView* m_list;
    PlaylistManager* m_pl;
    PlaybackManager* m_pm;
    QIcon m_playingIcon;
};

#endif // PLAYLISTLISTWIDGET_H
