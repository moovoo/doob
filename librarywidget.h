#ifndef LIBRARYWIDGET_H
#define LIBRARYWIDGET_H

#include "basewidget.h"
#include "mediaitem.h"

#include <QList>
#include <QModelIndex>
#include <QPoint>

class MediaGroupView;
class MediaListModel;
class MediaListView;
class LibraryManager;
class PlaylistManager;
class QLineEdit;
class QButtonGroup;
class QStackedWidget;
class QToolButton;
class UrlWizard;
class QWidget;

class LibraryWidget : public BaseWidget
{
    Q_OBJECT
public:
    inline static const QString id = "library";
    inline static const QString title = "Library";
    inline static const QString tag = widgetTags::library;

    // How the rows are presented: the flat table (List, the default) or
    // a group tree (artists → albums → tracks, albums → tracks,
    // (artist, album) tuples → tracks). The table and the tree are
    // separate views over one shared MediaListModel; the view mode is the
    // same state the toolbar buttons and the edit-mode menu switch.
    enum class ViewMode { List = 0, ByArtist, ByAlbum, ByArtistAlbum };

    explicit LibraryWidget(QWidget *parent = nullptr);
    QString widgetId() const override { return id; }
    QString widgetTitle() const override { return title; }

    WidgetConfiguration storeWidget() override;
    void restoreWidget(WidgetConfiguration& config) override;

    MediaListView* listView() const { return m_listView; }
    MediaGroupView* groupView() const { return m_groupView; }
    // The shared source model (both views resolve these ids).
    MediaListModel* listModel() const { return m_listModel; }
    ViewMode viewMode() const;

    // The edit-mode context menu: the base's entries plus the view mode.
    QMenu* widgetContextMenu() override;

protected:
    virtual QStringList chooseFiles();
    virtual QString chooseDirectory();
    // Opens the metadata editor for the given item ids. A test seam (the
    // dialog is modal, untestable offscreen), like the choosers above.
    virtual void editMetadata(const QList<quint64> &ids);
    // Opens the single-item tag editor for the given item. A test seam,
    // like editMetadata.
    virtual void editSingleMetadata(quint64 id);

private slots:
    void onAddFiles();
    void onAddFolder();
    void onAddUrl();
    // A drop of local files from the file manager: media files are
    // imported, external playlist files are flattened into their entries.
    void onFilesDropped(const QList<QUrl> &urls, int index);
    // A drop of non-local URLs from outside the app (a browser): the
    // import wizard, opened pre-filled with the dropped URLs.
    void onUrlsDropped(const QList<QUrl> &urls, int index);
    void onRemoveSelected();
    void onItemDoubleClicked(const QModelIndex &index);
    void onItemChanged(const MediaItem &item);
    // Right-click on the rows (outside edit mode): edit the selection's
    // metadata.
    void onViewContextMenu(const QPoint &pos);

private:
    void refresh();
    // Disables the operation buttons and shows the spinner while a
    // background library operation (an import, a scan, a sync) runs.
    void setOperationBusy(bool busy);
    // The search text applied to the active presentation (the filter
    // proxy in List mode, the group model's filter in the tree modes).
    void applySearch();
    // Switches the presentation (the stacked widget's page; the toolbar
    // buttons and the edit-mode menu reflect it from here).
    void setViewMode(ViewMode mode);
    void syncViewButtons();

    MediaListModel* m_listModel;
    MediaListView* m_listView;
    MediaGroupView* m_groupView;
    QStackedWidget* m_stack;
    QLineEdit* m_search;
    QToolButton* m_addButton;
    QToolButton* m_removeButton;
    QButtonGroup* m_viewButtons;
    QWidget* m_spinner;
    LibraryManager* m_manager;
    PlaylistManager* m_pl;
    // The "Add by URL" wizard's single-instance plumbing (shared by the
    // widgets that offer it; it parents the wizard to this widget).
    UrlWizard* m_wizard;
};

#endif // LIBRARYWIDGET_H
