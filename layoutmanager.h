#ifndef LAYOUTMANAGER_H
#define LAYOUTMANAGER_H


#include <QJsonDocument>
#include <QMenu>
#include <QObject>
#include <QPointer>
#include <QList>

#include "entrystore.h"
#include "widgetconfiguration.h"
#include "widgetregistry.h"

class MainWindow;
class WidgetContainer;
class BaseWidget;
class WindowsDialog;
class LayoutStyleDialog;

// The layout store: the user's layouts (layouts/<name>.json), the
// read-only presets, and the active layout — parallel to the style
// store (StyleManager). The active layout is the "layout.current"
// setting: the name of a user file (an unset
// current means no layout — the first start before the picker has
// been answered, or a start with everything deleted). A preset is a
// template that is never current itself: selecting a preset takes a
// copy (forkLayout), the user's "<name>.modified.json" (adopted when
// it already exists), and the copy becomes current. A layout's
// provenance rides in its document as the "origin" root key
// ("preset:<name>"), set by a fork and shown as "(from preset X)".
//
// Saving the current layout (a switch, the app's exit, a new layout)
// writes the live state into the current layout's file — a plain
// write for a user file, a fork for a stale preset current (a
// reference left by an older build), a no-op for an unset current
// (there is no file to write to).
class LayoutManager : public QObject
{
    Q_OBJECT
public:
    // The content operations of the layouts' store (the store's
    // ContentOps, entrystore.h): the entry's content is the layout
    // document (a QJsonObject), the origin rides in it as the "origin"
    // root key, the title in the "title" key.
    struct LayoutContent {
        using Value = QJsonObject;
        // The document of the file at `path` ({} when unreadable).
        static Value read(const QString &path);
        // `content` + the "origin" key when `origin` is non-empty, as
        // the file's bytes.
        static QByteArray write(const Value &content, const QString &origin);
        // The "origin" key a file records ("" when it has none).
        static QString origin(const QString &path);
        // The "title" key a document carries ("" when it has none).
        static QString title(const Value &content);
        // The unmodified check (the "modified" marker, the stale-
        // preset save's no-op): byte-identical after dropping the
        // display keys ("title", "origin") the live document does not
        // carry.
        static bool equivalent(const Value &a, const Value &b);
        // A document's live form (the provenance belongs to the file,
        // not to the live state — the identity for documents).
        static Value live(const Value &content);
        // A fresh empty document (the (empty) container, the version).
        static Value empty();
    };

    using EntryStore = ::EntryStore<LayoutContent>;

    // A layout in the listing: its name (the file's stem), the title
    // the menus and dialogs show, and whether it is read-only (a
    // preset).
    using LayoutInfo = EntryStore::Info;

    static LayoutManager* getInstance(QObject *parent = nullptr);
    void show();

    MainWindow* window() const { return m_window; }

    // The container's edit-mode menu (popped at the cursor). The
    // widget's own submenu is titled "Edit {widget type name}" at the
    // insertion point (the base's widgetContextMenu keeps the bare
    // title).
    void popupContainerMenu(WidgetContainer* container);

    bool isEditMode() const { return m_editMode; }

    // The edit-mode right-click flip-flop (7d): off — the next right-
    // click shows the container's edit menu, on — the next is a pass-
    // through to the widget's own native context menu (the right-button
    // release and the synthesized context-menu event go through, the
    // widget's Qt::CustomContextMenu slot shows its menu as in normal
    // mode). The state is driven by the MENUS, not the clicks: the
    // edit menu's aboutToHide turns it on (native next), a pass-
    // through turns it off (edit next) — the native menu's
    // disappearance is not observed, the flip back happens the moment
    // the pass-through is taken. Clicks only follow the state (the
    // container's event filter records the decision on the press and
    // acts on it at the release / context-menu event), so it is
    // immune to the platform's event order (a
    // QStyleHints::contextMenuTrigger() == Press platform synthesizes
    // the context-menu event from the press, before the release, and a
    // press-triggered native menu grabs the mouse and eats the release
    // — a click-driven toggle would stick). Reset when edit mode is
    // toggled (a stale "on" must not eat the first right-click after
    // re-entering edit mode). Per session, never persisted.
    bool editRightClickPassThrough() const { return m_editRightClickPassThrough; }
    void setEditRightClickPassThrough(bool passThrough) { m_editRightClickPassThrough = passThrough; }

    // The mouse is in `container` — called from its event filter on the
    // QEvent::Enter of one of its child widgets AND on a clean right-
    // click press (which proves the mouse is there). The flip-flop is
    // global, so entering a DIFFERENT container resets it to "edit next":
    // the first right-click on a widget is always the edit menu, not the
    // "native next" a previous widget left behind. Not a reset: re-entering
    // the SAME container (the QEvent::Enter Qt fires when the mouse
    // re-enters the widget after a menu closes), and entering an ANCESTOR
    // container — when a popup closes, Qt re-enters the whole chain from
    // the outermost container down to the widget under the cursor, and
    // resetting on those ancestor Enters would undo the "native next" the
    // just-closed menu set (the flip-flop would never reach the native
    // menu).
    void mouseEnteredContainer(WidgetContainer* container);

