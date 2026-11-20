#!/bin/sh
# build.sh — build doob's AppImage (the ffmpeg and Spotify plugins fully
# bundled, everything the image links against inside it).
#
#   packaging/appimage/build.sh [options]
#
# Options:
#   --workdir DIR     work directory (default: a fresh /tmp/doob-appimage.*)
#   --rebuild-image   rebuild the build image even if it exists
#   --sign KEYID      sign the result with the GPG key (embedded
#                     appimagetool signature + a detached .asc); the
#                     key's secret part must be in the invoking
#                     user's keyring and unpassphrased (it crosses
#                     into the build container as an import file)
#   --keep            keep the work directory on success
#
# The build runs entirely inside the pinned build image
# (packaging/appimage/Dockerfile, fedora:42 by digest): compile,
# test, assemble the AppDir, bundle the dependency closure, package
# with appimagetool. Same source tree + same image digest + same
# tool hash => bit-for-bit the same AppImage — the last step
# included: appimagetool's bundled mksquashfs 4.4 compresses on N
# deflate threads, and with N > 1 the pipeline's scheduling changes
# individual blocks' compressed sizes, shifting every later offset
# (observed: ~20 MB of the ~218 MB payload differs between runs).
# -processors 1 (step 7) makes the pack sequential and the artifact
# deterministic.
#
# The Spotify Web Playback SDK is fetched at configure time, hash-pinned
# in plugins/spotify/CMakeLists.txt; for an offline build pass
# DOOB_SPOTIFY_SDK_FILE=/path/to/spotify-sdk.js.

set -eu

SCRIPT_DIR=$(dirname "$(readlink -f "$0")")
REPO_ROOT=$(dirname "$(dirname "$SCRIPT_DIR")")

# The base image, pinned by digest (the tag moves with point releases).
BASE_IMAGE="fedora:42@sha256:99e203b80b1c3d8f7e161ec10a68fd02b081ef83a3963553e513c82846b97814"
BUILD_IMAGE="doob-appimage-build"

# appimagetool (the packaging tool, not the runtime — the runtime is
# embedded in the artifact): pinned by hash, extracted and run in
# place (it finds its mksquashfs by relative path).
APPIMAGE_TOOL_URL="https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage"
APPIMAGE_TOOL_SHA256="b90f4a8b18967545fda78a445b27680a1642f1ef9488ced28b65398f2be7add2"

# The mtime anchor for the artifact's reproducibility. Two consumers:
#  - the compile step exports it as SOURCE_DATE_EPOCH, so rcc stamps the
#    embedded resources (the SDK bundle's mtime otherwise varies with the
#    copy) with a constant;
#  - the packaging step normalizes the AppDir's file mtimes to it (the
#    squashfs takes its timestamps from the files).
# Do NOT export SOURCE_DATE_EPOCH around the mksquashfs/appimagetool call
# itself: there it would be a no-op (appimagetool pins the superblock time
# with -mkfs-time 0, and the file mtimes are normalized above).
MTIME_EPOCH=1767225600  # 2026-01-01 00:00:00 UTC

WORKDIR=""
REBUILD_IMAGE=0
SIGN_KEY=""
KEEP=0
while [ $# -gt 0 ]; do
    case "$1" in
    --workdir) WORKDIR=$2; shift 2 ;;
    --rebuild-image) REBUILD_IMAGE=1; shift ;;
    --sign) SIGN_KEY=$2; shift 2 ;;
    --keep) KEEP=1; shift ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

if [ -z "$WORKDIR" ]; then
    WORKDIR=$(mktemp -d /tmp/doob-appimage.XXXXXX)
fi
rm -rf "$WORKDIR"
mkdir -p "$WORKDIR/build" "$WORKDIR/appdir" "$WORKDIR/tools" "$WORKDIR/out"
# The intermediates go on exit (the artifact in out/ stays, or with
# --keep everything does). The container runs as root, so a failure
# before the hand-back chown below leaves root-owned files behind —
# chown them in the trap too (best effort: the image may be gone).
trap 'if [ "$KEEP" -eq 0 ]; then
    docker run --rm -v "$WORKDIR":/w -e UID_GID="$(id -u):$(id -g)" \
        "$BUILD_IMAGE" sh -c "chown -R \"\$UID_GID\" /w" 2>/dev/null || true
    rm -rf "$WORKDIR/build" "$WORKDIR/appdir" "$WORKDIR/tools"
fi' EXIT

