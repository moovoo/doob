#!/bin/sh
# bundle-deps.sh — copy the shared-library closure of everything in the
# AppDir's usr/ tree into AppDir/usr/lib, so the image resolves against
# itself (linuxdeployqt then only adds the Qt plugins/resources).
#
# Runs inside the BUILD container (doob-appimage-build), where every
# library the tree links against exists:
#   - Qt:      resolved against the container's /usr/lib64 (the distro
#              Qt — the libraries, on Fedora, sit outside the qt6 tree)
#   - non-Qt:  resolved against the container's /usr/lib64 (the same
#              libraries the binaries were built against)
#
# Skipped (provided by the target system, the image's glibc floor):
# the dynamic loader and the glibc family (libc, libm, libpthread,
# libdl, librt, libutil, libresolv, libnss_*, libnsl).
#
# Loops until a pass adds nothing (Qt plugins linuxdeployqt adds later
# bring their own NEEDED — run it again after linuxdeployqt).

set -e

# The Qt libraries' home (the distro's /usr/lib64 — on Fedora the qt6
# tree holds plugins/libexec, not the libraries).
QT_LIB_DIR=/usr/lib64
APPDIR="${1:-/appdir}"

skip_lib() {
    case "$1" in
        */ld-linux-*|libc.so.*|libm.so.*|libpthread*|libdl.so.*|librt.so.* \
        |libutil.so.*|libresolv*|libnss_*|libnsl*|libgcc_s.so.*)
            return 0 ;;
    esac
    return 1
}

copy_pass() {
    added=0
    for f in $(find "$APPDIR/usr/bin" "$APPDIR/usr/lib" "$APPDIR/usr/libexec" \
                   "$APPDIR/usr/plugins" -type f 2>/dev/null); do
        file -b "$f" | grep -q ELF || continue
        # Resolve with the AppDir first (already bundled), then the
        # build environment.
        out=$(LD_LIBRARY_PATH="$APPDIR/usr/lib:$QT_LIB_DIR" ldd "$f" 2>/dev/null \
              | grep -oE '=> /[^ ]+' | sed 's/^=> //') || continue
        for lib in $out; do
            base=$(basename "$lib")
            skip_lib "$base" && continue
            [ -e "$APPDIR/usr/lib/$base" ] && continue
            cp -L "$lib" "$APPDIR/usr/lib/$base" && added=1
        done
    done
    return $((1 - added))
}

while copy_pass; do
    :
done

# Versioned real files were copied; add the unversioned soname
# symlinks the loader looks for (libfoo.so.6.2.1 -> libfoo.so.6).
for f in "$APPDIR"/usr/lib/*.so.*; do
    [ -e "$f" ] || continue
    soname=$(readelf -d "$f" 2>/dev/null | sed -n 's/.*SONAME:\t\([^)]*\)).*/\1/p')
    [ -n "$soname" ] && [ ! -e "$APPDIR/usr/lib/$soname" ] && \
        ln -s "$(basename "$f")" "$APPDIR/usr/lib/$soname"
done

echo "bundled $(ls "$APPDIR"/usr/lib | wc -l) files in usr/lib"
