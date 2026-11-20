# Key data & signals

- The current track is a `MediaItem` (mediaitem.h) held by the
  `PlaybackManager` singleton: read via `playingItem()`, changes via the
  `playingItemChanged(const MediaItem&)` signal — emitted on every track
  start **and** on stop (with an invalid `MediaItem`), so connecting to it
  alone is enough to keep a display in sync. Also: `stateChanged`,
  `positionChanged`, `durationChanged`. `MediaItem::toVariantMap()` is the
  key/value view of all metadata (QVariantMap, so keys iterate in sorted
  order). `playItem()` is provider-aware: the item's provider (looked up
  by the id recorded at import) may switch in its own `AudioPlayer`
  driver (`setPlayer`, so it stays active for subsequent items until the
  next switch) and resolve the stored source to a stream URL —
  **asynchronously** via `resolvePlayback` (a resolution can be a network
  round-trip), the stream played as an item copy so the display item
  (`m_playingItem`) stays the original. A resolution is superseded by a
  newer `playItem` or `stop` (a token makes the stale callback a no-op,
  so a slow track never starts over a newer one). A provider that
  produces no stream and owns no player is unplayable: `playItem` stops
  (the stored source is never handed to a player that cannot play it)
  and emits `playbackError(const QString &message)` — a user-visible
  failure the `MainWindow` shows as a transient status-bar message, so
  a silent stop is not read as "the track ended". The in-flight
  resolution is observable as `resolvingItem()` / the
  `resolvingItemChanged(const MediaItem&)` signal (set when `playItem`
  starts a provider resolution, invalid when it lands or is
  superseded), and a failed play as `failedItem()` (set on any play
  failure — the no-stream failure, a missing local file (checked
  before the player is asked), or `AudioPlayer::errorOccurred`; kept
  across stops, cleared by the next `playItem`) — the media views'
  status column (see the playlist
  bullet) is derived from these plus `playingItem()`/`state()`. Every
  track start is counted via `MediaItemStore::registerPlay()`.
- **Volume**: `PlaybackManager` owns the level, the mute over it and
  the **boost**, as the registered settings `playback.volume` (0-100) /
  `playback.muted` / `playback.volume_boost` (0-200, 100 = no change;
  `loadSettings()` reads them; `setVolume`/`setMuted`/`setVolumeBoost`
  write through). The old slider ceiling (`playback.max_volume`, 1-200)
  was the user's boost headroom: `loadSettings()` migrates it once
  (an explicit `playback.volume_boost` wins) and `resetToDefault`s the
  key. `loadSettings()` also **(re)binds** the manager to the current
  `SettingsManager`: the settings page's **live edits** of those keys
  arrive through its `changed()` signal (an idempotent handler — the
  `set*()` methods' own writes land there as no-ops — moves the state
  and re-emits the signals, so a widget follows a config-page edit; a
  `SettingsManager` edit of `playback.volume` does **not** trigger the
  unmute side effect, which is a `setVolume` interaction semantic).
  `setVolume` clamps to 0-100, emits `volumeChanged` only on a real
  change, and **unmutes when muted** (the user just expressed a
  loudness); `setMuted` keeps the stored level — the mute is a flag
  over the level, never a zeroed level; `setVolumeBoost` clamps to
  0-200 and emits `volumeBoostChanged` on a real change. The state is
  pushed to the active driver through three **optional** capabilities
  of `AudioPlayer` (like `durationMs`/`setPositionMs`: `supports*()`
  reports, the default is a no-op): `setVolume` (the level as a linear
  0.0..1.0), `setMute` (the mute flag — a driver without it gets the
  old way, the mute pushed as a 0.0 level, the stored level kept) and
  `setBoost` (the gain as a linear multiplier on the frames the driver
  sends out — 1.0 is a no-op, above 1.0 amplifies and can clip, which
  is the nature of the feature). A driver switch applies the current
  level, mute and boost to the fresh player (`setPlayer` →
  `applyVolume`/`applyBoost`) and emits `volumeSupportChanged` when the
  volume capability changes (the volume widget disables its controls
  on a driver without it). **The server is the source of truth**: a
  driver whose output sits on a server (the PulseAudio sink input)
  reports the level and mute it reads back from the server through
  `AudioPlayer::sinkVolumeChanged(float)` / `sinkMuteChanged(bool)` —
  an external client (pavucontrol) moving the app's stream arrives as
  such a report — and the manager's slots move the state **and the
  settings** to the reported values and re-emit; they never push the
  previous level back (an echo of doob's own push lands as the same
  value, the comparison makes it a no-op). The MPRIS `Volume` property
  follows the same state (muted → 0.0).
