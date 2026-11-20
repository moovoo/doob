# Presets

The read-only preset layouts and styles shipped with doob. The package
manager installs them to `<datadir>/doob/{layouts,styles}`; a dev build
copies this tree to `<build>/share/doob` (next to the executable — see
`Settings`).

- `layouts/*.json` — the preset layout documents (the versioned format:
  the optional `"version"` root key, the required `"container"` root
  key).
- `styles/*.qss` — the preset style sheets (a preset is the original:
  no provenance comment).

A user file with the same name shadows a preset (the user's
layouts/styles directory wins on lookup). A preset is a template, never
rewritten and never the active entry itself: selecting one takes a copy
into the user's directory, `<name>.modified.{json,qss}`, with the origin
recorded (an existing copy is adopted, not duplicated).

The first presets: the "Classic" layout (a wide, column-trimmed default
arrangement) and the "Kiosk" layout (frameless, the menu bar hidden —
a `menubutton` + `titlebar` strip stands in for the window chrome,
over a library/playlists split and a playback bar), and six style
looks — "default" (an empty sheet: the platform palette, unchanged),
"compact" (small fonts, tight spacing, the platform palette's colors),
"matrix" (green-on-black phosphor), "dusk" (warm dark, rounded edges,
amber accent), "dawn" (warm light, rounded edges, dusty blue accent)
and "terminal" (small monospace font — pixel fonts preferred when
installed — on near-black, square geometry). CMake's install and
dev-copy rules ship the tree recursively: a new presets/ subdir ships
itself.
