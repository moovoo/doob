#ifndef LAYOUTSTYLEDIALOG_H
#define LAYOUTSTYLEDIALOG_H

#include <QDialog>

class QTabWidget;
class QPushButton;
class QTreeWidget;
class UiState;

// The layout/style picker: two panes (one per store), each listing the
// read-only presets and the user's files — the user entries carry
// their provenance ("(from preset X)") and their "(modified)" marker
// (content diverged from the origin preset).
//
// FirstRun mode (the first start, modal, above the empty window): a
// selection previews it live (no saves); "Use" commits the selection
// (a selected layout preset is taken as a copy — forked into the
// user's directory — a style preset is used as-is, its copy appearing
// only when it is edited; a user entry as-is) and is grayed out while
// the layout pane has no selection (nothing to use — the style is
// optional, a missing one starts a fresh "default"); "Start empty"
// creates fresh empty layout and style files under the name "default"
// (they become the currents) and forces edit mode on (the from-scratch
// start). There is no Cancel: the start is resolved one way or the
// other, so after the dialog the currents are files and the picker is
// not repeated.
//
// Picker mode (the menu's "Manage layouts" / "Manage styles",
// non-modal, single instance over the LayoutManager): a selection is
// a live switch (the previous entry is saved by the store; a layout
// preset is switched as a copy, a style preset as-is), and the
// per-pane buttons manage the user's entries (New — a fresh empty
// file under the prompted name, the current saved first; Copy to —
// the live state forked under the prompted name; Delete — confirms;
// Import — the manager's file dialog, the import lands in the
// user's directory under the picked file's name and becomes current;
// Export — the manager's file dialog, the live entry under the
// picked file, its provenance carried; Delete and Export are enabled
// only for a user-owned selection, Import always).
class LayoutStyleDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { FirstRun, Picker };
    // Which store the dialog was opened for (the menu's "Manage
    // layouts" / "Manage styles"): the dialog opens on the matching
    // tab (a re-trigger of an open dialog switches to it). The panes
    // are the stores — the picker's per-pane handlers below are the
    // same code over the pane's store.
    enum class Store { Layout, Style };

    explicit LayoutStyleDialog(Mode mode, QWidget *parent);
    // Switches to the tab of `store` (Picker mode; FirstRun has no
    // tabs). Idempotent — a no-op when already on the tab.
    void showTab(Store store);

protected:
    // Test seams: the modal sub-dialogs (QInputDialog/QMessageBox are
    // untestable offscreen). promptName returns an empty name for a
    // cancel.
    virtual QString promptName(const QString &caption);
    virtual bool confirmDelete(const QString &title);

private slots:
    void onUseClicked();
    void onStartEmptyClicked();

private:
    // Rebuilds both trees, keeping the selection on the current
    // layout/style.
    void populate();
    // The "Use" button follows the selection: enabled for a layout
    // selection (the style is optional — without one, "Use" starts a
    // fresh "default" style; the dialog with no entries at all stays
    // on "Start empty").
    void updateUseState();
    // The delete and export buttons follow the selection: enabled for
    // a user-owned entry, refused for the presets (import is always
    // enabled).
    void updateManageState();
    // The selected entry's name ("" when nothing is selected — the
    // group headers are not selectable, so any selection is an entry).
    QString selectedName(QTreeWidget *tree) const;
    void selectName(QTreeWidget *tree, const QString &name);
    // The picker's per-pane handlers: the two panes are the same code
    // over the pane's store (the pane's operations — the listing, the
    // current, the user-entry operations, the file dialogs, the
    // selection's effect — live with the pane's helpers in the cpp).
    void onNewClicked(Store pane);
    void onCopyToClicked(Store pane);
    void onDeleteClicked(Store pane);
    void onImportClicked(Store pane);
    void onExportClicked(Store pane);
    void onSelectionChanged(Store pane);

    Mode m_mode;
    QTabWidget* m_tabs = nullptr;
    QTreeWidget* m_layouts;
    QTreeWidget* m_styles;
    QPushButton* m_deleteLayout = nullptr;
    QPushButton* m_deleteStyle = nullptr;
    // The picker's per-pane "Export" buttons (enabled only for a
    // user-owned selection, like the delete buttons).
    QPushButton* m_exportLayout = nullptr;
    QPushButton* m_exportStyle = nullptr;
    // The first-run "Use" button (Picker mode has none — its
    // selections switch live).
    QPushButton* m_use = nullptr;
    UiState* m_state = nullptr;
};

#endif // LAYOUTSTYLEDIALOG_H
