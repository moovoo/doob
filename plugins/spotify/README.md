# Spotify plugin

Source provider and playback driver for Spotify links: `spotify:track:/
album:/playlist:` URIs and the `open.spotify.com` track, album and
playlist pages. Requires **Spotify Premium** (the Web Playback SDK only
streams audio for Premium accounts) and a **Spotify developer app**
(the built-in client id is the default; bring your own via the settings
page).

## How it works

- **`SpotifyAuth`** — the PKCE sign-in (a loopback redirect,
  `http://127.0.0.1:<port>/callback`), the token file in the plugin's
  data directory and the on-demand refresh (an expired access token
  counts as signed in).
- **`SpotifyProvider`** — claims the links (the stored form is the
  `open.spotify.com` web link — the shareable form the native player
  copies; a re-pasted link with a share token, or a `spotify:` URI,
  hits the same item), serves metadata and collections (tracks,
  albums, playlists) from the Web API, and hands playback to the
  driver.
- **`SpotifyPlayer`** (the driver, id `spotify`) — brings the device
  up on the first play and reads the item's `spotify:` URI (the Web
  API's play-command shape) as its play command. The device is three
  parts:
   1. a **private audio server** (the plugin's own `pulsestub`, a
      minimal fake PulseAudio — one null sink `doob` and nothing
      else — the user's system server is never pointed at by the
      page) run in-process by the driver, on its own thread,
   2. the **`spotify-web` helper** — a separate executable hosting the
      Web Playback SDK in QWebEngine (its tree is pointed at the private
      server with `PULSE_SERVER`); it speaks JSON lines to the driver
      (commands in, state/position/log events out) and does the Web API
      play call with its own token,
   3. the **capture** — the server's sink hands the PCM (remapped to the
      pinned s16le/44100/stereo) to a ring whose back-pressure paces the
      engine; a self-paced pump thread reads the ring (the same shape the
      FFmpeg driver's output pulls its ring) and feeds it into doob's
      normal pipeline (DSP chain, meters/visualizations, a relay to the
      system sink), so Spotify audio is treated like any other
      track's. The `pulsestub` executable (the same server as a separate
      process) remains as the test fixture.
  The helper binary is looked for next to the plugin's directory
  (`<app dir>/plugins/spotify-web`, then `<app dir>/spotify-web`,
  then `<app dir>/../plugins/spotify-web` — the split layout: the
  binary in `usr/bin`, the helper beside the plugins in `usr/plugins`).
  The helper's token is the one from its start (the `SPOTIFY_TOKEN`
  environment variable), and the driver keeps it current: a token
  **refresh** of the same account is pushed to the running helper
  (the `token` command — the SDK re-asks for a token on expiry), a
  **sign-out** tears the device down, and a **re-login** tears it
  down too (the device is registered on the previous account's
  token) — the next play starts it fresh. The refresh itself is
  **ahead** of the expiry (a margin before it, the same one a token
  counts as expired on), so a long idle never hands the helper an
  expired token — and a play on a **warm** device re-validates the
  token first (a refresh, and the push it triggers, lands before the
  play command does). The engine's **transient hiccups** are handled
  the same way: an `authentication_error` (a stream handshake the
  backend rejected once — the log's "Invalid token scopes.") and a
  `playback_error` (a startup race — the log's "no list was
  loaded.") are not failures, the engine retries on its own (re-asking
  for a token, re-loading the list) and recovers while the audio
  starts — the driver re-pushes the current token and keeps the
  playback, and a position report in the hiccup's recovery window
  (5 s) cancels the stop; only a hiccup the engine does not recover
  from (no report in the window) is surfaced as a failure. A track's
  **end** is the engine pausing at the track's reset position (doob
  plays single tracks — the SDK's context is empty and has nothing to
  auto-advance to): the driver emits finished() (not paused()), the
  playlist moves on, and the end's repeated state events are ignored.

## Settings

| Key | Default | Meaning |
|---|---|---|
| `spotify.client_id` | the built-in id | your own developer app (a change signs the account out) |
| `spotify.redirect_port` | `8888` | the loopback port — the app's registered redirect URI must match |

## Signing in

Preferences → Plugins → **Spotify**: the account state, **Sign in**
(doubles as re-login) and **Sign out**. Not signed in, playback fails
with the sign-in hint (the status bar) and an import of a Spotify link
is skipped with the same hint — nothing is imported silently.

## Build

In-tree (the dev layout, `DOOB_BUILD_PLUGINS` on by default):

```
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure
```

Standalone against the installed API (like the example plugin):

```
cmake -S plugins/spotify -B build \
    -DCMAKE_PREFIX_PATH=<doob install prefix>
cmake --build build && ctest --test-dir build --output-on-failure
```

## Notes / limits

- The SDK bundle is fetched from Spotify's CDN at configure time
  (pinned by hash; `-DSpotifySdkFile=...` points at a local copy
  instead) and embedded in the `spotify-web` executable — the repo
  ships no Spotify-owned file. The bundle is the SDK's *wrapper* only:
  the audio engine runs in an `sdk.scdn.co` iframe, so a working
  network connection to Spotify is required for playback.
- Volume is capped at 1.0 (the SDK has no boost), and there is no stop
  (the driver's stop pauses; the next play starts fresh).
- v1 imports tracks, albums and playlists (as their tracks) — there is
  no search/browse UI.
