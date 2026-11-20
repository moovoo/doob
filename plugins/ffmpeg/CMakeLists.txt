cmake_minimum_required(VERSION 3.16)
project(ffmpegplugin LANGUAGES CXX)

# Two build modes (like plugins/tubemusic):
#  1. In-tree (the core's build does add_subdirectory(plugins/ffmpeg)):
#     the parent project defines the doob_lib target; the .so lands in
#     <build>/plugins, one of the PluginManager's search paths, so a
#     built doob loads the plugin as-is.
#  2. Standalone: find_package(doob) against an installed doob (the
#     main build's install rules ship libdoob_lib, the headers under
#     include/doob and the find_package config):
#
#       cmake -S . -B build -DCMAKE_PREFIX_PATH=<prefix>
#
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    find_package(doob REQUIRED)
    set(DOOB_LIB doob::doob_lib)
else()
    set(DOOB_LIB doob_lib)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# The FFmpeg + PulseAudio libraries (the in-tree build gates the
# add_subdirectory on their presence; the standalone build refuses
# without them).
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
        libavformat libavcodec libavutil libswresample)
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)

if(Qt6_FOUND)
    qt_add_plugin(ffmpeg ffmpegplugin.cpp ffmpegplugin.h
                  ffmpegplayer.cpp ffmpegplayer.h
                  decodeworker.cpp decodeworker.h
                  pulseout.cpp pulseout.h
                  audiotap.cpp audiotap.h
                  equalizer.cpp equalizer.h
                  equalizerprocessor.cpp equalizerprocessor.h)
else()
    add_library(ffmpeg MODULE ffmpegplugin.cpp ffmpegplugin.h
                  ffmpegplayer.cpp ffmpegplayer.h
                  decodeworker.cpp decodeworker.h
                  pulseout.cpp pulseout.h
                  audiotap.cpp audiotap.h
                  equalizer.cpp equalizer.h
                  equalizerprocessor.cpp equalizerprocessor.h)
endif()
target_link_libraries(ffmpeg PRIVATE ${DOOB_LIB} PkgConfig::FFMPEG
        PkgConfig::PULSE)
# In both modes the .so lands in <build>/plugins (one of the
# PluginManager's search paths in the dev tree).
set_target_properties(ffmpeg PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)

# The plugin's tests: they live in this directory and register themselves
# with CTest from here, so the plugin is self-sufficient in both build
# modes — in-tree the core's build picks them up (the root calls
# enable_testing() before the add_subdirectory), standalone the plugin's
# own build does. The unit tests compile the plugin's pure-C++ sources
# straight in (no plugin meta-object to link); ffmpegplugin_test only
# loads the built .so, never links it.
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
enable_testing()

set(PLUGIN_TEST_SOURCES
    tests/audiotap_test.cpp
    tests/ringbuffer_test.cpp
    tests/decodeworker_test.cpp
    tests/pulseout_test.cpp
    tests/equalizer_test.cpp
    tests/equalizerprocessor_test.cpp
    tests/ffmpegplayer_test.cpp
    tests/ffmpegplugin_test.cpp)
foreach(test_source ${PLUGIN_TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)
    if(Qt6_FOUND)
        qt_add_executable(${test_name} ${test_source})
    else()
        add_executable(${test_name} ${test_source})
    endif()
    target_link_libraries(${test_name} PRIVATE
        ${DOOB_LIB} Qt${QT_VERSION_MAJOR}::Test)
    target_include_directories(${test_name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

# The sources each unit test compiles (the same way tests/plugins/
# compiles the fake plugin): pure C++ where possible, so no extra link
# libraries — FFmpeg/Pulse only where the source uses them.
target_sources(audiotap_test PRIVATE audiotap.cpp)
target_sources(equalizer_test PRIVATE equalizer.cpp)
target_sources(equalizerprocessor_test PRIVATE equalizer.cpp
    equalizerprocessor.cpp)
# The RingBuffer (ringbuffer_test, the decodeworker and the output's
# ring) lives in the core's doob_lib (both the FFmpeg and the Spotify
# drivers share it).
target_sources(decodeworker_test PRIVATE decodeworker.cpp)
target_sources(pulseout_test PRIVATE pulseout.cpp audiotap.cpp)
target_sources(ffmpegplayer_test PRIVATE ffmpegplayer.cpp decodeworker.cpp
    pulseout.cpp audiotap.cpp equalizer.cpp equalizerprocessor.cpp)
target_link_libraries(ringbuffer_test PRIVATE PkgConfig::FFMPEG)
target_link_libraries(decodeworker_test PRIVATE PkgConfig::FFMPEG)
target_link_libraries(pulseout_test PRIVATE PkgConfig::PULSE)
target_link_libraries(ffmpegplayer_test PRIVATE PkgConfig::FFMPEG PkgConfig::PULSE)

# The pulseout test's private server is the spotify plugin's pulsestub
# (discard mode): in the in-tree build that target exists (the root
# adds the spotify plugin before the ffmpeg one).
if(TARGET pulsestub)
    target_compile_definitions(pulseout_test PRIVATE
        PULSESTUB_PATH="$<TARGET_FILE:pulsestub>")
endif()

# The plugin test points at the built .so (this directory's <build>/plugins).
target_compile_definitions(ffmpegplugin_test PRIVATE
    FFMPEG_PLUGIN_DIR="${CMAKE_BINARY_DIR}/plugins")
