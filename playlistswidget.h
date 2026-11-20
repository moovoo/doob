#ifndef PLAYLISTSWIDGET_H
#define PLAYLISTSWIDGET_H

#include "basewidget.h"

#include <QHash>
#include <QIcon>
#include <QList>
#include <QModelIndex>
#include <QPoint>

#include "mediaitem.h"
#include "playbackmanager.h"

class QDragEnterEvent;
class QEvent;
class QMimeData;
class QObject;
class QTabWidget;
class QTimer;
class MediaItemStore;
class MediaListView;
class PlaylistManager;
class UrlWizard;

class PlaylistsWidget : public BaseWidget
{
    Q_OBJECT
public:
    inline static const QString id = "playlists";
    inline static const QString title = "Playlist tabs";
    inline static const QString tag = widgetTags::library;

    explicit PlaylistsWidget(QWidget *parent = nullptr);
    QString widgetId() const override { return id; }
    QString widgetTitle() const override { return title; }

    WidgetConfiguration storeWidget() override;
    void restoreWidget(WidgetConfiguration& config) override;

    QMenu* widgetContextMenu() override;

    QTabWidget* tabWidget() const { return m_tabs; }

    // The tab the pointer is over (the hover switch: after a short delay
    // on a non-current tab, that tab is shown; -1 clears it, as when the
    // pointer leaves the strip). Public for the synthetic hover tests
    // (this Qt does not route synthetic mouse moves to widgets).
    void hoverTab(int index);

    // Public to match QObject::eventFilter() (it is public in Qt 6).
    bool eventFilter(QObject *obj, QEvent *event) override;

protected:
    // Opens the metadata editor for the given item ids. A test seam (the
    // dialog is modal, untestable offscreen).
    virtual void editMetadata(const QList<quint64> &ids);
    // Opens the single-item tag editor for the given item. A test seam,
    // like editMetadata.
    virtual void editSingleMetadata(quint64 id);
    // Asks for a new playlist name (the tab context menu's rename); empty
    // when the user bailed. A test seam (QInputDialog is untestable
    // offscreen).
    virtual QString askPlaylistName(const QString &current);
    // Asks for the export file's path (the tab context menu's export);
    // empty when the user bailed. A test seam, like askPlaylistName.
    virtual QString chooseExportPath(const QString &suggested);
    // Asks for the files to add (the toolbar's "Add files...", added to
    // the displayed tab). A test seam (QFileDialog is untestable
    // offscreen), like the library widget's choosers.
    virtual QStringList chooseFiles();

private slots:
    void onPlaylistsChanged();
    void onPlaylistChanged(quint64 playlistId);
    void onItemDoubleClicked(const QModelIndex &index);
    void onPlayingItemChanged(const MediaItem &item);
    void onStateChanged(PlaybackManager::State state);
    void onCurrentIndexChanged(int index);
    void onItemsDropped(const QList<quint64> &ids, const QList<int> &rows,
                        const QString &origin, int index);
    // A drop of local files from the file manager on a tab: media files
    // are imported, external playlist files are flattened into their
    // entries, into that tab's playlist at the drop position.
    void onFilesDropped(const QList<QUrl> &urls, int index);
    // The toolbar's "+" menu: the chosen files go through the same import
    // as a file drop (playlist files flattened, the block appended), the
    // URLs through the import wizard — all into the displayed tab's
    // playlist; "−" removes the displayed tab's selection.
    void onAddFiles();
    void onAddUrl();
    void onRemoveSelected();
    void onAddPlaylistClicked();
    void onItemChanged(const MediaItem &item);
    // Right-click on a tab's rows (outside edit mode): remove the
    // selection from that tab's playlist.
    void onViewContextMenu(const QPoint &pos);
    // Right-click on the tab strip (outside edit mode): rename the tab's
    // playlist, export it to a file, and (for an imported playlist) save
    // it back to the file it came from.
    void onTabContextMenu(const QPoint &pos);

private:
    void rebuildTabs();
    void refreshTab(quint64 playlistId);
    void closeTabAt(int index);
    // A drop of local files on the tab strip: over a tab (tab >= 0) the
    // files (media, and playlist files flattened) are appended to that
    // tab's playlist; over the empty strip (tab < 0) each external
    // playlist file becomes a new standalone playlist.
    // The strip's DragEnter/Drop events (the eventFilter's drop half):
    // over a tab the drag — media files, external playlist files
    // (flattened into their entries) or items dragged from a media view —
    // is appended to that tab's playlist; over the empty strip each
    // external playlist file becomes a new standalone playlist (files
    // other than playlist files, and item drags, are refused there).
    bool onTabStripDropEvent(QObject *obj, QEvent *event);
    bool onTabStripDragEnter(QDragEnterEvent *de, bool overStrip);
    void onTabStripDrop(const QMimeData *mime, int tab);
    // The status column's marker (resolving / playing / paused / a failed
    // play), on the tab that is the current playlist — its current row.
    void updateStatusMarker(MediaListView *view);
    void updatePlayingTabIcon();

    QTabWidget* m_tabs;
    PlaylistManager* m_pl;
    PlaybackManager* m_pm;
    MediaItemStore* m_store;
    QHash<quint64, MediaListView*> m_tabViews;
    QIcon m_playingIcon;
    // The "Add by URL..." wizard's single-instance plumbing (shared with
    // the other media widgets); its sink feeds the displayed tab.
    UrlWizard* m_wizard;
    // The tab the pointer is over (the hover switch: after a short delay
    // on a non-current tab, that tab is shown), and the timer for it.
    QTimer* m_hoverTimer;
    int m_hoverTab = -1;
    // Set by tabMoved so the playlistsChanged that movePlaylist emits in
    // the same call is consumed without a tab rebuild (the bar already
    // reordered the tabs; a rebuild would destroy the dragged tab and
    // abort a live drag). Consumed by onPlaylistsChanged.
    bool m_rebuildTabsSuppressed = false;
    // The follow playback: the widget's state (the edit-mode toggle),
    // applied to every tab view (a new tab inherits it on a rebuild).
    bool m_followPlayback = false;
};

#endif // PLAYLISTSWIDGET_H
