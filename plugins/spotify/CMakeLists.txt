cmake_minimum_required(VERSION 3.16)
project(spotifyplugin LANGUAGES CXX)

# Two build modes:
#  1. In-tree (the core's build does add_subdirectory(plugins/spotify)):
#     the parent project defines the doob_lib target; the .so lands in
#     <build>/plugins, one of the PluginManager's search paths, so a
#     built doob loads the plugin as-is.
#  2. Standalone: find_package(doob) against an installed doob (the main
#     build's install rules ship libdoob_lib, the headers under
#     include/doob and the find_package config):
#
#       cmake -S . -B build -DCMAKE_PREFIX_PATH=<prefix>
#
#     (equivalently: -Ddoob_DIR=<prefix>/lib/cmake/doob)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    find_package(doob REQUIRED)
    set(DOOB_LIB doob::doob_lib)
else()
    set(DOOB_LIB doob_lib)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# The playback path links libpulse (the context the relay's
# pa_simple_new_with runs on) and libpulse-simple (the blocking
# pa_simple the system-sink relay and the tests' tone writer use — a
# separate library in this distro's build). In-tree the root build has
# already run pkg_check_modules(PULSE ...) for the FFmpeg gating, so
# the PULSE target exists and only the simple library needs its own
# check (a same-named one would be skipped as already found).
find_package(PkgConfig REQUIRED)
if(NOT TARGET PkgConfig::PULSE)
    pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)
endif()
pkg_check_modules(PULSE_SIMPLE REQUIRED IMPORTED_TARGET libpulse-simple)

# The default client id: a public identifier (the PKCE flow's security
# comes from the challenge, not from hiding it) — the user overrides it
# with their own app via the spotify.client_id setting.
set(SpotifyClientId "fdffd336889e471fac16ca0a1710b662"
    CACHE STRING "The default Spotify client id")

set(SpotifySources
    spotify.cpp spotify.h
    spotifyauth.cpp spotifyauth.h
    spotifywebapi.cpp spotifywebapi.h
    spotifyprovider.cpp spotifyprovider.h
    spotifysettingspage.cpp spotifysettingspage.h
    spotifyplayer.cpp spotifyplayer.h
    spotifyipc.cpp spotifyipc.h
    privatepulse.cpp privatepulse.h
    pulsestub.cpp pulsestub.h
    capturepump.cpp capturepump.h
    systemrelay.cpp systemrelay.h
    audiotap.cpp audiotap.h)

# The private audio server (the fake PulseAudio the WebEngine is
# pointed at): plain C++17, no Qt (the libpulse headers are for the
# protocol's constants only — nothing is linked). The plugin runs it
# in-process (pulsestub.cpp is above); the executable (pulsestub.cpp +
# pulsestub_main.cpp) is the standalone form — the test fixture the
# ffmpeg plugin's pulseout test runs in discard mode. It lands next to
# the .so in <build>/plugins (the tests' PULSESTUB_PATH definition
# points at the build-tree binary).
find_package(Threads REQUIRED)
add_executable(pulsestub pulsestub.cpp pulsestub_main.cpp)
target_link_libraries(pulsestub PRIVATE Threads::Threads)
set_target_properties(pulsestub PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)

if(Qt6_FOUND)
    qt_add_plugin(spotify ${SpotifySources})
else()
    add_library(spotify MODULE ${SpotifySources})
endif()
target_link_libraries(spotify PRIVATE ${DOOB_LIB} PkgConfig::PULSE
    PkgConfig::PULSE_SIMPLE Threads::Threads)
target_compile_definitions(spotify PRIVATE
    SPOTIFY_CLIENT_ID_DEFAULT="${SpotifyClientId}")
# In both modes the .so lands in <build>/plugins: in-tree that is one of
# the PluginManager's search paths (the dev doob loads it as-is),
# standalone it sits next to the test executable, which loads it from
# <app dir>/plugins.
set_target_properties(spotify PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)

# The spotify-web helper: a separate executable that hosts the Web
# Playback SDK (QWebEngine + the QWebChannel bridge) and speaks JSON
# lines to the driver's SpotifyIpc. It is a process, not a thread: its
# tree must be pointed at the private PulseAudio server (PULSE_SERVER)
# before the WebEngine children fork, and the main doob process must
# never be. The page and the SDK bundle are embedded resources
# (qrc:/spotify/...). It lands next to the .so in <build>/plugins
# (the driver's helperPath() looks there first).
set(CMAKE_AUTORCC ON)
find_package(QT NAMES Qt6 Qt5 REQUIRED
    COMPONENTS WebEngineWidgets WebChannel)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED
    COMPONENTS WebEngineWidgets WebChannel)
# The Web Playback SDK bundle: fetched from Spotify's CDN at configure
# time — the repo ships no Spotify-owned file. The CDN serves a
# floating URL (no version-pinned path), so the fetch is pinned by
# hash: a new SDK from Spotify fails the configure until the pin (and
# the page, which tracks the SDK's v1.10.0 quirks) is deliberately
# updated. A previous fetch is reused (reconfigures stay offline), and
# a local copy wins for proxies/offline machines: -DSpotifySdkFile=...
set(SpotifySdkUrl "https://sdk.scdn.co/spotify-player.js"
    CACHE STRING "The Web Playback SDK download URL")
