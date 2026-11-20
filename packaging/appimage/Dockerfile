# doob AppImage build environment.
#
# fedora:42 base — pin by digest for release builds (the tag moves
# with point releases, which changes the dnf packages and breaks
# bit-for-bit reproducibility).
#
# The image is the artifact's compatibility floor: glibc 2.41, and the
# distro's Qt 6.10.2 + its WebEngine. Why the distro Qt and not aqt's
# 6.12.0 (the previous image's): the aqt/official 6.12 WebEngine is
# built without the AAC decoder, so the Spotify plugin (the Web
# Playback SDK streams AAC) cannot create its MediaSource buffer —
# the distro build has it (probed: mp4a.40.2 supported in 6.10.2).
# Everything the AppImage needs is installed here, so the built
# artifact only ever resolves against itself.
#
#   docker build -f packaging/appimage/Dockerfile -t doob-appimage-build .
#
# The Spotify SDK bundle is fetched at configure time (hash-pinned in
# plugins/spotify/CMakeLists.txt); for offline/deterministic builds pass
# -DSpotifySdkFile=... to the cmake configure.

# The base, pinned by digest by build.sh (the tag moves with point
# releases, which changes the dnf packages and breaks bit-for-bit
# reproducibility); the ARG default keeps ad-hoc builds working.
ARG BASE_IMAGE=fedora:42
FROM $BASE_IMAGE

# Build tools + Qt 6 (the distro's 6.10.2 — see the header for why) +
# the runtime dependencies the AppImage bundles: FFmpeg (the ffmpeg
# player plugin), PulseAudio, Ogg/Vorbis/Opus (the ffmpeg plugin's
# decoders), the glib/xkbcommon/glib/png the Qt stack links, and the
# GPG toolchain the signing step runs in the container. gpgme is the
# appimagetool binary's own runtime dependency (its embedded-signing
# path links libgpgme.so.11) — a build-time need only, it never
# reaches the artifact. The runtime closure (the X11/xcb stack, NSS,
# ALSA, fontconfig, GL — WebEngine's transitive needs) rides in as the
# qt6 packages' own dependencies. Weak deps (the wayland stack a
# headless build never loads) are off.
RUN dnf install -y --setopt=install_weak_deps=False \
        gcc-c++ make cmake ninja-build pkg-config file binutils \
        curl tar desktop-file-utils \
        qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtdeclarative-devel \
        qt6-qtwebengine-devel qt6-qtsvg-devel \
        ffmpeg-free-devel pulseaudio-libs-devel \
        libogg-devel libvorbis-devel opus-devel \
        glib2-devel libxkbcommon-devel libpng-devel \
        utf8cpp-devel \
        gnupg2 gpgme \
    && dnf clean all

# TagLib 2.3.2 from source (hash-pinned, the same release the dev
# trees build against): the distro's TagLib 1.x lacks the property
# API the tree uses, and no distro ships 2.x. Built shared — doob_lib
# is a .so and the default static build is not -fPIC. The install
# lands libtag.so.2 + the taglib.pc the CMake's
# pkg_check_modules(taglib) resolves.
RUN set -eux; \
    curl -fsSL -o /tmp/taglib.tar.gz \
        https://github.com/taglib/taglib/releases/download/v2.3.2/taglib-2.3.2.tar.gz; \
    echo "3ca2d8afaa7f1cf7f6ed10e511ebc368bfacd6dcaa3dbfa690b89e502e8963dc  /tmp/taglib.tar.gz" | sha256sum -c -; \
    tar -C /tmp -xzf /tmp/taglib.tar.gz; \
    cmake -S /tmp/taglib-2.3.2 -B /tmp/taglib-build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
        -DBUILD_SHARED_LIBS=ON -DBUILD_STATIC=OFF; \
    cmake --build /tmp/taglib-build; \
    cmake --install /tmp/taglib-build; \
    ldconfig; \
    rm -rf /tmp/taglib-2.3.2 /tmp/taglib-build /tmp/taglib.tar.gz

# The Qt tree is split on Fedora: the plugins/libexec under
# /usr/lib64/qt6, the shared libraries in /usr/lib64, and the
# WebEngine resources/locales under /usr/share/qt6. build.sh copies
# each set from its part; the Qt libraries themselves are bundled by
# bundle-deps.sh, which resolves them from the container's default
# paths (hence no CMAKE_PREFIX_PATH — find_package finds the cmake
# configs under /usr/lib64 on its own).
ENV QT_PREFIX=/usr/lib64/qt6
ENV QT_SHARE=/usr/share/qt6

WORKDIR /src
