cmake_minimum_required(VERSION 3.16)

project(doob VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Needed so clang-tidy can be run with `-p build` (see .clang-tidy / AGENTS.md).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Test Sql Svg Network DBus)

find_package(PkgConfig REQUIRED)
pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)

# The FFmpeg player plugin (optional): the libav* and PulseAudio
# libraries. Absent → the plugin is skipped (the built-in null driver
# remains — playback fails visibly, it never plays silence).
pkg_check_modules(FFMPEGPLAYER QUIET IMPORTED_TARGET
        libavformat libavcodec libavutil libswresample)
pkg_check_modules(PULSE QUIET IMPORTED_TARGET libpulse)
set(DOOB_HAVE_FFMPEG_PLAYER OFF)
if(FFMPEGPLAYER_FOUND AND PULSE_FOUND)
    set(DOOB_HAVE_FFMPEG_PLAYER ON)
endif()

set(PROJECT_SOURCES
        mainwindow.cpp
        mainwindow.h
        widgetcontainer.h widgetcontainer.cpp
        widgetcontaineroverlay.h widgetcontaineroverlay.cpp
        basewidget.h basewidget.cpp
        layoutmanager.h layoutmanager.cpp
        windowmanager.h windowmanager.cpp
        windowsdialog.h windowsdialog.cpp
        windowtogglewidget.h windowtogglewidget.cpp
        menubutton.h menubutton.cpp
        titlebar.h titlebar.cpp
        widgetregistry.h widgetregistry.cpp
        selectwidgetdialog.h selectwidgetdialog.cpp
        splitterwidget.h splitterwidget.cpp
        tabswidget.h tabswidget.cpp
        tabposition.h tabposition.cpp
        settings.h settings.cpp
        settingsmanager.h settingsmanager.cpp
        uistate.h uistate.cpp
        dialogplumbing.h
        jsonfile.h jsonfile.cpp
        entrystore.h
        stylemanager.h stylemanager.cpp
        styleproperty.h
        stylesheetdialog.h stylesheetdialog.cpp
        layoutstyledialog.h layoutstyledialog.cpp
        settingsregistry.h settingsregistry.cpp
        preferencesdialog.h preferencesdialog.cpp
        configpage.h configpage.cpp
        valueeditdialog.h valueeditdialog.cpp
        librarysettingspage.h librarysettingspage.cpp
        dspregistry.h dspregistry.cpp
        dspchain.h dspchain.cpp
        ringbuffer.h ringbuffer.cpp
        audioprocessor.h
        silenceskip.h silenceskip.cpp
        dspsettingspage.h dspsettingspage.cpp
        shortcutmanager.h shortcutmanager.cpp
        shortcutspage.h shortcutspage.cpp
        rowclipboard.h rowclipboard.cpp
        rowmenu.h rowmenu.cpp
        librarywatcher.h librarywatcher.cpp
        database.h database.cpp
        mediaitem.h
        trackmetadata.h trackmetadata.cpp
        imagefetch.h imagefetch.cpp
        artsource.h artsource.cpp
        itunesartsource.h itunesartsource.cpp
        artfallback.h artfallback.cpp
        librarymanager.h librarymanager.cpp
        mediaitemstore.h mediaitemstore.cpp
        metatransform.h metatransform.cpp
        metadatable.h
        metadatadialog.h metadatadialog.cpp
        metatransformdialog.h metatransformdialog.cpp
        singlemetadatadialog.h singlemetadatadialog.cpp
        medialistmodel.h medialistmodel.cpp
        mediafilterproxy.h mediafilterproxy.cpp
        mediagroupmodel.h mediagroupmodel.cpp
        medialistview.h medialistview.cpp
        mediagroupview.h mediagroupview.cpp
        listexpander.h listexpander.cpp
        urlimportdialog.h urlimportdialog.cpp
        librarywidget.h librarywidget.cpp
        playlist.h
        playlistfile.h playlistfile.cpp
        playlistmanager.h playlistmanager.cpp
        cliimport.h cliimport.cpp
        appinstance.h appinstance.cpp
        mpris.h mpris.cpp
        trayicon.h trayicon.cpp
        urlwizard.h urlwizard.cpp
        playlisttoolbar.h playlisttoolbar.cpp
        currentplaylistwidget.h currentplaylistwidget.cpp
        playlistswidget.h playlistswidget.cpp
        playlistlistwidget.h playlistlistwidget.cpp
        playlistlistview.h playlistlistview.cpp
        audioplayer.h
        nullplayer.h nullplayer.cpp
        playbackmanager.h playbackmanager.cpp
        playerregistry.h playerregistry.cpp
        sourceprovider.h
        sourcemanager.h sourcemanager.cpp
        doobplugin.h
        doobapi.h doobapi.cpp
        pluginmanager.h pluginmanager.cpp
        playbackcontrol.h playbackcontrol.cpp
        checkablelistdialog.h checkablelistdialog.cpp
        positionslider.h positionslider.cpp
        volumecontrol.h volumecontrol.cpp
        currenttrackwidget.h currenttrackwidget.cpp
        tracklabel.h tracklabel.cpp
        paintrate.h paintrate.cpp
        barpaint.h barpaint.cpp
        vizwidget.h
        fft.h fft.cpp
        peakmeterwidget.h peakmeterwidget.cpp
        oscillogramwidget.h oscillogramwidget.cpp
        spectrumwidget.h spectrumwidget.cpp
        spectrogramwidget.h spectrogramwidget.cpp
        equalizerwidget.h equalizerwidget.cpp
        widgetconfiguration.h widgetconfiguration.cpp
        layoutformat.h layoutformat.cpp
        names.h
)