    // True when the last mouse press landed on an open popup (the
    // container's edit menu, a native menu a previous pass-through
    // showed): that click is a dismissal, not a new right-click command
    // — the container filters swallow its release whole (no flip-flop
    // toggle, no pass-through). The press goes to the popup's mouse
    // grab, so the container filters never see it; an app-level filter
    // (eventFilter below, installed in the ctor) records it. Every
    // press updates the state, so a stale "on" can outlive a dismissal
    // only until the next press, which resets it.
    bool takePressOnPopup();

    // The app-level press recorder (see takePressOnPopup).
    bool eventFilter(QObject *obj, QEvent *event) override;

    BaseWidget* createWidget(const QString& widgetId, WidgetConfiguration& config);

    // A name for a new instance of `widgetId`: "<widgetId>-<n>" with the
    // next number of a single monotonically growing counter (the
    // "widgets.name_counter" setting, so the numbers keep growing
    // across launches), bumped past any name a live layout widget already
    // carries or the style sheet references — a rule left behind by a
    // deleted widget must not be inherited by a fresh instance.
    QString freeInstanceName(const QString &widgetId);

    // Copy/paste support: a container's widget tree serialized to a JSON
    // document (the "container" object of the layout document format),
    // stored in the system clipboard.
    QString serializeContainer(WidgetContainer* container);
    bool isSerializedWidget(const QString& text) const;
    BaseWidget* createWidgetFromSerializedText(const QString& text);

    // The store.
    // All the layouts, sorted by title: the user's files and the
    // read-only presets — a user file shadows a same-named preset.
    QList<LayoutInfo> layouts() const;
    // The active layout's name (the "layout.current" setting; empty =
    // no current layout — the first start before the picker has been
    // answered, the key left unset so first-run detection sees it).
    QString currentLayout() const;
    void setCurrentLayout(const QString &name);
    // The document of the layout `name` (the user's file, else the
    // preset, else an invalid document — stale names).
    QJsonObject layoutDocument(const QString &name) const;
    // True when `name` is a read-only preset (no user file shadows it).
    bool isPreset(const QString &name) const;
    // The origin ("preset:<name>") a layout document records, "" when
    // it has none.
    QString originOf(const QString &name) const;
    // True when there is nothing to restore: no "layout.current"
    // (the setting unset) and no file in the user's layouts directory —
    // the
    // first start (or a start with everything deleted), which shows
    // the layout/style picker above the empty window.
    bool firstRun() const;

    // The live layout as a document (the "container", "window" and
    // "windows" keys at the root, the "version" key set) — the part of
    // a save that does not depend on the file.
    QJsonObject buildLayoutDocument() const;
    // Apply a document to the live window: the version check and the
    // migrations run first (a failed migration rejects the document,
    // like a failed strict import), then the root container, the child
    // windows and the window geometry. strict (the import) refuses a
    // document that references a widget type this build cannot create;
    // lenient (the automatic loads) skips those subtrees with a
    // warning. `source` names the file in the warnings (may be empty).
    bool applyLayoutDocument(const QJsonObject &doc, bool strict,
                             const QString &source = QString());
    // The save (see the class comment): the live layout into the
    // current layout's file — a plain write for a user file, a fork
    // for a stale preset current, a no-op for an unset current.
    // Returns the (possibly new) current name; a no-op for an unedited
    // preset.
    QString saveCurrentLayout();
    // Apply the current layout's document; a stale name (no file
    // anywhere) falls back to the first user file, or to no current
    // (the empty window) when there are none.
    void loadCurrentLayout();
    // Switch the active layout: the target is validated (a user file,
    // or a preset — switched as a copy, forkLayout), the current is
    // saved, then the target is applied and made current. A name
    // without a file is refused.
    bool switchLayout(const QString &name);
    // Fork the live layout to the user's directory under `name` (an
    // existing file is overwritten) and make it current. The
    // provenance of the current layout is carried over. False for a
    // bad name.
    bool saveLayoutAs(const QString &name);
    // Create a new empty layout under `name` (an existing file is
    // overwritten), apply it and make it current — saving the current
    // layout as-is first (a no-op for an unset current), so the
    // arrangement the user was working on is never lost. False for a
    // bad name.
    bool newLayout(const QString &name);
    // Take a copy of the layout `name` (a preset) into the user's
    // directory as "<name>.modified" (its provenance names the preset)
    // and make the copy current. An existing copy is adopted (switched
    // to) rather than duplicated; a missing one is created from the
    // preset's own document (not the live state). False when the
    // layout has no document.
    bool forkLayout(const QString &name);
    // Delete a user layout (presets are refused); when the deleted
    // layout was the current, the first remaining user file becomes
    // current, or the current is unset and the widgets cleared when
    // there are none.
    bool deleteLayout(const QString &name);
    // The "modified" marker of the listing: true when the user file
    // records an origin preset and its content (minus the display
    // keys) differs from that preset's.
    bool isModified(const QString &name) const;

