# Persistence

The data directory holds the database, the central settings file
(settings.json), the layout and the plugins' data.
`Settings` resolves it from the `DOOB_DATA_DIR` env var, else
`QStandardPaths::writableLocation(AppDataLocation)` — which is only
meaningful once the organization/application name is set (done in
`main.cpp` before any `Settings` use); `Settings::reset()` for tests.
The files:

- `doob.db` (SQLite) — the single database, with two **components**:
  each owns its connection, its tables and its own schema version, so
  they can evolve independently. Both point their `Database` at
  `Settings::dbPath()`.
  - `items` (`MediaItemStore`, v2): the **single item universe** —
    `media_items` (one row per item: `source TEXT UNIQUE`, an
    `in_library` flag, all standard metadata fields, `kind`,
    `provider` (the id of the source provider that owns the item —
    empty for local files and plain stream URLs), `imported_at` /
    `last_played_at` (epoch ms UTC, 0 = unset) and `play_count`
    (runtime-updated by the store), `metadata_json`),
    `item_meta` (EAV: one row per arbitrary tag,
    queryable by key or value regardless of which tags an item happens
    to carry),
    `item_artwork` (v2: one row per item that has art — `item_id` PK,
    `mime`, `data` BLOB; the blob never rides the `MediaItem` value:
    the RAM state is the set of ids that have art, the bytes are
    served from the database on demand — `hasArtwork`/`artwork`/
    `setArtwork` (empty data removes the art), and
    `itemArtworkChanged` for the displays; the import paths store the
    art with new items, the metadata editors edit it, and the
    art-fallback resolver fills a missing one on a play start).
    There is deliberately no FTS table: the store is
    filtered in memory, and a contentless FTS5 index would cost
    app-side maintenance on every write for a table nothing reads —
    re-introduce it with the search feature that reads it. The app
    keeps the whole universe in RAM — the
    single-column metadata indexes (title, artist, album, album_artist,
    composer, genre, year) and the composites `(artist, album)` /
    `(album_artist, album)` exist so future filtering — and a
    tree-shaped library view — can be pushed to SQL. `load()` reads the
    whole table into RAM at startup; after that every mutation writes
    through (below). Pruning happens **at mutation time** via
    `pruneIfUnreferenced(id)`: an item that is not in the library, not
    referenced by any playlist (`PlaylistManager::referencedIds()`) and
    is `kind == Local` is deleted in place — non-local rows are never
    pruned, because the row may be the only copy of their metadata.
    `save()` remains a full-rewrite **repair/snapshot** tool (it is no
    longer the durability mechanism): it re-inserts everything that is
    in memory, omitting the same unreferenced locals — no timer, no
    DELETE logic, so omitting is deleting.
  - `playlists` (`PlaylistManager`, v2): `playlists`
    (`id, name, position` — v1 ordered by id, the creation order; v2
    makes the order explicit — plus the external-import bookkeeping
    `imported`, `source_file`, `dirty`: a playlist imported from a file
    remembers `source_file`, and `dirty` marks a modification since the
    import, shown as a `*` in the display name until it is saved back)
    and `playlist_items` — a **pure reference**
    (`playlist_id, position, item_id`, no metadata copy). Row mutations
    go through the write-through primitives (`syncPlaylistRows`,
    `insertPlaylistRow`, `removePlaylistRow`, `renamePlaylistRow`);
    `syncPlaylistRows` upserts the playlist's own row first, so a
    freshly created playlist's first mutation can never leave orphaned
    item rows.
  - The library has **no database of its own**: `LibraryManager` is a
    membership facade over the store — membership is the `in_library`
    flag on each item, mirrored in RAM as a set of ids, and persisted
    by the store's write-through.