run() {
    docker run --rm \
        -v "$REPO_ROOT":/src:ro \
        -v "$WORKDIR/build:/build" \
        -v "$WORKDIR/appdir:/appdir" \
        -v "$WORKDIR/tools:/tools" \
        -v "$WORKDIR/out:/out" \
        "$BUILD_IMAGE" "$@"
}

# --- 1. the build image ---------------------------------------------------

# Rebuild when forced, when absent, or when the Dockerfile is newer
# than the image (the name alone would keep a stale image).
IMAGE_EPOCH=$(docker image inspect "$BUILD_IMAGE" -f '{{.Created}}' 2>/dev/null \
    | xargs -r -I{} date -d '{}' +%s)
if [ "$REBUILD_IMAGE" -eq 1 ] || [ -z "$IMAGE_EPOCH" ] \
        || [ "$(stat -c %Y "$SCRIPT_DIR/Dockerfile")" -gt "$IMAGE_EPOCH" ]; then
    echo "=== building $BUILD_IMAGE ==="
    docker build -f "$SCRIPT_DIR/Dockerfile" -t "$BUILD_IMAGE" \
        --build-arg BASE_IMAGE="$BASE_IMAGE" "$REPO_ROOT"
fi

# --- 2. the packaging tools ------------------------------------------------

if [ ! -x "$WORKDIR/tools/appimagetool-x86_64.AppImage" ]; then
    echo "=== fetching appimagetool ==="
    curl -fsSL -o "$WORKDIR/tools/appimagetool-x86_64.AppImage" "$APPIMAGE_TOOL_URL"
    (cd "$WORKDIR/tools" && echo "$APPIMAGE_TOOL_SHA256  appimagetool-x86_64.AppImage" | sha256sum -c -)
    chmod +x "$WORKDIR/tools/appimagetool-x86_64.AppImage"
    (cd "$WORKDIR/tools" \
        && ./appimagetool-x86_64.AppImage --appimage-extract \
        && mv squashfs-root ait-root)
fi

# --- 3. compile + test ------------------------------------------------------

# The SDK copy crosses into the container via the mounted tools dir
# (a host path would not exist there).
SDK_ARG=""
if [ -n "${DOOB_SPOTIFY_SDK_FILE:-}" ]; then
    cp "$DOOB_SPOTIFY_SDK_FILE" "$WORKDIR/tools/spotify-sdk.js"
    SDK_ARG="-DSpotifySdkFile=/tools/spotify-sdk.js"
fi

echo "=== configure + build ==="
# SOURCE_DATE_EPOCH pins the timestamps rcc embeds in the compiled-in
# resources (the SDK bundle's mtime, copied fresh each run, would
# otherwise vary build to build). CMAKE_INSTALL_LIBDIR is pinned to
# lib (CMake's Fedora platform default is lib64) — the AppDir's
# layout, AppRun's LD_LIBRARY_PATH and the bundle/audit steps all
# assume usr/lib.
run sh -c "
    export SOURCE_DATE_EPOCH=$MTIME_EPOCH
    cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_LIBDIR=lib $SDK_ARG \
    && cmake --build /build
"

echo "=== tests (the default gate — the gallery test is opt-in) ==="
run sh -c "
    QT_QPA_PLATFORM=offscreen ctest --test-dir /build --output-on-failure
"

# --- 4. assemble the AppDir --------------------------------------------------

