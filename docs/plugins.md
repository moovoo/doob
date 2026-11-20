# Plugins

A plugin is a Qt plugin `.so` (Q_PLUGIN_METADATA, IID `doob.plugin/1`)
that subclasses `DoobPlugin` (doobplugin.h) and, in `initialize(DoobApi*)`,
registers itself into the extension points below — and, in `shutdown()`,
**unregisters everything again** (the host unloads the library right
after, which deletes the plugin and whatever it parented; a registration
kept in the core would then dangle). `DoobApi` (doobapi.h) is the facade
the host hands over: the singletons (including `settingsManager`, the
`SettingsManager` — the plugin registers its settings in `initialize()`
and unregisters them in `shutdown()`, keys conventionally prefixed with
the plugin's id, `"tubemusic.playback_mode"`; the stored values survive
the unregistration), `window()` (nullptr before `LayoutManager::show()`)
and `pluginDataPath(name)` (the plugin's data directory
`<data dir>/plugins/<name>` — the files a plugin keeps that are not
settings: a cookie snapshot, a cache; the settings themselves live in
the central `settings.json`). `api->version` gates the ABI — a plugin
bails (a no-op) when it is not one it understands. The current version
is 4 (the v3 → v4 step added the `arts` enrichment registry; a plugin
built against v3 bails).

`PluginManager` (pluginmanager.{h,cpp}) loads them at startup — in
`main.cpp`, **before** `LayoutManager::show()`, so a saved layout may
reference a plugin's widgets — scanning the plugin directories in order:
the `DOOB_PLUGIN_DIR` env var, `<app dir>/plugins`, and the GenericData
location's `plugins` dir. A `.so` whose metadata carries a different (or
no) IID, or that exposes no `DoobPlugin`, is skipped with a `qWarning`;
the rest load. `shutdownPlugins()` (on `aboutToQuit`) calls `shutdown()`
in reverse load order, then unloads (deleting the instances).
`loadFromDir(dir)` is the per-directory step (also the test seam).