# doob_lib is SHARED on all platforms: plugin .so files link against it,
# so a loaded plugin and the host process share one copy of the
# singletons (a static core would give the plugin its own).
add_library(doob_lib SHARED ${PROJECT_SOURCES})
# A build-tree include dir mirroring the installed layout
# (include/doob/*.h): a plugin includes <doob/foo.h> both in-tree and in
# a standalone build against the installed API.
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/include)
if(NOT EXISTS ${CMAKE_BINARY_DIR}/include/doob)
    file(CREATE_LINK ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/include/doob
         SYMBOLIC)
endif()
# The include path a plugin sees: the source tree while building,
# <prefix>/include once installed (the headers land in include/doob).
# PUBLIC so doob_lib's own sources can include <doob/foo.h> too.
target_include_directories(doob_lib
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
           $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
           $<INSTALL_INTERFACE:include>)
# Widgets/Sql/Network/DBus are PUBLIC: the public headers use them
# (QWidget & co, QSqlDatabase in database.h/mediaitemstore.h/
# playlistmanager.h, the QNetwork* types imagefetch.h's plugins reach
# through it, QDBusAbstractAdaptor in mpris.h). Svg (runtime icon theme)
# and TagLib (trackmetadata.cpp only) are implementation details —
# PRIVATE keeps them out of the exported target's compile line.
target_link_libraries(doob_lib
    PUBLIC Qt${QT_VERSION_MAJOR}::Widgets
           Qt${QT_VERSION_MAJOR}::Sql
           Qt${QT_VERSION_MAJOR}::Network
           Qt${QT_VERSION_MAJOR}::DBus
    PRIVATE Qt${QT_VERSION_MAJOR}::Svg PkgConfig::TAGLIB)