set(SpotifySdkHash
    "SHA256=1e1cd93a4e8409c1736e3067398de64916556355a103d1c6341ebd2df10fa347"
    CACHE STRING "The expected hash of the SDK bundle")
set(SpotifySdkFile "" CACHE FILEPATH
    "A local sdk.js to use instead of the download")
set(SpotifySdkDest ${CMAKE_CURRENT_BINARY_DIR}/spotify-sdk.js)
if(SpotifySdkFile)
    set(SpotifySdk ${SpotifySdkFile})
elseif(EXISTS ${SpotifySdkDest})
    set(SpotifySdk ${SpotifySdkDest})
else()
    file(DOWNLOAD ${SpotifySdkUrl} ${SpotifySdkDest}
        EXPECTED_HASH ${SpotifySdkHash} SHOW_PROGRESS STATUS sdk_status)
    list(GET sdk_status 0 sdk_rc)
    if(NOT sdk_rc EQUAL 0)
        file(REMOVE ${SpotifySdkDest})
        message(FATAL_ERROR "Failed to download the Web Playback SDK "
            "from ${SpotifySdkUrl} (${sdk_status}); reconfigure online, "
            "or point SpotifySdkFile at a local copy of the bundle")
    endif()
    set(SpotifySdk ${SpotifySdkDest})
endif()
# The qrc is generated so the bundle's build-dir path rides along while
# the page's stays in the source tree; the aliases keep the
# qrc:/spotify/<name> addresses the page loads.
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/web.qrc
"<RCC>\n"
"    <qresource prefix=\"/spotify\">\n"
"        <file alias=\"page.html\">${CMAKE_CURRENT_SOURCE_DIR}/web/page.html</file>\n"
"        <file alias=\"sdk.js\">${SpotifySdk}</file>\n"
"    </qresource>\n"
"</RCC>\n")
if(Qt6_FOUND)
    qt_add_executable(spotify_web spotifyweb.cpp spotifywebapi.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/web.qrc)
else()
    add_executable(spotify_web spotifyweb.cpp spotifywebapi.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/web.qrc)
endif()
target_link_libraries(spotify_web PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::WebEngineWidgets
    Qt${QT_VERSION_MAJOR}::WebChannel)
# The output name is what the driver's helperPath() looks for
# (spotify-web, a hyphen — the target's underscore would be a
# different file).
set_target_properties(spotify_web PROPERTIES
    OUTPUT_NAME spotify-web
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)

# The plugin's tests: they live in this directory and register themselves
# with CTest from here, so the plugin is self-sufficient in both build
# modes — in-tree the core's build picks them up (the root calls
# enable_testing() before the add_subdirectory), standalone the plugin's
# own build does. The test loads the .so from <build dir>/plugins (next
# to the test executable) and never links it.
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
enable_testing()

set(PLUGIN_TEST_SOURCES tests/spotify_test.cpp
    tests/privatepulse_test.cpp tests/spotifyplayer_test.cpp)
foreach(test_source ${PLUGIN_TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)
    if(Qt6_FOUND)
        qt_add_executable(${test_name} ${test_source})
    else()
        add_executable(${test_name} ${test_source})
    endif()
    target_link_libraries(${test_name} PRIVATE
        ${DOOB_LIB} Qt${QT_VERSION_MAJOR}::Test)
    # The test executable lands in the top-level build directory, so it
    # finds the .so at <build>/plugins (the same relative layout in
    # standalone mode).
    set_target_properties(${test_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    # The plugin's pure sources compiled straight in (the same way the
    # ffmpeg plugin's tests compile theirs): the unit tests reach the
    # classes directly, the plugin-level tests use the built .so
    # (its own copies) through the core interfaces. Each test gets
    # the sources it reaches into — the pulse test the capture
    # side, the player test the driver's whole stack (against a fake
    # helper process and a fake server — no daemon, no WebEngine),
    # the rest the auth/api/provider core.
    target_compile_definitions(${test_name} PRIVATE
        PULSESTUB_PATH="$<TARGET_FILE:pulsestub>")
    if(test_name STREQUAL "privatepulse_test")
        target_sources(${test_name} PRIVATE
            privatepulse.cpp pulsestub.cpp capturepump.cpp)
        target_link_libraries(${test_name} PRIVATE PkgConfig::PULSE
            PkgConfig::PULSE_SIMPLE Threads::Threads)
    else()
        # The auth/api/provider core plus the driver's stack (the
        # provider's resolvePlayback reaches into the driver).
        target_sources(${test_name} PRIVATE
            spotifyauth.cpp spotifywebapi.cpp spotifyprovider.cpp
            spotifyplayer.cpp spotifyipc.cpp
            privatepulse.cpp pulsestub.cpp capturepump.cpp
            systemrelay.cpp audiotap.cpp)
        target_link_libraries(${test_name} PRIVATE PkgConfig::PULSE
            PkgConfig::PULSE_SIMPLE Threads::Threads)
        # The fake spotify-web helper (a bash script speaking the
        # JSON-lines protocol) the driver's tests run instead of the
        # real WebEngine process (spotify_test does not use it).
        if(test_name STREQUAL "spotifyplayer_test")
            target_compile_definitions(${test_name} PRIVATE
                FAKE_SPOTIFY_WEB="${CMAKE_CURRENT_SOURCE_DIR}/tests/fakehelper.sh")
        endif()
    endif()
    target_include_directories(${test_name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