- `settings.json` — **the single settings file** (user settings and
  session state alike), owned by the `SettingsManager`
  (settingsmanager.{h,cpp}). A component registers its settings at
  startup (the core from its singletons' ctors, a plugin in
  `initialize()` via `api->settingsManager`): a `Setting` is `{key, title,
  description, defaultValue, owner}` — the key is flat and dotted:
  `.` separates the parts, the owning component/namespace first
  (`"library.watch.dirs"`), and multi-word parts are snake_case
  (`"layout.edit_mode"`, `"tubemusic.cache_max_mb"` — the C++
  identifiers keep the Qt camelCase, only the keys are snake_case),
  the default doubles as the **type** (a
  `setValue` is type-checked against it; a value of a different type is
  refused with a `qWarning`), the owner is empty for the core and the
  plugin's id otherwise (a convention — the plugin passes the full key).
  The file is a flat JSON object holding **only explicitly set values**
  (overrides) at the dotted keys — a new setting's default applies to an
  old file automatically, and `isModified(key)` is exactly "present in
  the file". Stored values **survive unregistration**: a plugin that is
  removed leaves its keys in the file (orphaned) and a re-added plugin
  finds its settings intact. `value(key)` is stored → default → invalid;
  `setValue`/`resetToDefault` are write-through (below) and emit
  `changed(key, value)` — only when the value actually changes
  (a set to the same value is a no-op). `load()` (called in `main.cpp`
  before the components) coerces stored values to the registered type
  (a JSON array comes back as a `QStringList`, an object as a
  `QVariantMap`), so `value()` returns a stable type. No migrations: the
  file is read as-is, unknown keys stay. **Session state** is
  registered like any other setting (the core registers it from the
  singletons' ctors): the playlist session state — the current playlist
  as **id and name** (`playlist.current_id`, `playlist.current_name` —
  the id is the exact reference, names are not unique, the name keeps
  the file readable) plus `playlist.indexes`, **per-playlist current
  indices** (playlist id → row) — written on every change (not only on
  quit); on load the id wins, a missing or unknown id falls back to the
  first playlist with the stored name, a missing or unknown name falls
  back to the first playlist, and an index entry is applied only when it
  names an existing playlist and an in-range row (stale/out-of-range
  entries stay at the default). The **current layout and style** too:
  `layout.current` and `style.current` — the name of the active entry
  of each store (the user's file — for styles, a preset's name too;
  unset = no current — the first start before the
  picker has been answered; see the layouts/styles bullets below). And
  the widget **auto-naming counter** `widgets.name_counter`, a single
  monotonically growing int for all widget ids (see the per-instance
  styling paragraph below). **Dialog UI state** is registered the same
  way, on demand: the `UiState` helper (uistate.{h,cpp}) persists a
  dialog's (or any widget's) arrangement under one `QVariantMap`
  setting per state scope — the `dialogs.*` keys (`dialogs.preferences`,
  `dialogs.url_import`, `dialogs.metadata`, `dialogs.single_metadata`,
  `dialogs.metatransform`, `dialogs.style_sheet`, `dialogs.windows`,
  `dialogs.layout_style`, and the checkable-list dialogs'
  `dialogs.playback_buttons` / `dialogs.peak_meter_channels` /
  `dialogs.current_track_lines`), registered by the first `UiState` that
  names the key (later ones share it, no refused-registration warning).
  The standard fields: `"geometry"` `{x, y, width, height, maximized}` —
  the layout document's `window` shape (a maximized window stores its
  normal geometry plus the flag; the restore sanitizes the saved state
  — `UiState::restoredGeometry`: a position off every screen is
  re-centered on the primary screen (the size kept), a missing,
  degenerate or oversized size — one no screen can hold — keeps the
  default) — and
  `"columns"` `{stable column name: width}` (a stale name is ignored
  on restore, a missing one keeps the view's default). Free fields go
  through `set`/`field`. Plugging a dialog in: `new UiState(key, this)`
  + `track(this)` (saves the geometry on every hide, applies a stored
  maximized flag on the first show) + `restoreGeometry(this)` in the
  ctor; a table inside adds `restoreColumns(header, names)` in the ctor
  and `saveColumns(header, names)` in `hideEvent` (the names are the
  model's stable column keys, one per logical column) — a dialog's
  buttons close through reject/accept, which hide it *without* a
  `closeEvent`, so the columns are saved on every hide, like the
  geometry (an unchanged save is a no-op). A page of a
  dialog is not a window (it has no `closeEvent`): it shares the
  dialog's state scope (the page's columns and the dialog's geometry
  are fields of the same map) and saves its columns from `hideEvent`
  (the dialog's close cascades one to it, and the stacked widget's
  topic switch hides it too — an unchanged save is a no-op; the
  settings browser page does this). Dialog state is a user preference,
  not layout: it never travels with a layout document
  (which owns only what is on the main window's screen). The small
  fixed modals (the value editor, the widget picker) are not plugged
  in — the mechanism is available for them. The checkable lists are:
  `CheckableListDialog` wires the standard `UiState` under the caller's
  key (a dialog without a key persists nothing), and every production
  caller names one (the `dialogs.playback_buttons` /
  `dialogs.peak_meter_channels` / `dialogs.current_track_lines` keys
  above).
- `layouts/<name>.json` — the layout store, owned by `LayoutManager`
  (layoutmanager.{h,cpp}). Each layout is a file in the user's layouts
  directory (`Settings::layoutsDir()`). The active layout is the
  `layout.current` setting (session state, like the current playlist):
  the name of a user file (the key is left unset so the first-run
  detection sees it — an unset current means no layout, the empty
  window). "default" is a plain file name like any
  other (the conventional name the dialog's **Start empty** action
  creates, below). A **preset is a template, never current itself** —
  selecting one (the dialog's confirm, the picker's switch) takes a
  copy, the user's `<name>.modified.json` (below).
  
  **The document format** is versioned (layoutformat.{h,cpp}): the
  root carries a `version` key (absent = 1) plus `container` (the
  root container's configuration — the serialized widget tree),
  `window` (`x`, `y`, `width`, `height`, `maximized`, and the
  optional `menu_bar` / `frameless` chrome flags — each present only
  when it differs from the default, a missing key takes its default on
  restore, so no format migration): the main
  window's size/position, saved by `saveLayout` (a maximized window
  stores its normal geometry plus the flag) and restored by
  `loadLayout` (sanitized by `UiState::restoredGeometry` — a position
  off every screen is re-centered on the primary screen, a missing,
  degenerate or oversized size — one no screen can hold — keeps the
  default),
  `dock_state` (a base64 `QMainWindow::saveState()`) when a docked
  child window exists, `windows` (the child windows, below), and the
  optional display keys `title` (the listing's title) and `origin`
  (provenance, below). A document outlives the build that wrote it
  (a shipped preset, the user's saved layouts), so a format change is
  a **numbered migration**, not a rewrite: an ordered list of
  `LayoutMigration` steps (the same shape as the database's schema
  migrations, in memory — no per-step transactions; a failed step
  rejects the document whole) — `applyLayoutDocument` runs the pending
  steps first, and the migrated document lands in the new format on
  the next save. What versioning covers is the root document's shape
  only: per-widget state keys evolve leniently on their own
  (restoreWidget ignores unknown keys, missing keys take their
  defaults), new widget types are the registry's concern (the
  strict/lenient unknown-type walk, below), and widget ids are a
  stable API like plugin ids — deprecated and aliased, never renamed.
  
  **Presets**: the read-only preset layouts live in
  `Settings::presetLayoutsDir()` — the resolution (the same shape as
  the plugin directories): the `DOOB_PRESETS_DIR` env var, else
  `<app dir>/share/doob` (the dev build — CMake copies the repo's
  `presets/` tree next to the executable), else the `doob` directory
  of the standard data locations in order (the packaged install,
  `<datadir>/doob`); the data directory itself is never a preset
  source. Presets are never rewritten; a user file shadows a
  same-named preset; `layouts()` lists the user's files and the
  presets, sorted by title.
  
  **Provenance and taking presets as copies**: the `origin` root key
  (`"preset:<name>"`) records where a layout came from; the listing
  shows "(from preset X)" and a "(modified)" marker when the content
  (minus the display keys) differs from the origin preset
  (`isModified`). A preset is read-only and never the current:
  `forkLayout(name)` takes a copy of the preset into the user's
  directory as `<name>.modified.json` (the origin recorded) and makes
  the copy current — **adopting the copy when it already exists**
  (a preset maps to at most one user copy, re-selecting the preset
  re-enters it) and creating it from the preset's *own* document
  (not the live state) otherwise. `switchLayout(name)` is the same
  for a preset target (save the current first, then fork). Saving the
  current (`saveCurrentLayout` — a switch, the app's exit, a new
  layout) writes the live state into the current layout's file: a
  plain write for a user file, a fork to `<name>.modified.json` for a
  stale preset current (a reference left by an older build —
  unedited, it stays the read-only preset), a no-op for an unset
  current (there is no file to write to). `saveLayoutAs(name)` forks
  the live layout under `name` (the origin carried over),
  `newLayout(name)` saves the current as-is first (a no-op for an
  unset current) and then creates the fresh empty layout,
  `deleteLayout(name)` removes a user file (presets are refused);
  deleting the current falls back to the first remaining user file
  (which is applied and made current) or unsets the current and
  clears the widgets when there are none. A stale current on the
  automatic load (`loadCurrentLayout`) falls back the same way.
  
  **First run**: `firstRun()` = no `layout.current` and no file in
  the layouts directory. `main.cpp` then runs `firstRunDialog()`
  right after `show()`: the picker (`layoutstyledialog.{h,cpp}`) in
  FirstRun mode, modal, above the empty window — a selection previews
  live (no saves), **Use** commits the selection (a selected layout
  preset is taken as a copy — the fork becomes current; a selected
  style preset is used as-is — its copy appears only when it is
  edited; a user entry as-is)
  and is grayed out while a pane has no selection, **Start empty**
  creates fresh empty layout and style files under the name
  "default" (they become the currents) and forces edit mode on. There
  is no Cancel: the start is resolved one way or the other, so after
  the dialog the currents are files and the picker is not repeated.
  The same dialog's Picker mode (non-modal, single instance over the
  manager's `QPointer`, opened from View → Layout → "Manage layouts"
  and View → Styles → "Manage styles") lists everything — presets
  included — with a live switch per selection (the previous is saved;
  a layout preset is switched as a copy, a style preset as-is) and
  per-pane New/Copy to/Delete/Import/Export buttons (New is a fresh
  empty file under the prompted name, the current saved first; Copy to
  forks the live state under the prompted name; Delete confirms;
  Import/Export run the manager's file dialogs — the import lands in
  the user's directory under the picked file's name and becomes
  current, the export is the live entry under the picked file, its
  origin carried; the name prompt and the delete confirm are protected
  virtuals — test seams; Delete and Export are enabled only for a
  user-owned selection, Import always).
  
  **Menus**: View → Layout is re-built whole on aboutToShow
  (`fillLayoutMenu`): the **user's layouts only** (the files in the
  layouts directory; the presets are dialog-only) — the current
  checked, a click is `switchLayout` — then "Manage windows" (below)
  and "Manage layouts" (the picker dialog, above — the store's
  New/Copy to/Delete/Import/Export operations are its per-pane
  buttons).
  
  **Child windows**: the `windows` key is an array
  (omitted when empty) of one map per `WindowManager` window
  (windowmanager.{h,cpp} — the singleton that owns the standalone
  top-levels and the docked `QDockWidget`s; View → Layout →
  "Manage windows" is its live editor, `windowsdialog.{h,cpp}`, shown
  non-modal and kept as a single instance (a second trigger raises
  the open one) because a window shown from it must be interactable
  at once — a modal dialog would lock the user out of a window big
  enough to cover it; the `windowtoggle` widget is a per-widget
  show/hide switch bound to a window id): `id` (`"window-<n>"`, bumped past the ids in use),
  `kind` (`"standalone"`/`"docked"`), `title`, `visible`,
  `geometry` (standalone only: `x`, `y`, `width`, `height`,
  `maximized`) and `container` (the window's own widget tree,
  restored through the same registry as the main one — the strict
  import walks it too; the standalone's saved geometry is sanitized on
  restore the same way as the main window's state, above). A docked window's arrangement (edge, tab,
  size, float, which docks are hidden) is the main window's state
  under `window.dock_state` (above): the dock's `objectName` is the
  window's id, which `saveState`/`restoreState` key on. A standalone
  window's `visible` is tracked by an event filter on the carrier
  (show/hide events — the window's close button hides, never
  destroys) and its geometry by the carrier's move/resize events; the
  geometry is applied once the window is shown (a window manager may
  re-place a window on the map, so the position set before the show
  is only a request — the main window's position is restored the same
  way, after the show). The carrier does not count as an app-anchor
  window (`Qt::WA_QuitOnClose` off): closing the main window quits
  the app, and its `closeEvent` saves the layout first — before the
  base class hides the window, because the quit that follows tears
  the windows down (the hide cascades to the docked children,
  `QApplication`'s quit handler closes all top-levels and the main
  window a second time, whose `closeEvent` skips the save via a
  visibility guard), and a save after the teardown would record every
  window as closed. The same filter tracks a docked window's
  `visible`, except it ignores a hide that arrives while the main
  window is not visible: hiding the main window cascades a hide to
  its docked children (the app's exit does that after the save, a
  minimize does it until the restore), and a dock restored from a
  cascade-flipped flag would come back hidden — a hide with the main
  window visible is a user close and is recorded as usual. **Wayland caveat**: on Wayland the compositor owns window
  placement — a standalone window's position is not restorable there
  (the `geometry` rides in the document and is applied on X11, where
  it sticks; on Wayland only the size survives, and the window lands
  wherever the compositor puts it on every show). Loading is two-moded
  (`applyLayoutDocument(doc, strict, source)`): the **import is
  strict** — a document that references a widget type this build
  cannot create (a missing plugin, a typo, a newer version) is
  refused whole, found by walking the serialized tree (any nesting) —
  while the **automatic load is lenient**: those subtrees are
  skipped with a `qWarning` (the registry's creator returns nullptr
  for them) and the rest of the layout is restored. An unknown
  **root** keeps the current layout in both modes.
- `styles/<name>.qss` — the style store, owned by the `StyleManager`
  singleton (stylemanager.{h,cpp}) — the layouts' store's parallel:
  each style is a QSS file in the user's styles directory
  (`Settings::stylesDir()`). The active style is the `style.current`
  setting: the name of a user file **or a preset** (empty = no
  current — the platform style, the empty sheet; "default" is a plain
  file name like any other, the conventional name the dialog's
  **Start empty** action creates). Unlike a layout — picking a layout
  changes the windows' state — picking a style changes nothing but
  the sheet, so **a preset is used as-is**: selecting it applies the
  preset's text and makes the read-only preset itself current, no
  copy is taken. The copy is taken only on write (the
  **copy-on-write**): a live sheet that has diverged from the
  preset's (an edit) is forked into the user's `<name>.modified` (the
  origin names the preset; an existing copy is written over with the
  live sheet — the sheet the user just approved is the state) on the
  next save (the style editor's OK, a switch, the app's exit), and
  the copy becomes current; an unedited preset current stays a no-op.
  The presets live in
  `Settings::presetStylesDir()` (the same
  resolution as the layouts'). Provenance rides in the file as a
  leading `/* doob-origin: preset:<name> */` comment (QSS has no key
  slot) — **file metadata, never of the live sheet**: the text
  applied to the app is the file's text with the comment stripped
  (every apply path — load, switch, fork, import, the dialog's
  preview — goes through `liveText`), and the user-file save
  re-records the file's origin, so the marker survives re-saves; the
  "modified" marker (`isModified`) compares the text, the comment
  stripped, against the origin preset's. The copy-on-write fork of
  `saveCurrentStyle` (above), `newStyle` (saving the current first),
  `saveStyleAs` and `deleteStyle` (user files only; deleting the
  current falls back to the first remaining user file, or unsets the
  current — the platform style — when there are none) mirror the
  layouts' store; `saveCurrentStyle` runs on the app's exit
  (alongside `saveCurrentLayout`) and on the style editor's OK — the
  editor's OK is where an edit of the current preset usually lands
  (Cancel reverts, so an abandoned edit takes no copy). The View →
  Styles menu is re-built whole on aboutToShow (`fillStylesMenu`):
  **all the styles** — the user's files and the presets (a preset is
  usable as-is, so it is listed like any other; the current — which
  may be a preset — is checked) — a click is `switchStyle` — then
  "Manage styles" (the picker dialog, above — the store's
  New/Copy to/Delete/Import/Export operations are its per-pane
  buttons) and "Edit style sheet" (below).
  The
  sheet is applied live with `setStyleSheet` (an empty text restores
  the platform style), which is what the style editor's preview
  relies on (`stylesheetdialog.{h,cpp}` — a live-preview editor with
  an auto-preview timer or a manual Preview button; OK commits the
  text and persists it through the save, Cancel reverts). QSS
  selectors key on `objectName` (e.g.
  `QPushButton#playPauseButton`), which is why widgets give their
  children stable object names. **Per-instance styling**: the
  edit-mode context menu of every widget (leaves and containers
  alike) carries "Style this widget..." (`BaseWidget::editOwnStyle`;
  the `openStyleEditor` protected virtual is the test seam): it gives
  the widget a name on demand — `LayoutManager::freeInstanceName`,
  `"<widgetId>-<n>"` from the `widgets.name_counter` setting (above),
  bumped past any name a live layout widget already carries
  (a copy/paste keeps the name) or the sheet references (a rule left
  behind by a deleted widget is never handed out again) — appends an
  empty `#name { }` rule when the sheet has none for the name, plus a
  `#name * { }` rule for composite widgets (a composite's visible parts
  are its unnamed children, which a bare `#name` rule does not reach —
  Qt's QSS does not cascade it down; a self-painted widget like the
  visualizations, `BaseWidget::selfPainted()`, gets the bare `#name`
  rule only, since it paints its own surface), and opens
  the style sheet dialog with the cursor in that rule
  (`StyleSheetDialog`'s optional `focusSelector`/`revertTo`
  parameters); Cancel reverts to the sheet of before the action, so
  an abandoned rule leaves nothing behind. The name rides in the node
  configuration (`"name"`, stored only when non-empty by the base
  `storeWidget()`/`restoreWidget()` — every override chains to the
  base), so it survives the layout save/load and the copy/paste
  serialization: a copy keeps its name and with it its `#name` look.
  Deleting a widget orphans its rule harmlessly (the name is simply
  never reused). **Custom-painted widgets are styled
  through the same surface**: Qt's QSS engine only understands
  `background` (merged into the widget's `Base`/`Window`/`Button`
  palette roles) and `color` (into `Text`/`WindowText`/`ButtonText`)
  as color properties, and every painted part resolves through
  `BaseWidget::styleColor(styled, fallback)` — the stylesheet's value
  only when a rule named the role for this widget, the theme's
  fallback role otherwise, so an unstyled widget paints exactly as
  before (the peak meter's bar: `Highlight`, backgrounds read `Base`
  directly). The sheet also carries `x-` custom properties for the
  custom-painted widgets' paint parameters — colors, sizes, widths,
  counts (the display modes are widget settings, see below): the
  oscillogram's `x-line-width` (px, the "Line" trace style),
  `x-box-size` (px or % of the height) and `x-box-color-off` (the
  "Boxes" trace style), the peak meter's and the spectrum's
  `x-segments` (the "Segments" display style), `x-bar-margin` (px,
  the space between and around the bars — auto: a fixed share of the
  across-axis) and `x-track-color` (a bar's unfilled part — auto: the
  background lightened), and the spectrogram's `x-cmap` (the intensity
  gradient's color map: `mono` — the background up to the bar color —
  or the fixed `heat`/`fire`/`ice`/`viridis` maps) and `x-floor-db`
  (the dB level below which the image is its darkest color). The
  properties are **documented in code**:
  each custom-painted widget declares its `x-` properties as
  `StyleProperty` records (styleproperty.h — name, the widget's title
  as owner, what it does, the coded default, the unit, the numeric
  range when it has one) in a static `styleProperties()` (the bar
  widgets' shared ones live in `barpaint::styleProperties(owner)`),
  and `registerBuiltinWidgets` registers them into the `StyleManager`
  (`registerStyleProperties`, keyed by name and idempotent, so the set
  is complete even for a widget type that never lands on screen) — the
  style sheet editor's **Help button** renders them (`helpHtml()`,
  one section per widget with property/default/range tables) with the
  Qt style sheet reference linked at the bottom. Qt does not understand `x-` declarations: it warns
  "Unknown property" for each one and applies the rest of the rule —
  the warnings are expected, the sheet's text is passed to Qt
  unmodified. A linear scan in `StyleManager` keeps the rules in
  document order (every rule's selectors, each rule's `x-`
  declarations) and the widgets read them back through
  `BaseWidget::styleValue`/`styleColorValue`/`styleInt`/`styleLength`
  (typed helpers with fallbacks): the widget itself resolves before
  its ancestors (a container's rule cascades down to custom-painted
  descendants), and within one node the best matching rule wins — a
  `#name` selector beats a bare class name regardless of document
  order, at equal specificity the later rule in the sheet wins (the
  same resolution Qt applies to the native properties, so a
  per-instance `#name` rule overrides the class rule in both halves of
  the styling).

**What belongs where — the settings/layout boundary**: the question is
"does this travel with the layout, or with the user?" Everything about
*how the UI is arranged* lives in the layout document (the layouts'
store, above), **never** in the `SettingsManager`: the window's
size/position, and each
widget's own state — a splitter's orientation and part sizes, a
`PlaybackControlWidget`'s display sequence (the visible buttons —
transport and the shuffle/repeat mode buttons — and the layout
stretchers between them), its stretch policies (the buttons expand on
the horizontal/vertical axis, both on by default) and its orientation
(horizontal by default) (there can be several playback widgets on
screen at once, and each keeps its own), view column states, the active tab. The same split
holds *within* a widget: **how it renders** (the peak meter's Solid vs.
Segments display style, the spectrum's Solid vs. Segments style, band
count and analyzed channel, the oscillogram's Bars/Line/Boxes trace
style and its window length — the audio span the scrolling window
holds — and the spectrogram's Scroll vs. Sweep display mode and its
window length — the FFT's size, the time/frequency resolution
trade-off)
is per-instance state in the node configuration — an edit-mode
context-menu option, stored only when non-default, a stale out-of-range
value ignored on restore — while the **paint parameters** (colors,
sizes, widths, counts) are the style sheet's `x-` custom properties
(above). The style sheet is its own store (above) for the same
reason: a layout and its design are two units the user switches
together — the picker and the menus keep them in lockstep — and
neither leaves anything behind in settings. User
*preferences* — choices about behaviour, not arrangement
(`playback.player`, `library.watch.dirs`, `layout.edit_mode`,
`visualization.fps`) — are registered settings; *session state* (the
current playlist and row, the current layout and style) and *dialog UI
state* (a dialog's size/position and internal control state, the
`dialogs.*` keys via the `UiState` helper) are registered settings in
the same file.

