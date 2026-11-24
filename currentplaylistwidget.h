#ifndef CURRENTPLAYLISTWIDGET_H
#define CURRENTPLAYLISTWIDGET_H

#include "basewidget.h"

#include <QModelIndex>
#include <QPoint>

#include "mediaitem.h"

class MediaItemStore;
class MediaListModel;
class MediaListView;
class PlaylistManager;
class PlaybackManager;
class QActionGroup;
class QMenu;
class QToolButton;
class UrlWizard;

class CurrentPlaylistWidget : public BaseWidget
{
    Q_OBJECT
public:
    inline static const QString id = "playlist";
    inline static const QString title = "Current playlist";
    inline static const QString tag = widgetTags::library;

    explicit CurrentPlaylistWidget(QWidget *parent = nullptr);
    QString widgetId() const override { return id; }
    QString widgetTitle() const override { return title; }

    WidgetConfiguration storeWidget() override;
    void restoreWidget(WidgetConfiguration& config) override;

    QMenu* widgetContextMenu() override;

    MediaListView* listView() const { return m_view; }

protected:
    // Asks for the files to add (the toolbar's "Add files..."). A test
    // seam (QFileDialog is untestable offscreen), like the library
    // widget's choosers.
    virtual QStringList chooseFiles();
    // The name the user entered for a new playlist (empty on cancel);
    // the toolbar's playlists menu's "New playlist". A test seam, like
    // PlaylistListWidget::askPlaylistName.
    virtual QString askPlaylistName();
    // The name the user entered for the current playlist's rename
    // (empty on cancel); a test seam, like askPlaylistName.
    virtual QString askRenameName(const QString &current);
    // The path the user chose for the current playlist's export (empty
    // on cancel); a test seam, like askPlaylistName.
    virtual QString chooseExportPath(const QString &suggested);

    // Opens the metadata editor for the given item ids. A test seam (the
    // dialog is modal, untestable offscreen).
    virtual void editMetadata(const QList<quint64> &ids);
    // Opens the single-item tag editor for the given item. A test seam,
    // like editMetadata.
    virtual void editSingleMetadata(quint64 id);

private slots:
    void onItemsDropped(const QList<quint64> &ids, const QList<int> &rows,
                        const QString &origin, int index);
    // A drop of local files from the file manager: media files are
    // imported, external playlist files are flattened into their entries,
    // at the drop position.
    void onFilesDropped(const QList<QUrl> &urls, int index);
    // The toolbar's "+" menu: the chosen files go through the same import
    // as a file drop (playlist files flattened, the block appended), the
    // URLs through the import wizard (the same flow as the library's).
    void onAddFiles();
    void onAddUrl();
    void onRemoveSelected();
    // The toolbar's playlists menu is rebuilt on every show (the playlist
    // set is dynamic): the entries, and the verbs of the current playlist.
    void onPlaylistsMenuAboutToShow();
    // Right-click on the rows (outside edit mode): remove the selection.
    void onViewContextMenu(const QPoint &pos);
    void onPlaylistChanged(quint64 playlistId);
    void onCurrentIndexChanged(int index);
    void onItemDoubleClicked(const QModelIndex &index);
    void onPlayingItemChanged(const MediaItem &item);
    void onItemChanged(const MediaItem &item);

private:
    void refresh();
    void selectCurrentRow();
    // The status column's marker: resolving / playing / paused / a failed
    // play, on this (current) playlist's current row.
    void updateStatusMarker();

    MediaListModel* m_listModel;
    MediaListView* m_view;
    PlaylistManager* m_pl;
    PlaybackManager* m_pm;
    MediaItemStore* m_store;
    // The toolbar's playlists button (the switcher and the management
    // verbs) and its menu (rebuilt on every show), and the entries'
    // exclusive group (the menu's: the rebuilt actions join and leave it).
    QToolButton* m_playlistsButton;
    QMenu* m_playlistsMenu;
    QActionGroup* m_playlistsGroup;
    // The "Add by URL..." wizard's single-instance plumbing (shared with
    // the other widgets that offer it).
    UrlWizard* m_wizard;
};

#endif // CURRENTPLAYLISTWIDGET_H