- `PlaybackManager` follows **user-driven** playlist current-index changes
  (a double-click plays that track); its own index changes
  (next/previous/auto-advance) are guarded by the `m_followingPlaylist`
  flag so the follow logic doesn't re-trigger. The active playback driver
  is a `PlayerRegistry` id stored as the `playback.player` setting
  (`setActivePlayer()` validates against the registry, persists and
  emits `activePlayerChanged`; the choice applies at the next
  `ensurePlayer()`, so the current player keeps playing). The built-in
  fallback is the `NullPlayer` — not a registry entry, created directly
  by `createDefaultPlayer()`: it plays nothing (a `play` answers
  `errorOccurred`), so a doob without the FFmpeg plugin fails the
  track visibly instead of playing into the void. With no explicit
  selection (an empty setting) — or one left by a removed driver —
  `ensurePlayer()` falls back to the registry's **preferred default**
  first: a plugin advertises its driver via
  `PlayerRegistry::setPreferredDefault()` in `initialize()` and clears
  it in `shutdown()` (an id that is not registered is refused; a stale
  one is harmless — `create()` answers nullptr), so the FFmpeg plugin
  present makes FFmpeg the out-of-the-box backend; only with no
  preferred default does the virtual `createDefaultPlayer()` apply.
- **Gapless** is an opt-in driver capability on `AudioPlayer`
  (`supportsGapless()`, default false; `bool queueNext(const MediaItem&)`,
  default false). The manager ↔ driver **contract**:
  - `queueNext(item)` answers **true only when the item is really
    queued** — it will be played when the current item ends. False =
    the driver ends the current item plainly, and the manager plays
    the armed item itself (a gap, never a stuck state). Idempotent per
    source: a re-queue of the same source is a no-op that **reports
    queued (true)** — it is queued — and after a seek (which kills the
    driver's queue) the same source re-queues fresh.
  - `finished()` fires **exactly once per item**, at the moment the
    item's **last audible frame leaves the output** — not the
    container's end (a short track decodes in milliseconds, and its
    ring tail must play out first), not the decoder's eos — **or the
    moment its tail is discarded by a user seek/pause, whichever is
    first** (a seek or pause in the play-out window resolves the
    boundary at that moment — the driver's section in docs/plugins.md).
    When a queued item is already playing (the handoff), the boundary
    is still the *current* item's finished.
  - The audible invariant: **the queued item's audio never begins
    before the current item's last frame has played** — no overlap,
    no crossfade. The display (`playingItem`) leads the audio by the
    pre-roll (standard for gapless players).
  - The driver may kill the queued decode on a **pause** (a resume
    re-decodes it from the top — the driver keeps the item) and on a
    **seek** (the manager re-queues). Nothing asks the driver to
    unqueue.

  **The manager's arm** (`maybeArmGapless`, on `positionChanged`):
  while Playing with a known duration, inside the final lead, repeat
  not `RepeatTrack`, a next row exists, the driver has the capability,
  and the next item is **playable by the active driver** — a local
  item, or a provider that resolves a stream URL and owns no driver.
  A next item whose provider **owns a player** is not armed: the
  driver switch at the boundary is the plain path's job (a gap is
  accepted there). The lead is `kGaplessLeadMs` (3 s) for a local
  next, `kProviderGaplessLeadMs` (10 s) for a provider-resolved one
  (its stream has to connect and buffer first — the short lead is
  eaten by the connection). The arm advances the playlist index
  **now** (the display leads), calls `queueNext` — **provider-aware**
  (`queueGaplessItem`, the same seam `playItem` uses: a provider
  item's stored source is the provider's watch page, so it is
  resolved via `resolvePlayback` and the resolved copy is queued on
  the landing; a stale or failed landing is a no-op) — and latches
  `m_gaplessArmed` (a boolean, not the item's id — the arm switches
  the display item, so an id guard would re-arm within the same
  window and skip a track) and `m_gaplessQueued` (the `queueNext`
  result). A seek re-queues (the driver's queue died with the seek).
  `RepeatTrack` never arms (its replay is the current item); a driver
  without the capability keeps the plain `finished`-advances path
  untouched.

  **The boundary** (`onPlayerFinished`): a `finished` that lands
  after a stop is a no-op (the playback is dead). If `m_gaplessArmed`:
  the latches clear, and the finished is a **no-op only if
  `m_gaplessQueued`** — the handoff already happened (the display is
  on the armed item, the audio is playing it; a second advance would
  skip a track). If the queue never reached the driver (a slow
  resolution, a killed queue), the driver ended the old item plainly
  and the manager plays the armed item itself through the
  provider-aware `playCurrent` (a gap, never a stuck state).
  Otherwise: the plain advance (shared with `next()`). A stop, a new
  play and a driver switch clear the latches (the outgoing driver's
  queue is gone). The driver's own implementation of the contract
  (the FFmpeg two-worker handoff, the gate, the boundary accounting)
  is in docs/plugins.md; the built-in null driver stays non-gapless
  (it has no backend at all).
- Playlist changes come from `PlaylistManager`:
  `playlistChanged(quint64 playlistId)` (carries the affected playlist so
  each PlaylistsWidget tab refreshes independently), `currentIndexChanged`,
  `currentPlaylistChanged`. Invariants: the manager always holds at least
  one playlist — the default "Playlist" is restored by `reset()` (tests use
  it in init/cleanup); `removePlaylist` never leaves it empty (closing the
  last one creates a fresh empty "Playlist" and makes it current; removing
  the current one makes the first remaining playlist current);
  `addPlaylist` does not change the current one; switching the current
  playlist stops playback. A playlist holds **item ids** (`QList<quint64>`),
  never item data: `itemAt()`/`currentItem()` resolve through
  `MediaItemStore::resolve()`, so an in-place metadata update is visible
  without any copy to refresh. Import:
  `importSources(playlistId, sources, index = -1)` accepts local
  files, provider-claimed sources and plain `http(s)` stream URLs, and is
  **asynchronous and serialized** (mirrors the library's worker pattern —
  local tag reads of sources the store does not hold run on a worker
  thread; provider sources are fetched from their provider on the GUI
  thread, **sequentially at the import cursor** — each result advances
  the cursor, which starts the next fetch — so all reads resolve in
  source order; plain stream URLs are built on the spot; sources the
  store already holds are reused without a file read via `ensureItem`;
  a source whose provider vanishes before its fetch lands, **or whose
  fetch fails (an empty `TrackMeta`)**, contributes no row and no item
  — a re-add can retry it, and the import always finishes), inserts the
  rows in source order at `index` (a negative index appends, the default
  — each row lands at `index + landed`, so the block ends up exactly
  where the drop pointed, shifting `currentIndex` by the rows inserted
  before it): re-importing a source the target already holds appends
  another row, because a playlist may hold the same item any number of
  times — each row writes through, and the finished import emits
  `importFinished(playlistId)`; `importing()` gates
  concurrent calls (a second one is refused with a `qWarning`).
- The File menu's "Open..." import lands in the `import.target`
  setting's playlist (`MainWindow::importTargetPlaylistId()`):
  `current` (the default, today's behaviour) is
  `PlaylistManager::currentPlaylistId()`; `focused` is the playlist of
  the `MediaListView` that has keyboard focus —
  `MediaListView::focusedPlaylistId()` walks
  `QApplication::focusWidget()` up the ancestor chain, a tab view
  resolving by its `playlistId` property and the current playlist's view
  (no property) by the current id — falling back to `current` when no
  view has focus. The CLI path (no focus) and the view-targeted drops
  are untouched by the setting.
- **Follow playback** is a per-widget display state (default off, in the
  layout document — arrangement, not a user setting):
  `MediaListView::setFollowPlayback(bool)` / `followPlayback()`, and
  `scrollToPlayingItem(item)` scrolls the item's row into view
  (`MediaListModel::rowOf(id)` — the item's **first** row; a stop, an
  invalid item, or an id not in the model is a no-op). **Scroll only,
  never a selection change** — the selection is the row the Delete /
  copy-row shortcuts act on, so a track change mid-browsing must not
  move it. `CurrentPlaylistWidget` wires it to
  `playingItemChanged` and its list refresh; `PlaylistsWidget` (the
  state is the widget's, applied to every tab view — a rebuilt tab
  inherits it) to `playingItemChanged` for all tab views and to a tab
  switch for the now-shown one. The edit-mode context menu's checkable
  "Follow playback" toggles it; it is persisted as `followPlayback`
  only when on, so a fresh layout document cannot pin it.
- Library changes come from `LibraryManager`: `itemAdded(const MediaItem&)`,
  `itemRemoved(const MediaItem&)` (carries the item **with the flag already
  cleared** — `inLibrary` is false in the payload; the item itself stays in
  the store), `itemChanged(const MediaItem&)` (a library item's metadata was
  replaced in place — the metadata editor, any writer), `itemsCleared()`,
  plus `busyChanged(bool)` for file operations and
  `sourceSkipped(const QUrl &, const QString &)` (a source an import
  operation skipped — its provider's metadata fetch failed; the source as
  pasted and the provider's failure hint, which may be empty; a re-add can
  retry it). `addFile`/`addFiles`/`scanDirectory`/`syncDirectory`/
  `readMetadata` are **asynchronous and serialized**: the call
  validates on the GUI thread and returns immediately; the tag reading runs
  on a worker thread (dispatched with a queued `QMetaObject::invokeMethod`);
  results are applied back on the GUI thread as per-item signals followed by
  `busyChanged(false)` (queued order guarantees the per-item signals land
  first). A second operation while one is running is refused with a
  `qWarning`. Sources split by kind: **local** files to the worker
  (TagLib), **provider-claimed** sources to their provider's
  `fetchMetadata` — dispatched on the GUI thread, because provider
  objects are GUI-threaded and the worker never touches them — and plain
  **http(s)** stream URLs need no read (added synchronously, kind
  `Remote`). Completion is a counter: the worker's finished signal plus
  every in-flight provider callback must land before the operation ends.
  A failed provider fetch (an empty `TrackMeta`) skips the source — no
  item is created (like the worker's missing-file path) — and the
  operation still finishes, so a broken provider degrades to missing
  items instead of a stuck spinner. The `addFiles(sources, metadata)`
  variant takes a `QHash<QString, TrackMeta>` of metadata the caller
  already holds (the import wizard's review: the list's own listing, the
  fetched metadata of the plain sources): a claimed source with provided
  metadata is imported straight from it — synchronously, no provider
  re-fetch — and a claimed source without it is fetched as usual (a
  failed fetch skips it, as before).
  `readMetadata` is the read-only operation: it re-reads the tags of the
  given items' local files (worker thread) and hands the result — a
  `QHash<quint64, TrackMeta>` keyed by item id — to its `finished`
  callback on the GUI thread; nothing is written, the caller decides what
  to do with the read values (the metadata editor previews them in its
  table, and on confirm writes them through the store's `updateItem`,
  which lands here as `itemChanged` for a library item). `progress`
  reports (resolved, total), `(0, total)` up front; items that are not
  local files, or whose file is missing, are simply not in the result. A
  call while an operation is running is refused (the result is empty).
  Adding a
  file the store already holds without the flag (imported into a playlist
  before) reuses the existing item under the same id and flags it. Removing
  a library item clears the flag, and the item is **pruned at once** when
  nothing references it anymore (`pruneIfUnreferenced` — the itemRemoved
  payload is captured before the prune, which may delete the row; a
  playlist-referenced item stays in the store, resolving as before). UI: observe `busyChanged` to disable
  the toolbar buttons and show a busy indicator (see `librarywidget.cpp`).
  Playlist views repaint an updated row in place from the store's
  `itemChanged` (see `currentplaylistwidget.cpp` / `playlistswidget.cpp`).
   `syncDirectory(dir)` brings the library in line with a directory:
   supported files in it that are not in the library are added
   (`itemAdded`), and library items whose local file lived under `dir`
   but is gone are removed (`itemRemoved` — and pruned when unreferenced,
   as any removal). A missing directory syncs to "everything under it is
   gone". It is the primitive `LibraryWatcher` runs for a watched
   directory on every change.
- `LibraryWatcher` (librarywatcher.{h,cpp}) is a singleton that owns the
  library's watch directories (persisted as the `library.watch.dirs`
  setting, see docs/persistence.md) and a `QFileSystemWatcher` (the filesystem watcher is not
  recursive — the tree's subdirectories are watched individually, and
  `watchTree` re-registers new ones as they appear). `load()` (called in
  `main.cpp` after the library) restores the list, starts watching and
  runs the initial import; `addWatchDir(path)` (persisted, imports the
  contents) and `removeWatchDir(path)` (persisted, stops watching — the
  items stay in the library) mutate the list and emit
  `watchDirsChanged()`. A `directoryChanged` event maps to its top-level
  watch directory and requests a `syncDirectory` for it; requests are
  **queued while the library is busy** and drained one at a time when it
  goes idle (a directory's own watch is not recursive, so nested changes
  are covered by the top-level `syncDirectory`). `clear()` is the test
  seam (drops
  the list, the queue and the watches). The settings page
  (`librarysettingspage.{h,cpp}`, the "Library" preferences topic) is a
  thin view over this list: a `watchDirs` list plus Add (a
  `chooseDir()` protected virtual — test seam, like `LibraryWidget`'s
  choosers) and Remove buttons.
- `LibraryWidget` presents the library in four view modes (an exclusive
  toolbar toggle group, the same switch in the edit-mode menu; persisted
  in the layout document as `view`, only when not the default): **List**
  (the flat `MediaListView` over a `MediaFilterProxyModel` — the only
  sortable view: a header click cycles ascending → descending → off) and
  three group trees — a `MediaGroupView` over a `MediaGroupModel` proxy
  on the same `MediaListModel`, so switching modes changes only the
  arrangement: **Artists** (artists → albums → tracks), **Albums**
  (albums → tracks) and **Artist + Album** ((artist, album) pairs →
  tracks), each fully collapsed on a rebuild. The toolbar's search box
  filters the active presentation by case-insensitive partial match over
  the item's data columns (the proxy in List mode, the group model in
  the tree modes). The library never plays tracks, so the status column
  is unavailable there (hidden and out of the header menu). The toolbar
  add menu carries one source the playlist widgets' menus don't: an
  "Add folder..." action (`scanDirectory`).
- `LibraryWidget::chooseFiles()`/`chooseDirectory()` are protected
  virtuals — a test seam, because QFileDialog is untestable offscreen
  (tests override them in a subclass). The "Add by URL..." entry and
  a drop of non-local URLs on the library views (see the drag & drop
  bullet) open the import wizard (`UrlImportDialog`,
  urlimportdialog.{h,cpp}) —
  non-modal, single instance (a second trigger raises the open one),
  UI-only: it never touches the library, the widget connects its
  `sourcesConfirmed(sources, metadata)` signal to
  `LibraryManager::addFiles(sources, metadata)`. Stage 1 (input): one
  URL per line, parsed with `QUrl::fromUserInput` — a URL stays a URL,
  an absolute local path becomes a `file:` URL; a parse that yields no
  source stays on the input stage. Stage 2 (working): the slow phase,
  with a status line and a cancel — `ListExpander::discover`
  (listexpander.{h,cpp}) asks every claiming provider for its list
  entries (`fetchList`, asynchronously — it reports every claimed url
  as it answers, with possibly-empty entries, and its done fires once
  all fetches are **started** and answered, so a synchronous provider
  cannot fire it early), then the metadata of every non-list source is
  fetched (`fetchMetadata`, to fill its review row). A batch with no
  claimed url, or with no collection behind its claimed urls (no list,
  no failed read, no empty collection), fast-paths: confirmed
  immediately, no review. Stage 3 (review): a tree of
  sections — one per discovered list (titled "Title  (N)"), its videos
  as rows from the list's own listing (`ListVideo`'s title/artist/
  album/duration — no per-video fetch), a section with no rows for a
  legitimately **empty collection** (a claimed url whose list came
  back empty without an error — `ListEntries::isCollection`), a
  "Failed sources" section for a claimed url whose collection read
  **failed** (`ListEntries::error` — the row shows the url and the
  reason, is not checkable, and is never imported: a failed expansion
  must not turn the container link into a single library item), plus
  an "Other sources" section for the plain sources (their fetched
  metadata; a failed fetch shows the url; a pasted local path shows
  the file name) — all checked by
  default, the section head toggling the whole list (the check-state
  propagation is manual: this Qt's QTreeWidget cascades it in neither
  direction, and it skips the not-checkable failed rows — the
  select-all and the head toggle do too). The strip above the tree: a filter (case-insensitive on
  the row's title/artist, display-only — hidden rows keep their check
  state and hidden checked rows still count for the Add button) and
  Select all / Select none, which act on the *visible* rows only (the
  big-list workflow: clear the selection, filter, pick the wanted
  rows). A row's title/artist/album can be edited from the row's
  context menu before the Add — the row alone, or the selected rows
  in bulk (a small modal, empty fields keep the gathered value); the
  edits ride the confirmed metadata (an import reuses the store's
  item for a source it already holds, so an edit applies at
  creation only). A list with nothing checked contributes nothing
  (the list
  link is not a source of its own); a cancel confirms nothing.
  `ListExpander::apply` turns the checked set into the import batch:
  each checked list contributes its checked videos in list order, the
  checked plain sources as pasted, in batch order, deduped against
  what came earlier. The checked sources' metadata rides back as the
  `metadata` argument: the import uses it as-is (a source with provided
  metadata is not fetched again — the only yt-dlp run for a list import
  is the list expansion itself).