    // Export/import support: the layout document written to / read from
    // an explicit file. strict (the default, the import) rejects a
    // document that references a widget type this build cannot create;
    // the automatic loads pass false so those subtrees are skipped with
    // a warning instead — the rest of the layout is restored.
    bool saveLayoutTo(const QString &path) const;
    bool loadLayoutFrom(const QString &path, bool strict = true);

    // The View → Layout menu: clears it and re-fills it whole — the
    // user's layouts (the files in the layouts directory; the presets
    // are dialog-only), the current one checked, a click switches (the
    // previous is saved; a preset is switched as a copy), then
    // "Manage windows" (the windows editor) and "Manage layouts" (the
    // picker dialog — the store's New/Copy to/Delete/Import/Export
    // operations are its per-pane buttons). Called on the menu's
    // aboutToShow, so a layout added elsewhere shows up on the next
    // open.
    void fillLayoutMenu(QMenu *menu);

public slots:
    void setEditMode(bool enabled);

    // View → Layout: the layout/style dialog in Picker mode —
    // non-modal and single-instance (a second trigger raises the open
    // dialog): browsing everything (presets included) with a live
    // switch per selection, plus the per-pane management buttons.
    // The picker, opened for a specific store: the dialog opens on the
    // matching tab (a re-trigger of an open dialog switches to it).
    // store: 0 = layout, 1 = style (LayoutStyleDialog::Store).
    void showLayoutStyleDialog(int store);

    // The first start (no current layout, no user layouts): the dialog
    // in FirstRun mode, modal, above the empty window — a selection
    // previews live, Use commits (a selected preset is taken as a
    // copy), "Start empty" creates fresh empty layout and style files
    // under the name "default". There is no Cancel: the start is
    // resolved one way or the other, so after the dialog the currents
    // are files and the picker is not repeated.
    void firstRunDialog();

    // The app's exit (the main window's closeEvent): the save of the
    // current layout and the current style (a no-op for an unset
    // current).
    void saveLayout();

    // The picker dialog (View → Layout → "Manage layouts"): pick a
    // file in a dialog and export the current layout to it (the
    // current layout's provenance carried over) / import it into the
    // user's layouts directory (it becomes the current layout). True
    // when the file was written/read, false for a dialog cancel or a
    // failed read/write (a warning is shown).
    bool exportLayout();
    bool importLayout();

    // "View" menu: edit the application's Qt style sheet in a dialog
    // with a live preview (auto-preview by default, manual via the
    // Preview button). The committed sheet is saved to the current
    // style's file on OK.
    void editStyleSheet();

    // "View" → "Layout" menu: the windows editor (the WindowManager's
    // add/remove/rename/type/show-hide dialog, live over the current
    // set of windows). Non-modal and single-instance (a second trigger
    // raises the open dialog): a window shown from it must be
    // interactable at once, and a modal dialog would lock the user out
    // of a window big enough to cover it.
    void editWindows();

    // The picker dialog (View → Styles → "Manage styles"): pick a
    // file in a dialog and export the current style to it (the
    // current style's provenance carried over) / import it into the
    // user's styles directory (it becomes the current style). True
    // when the file was written/read, false for a dialog cancel or a
    // failed read/write (a warning is shown).
    bool exportStyle();
    bool importStyle();

    // "File" menu: the preferences dialog — the settings topics of the
    // core and the plugins, one page per topic.
    void showPreferences();

signals:
    void editModeChanged(bool);

protected:
    bool m_editMode;
    // The edit-mode right-click flip-flop (see editRightClickPassThrough).
    bool m_editRightClickPassThrough = false;
    // The last mouse press landed on an open popup (see takePressOnPopup).
    bool m_pressOnPopup = false;
    QPointer<WidgetContainer> m_lastContainer;
    // The container the mouse last entered (see mouseEnteredContainer).
    QPointer<WidgetContainer> m_mouseContainer;

    MainWindow* m_window;

    // The open windows editor (non-modal, WA_DeleteOnClose — the
    // QPointer tracks it).
    QPointer<WindowsDialog> m_windowsDialog;

    // The open layout/style picker (non-modal, WA_DeleteOnClose — the
    // QPointer tracks it).
    QPointer<LayoutStyleDialog> m_layoutStyleDialog;

private:
    inline static LayoutManager* m_layoutManager;
    explicit LayoutManager(QObject *parent = nullptr);

    static QList<WidgetRegistry::WidgetOption> widgetOptions();

    // The store's shared core (the paths, the current, the listing,
    // the save/load/fallback operations — entrystore.h).
    EntryStore m_store;

private slots:
    void onMenuSetWidget(bool checked);
    void onMenuCopyWidget();
    void onMenuCutWidget();
    void onMenuDeleteWidget();
    void onMenuPasteWidget();
    void onMenuHide();

    // The automatic load, run when the window is shown (the save is
    // saveLayout, a public slot the main window's closeEvent calls).
    void loadLayout();
};

#endif // LAYOUTMANAGER_H
