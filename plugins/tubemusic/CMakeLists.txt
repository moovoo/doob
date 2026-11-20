cmake_minimum_required(VERSION 3.16)
project(tubemusicplugin LANGUAGES CXX)

# Two build modes:
#  1. In-tree (the core's build does add_subdirectory(plugins/tubemusic)):
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

if(Qt6_FOUND)
    qt_add_plugin(tubemusic tubemusic.cpp tubemusic.h ytdlp.cpp ytdlp.h)
else()
    add_library(tubemusic MODULE tubemusic.cpp tubemusic.h ytdlp.cpp ytdlp.h)
endif()
target_link_libraries(tubemusic PRIVATE ${DOOB_LIB})
# In both modes the .so lands in <build>/plugins: in-tree that is one of
# the PluginManager's search paths (the dev doob loads it as-is),
# standalone it sits next to the test executable, which loads it from
# <app dir>/plugins.
set_target_properties(tubemusic PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)

# The plugin's tests: they live in this directory and register themselves
# with CTest from here, so the plugin is self-sufficient in both build
# modes — in-tree the core's build picks them up (the root calls
# enable_testing() before the add_subdirectory), standalone the plugin's
# own build does. The test loads the .so from <build dir>/plugins (next
# to the test executable) and never links it.
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
enable_testing()

set(PLUGIN_TEST_SOURCES tests/tubemusic_test.cpp)
foreach(test_source ${PLUGIN_TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)
    if(Qt6_FOUND)
        qt_add_executable(${test_name} ${test_source})
    else()
        add_executable(${test_name} ${test_source})
    endif()
    target_link_libraries(${test_name} PRIVATE
        ${DOOB_LIB} Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