**Write-through**: both database components persist every completed
mutation immediately — memory first, then the database in one
transaction; a failed persist rolls the memory change back and the
caller skips its signal, so RAM and the file never drift and a crash at
any point leaves a consistent, current database. That is why there is no
save on quit. All writes funnel through the store's
`insert`/`remove`/`setInLibrary`/`updateItem` and the playlist manager's
row primitives — never write a component's table directly from
elsewhere. The `SettingsManager` is write-through the same way (atomic
temp-file + rename, so a crash mid-write cannot tear the file); the
session state in it is best-effort, cheap to lose.

**Schema migrations** (database.{h,cpp}): a component's schema history is
an ordered `QList<DatabaseMigration>` next to its DDL — each entry is a
version plus the diff from the previous version (v0 is the empty
database), as a `std::function<bool(QSqlDatabase&)>` that returns false
on a failed statement. `Database::open(path, component, migrations)`
reads the component's version from the `schema_version` table (one row
per component; a missing table or row is version 0), then applies every
pending step one by one, **each in its own transaction together with the
version bump** — a failed step rolls back completely, `open()` returns
false, and the step is retried on the next open. `Database::execStatements`
and `Database::tableColumns` are the building blocks for steps (the latter
scopes its `PRAGMA table_info` query — see the schema-lock gotcha in
AGENTS.md).
Both components are at v2 (items: the `item_artwork` side table;
playlists: the explicit order plus the import bookkeeping) — those
steps are the mechanism's first real migrations, and a legacy v1
database is brought up by them on open.
Adding a schema change: append a new `DatabaseMigration` to the
component's list and add a test that crafts the previous version by hand.