# icons.qrc goes into the executables, not doob_lib: the app and the tests
# each embed the icon resources themselves.
if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(doob
        MANUAL_FINALIZATION
        main.cpp
        icons.qrc
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET doob APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    add_executable(doob
        main.cpp
        icons.qrc
    )
endif()

target_link_libraries(doob PRIVATE doob_lib)
# The command line's --version (the project's version, at configure time).
target_compile_definitions(doob PRIVATE DOOB_VERSION="${PROJECT_VERSION}")

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.doob)
endif()
set_target_properties(doob PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS doob
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# The plugin API: the shared core (plugins link it, so a loaded plugin
# and the host share one copy of the singletons) plus the headers a
# plugin includes — the base class and facade, the three registries,
# the interfaces a plugin implements and the data types they use.
install(TARGETS doob_lib
    EXPORT doobTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(EXPORT doobTargets
    NAMESPACE doob::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/doob)

# The find_package(doob) entry point: finds the Qt components doob_lib's
# public headers use, then pulls in the exported target (doob::doob_lib).
configure_file(cmake/doob-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/doob-config.cmake @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/doob-config.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/doob)
# pluginmanager.h and settings.h ship too: a plugin's own tests load
# the built .so through PluginManager and resolve the data dir through
# Settings, so the standalone test build needs them.
install(FILES
    doobplugin.h doobapi.h
    pluginmanager.h
    sourcemanager.h sourceprovider.h
    widgetregistry.h basewidget.h widgetconfiguration.h
    playerregistry.h audioplayer.h
    dspregistry.h dspchain.h audioprocessor.h
    ringbuffer.h
    settingsregistry.h
    settingsmanager.h
    settings.h
    mediaitem.h trackmetadata.h
    imagefetch.h
    artsource.h
    eq.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/doob)

# The shipped presets: the read-only layouts and styles (Settings
# resolves the packaged ones from the standard data locations,
# <datadir>/doob). The tree is optional — an empty presets/ installs
# nothing. Recursive: a new presets/ subdir ships itself.
file(GLOB_RECURSE DOOB_PRESET_LAYOUTS RELATIVE
    "${CMAKE_CURRENT_SOURCE_DIR}/presets"
    "${CMAKE_CURRENT_SOURCE_DIR}/presets/layouts/*")
file(GLOB_RECURSE DOOB_PRESET_STYLES RELATIVE
    "${CMAKE_CURRENT_SOURCE_DIR}/presets"
    "${CMAKE_CURRENT_SOURCE_DIR}/presets/styles/*")
foreach(preset ${DOOB_PRESET_LAYOUTS} ${DOOB_PRESET_STYLES})
    get_filename_component(preset_dir "${preset}" DIRECTORY)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/presets/${preset}"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/doob/${preset_dir}")
endforeach()
# The dev build: an uninstalled executable resolves the presets from
# <app dir>/share/doob (next to itself — see Settings), so the repo's
# presets/ tree is copied into the build tree next to the executable.
if(DOOB_PRESET_LAYOUTS OR DOOB_PRESET_STYLES)
    # The dev presets as a real generated directory: a custom target the
    # build's "all" depends on (the doob executable's own target rule is
    # the right place for a POST_BUILD copy only while the target is
    # being relinked — a no-op build would leave a deleted copy gone).
    # The sources list makes the copy re-run when the tree changes.
    file(GLOB_RECURSE DOOB_PRESET_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/presets/*")
    add_custom_target(doob_presets DEPENDS
        "${CMAKE_CURRENT_BINARY_DIR}/share/doob/.stamp"
        COMMENT "Copying the preset tree")
    add_custom_command(OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/share/doob/.stamp"
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "${CMAKE_CURRENT_BINARY_DIR}/share/doob"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/presets"
            "${CMAKE_CURRENT_BINARY_DIR}/share/doob"
        COMMAND ${CMAKE_COMMAND} -E touch
            "${CMAKE_CURRENT_BINARY_DIR}/share/doob/.stamp"
        DEPENDS ${DOOB_PRESET_FILES}
        COMMENT "Refreshing the preset tree")
    add_dependencies(doob doob_presets)
endif()

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(doob)
endif()

# The test plugin: a real .so that the PluginManager tests load (one
# source provider, one widget, one playback driver). It links doob_lib,
# so the loaded plugin and the host process share the singletons.
if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_plugin(fakeplugin
        tests/plugins/fakeplugin.cpp
        tests/plugins/fakeplugin.h
    )
else()
    add_library(fakeplugin MODULE tests/plugins/fakeplugin.cpp tests/plugins/fakeplugin.h)
endif()
target_link_libraries(fakeplugin PRIVATE doob_lib)
set_target_properties(fakeplugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests/plugins
)

# The plugins/ plugins, built alongside the core (default on, for
# development): in-tree each plugin's CMakeLists links the doob_lib
# target and its .so lands in <build>/plugins, one of the
# PluginManager's search paths, so a built doob loads them as-is. A
# plugin still builds standalone against the installed API. Each
# plugin's CMakeLists also builds and registers the plugin's own tests
# (plugins/<name>/tests/) with CTest, so a plugin is self-sufficient —
# enable_testing() must precede the add_subdirectory calls so the
# subdirectories' add_test() calls are picked up.
option(DOOB_BUILD_PLUGINS "Build the plugins/ plugins alongside the core" ON)
enable_testing()
if(DOOB_BUILD_PLUGINS)
    add_subdirectory(plugins/tubemusic)
    add_subdirectory(plugins/spotify)
    if(DOOB_HAVE_FFMPEG_PLAYER)
        add_subdirectory(plugins/ffmpeg)
    else()
        message(STATUS "ffmpeg player plugin: FFmpeg/PulseAudio not found — skipped")
    endif()
endif()

set(TEST_SOURCES
        tests/tabswidget_test.cpp
        tests/tabposition_test.cpp
        tests/splitterwidget_test.cpp
        tests/widgetcontainer_test.cpp
        tests/layoutmanager_test.cpp
        tests/mainwindow_test.cpp
        tests/windowmanager_test.cpp
        tests/windowsdialog_test.cpp
        tests/windowtogglewidget_test.cpp
        tests/menubutton_test.cpp
        tests/titlebar_test.cpp
        tests/settings_test.cpp
        tests/settingsmanager_test.cpp
        tests/uistate_test.cpp
        tests/jsonfile_test.cpp
        tests/configpage_test.cpp
        tests/valueeditdialog_test.cpp
        tests/stylemanager_test.cpp
        tests/stylesheetdialog_test.cpp
        tests/database_test.cpp
        tests/trackmetadata_test.cpp
        tests/imagefetch_test.cpp
        tests/artsource_test.cpp
        tests/itunesartsource_test.cpp
        tests/artfallback_test.cpp
        tests/librarymanager_test.cpp
        tests/mediaitemstore_test.cpp
        tests/metatransform_test.cpp
        tests/metadatadialog_test.cpp
        tests/singlemetadatadialog_test.cpp
        tests/medialistmodel_test.cpp
        tests/medialistview_test.cpp
        tests/mediafilterproxy_test.cpp
        tests/mediagroupmodel_test.cpp
        tests/librarywidget_test.cpp
        tests/listexpander_test.cpp
        tests/urlimportdialog_test.cpp
        tests/playlistfile_test.cpp
        tests/playlistmanager_test.cpp
        tests/cliimport_test.cpp
        tests/appinstance_test.cpp
        tests/mpris_test.cpp
        tests/trayicon_test.cpp
        tests/currentplaylistwidget_test.cpp
        tests/playlistswidget_test.cpp
        tests/playlistlistwidget_test.cpp
        tests/playbackmanager_test.cpp
        tests/playbackcontrol_test.cpp
        tests/checkablelistdialog_test.cpp
        tests/positionslider_test.cpp
        tests/volumecontrol_test.cpp
        tests/currenttrackwidget_test.cpp
        tests/tracklabel_test.cpp
        tests/peakmeterwidget_test.cpp
        tests/oscillogramwidget_test.cpp
        tests/spectrumwidget_test.cpp
        tests/spectrogramwidget_test.cpp
        tests/fft_test.cpp
        tests/equalizerwidget_test.cpp
        tests/layoutmanager_copy_paste_test.cpp
        tests/basewidget_test.cpp
        tests/selectwidgetdialog_test.cpp
        tests/sourcemanager_test.cpp
        tests/pluginmanager_test.cpp
        tests/settingsregistry_test.cpp
        tests/playerregistry_test.cpp
        tests/widgetregistry_test.cpp
        tests/preferencesdialog_test.cpp
        tests/librarywatcher_test.cpp
        tests/librarysettingspage_test.cpp
        tests/dspregistry_test.cpp
        tests/dspchain_test.cpp
        tests/silenceskip_test.cpp
        tests/dspsettingspage_test.cpp
        tests/shortcutmanager_test.cpp
        tests/shortcutspage_test.cpp
        tests/rowclipboard_test.cpp
        tests/layoutformat_test.cpp
        tests/stylestore_test.cpp
        tests/layoutstyledialog_test.cpp
)

# The gallery test is a screenshot-regeneration tool, not a gate: it
# writes PNGs into the repo's gallery/ dir (committed artifacts, and
# font rendering differs per machine), so it is built only on request:
# -DDOOB_BUILD_GALLERY_TEST=ON.
option(DOOB_BUILD_GALLERY_TEST
    "Build the gallery screenshot test (writes into gallery/)" OFF)
if(DOOB_BUILD_GALLERY_TEST)
    list(APPEND TEST_SOURCES tests/gallery_test.cpp)
endif()

foreach(test_source ${TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)
    if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
        qt_add_executable(${test_name} ${test_source} icons.qrc)
    else()
        add_executable(${test_name} ${test_source} icons.qrc)
    endif()
    target_link_libraries(${test_name} PRIVATE doob_lib Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

# The track metadata test embeds a picture into a generated FLAC through
# TagLib directly (the library's own reader is what is under test).
target_link_libraries(trackmetadata_test PRIVATE PkgConfig::TAGLIB)

# The plugin manager test points at the built fake plugin and at
# doob_lib itself (copied into a temp dir as a "broken" .so).
target_compile_definitions(pluginmanager_test PRIVATE
    FAKE_PLUGIN_DIR="${CMAKE_CURRENT_BINARY_DIR}/tests/plugins"
    DOOB_LIB_PATH="$<TARGET_FILE:doob_lib>")

# The preferences dialog test points at the built fake plugin (the
# plugin-without-settings case).
target_compile_definitions(preferencesdialog_test PRIVATE
    FAKE_PLUGIN_DIR="${CMAKE_CURRENT_BINARY_DIR}/tests/plugins")

# The gallery test (opt-in, see above) resolves the repo's preset tree
# (this run's DOOB_PRESETS_DIR) and writes the shots to the repo's
# gallery/ dir.
if(DOOB_BUILD_GALLERY_TEST)
target_compile_definitions(gallery_test PRIVATE
    GALLERY_DIR="${CMAKE_CURRENT_SOURCE_DIR}/gallery"
    GALLERY_PRESETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/presets")
endif()

# The plugins' tests (tubemusic, ffmpeg) are built and registered by
# the plugins' own CMakeLists (see the DOOB_BUILD_PLUGINS block above).
