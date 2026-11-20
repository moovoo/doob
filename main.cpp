#include "appinstance.h"
#include "artfallback.h"
#include "cliimport.h"
#include "layoutmanager.h"
#include "librarymanager.h"
#include "librarywatcher.h"
#include "mainwindow.h"
#include "mediaitemstore.h"
#include "mpris.h"
#include "playbackmanager.h"
#include "playlistmanager.h"
#include "pluginmanager.h"
#include "settings.h"
#include "settingsmanager.h"
#include "trayicon.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName("doob");
    QCoreApplication::setApplicationName("doob");
    QCoreApplication::setApplicationVersion(DOOB_VERSION);
    QCoreApplication::setAttribute(Qt::AA_UseStyleSheetPropagationInWidgetStyles, true);
    QApplication a(argc, argv);

    // The command line: the positional sources (files, playlist files
    // m3u/m3u8/pls, URLs) and the two behaviour flags. A running doob
    // takes the sources over, so a second invocation forwards them and
    // exits before any state is loaded (the OS "play with doob" /
    // "append to playlist" context-menu use — see AppInstance).
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("doob — a modular desktop audio player"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("sources",
                                 QStringLiteral("Files, playlist files (m3u/m3u8/pls) and URLs to import."),
                                 QStringLiteral("[sources...]"));
    const QCommandLineOption playOption(
        QStringLiteral("play"),
        QStringLiteral("Start playing the first imported track (the default leaves the playback alone)."));
    // --new is a flag and --name its companion (this Qt's parser has no
    // optional option value: a valued --new would eat the first source).
    const QCommandLineOption newOption(
        QStringLiteral("new"),
        QStringLiteral("Import the sources into a new playlist (the current one by default)."));
    const QCommandLineOption nameOption(
        QStringLiteral("name"),
        QStringLiteral("The new playlist's name (with --new; the default name without it)."),
        QStringLiteral("name"));
    parser.addOption(playOption);
    parser.addOption(newOption);
    parser.addOption(nameOption);
    parser.process(a);

    CliRequest request;
    request.sources = parser.positionalArguments();
    request.play = parser.isSet(playOption);
    request.newPlaylist = parser.isSet(newOption);
    request.newPlaylistName = parser.value(nameOption);

    AppInstance instance;
    if (!instance.acquire(request))
        return 0;  // a running doob took the sources

    // The app icon (the title bar, the taskbar, the window list): set on
    // the application, so every top-level — the main window, the child
    // windows, the dialogs — carries it.
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/doob.svg")));

    // Resolve the data directory and load persisted state. The store and
    // the playlists are the two components of the single doob.db (each with
    // its own connection, tables and schema version); the library is a
    // membership set over the store's items, persisted by the store's
    // write-through. The SettingsManager (settings.json — user settings
    // and session state alike) loads first: the components read their
    // values from it.
    Settings::instance();
    const QString dbPath = Settings::instance()->dbPath();

    SettingsManager* settings = SettingsManager::getInstance();
    settings->setSettingsPath(Settings::instance()->settingsPath());
    settings->load();

    // The store loads first: the library's membership is the in_library
    // flag on the store's items.
    MediaItemStore* store = MediaItemStore::getInstance();
    store->setStoragePath(dbPath);
    store->load();

    LibraryManager* lib = LibraryManager::getInstance();
    lib->load();

    // The watched directories (the "library.watch.dirs" setting): the
    // initial import runs
    // asynchronously through the library's operation queue, and the
    // watcher keeps the library in line with the directories afterwards.
    LibraryWatcher::getInstance()->load();

    PlaylistManager* pl = PlaylistManager::getInstance();
    pl->setStoragePath(dbPath);
    pl->load();

    // The selected playback driver (the "playback.player" setting);
    // with no selection the default resolution applies at the first
    // track start: a preferred driver a plugin registered (the plugins
    // load after this, so the resolution is lazy), else the built-in
    // null driver.
    PlaybackManager* playback = PlaybackManager::getInstance();
    playback->loadSettings();

    // The art fallback (the art.fallback setting): a play start on an
    // artless item looks its art up through the named source. Before
    // the plugins: a plugin's source must be registered before a play
    // start can name it.
    ArtFallback::getInstance();

    // No save on quit: both components are write-through — every mutation
    // is already in the database, so a crash and a clean exit leave the
    // same state. (save() remains the manual repair/full-snapshot path.)

    // The plugins, before the window is shown: their widgets/providers/
    // drivers are registered before the layout is restored, so a saved
    // layout may reference them. They are unloaded on the way out.
    PluginManager* plugins = PluginManager::getInstance();
    plugins->loadPlugins();
    QObject::connect(&a, &QCoreApplication::aboutToQuit, plugins,
                     &PluginManager::shutdownPlugins);

    LayoutManager* layout = LayoutManager::getInstance();
    layout->show();

    // The MPRIS (org.mpris.MediaPlayer2.doob on the session bus): the desktop
    // media UIs and the media keys a compositor routes through MPRIS.
    // After the window (Raise() brings it forward); parented to the app,
    // so the quit tears it down. A headless run (no session bus) runs
    // without it (start() reports false and warns once).
    Mpris* mpris = new Mpris(&a);
    mpris->setWindow(layout->window());
    mpris->start();

    // The system tray (the QSystemTrayIcon with the transport menu):
    // after the window ("Show window" — and a tray click — brings it
    // forward), parented to the app, so the quit tears it down. A
    // system without a tray (Wayland without a status-notifier host)
    // runs without it (one warning, the "tray" close action degrades
    // to "exit").
    TrayIcon::getInstance(&a)->setWindow(layout->window());

    // The sources (its own command line, and the ones a second doob
    // forwards): imported through the same paths as the UI's adds —
    // after the plugins (a source's provider may claim it) and the
    // window (the playback the --play flag starts is visible). Before
    // the first-run dialog: it is modal, and the command line's sources
    // must not wait for a layout to be picked (the async imports finish
    // when the event loop turns, i.e. after the dialog is answered).
    CliImport cliImport;
    if (!request.sources.isEmpty())
        cliImport.import(request);
    QObject::connect(&instance, &AppInstance::payloadReceived,
                     [&cliImport](const CliRequest &received) {
        cliImport.import(received);
    });

    // The first start (no current layout, no user layouts): the
    // layout/style picker above the empty window — a selection previews
    // live, Use commits (a selected layout preset is taken as a copy,
    // a style preset is used as-is), "Start empty" creates fresh empty
    // files under the name "default".
    layout->firstRunDialog();

    return QCoreApplication::exec();
}