- Media view drag & drop: the **model** (`MediaListModel`) owns
  `mimeTypes`/`mimeData`/`dropMimeData` — MIME type
  `application/x-doob-media`, payload `{"ids": [...], "rows": [...],
  "origin": "…"}` (one id entry *and* one row entry per dragged row, in
  the same order — `ids` repeats when the rows repeat an item, `rows`
  numbers the rows in the source model; a bare id array is the older form
  and parses with no rows and an empty origin) — and emits
  `itemsDropped(QList<quint64>, QList<int>, QString, int)`, which
  `MediaListView` only re-emits. The origin names the source
  (`"library"` or `"playlist:<id>"`, via `setOriginId` /
  `MediaListModel::libraryOrigin()` / `playlistOrigin(id)`):
  LibraryWidget sets it in its ctor, the playlist widgets set it when
  (re)building their views. The view/model never reorder themselves:
  every media view is a DragDrop source and target (the library views
  accept the drop but nobody acts on `itemsDropped` — the library has no
  sequence), and `CurrentPlaylistWidget`/`PlaylistsWidget` decide move
  vs insert **from the
  origin** — a drag that started in the target playlist's own rows is a
  reorder (the payload's `rows` name exactly the rows that move),
  anything else is an insertion (one row per dragged row, in order,
  repetitions included — id membership alone cannot decide: a library
  drag may name an item the playlist already holds). `PlaylistManager`
  does the list math; item data is resolved against the item store at
  display and playback time, so a drag copies no metadata.
  (QAbstractItemView's InternalMove was rejected: it depends on
  drag-session state and is not unit-testable.) **External drops**
  (outside the app) are plumbed by the same model through two more
  signals: `filesDropped(urls, index)` for local files (the file
  manager) and `urlsDropped(urls, index)` for non-local URLs (a
  browser) — a mixed drop emits both, and the widgets decide per half
  (the models/proxies/views only forward): the library imports the
  files (flattening dropped M3U/PLS files) and opens the import wizard
  pre-filled with the URLs (an open wizard is raised, not replaced — a
  second `start()` would replace its in-progress flow); the playlist
  widgets import the files and ignore the URLs.
- A playlist is a **sequence that may repeat items**: the item store is
  unique by source (the `media_items.source` UNIQUE constraint; the
  library is the subset of items with the `in_library` flag), but a
  playlist may hold the same item any number of times, and no add path
  dedups —
  `addItems` (the library's double-click), `insertItems(Into)` (drag
  insertion) and `importSources` (file import) all append one row per
  entry, in order, so a source that repeats an item (an external playlist,
  a re-run of the same folder) is preserved as-is. With duplicated items
  an id cannot name a row, so row operations are **row-based**:
  `moveRows(InPlaylist)` moves exactly the dragged rows (tracking
  `currentIndex` row by row through the move), the Remove button removes
  the selected *rows* (`MediaListView::selectedRows`), a double-click
  targets the *clicked* row, and insertions shift `currentIndex` by the
  rows actually inserted. The **status column** (the headerless first
  column, `MediaListModel::Playing`, historical key `"playing"`, header
  menu label "Status") shows a glyph on the **current row of the current
  playlist** for the state that row's playback is in
  (`MediaListModel::setRowStatus(row, RowStatus)`): ▶ playing, ⏸ paused,
  ↓ a provider resolution (a download) in flight
  (`PlaybackManager::resolvingItem` / `resolvingItemChanged` — set when
  `playItem` starts a provider resolution, cleared when it lands), ⚠ the
  last play attempt failed (`PlaybackManager::failedItem` — set on any
  play failure: the provider resolved no stream, a local file is missing
  (checked in `playItem` before the player is asked), or the player
  reported an error (`AudioPlayer::errorOccurred` — a failure is not a
  track end: the playback stops, it does not auto-advance); kept across
  stops, cleared by the next play attempt). Each glyph carries a tooltip
  (`MediaListModel::rowStatusText`). The other tabs show no marker, so a
  duplicated item is marked in exactly one row. The tab of the current
  playlist likewise carries the
  play icon (before its title) while the playback state is **Playing**;
  pause and stop clear it (the icon follows `stateChanged` +
  `playingItemChanged` plus a refresh in `rebuildTabs`, so a widget
  created mid-playback starts correct).
- `WidgetConfiguration` is a `QVariantMap`. `WidgetContainer::storeContainer()`
  writes `config["type"] = widgetId()` and `restoreContainer()` dispatches on
  it, so the layout persistence works with no extra work once the id is
  registered in the `WidgetRegistry` (see the widget checklist in
  AGENTS.md).
