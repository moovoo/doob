#ifndef PLAYLISTTOOLBAR_H
#define PLAYLISTTOOLBAR_H

#include <QMenu>
#include <QToolButton>
#include <QString>
#include <QStringList>

#include <functional>

class QToolBar;
class QAction;
class QWidget;

// The toolbar button set shared by the media views' strips: a "+" add
// menu ("Add files..." — a file chooser — and "Add by URL..." — the
// import wizard) and a "−" button that removes the current selection.
// The helper creates and parents everything (buttons to the toolbar
// strip, the menu to the add button, the actions to the menu) with
// stable object names — wiring the actions to the widget's slots stays
// with the widget, since only it knows which collection an action feeds
// (the current playlist, the displayed tab, the library), and any
// extras (the library's "Add folder..." entry, view buttons, busy
// indicator, search box) the widget appends itself.
struct PlaylistToolbarButtons
{
    QToolButton *addButton = nullptr;
    QToolButton *removeButton = nullptr;
    QAction *addFilesAction = nullptr;
    QAction *addUrlAction = nullptr;
    QMenu *addMenu = nullptr;
};

// `target` names the collection the actions act on, in the tooltips
// ("the current playlist", "the displayed playlist", "the library").
PlaylistToolbarButtons createPlaylistToolbar(QToolBar *toolbar,
                                             const QString &target);

// The expanding spacer that keeps a toolbar's leading buttons
// left-aligned — the caller places it (the strips that end right after
// the buttons, the library's before its search box).
void addToolbarSpacer(QToolBar *toolbar);

// The QFileDialog filter the "Add files..." chooser shows: the
// supported audio extensions, then any file (playlist files included —
// the add handlers flatten them).
QString audioFileChooserFilter();

// The "Add files..." chooser (the filter above), as the widgets'
// protected chooseFiles() seams show it.
QStringList chooseAudioFiles(QWidget *parent);

// The "Export playlist..." save dialog: M3U/PLS by default, any file
// allowed (an empty path on cancel).
QString choosePlaylistExportPath(QWidget *parent, const QString &suggested);

// The rename prompt: the current name prefilled, an empty string on
// cancel.
QString askPlaylistRename(QWidget *parent, const QString &current);

// The playlist-level verbs — "Rename...", "Export to file..." and
// "Save" (enabled for an imported playlist with a source file only) —
// appended to `menu` for the named playlist. The surfaces that offer
// them (the tabs' right-click menu, the playlist list's row menu, the
// toolbar's playlists menu) share the entries. `askName` and
// `chooseExport` are the surface's prompt seams (the dialogs are
// untestable offscreen); the playlist is re-read at trigger time, so a
// stale id is a no-op.
void addPlaylistVerbActions(QMenu *menu, quint64 playlistId,
                            const std::function<QString(const QString &)> &askName,
                            const std::function<QString(const QString &)> &chooseExport);

#endif // PLAYLISTTOOLBAR_H