The core is the plugin's ABI: `doob_lib` is **SHARED on all platforms**
and the plugin links it, so a loaded plugin and the host process share
one copy of the singletons (a static core would give the plugin its own).
`cmake --install` ships the library plus the plugin API headers
(doobplugin.h, doobapi.h, pluginmanager.h — a plugin's own tests load
the built .so through it — settings.h and settingsmanager.h, the three
registries, the interfaces a plugin implements and the data types they
use) under `<prefix>/include/doob`
and a CMake package under `<prefix>/lib/cmake/doob`:
`find_package(doob REQUIRED)` (point `-DCMAKE_PREFIX_PATH` at the
prefix) finds the Qt components the public headers use and provides the
imported target `doob::doob_lib` (headers resolve as `<doob/foo.h>`).
The target's interface carries only what a plugin's compile line needs —
Widgets/Sql/Network/DBus (the public headers use them) —
while Svg and TagLib are PRIVATE (implementation details). `examples/exampleplugin/`
is a minimal, self-contained plugin (one `example:`-scheme provider,
the full initialize/shutdown lifecycle) built standalone against the
installed API — the template for writing a plugin.
`plugins/` holds shippable plugins (each its own directory +
CMakeLists). The root build builds them alongside the core by default
(`DOOB_BUILD_PLUGINS`, on for development): in-tree a plugin's
CMakeLists links the `doob_lib` target directly and its `.so` lands in
`<build>/plugins` — one of the `PluginManager`'s search paths — so a
fresh `cmake --build build` yields a doob that loads them. Each plugin
also builds standalone against the installed API (`find_package(doob)`
with `-DCMAKE_PREFIX_PATH` at the prefix), the way
`examples/exampleplugin/` does. **A plugin is self-sufficient about
building and testing itself**: its CMakeLists builds the `.so` *and*
the plugin's own tests (in `plugins/<plugin>/tests/`, one `QTEST_MAIN`
class per file, registered with CTest from the same file, the tests
loading the `.so` from `<build dir>/plugins` — next to the test
executable — and compiling the plugin's pure-C++ sources straight in,
never linking the `.so`'s meta-object). `plugins/tubemusic/` is the Tube
provider: the URL claim logic (`accepts`: watch/shorts/youtu.be
shapes, no channel/search pages), metadata (`yt-dlp -J` mapped onto a
`TrackMeta`) and playback (`resolvePlayback`, m4a/AAC preferred, two
modes from the `tubemusic.playback_mode` setting) all run the `yt-dlp`
binary through the plugin-local `YtdlpRunner` (ytdlp.{h,cpp}) — the
runner owns the `tubemusic.ytdlp` binary-path setting (re-read on every
run) and a process **pool of 4** shared by all runs: a finished
process's pipes stay open until the object is destroyed, so a per-run
process would exhaust the file-descriptor limit on a big import; runs
beyond the pool queue (FIFO) and every run is guaranteed exactly one
callback — a failed start (`QProcess::FailedToStart`) answers empty
through `errorOccurred`, so a missing binary degrades to failed fetches
instead of hanging an import cursor; a 30 s watchdog kills a hang
(tagged per process, so a reused process's successor is spared).
The provider owns the cookie policy (the cookies file, the stale-cookie
fallback) and prepends its cookie args to the runs.
Every run also logs its full command line (debug) and, on finish,
its **stderr in full**: a warning with the exit code when the run
failed, a debug line when it succeeded — a 403/429/bot-check is
undiagnosable without yt-dlp's own words.
`canonicalUrl`
strips a pasted link to the bare video id (the `list=`, `start_radio=`,
`si=`, `t=` arguments are dropped; the host is kept as pasted), and
`fetchList` expands a list reference (a `list=` argument — including
`RD…` radio mixes) via `yt-dlp -J --flat-playlist --yes-playlist` into
the list's title and its videos (in list order, canonical watch URLs
plus the flat entry's display metadata — title, the channel as artist,
the duration — which the wizard's review shows as-is; a link without a
`list=` argument answers empty immediately, no binary run). **Cookies**: a cookie-less client is
throttled by YouTube under import volume, so the settings page
(browser combo + "Fetch cookies" + "Open YouTube" + a status line)
exports the chosen browser's cookies via
`yt-dlp --cookies-from-browser <b> --cookies <file>` (the export
happens before yt-dlp's URL validation, so the run's exit code is
irrelevant — the file is the result) into
`<data dir>/plugins/cookies.txt`, keeping only the google/youtube
domains (the raw export would copy every other site's session into
the data dir). The metadata and list runs pass `--cookies <file>`
when it is non-empty — **the playback resolution in stream mode does
not**: a session-bound resolution yields a stream URL that only the
same client context can fetch (the player's bare fetch gets 403),
while a cookie-less one is self-contained. **Stale-cookie fallback**:
YouTube rotates sessions, and a run with invalid cookies 403s where
cookie-less would work — a failed with-cookies run is therefore
retried cookie-less and the breakage is remembered for the provider's
lifetime (`cookiesBroken()`, the settings page shows it) until the
next fetch. Both the resolution
(`tubemusic: resolved <source> -> <stream>`) and the PlaybackManager
(`playing item <id> from <url>`) log the URLs they pass around.
**Playback modes**: `"stream"` (the default of the other mode is
`"download"`) resolves a fresh signed stream URL per play
(`yt-dlp --get-url`, no cookies — see above); instant start, the
playback dies with the network connection. `"download"` (the default)
caches a local copy first — `yt-dlp -o <cache>/<id>.%(ext)s` (yt-dlp
writes a `.part` file and renames on completion, so a failed download
leaves no finished file; the run gets a 10-minute kill timeout
instead of the 30-second one). A second `resolvePlayback` of the same
id while a download is in flight joins it (one yt-dlp per id — the
gapless pre-roll resolution and the plain play of the same item, a
double play press, must not race on the same `.part` file); the
completion delivers the file URL (or empty) to every queued callback.
`"download"` playback is then network-independent
and a cached track resolves to the same `file://` URL instantly
(offline-capable). The cache lives at `<data dir>/plugins/tubemusic/
cache/` and is kept at or below the `tubemusic.cache_max_mb` cap (default 512,
the settings page's spinbox; the page also shows the current usage):
after every download **and** every cache hit the least-recently-
modified finished files are deleted until the cap fits.
`plugins/spotify/` is the Spotify provider and playback driver: the
Web Playback SDK runs in a separate `spotify-web` helper process (a
QWebEngine page + a QWebChannel bridge; JSON lines over stdin/stdout,
`SpotifyIpc` on the driver side) — the SDK's audio must land on the
plugin's private audio server (`PULSE_SERVER` is in the helper's
environment before its WebEngine tree forks; the main doob process is
never pointed there), and the driver reads the private server's sink output from a
`RingBuffer` (the core's, shared with the FFmpeg driver), runs the DSP
chain and relays to the user's default sink (`SystemSinkRelay`). The
private server is the plugin's own `pulsestub`
(`plugins/spotify/pulsestub.cpp`): a minimal fake PulseAudio that
speaks the native protocol on its own AF_UNIX socket and serves one
null sink (`doob`). The plugin runs it **in-process**
(`PrivatePulse` owns the server's thread and socket; the helper's
`PULSE_SERVER` points at the server's socket in the per-launch
directory) — no daemon, no fifo: the server's sink callback remaps
each stream's PCM to the capture's pinned format (s16le, 44100,
stereo) and writes it to the ring, whose backpressure paces the whole
chain (a full ring stops the server's write requests, the client's
buffer fills, the WebEngine slows to real time). The server's write
requests are issued a persistent lead ahead of the play position
(`pulsestub::Server::kLeadMs` — the client's writable headroom stays
at the lead): the pacing runs on the server's thread, which a
deschedule can delay, and a zero headroom turns that delay into a
~1 ms hole in the audio (a pop); the server's thread asks for a
raised priority (SCHED_FIFO, a high nice — best effort, the lead
carries an unprivileged session). `CapturePump` (a self-paced 20 ms
thread, silence on an underrun) pulls the ring the same way the
FFmpeg driver's output does. The `pulsestub` executable
(the same server as a separate process, fifo or discard output) is
kept as the test fixture — the FFmpeg plugin's `pulseout` test runs
it in discard mode.
Starting a track is a Web API call (`PUT /me/player/play`, the
helper's own token and device id); pause/resume/seek/volume are the
SDK's transport methods. **The device's lifecycle**: everything is
lazy (the private server, the helper and the capture start on the
first play) and stays warm afterwards. A stop is a release, not a
teardown: the driver sends the SDK's `disconnect` (the session closes,
the device drops out of Spotify Connect — while doob is idle, no other
client can play through it, and Spotify does not see doob's queue) and
the next play re-registers with `connect` (the device id is new on
every registration — the `ready` event carries it, the play names it;
a connect the play rides on can fail — a bad token, no network — and
its refusal fails the play). A full teardown (a dead helper, a sign-out, a re-login, the
plugin's shutdown) rebuilds the server, the capture and the helper
together on the next play. The
helper also runs with
`--disable-features=HardwareMediaKeyHandling` in
`QTWEBENGINE_CHROMIUM_FLAGS` (set before `QApplication` — Chromium is
not started yet): Chromium's media-key listener would register a
second MPRIS player (`org.mpris.MediaPlayer2.chromium.instance<Pid>`,
Identity "doob-spotify-web") on top of doob's — doob is the player and
owns the media keys, the helper stays off the bus.
`plugins/ffmpeg/` is the FFmpeg playback driver (the preferred
default, via `PlayerRegistry::setPreferredDefault`): a `DecodeWorker`
(a file: / http(s): URL → interleaved float32 stereo at the fixed
`kOutputRate = 48000` — PulseAudio resamples to the device rate)
feeds a `RingBuffer` (a fixed 2 s; the writer blocks on a full ring —
back-pressure — the reader gets silence on an empty one — an
underrun is a gap, never a stall) that a persistent `PulseOut` pulls
(a self-paced PulseAudio loop — the wake-ups must never depend on the
GUI thread; the DSP chain and the audio tap run there, so the meters
and the position count the frames that go out). The position is the
output's played-frames count from a per-item offset; the duration
comes from the container. **Volume, mute and boost** (the capability
contract is in docs/components.md): the level and the mute sit on the
**server side** of the output — `setSinkVolume`/`setSinkMute` store
the values (atomics) and apply them through the thread-safe
`pa_context_set_sink_input_volume`/`_mute` (dispatched to the
mainloop thread; a not-ready stream is a no-op, and the stream's ready
re-applies the stored values) — so other clients (pavucontrol) see
them and can change them back: on context ready, `PulseOut`
subscribes to `PA_SUBSCRIPTION_MASK_SINK_INPUT`, and a NEW/CHANGE of
this stream's own sink input (matched against `pa_stream_get_index`)
is read back with `pa_context_get_sink_input_info` and reported as
`sinkVolumeChanged` (the channels' average, linear) /
`sinkMuteChanged` — the initial sync, the confirmation of doob's own
push and an external change all land the same way. The boost is
**client-side**: `setBoost` stores a linear gain the pump applies
after the DSP chain and before the tap (the meter shows what is
heard; a gain above 1.0 can clip — the nature of the feature).
**Gapless** (the manager ↔ driver contract is in docs/components.md)
is a **two-worker handoff into the one ring**:
- **The ring's order invariant**: the ring is a FIFO, so its frame
  order is the *write* order — and two live writers race that order.
  **All of the current item's frames must be written before the
  queued item's first frame is**, or the output plays the two tracks
  interleaved (a chunk of one after a chunk of the other — the
  audible overlap, the next track starting seconds early). A
  per-writer fill ceiling cannot fix this: it limits a writer's
  *share* of the ring, not the *order* of its frames (the Phase 6
  ceiling is deleted by this phase for that reason). The order is
  enforced by a **gate** (a player-owned `std::atomic<bool>`, closed
  on `play`/seek/stop, open on the current decode's eos — the
  worker's last write precedes its eos) plus a **per-worker gated
  flag** (set when the worker is started as the queued one, cleared
  unconditionally on its promotion): a worker detours its frames to a
  **bounded private staging** (2 s, back-pressured — a
  faster-than-real-time decode parks at the cap, a wire-paced one
  keeps its demuxer buffer) while it is gated *and* the gate is
  closed, and enters the ring only while the gate is open (draining
  its own staging first). The gated flag — not the gate — must follow
  the worker's role: a *live* promoted worker (any next track longer
  than ~4 s, and every wire-paced one — the common case) becomes the
  current and must write straight into the ring from the handoff on;
  a gate that closed on it would detour its mid-track frames to the
  staging (silence mid-track, a burst at the end). After the
  promotion the gate serves only the *next* queued worker (and a
  parked corpse's drain-wait).
- **The staging's rules**: the worker drains its own staging on its
  clean exit (parking on the closed gate — the corpse case: the
  queued decode finished before the current's eos, its staging full,
  its thread parking there until the gate opens and the ring takes
  the frames); the player never touches the staging (a full-ring
  drain would block the GUI thread), and a stop — which always kills
  the queued worker — is the only way the parking ends without an
  eos. The full-staging park is a 5 ms poll (breaking on a stop),
  never a blocking staging write — `stop()` requests the *shared*
  ring only, so a blocking private write would hang the worker's join
  on the GUI thread — and the drain loop breaks on a stop too. Two
  accounting invariants: the staging capacity is ≤ the ring's
  capacity (a corpse's drain then finishes within one ring-full of
  reads after the gate opens — before its boundary), and a worker
  never resumes direct decoding without draining its staging first
  (the boundary counts `framesWritten`, and every frame it counts
  must be in the ring).
- **The boundary accounting**: the current item's stretch is
  `m_segStartReads + framesWritten()` in **ring read counts** (the
  silence padding and any DSP drops cannot skew a read count), noted
  on the current decode's eos (`noteStretchEnd`) and checked in the
  250 ms position timer. At the boundary the **handoff** promotes the
  queued worker: the offset re-anchors to the queued frames already
  read out, the duration/stream-info switch to its container, the
  output and its tap continue unbroken, the promoted worker's gated
  flag is cleared (it is the current now) and the gate reflects the
  new state (open iff the promoted decode is already done — the
  corpse's drain-wait ends there), and `finished` fires (the
  manager's early-armed no-op). Without a queued worker the boundary
  is the play-out's end (a stop + `finished`).
- **The play-out window** (the current decode is a corpse — its eos
  has fired, its tail plays out of the ring, the display already
  shows the armed item): a **seek** resolves the boundary at once
  (the handoff — `finished` fires, the manager's no-op), then seeks
  the promoted worker; a **pause** does the same and then pauses it.
  A corpse has no consumer for a seek or a pause — when the current
  (or the promoted worker) is a corpse, it is **re-decoded from the
  operation's point** (a fresh worker with an initial seek: the seek
  target, the pause position), so the audio continues where the user
  put it and a later boundary fires. Without this, a seek in the
  window is a silent no-op and the boundary never re-fires — the
  playback ends silently with the position frozen at the end.
- **Lifecycle**: `play` starts the current worker (gate closed, the
  stretch anchored at the session's drain); `queueNext` starts the
  queued worker (gated; idempotent per source — a same-source
  re-queue reports queued, true; a refusal reports false); a seek
  also re-anchors the position at the seek itself (the output's
  frame count is set to the seek and the item's offset reset — a
  seek in a later track must not read the position from the session
  start). A **pause** kills the
  queued worker (its staging goes with the ring's drain) and re-seeks
  the current one to the position (the resume re-decodes the queued
  one from the top — the remembered `m_queuedItem` — and re-anchors
  the stretch at the drain's read count); in the play-out window it
  resolves the boundary first (the handoff, the corpse's re-decode).
  A **seek** kills the queued
  worker too (the manager re-queues) and closes the gate (the
  re-decoded current hits a fresh eos); in the play-out window it
  resolves the boundary first (the handoff, the corpse's re-decode).
  A **stop** retires both
  workers, drains the ring (the drain clears the per-writer stop
  flags) and closes the gate. The ring's stop flag is **per writer**
  (`kMaxWriters = 2`; the current is 0, the queued 1) — killing the
  queued worker must not wake or silence the current one (a global
  flag no-op'd the current's in-flight writes, its frame counter kept
  counting, and its boundary became unreachable).

The extension points a plugin registers into:

- **Source providers** (sourceprovider.h, sourcemanager.{h,cpp}): a
  `SourceProvider` claims sources at import time (`accepts(url)` — the
  first registered claimant wins; the winner's id is recorded in the
  item's `provider` field), canonicalizes its claimed sources
  (`canonicalUrl` — the store applies it at insert **and** lookup, so
  a re-paste with incidental arguments hits the same item; default:
  identity), serves metadata asynchronously (`fetchMetadata(url, cb)` —
  `cb` on the GUI thread, exactly once, empty `TrackMeta` on failure),
  optionally the collection a source references (`fetchList(url, cb)` —
  the list's title and its videos in list order, each a `ListVideo`
  (the video URL plus the display metadata the listing already carries
  — title, artist, album, duration — the import wizard's review shows it
  without a per-video fetch). The read's outcome rides in the
  `ListEntries` fields: a plain answer (no videos, no `error`, no
  `isCollection`) means the source references no collection (a single
  track — the wizard imports it as a plain source); `error` set means
  the collection read failed (the wizard shows the source as failed
  and does not import the container link); `isCollection` set without
  an error means a legitimately empty collection (an empty section,
  nothing to import); default: empty) and optionally playback
  (`resolvePlayback(item, cb)` resolves a stream URL **asynchronously** —
  `cb` on the GUI thread, exactly once, an empty `QUrl` when the item
  cannot be played; the synchronous `playableUrl` is the default
  implementation behind it, `player()` a provider-owned `AudioPlayer`,
  `shareableUrl` the human form of the source).
  `SourceManager` is a non-owning registry (duplicate ids rejected,
  registration order is claim order); `providerById` backs the runtime
  lookup, so a vanished provider degrades its items (unplayable, not
  re-syncable) without making them unresolvable.
- **Playback drivers** (playerregistry.{h,cpp}): `PlayerRegistry` maps a
  driver id to a creator `std::function<AudioPlayer*(QObject *parent)>`.
  The built-in fallback is the `NullPlayer` — not a registry entry,
  created directly by `PlaybackManager::createDefaultPlayer()` (it
  plays nothing; a `play` answers `errorOccurred`); the user selection
  lives in the `playback.player` setting. A creator that parents the player is owned
  (and deleted) by the `PlaybackManager`; a plugin-owned (unparented)
  driver is not. A driver
  may report failures through the `AudioPlayer::errorOccurred(QString)`
  signal (a signal, not a virtual — the driver ABI is untouched); the
  manager then marks the item failed and stops instead of
  auto-advancing.
- **Widgets** (widgetregistry.{h,cpp}): `WidgetRegistry` maps a widget id
  to (title, tag, creator) — the tag (a free-form string, the core's
  groups are the `widgetTags` constants in basewidget.h; no tag is
  "other") is the "Set widget..." dialog's group: the dialog lists the
  groups in a fixed order (the core's, then a plugin's own tags
  alphabetically, "other" last), each as a non-selectable heading row,
  and a group with no widget is hidden; the built-ins register from the `LayoutManager`
  ctor, and `widgetOptions()`/`createWidget()` are thin lookups (see the
  widget checklist).
- **Settings topics** (settingsregistry.{h,cpp}): the preferences dialog
  (File → Preferences, `PreferencesDialog`) lists the core's topics under
  "Main" and the plugins' under "Plugins", one settings page per topic
  (the creator makes the page `QWidget`). `SettingsRegistry` maps a topic
  id to (title, owner, page creator); the core's topics (owner empty —
  today: the "Library" watch-dirs form page, the "Settings" topic,
  the about:config-style browser `configpage.{h,cpp}` over the
  `SettingsManager`: every registered setting in a key/type/value/default
  tree grouped by owner, a search box, a "Modified only" filter, the
  "Orphaned" stored keys of unregistered settings, double-click opens a
  type-specific value editor (`valueeditdialog.{h,cpp}`), the "DSP"
  topic (`dspsettingspage.{h,cpp}`) and the "Shortcuts" topic
  (`shortcutspage.{h,cpp}`)) register from
  the dialog's ctor, a plugin registers its topics in `initialize()` via
  `api->settings->registerTopic(id, title, ownerId, creator)` and
  unregisters them in `shutdown()`. A plugin that exports no topics
  declares itself with `registerOwner(ownerId)` (undone in `shutdown()`)
  and gets listed with a "no settings" note in place of a page. The form
  pages and the browser coexist: both read/write through the
  `SettingsManager`, so they stay in sync via its `changed()` signal.
- **DSPs** (audioprocessor.h, dspregistry.{h,cpp}, dspchain.{h,cpp}):
  an `AudioProcessor` is a transform over the frames as they leave a
  driver's open PCM path — `id()`/`title()`, the per-stream lifecycle
  `start(sampleRate, channels)`/`stop()` (the audio thread), and
  `process(frames, frameCount, channels)` — in place, **returns the
  frames left**, so a DSP can shorten a chunk (a silence skip returns
  0 for a skipped chunk); a fork-to-sink DSP returns the full count
  and feeds its own destination (no extra surface). The `DspRegistry`
  (in `DoobApi` as `dpsp`) is the non-owning registry — the processor
  is parented to the plugin and dies with it, a running chain holds
  `QPointer`s and skips a dead entry — and owns the user's chain state:
  `dsp.order` (ids, the absent ones run last) and `dsp.enabled` (the
  absent ones on), written through like any setting; a new DSP is
  enabled, and `chainChanged()` (also for a `SettingsManager` edit of
  the two keys) refreshes the running chains and the preferences. The
  core
  registers a built-in from the registry's ctor (`silenceskip` — the
  leading-skip, `dsp.silence.threshold_db`/`dsp.silence.max_seconds`).
  Execution is the driver's job: the only
  open-PCM driver (FFmpeg, `plugins/ffmpeg/`) runs a `DspChain` (one
  per stream, owned by its `PulseOut`) over every chunk **before the
  tap** — the meters and the position count the frames that go out, so
  a dropping DSP starts a track fast and the position tracks what is
  heard (seeking across a time-mutating DSP is approximate — v1's
  documented limit). The "DSP" preferences topic (`dspsettingspage`)
  lists the chain in order with a switch, up/down rearrange and a
  Configure button for a DSP that ships a settings page. The equalizer
  is a DSP of this chain (the FFmpeg plugin's
  `EqualizerProcessor`, id `ffmpeg.eq` in `eq.h`; the gains are the
  `ffmpeg.eq.gains` setting, the on/off switch the registry's entry);
  a driver without an open audio path (the built-in null driver)
  never plays at all.
- **Art sources** (artsource.h): an `ArtSource` looks up an item's
  enrichment (its `kind` — `Art` now, `Lyrics` reserved) keyed on the
  item's display identity (artist/title/album), independent of the
  item's source — `fetch(item, kind, cb)`, cb on the GUI thread,
  exactly once, an empty result = none found. The `ArtRegistry` (in
  `DoobApi` as `arts`) is the non-owning registry — the source is
  parented to the plugin and dies with it — and the core registers its
  built-in `itunes` source (the iTunes Search API, no key) from the
  registry's ctor. The iTunes source's search
  terms resolve from the item through the `art.itunes_search_patterns`
  setting — an ordered pattern list (tokens `{artist}`, `{title}`,
  `{album}`, `{album_artist}`; an empty field and an unknown token drop
  out, a pattern that resolves to nothing is skipped) whose default
  tries the album's identity (`{artist} {album}`) before the track's
  (`{artist} {title}`) — one search per term, the first term whose
  first result carries art wins. The core's art-fallback
  resolver (artfallback.{h,cpp}) watches `playingItemChanged`: a play
  start on an item with no stored art looks it up through the source
  the `art.fallback` setting names (default `itunes`, `off` disables
  it) — one lookup at a time, a miss lands in a RAM negative cache
  (a TTL) so it is not re-fetched on every play, a hit goes through
  `MediaItemStore::setArtwork` (the `itemArtworkChanged` signal
  updates the displays). A plugin that ships an art source registers
  it in `initialize()` (and unregisters it in `shutdown()`); naming
  it in `art.fallback` (the config browser) makes it the fallback.
- Provider fetches are dispatched **from the GUI thread** (providers are
  GUI-threaded; workers never touch them), and operation/import
  completion counts every in-flight callback (see the LibraryManager and
  PlaylistManager notes in docs/components.md).