`MediaItemStore` (mediaitemstore.{h,cpp}) is the app-wide **id authority
and resolver**: every item — library or not — lives in exactly one place
with exactly one id from the store's counter (never persisted — load
reserves past the stored max id). `MediaItemStore::resolve(id)` is the
one lookup from id to `MediaItem`; playlists, views and playback resolve
through it, so a dangling id (the store does not hold it) renders
empty/invalid without dropping the row. `ensureItem(source, meta)` is the
import primitive: a hit (any item for that source, library or not) is
reused as-is (no file read, meta ignored); a miss creates a new item: an
existing local file (kind `Local`), a source a registered provider claims
(kind `Plugin`, `provider` set to the claimant's id, the source stored in
the claimant's **canonical form** — `canonicalUrl` strips the URL's
incidental arguments, so a re-pasted link with extras hits the same
item) or a plain `http(s)` stream URL (kind `Remote`, the source as
pasted); anything else is rejected. The item building
(`mediaItemFromMeta`, trackmetadata.{h,cpp}) trims every string field
(title/artist/album/albumArtist/composer/genre/comment and the extra
tags) of surrounding whitespace — tag values come from the wild — so
every import path (files, providers, the playlist import, the
metadata editors) stores clean fields; a whitespace-only title counts
as empty (the file-name/host fallback applies).
`setInLibrary(id, bool)` flips the flag, `updateItem(item)` replaces an
item's metadata in place and
`registerPlay(id)` records one play (`playCount++`, `lastPlayedAt =
now`) — all three emit the store's `itemChanged`, are owned by their
users (the library flips flags, the metadata editors update items,
`PlaybackManager::playItem()` is the sole play counter — the single
choke point for every track start) and all write through. Load order in
`main.cpp`: `SettingsManager` (settings.json) → store → library (the
membership mirror is built from the store's flags) → watcher →
playlists; there is no save on quit (write-through).

The database holds **content only** (library/playlist stuff). Any other
state lives in the `SettingsManager` as registered settings in
`settings.json` — user settings and session state alike. Introducing a
new user setting: register it from the
owning component's ctor (idempotent — a taken key is refused), read it
with `value(key)` (it falls back to the registered default) and write it
with `setValue(key, …)` (write-through, emits `changed(key, value)` —
connect to that to keep views in sync; the preferences dialog's
"Settings" topic — `configpage.{h,cpp}`, the about:config-style browser
— renders it for free, including a type-specific value editor and a
reset-to-default). There is no save-on-quit. Reference:
`PlaybackManager` (`playback.player` — its `audioLevels`/`audioWaveform`/
`audioFrames` re-emits are one-for-one and carry the stream's sample
rate, which the oscillogram's audio-clock scroll and the spectrum's
and the spectrogram's FFT windows use), `LibraryWatcher`
(`library.watch.dirs`), `LayoutManager` (`layout.edit_mode`) and the
visualization widgets (`visualization.fps`, the paint rate — registered
by the shared `paintrate.{h,cpp}` helper from the widgets' ctors, the
later calls no-ops; the oscillogram paints from a timer at the rate,
its trace scrolling at the audio's rate, one column per closed tap
bucket — see its class comment).