echo "=== assembling the AppDir ==="
run sh -c '
    cmake --install /build --prefix /appdir/usr

    # The plugins (the player/source plugins, the spotify-web helper,
    # the pulsestub test fixture — shipped so the image is self-testable).
    mkdir -p /appdir/usr/plugins
    cp /build/plugins/libffmpeg.so /build/plugins/libspotify.so \
       /build/plugins/libtubemusic.so /build/plugins/spotify-web \
       /build/plugins/pulsestub /appdir/usr/plugins/

    # The scaffolding.
    cp /src/packaging/appimage/AppRun /appdir/AppRun
    chmod +x /appdir/AppRun
    cp /src/packaging/appimage/doob.desktop /appdir/doob.desktop
    mkdir -p /appdir/share/icons/hicolor/256x256/apps
    cp /src/packaging/appimage/icon.png \
       /appdir/share/icons/hicolor/256x256/apps/doob.png
    # The embedded AppImage icon (the desktop file names Icon=doob).
    cp /src/packaging/appimage/icon.png /appdir/doob.png
    # .DirIcon, pre-created: appimagetool would otherwise create it at
    # packaging time, after the mtime normalization below, and the extra
    # entry (plus the bumped directory mtime) would break reproducibility.
    # A pre-existing symlink to a regular file is left alone by the tool.
    ln -s doob.png /appdir/.DirIcon

    # Dev-time install rules the artifact does not ship.
    rm -rf /appdir/usr/include /appdir/usr/lib/cmake

    # The Qt runtime parts (linuxdeployqt is out — its glibc guard and
    # this Qt floor (2.38) are incompatible — so the set is explicit):
    # the platforms actually used (xcb desktop, offscreen for tests and
    # headless), the sqlite driver, the image formats, the OpenSSL TLS
    # backend (the spotify/tubemusic plugins need HTTPS), the WebEngine
    # helper process + resources + locales, and the qt.conf that pins
    # the prefix to the AppDir. On Fedora the Qt tree is split:
    # plugins/libexec under $QT_PREFIX, the WebEngine resources and
    # locales under $QT_SHARE (both set by the image).
    #
    # The qt.conf goes next to each executable Qt starts (usr/bin for
    # doob, usr/plugins for the WebEngine helper spotify-web, and
    # usr/libexec for QtWebEngineProcess — Qt looks for it next to the
    # executable of that process). Without it, QLibraryInfo falls back
    # to the compile-time prefix, which on a machine where that path
    # resolves to a different Qt (a host with /usr/lib64 -> /usr/lib
    # lands the Fedora /usr/lib64/qt6 path on the host Qt) serves the
    # platform plugins and locale .pak files of the foreign Qt — the
    # helper then dies on a Chromium CHECK (SIGTRAP) during WebEngine
    # init, the renderer on a V8 snapshot mismatch. With the file,
    # Prefix is relative to it: ".." = usr, so the plugins,
    # translations and resources resolve to the AppDir.
    P=$QT_PREFIX
    mkdir -p /appdir/usr/plugins/platforms /appdir/usr/plugins/sqldrivers \
             /appdir/usr/plugins/imageformats /appdir/usr/plugins/tls \
             /appdir/usr/libexec /appdir/usr/resources \
             /appdir/usr/translations/qtwebengine_locales
    cp $P/plugins/platforms/libqxcb.so $P/plugins/platforms/libqoffscreen.so \
       /appdir/usr/plugins/platforms/
    cp $P/plugins/sqldrivers/libqsqlite.so /appdir/usr/plugins/sqldrivers/
    cp $P/plugins/imageformats/libq*.so /appdir/usr/plugins/imageformats/
    cp $P/plugins/tls/libq*.so /appdir/usr/plugins/tls/
    cp $P/libexec/QtWebEngineProcess /appdir/usr/libexec/
    # The browser process finds the renderer through the qt.conf-
    # derived libexec path (usr/libexec) — and, without the qt.conf,
    # through the compile-time prefix, which on a foreign host can be
    # another Qt entirely (the pre-qt.conf image died on exactly that:
    # the renderer missing, then SIGABRT). The copy in usr/plugins is
    # the second candidate, next to the browser process. Keep both.
    cp $P/libexec/QtWebEngineProcess /appdir/usr/plugins/
    cp $QT_SHARE/resources/* /appdir/usr/resources/
    cp $QT_SHARE/translations/qtwebengine_locales/*.pak \
       /appdir/usr/translations/qtwebengine_locales/
    for d in /appdir/usr/bin /appdir/usr/plugins /appdir/usr/libexec; do
        printf "[Paths]\nPrefix = ..\nPlugins = plugins\nTranslations = translations\nResources = resources\n" > "$d/qt.conf"
    done

    # The set is explicit — assert it landed (a silent miss would
    # produce an image that "resolves" but lacks its Qt plugins). On
    # Fedora there is no icudtl.dat to check (unlike the aqt layout):
    # the distro Qt links the system libicu, whose data is built into
    # libicudata — the libs themselves ride in via the NEEDED closure.
    for f in /appdir/usr/plugins/platforms/libqxcb.so \
             /appdir/usr/plugins/sqldrivers/libqsqlite.so \
             /appdir/usr/plugins/tls/libqopensslbackend.so \
             /appdir/usr/libexec/QtWebEngineProcess \
             /appdir/usr/plugins/QtWebEngineProcess \
             /appdir/usr/bin/qt.conf /appdir/usr/plugins/qt.conf \
             /appdir/usr/libexec/qt.conf \
             /appdir/usr/translations/qtwebengine_locales/en-US.pak; do
        [ -e "$f" ] || { echo "missing Qt part: $f" >&2; exit 1; }
    done
'

# --- 5. bundle the dependency closure ---------------------------------------

echo "=== bundling the non-Qt closure ==="
run sh /src/packaging/appimage/bundle-deps.sh /appdir

# --- 6. the resolution audit --------------------------------------------------

echo "=== auditing the AppDir (every ELF must resolve from inside it) ==="
run sh -c '
    export LD_LIBRARY_PATH=/appdir/usr/lib
    missing=0
    for f in $(find /appdir/usr/bin /appdir/usr/lib /appdir/usr/libexec \
                   /appdir/usr/plugins -type f); do
        file -b "$f" | grep -q ELF || continue
        m=$(ldd "$f" 2>/dev/null | grep "not found" || true)
        if [ -n "$m" ]; then
            echo "== $f"
            echo "$m"
            missing=1
        fi
    done
    if [ $missing -ne 0 ]; then
        echo "UNRESOLVED libraries in the AppDir" >&2
        exit 1
    fi
    echo "ALL RESOLVED"
'

# --- 7. package ----------------------------------------------------------------

# The signing key's secret part (for the embedded signature) crosses
# into the container as an import file — it never sits in a layer.
# The key id is positional: --local-user selects the sign/encrypt key,
# it does NOT filter --export-secret-keys (that would export every
# secret key in the keyring and prompt for their passphrases).
if [ -n "$SIGN_KEY" ]; then
    gpg --batch --yes --armor \
        --export-secret-keys "$SIGN_KEY" \
        > "$WORKDIR/tools/doob-signing.gpg"
fi

echo "=== packaging ==="
# The touch is the last mutation of the AppDir: the output goes to /out,
# not into the AppDir (a file created inside it post-normalization would
# land in the squashfs with a build-time mtime and break reproducibility).
run sh -c "
    find /appdir -exec touch -h -d @$MTIME_EPOCH {} +
    if [ -n \"$SIGN_KEY\" ]; then
        # appimagetool's embedded signature: it signs the hex SHA256 of
        # the file as packed (the reserved sections still zero) and
        # writes the armored result into the runtime's .sha256_sig ELF
        # section (file offset 0x2c78f, 1024 B) plus the exported
        # public key into .sig_key (0x2cb8f, 8192 B). Verification:
        # zero both sections, sha256sum the result, gpg --verify the
        # armored block against the 64 hex chars (no trailing newline).
        # The runtime's --appimage-signature-show does not implement
        # this (version 5735cc5); the detached .asc covers the finished
        # file and is the one to publish.
        # The embedded signature, from a throwaway GNUPGHOME seeded
        # with the caller's key (the key must be unpassphrased).
        export GNUPGHOME=/gnupg
        mkdir -p /gnupg && chmod 700 /gnupg
        gpg --batch --import /tools/doob-signing.gpg
        # --sign is a flag; the key goes to --sign-key (a positional
        # key id would be parsed as the SOURCE argument).
        /tools/ait-root/usr/bin/appimagetool --sign --sign-key $SIGN_KEY \
            --mksquashfs-opt -processors --mksquashfs-opt 1 \
            /appdir /out/doob-x86_64.AppImage
    else
        # -processors 1: the multithreaded pack is not bit-for-bit
        # deterministic (see the header note).
        /tools/ait-root/usr/bin/appimagetool \
            --mksquashfs-opt -processors --mksquashfs-opt 1 \
            /appdir /out/doob-x86_64.AppImage
    fi
"

# --- 8. the detached signature (optional) --------------------------------------

if [ -n "$SIGN_KEY" ]; then
    echo "=== detached signature ==="
    gpg --batch --yes --armor --detach-sign \
        --local-user "$SIGN_KEY" \
        "$WORKDIR/out/doob-x86_64.AppImage"
fi

# --- 9. checksums + hand-back ----------------------------------------------------

(cd "$WORKDIR/out" && sha256sum doob-x86_64.AppImage* > SHA256SUMS)
rm -f "$WORKDIR/tools/doob-signing.gpg"
# The container ran as root — the hand-back runs in one (root can chown,
# the host user cannot).
docker run --rm -v "$WORKDIR":/w \
    -e UID_GID="$(id -u):$(id -g)" "$BUILD_IMAGE" \
    sh -c 'chown -R "$UID_GID" /w'
echo
echo "=== done ==="
ls -la "$WORKDIR/out/"
cat "$WORKDIR/out/SHA256SUMS"
echo
echo "artifact: $WORKDIR/out/doob-x86_64.AppImage"
echo "(intermediates removed on exit; re-run with --keep to retain them)"
