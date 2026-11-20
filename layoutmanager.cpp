#include "layoutmanager.h"

#include "mainwindow.h"
#include "basewidget.h"
#include "widgetcontainer.h"
#include "preferencesdialog.h"
#include "selectwidgetdialog.h"
#include "stylesheetdialog.h"
#include "stylemanager.h"
#include "splitterwidget.h"
#include "tabswidget.h"
#include "librarywidget.h"
#include "currentplaylistwidget.h"
#include "playlistswidget.h"
#include "playlistlistwidget.h"
#include "playbackcontrol.h"
#include "positionslider.h"
#include "volumecontrol.h"
#include "currenttrackwidget.h"
#include "tracklabel.h"
#include "peakmeterwidget.h"
#include "oscillogramwidget.h"
#include "spectrumwidget.h"
#include "spectrogramwidget.h"
#include "equalizerwidget.h"
#include "windowtogglewidget.h"
#include "menubutton.h"
#include "titlebar.h"
#include "widgetconfiguration.h"
#include "widgetregistry.h"
#include "settings.h"
#include "settingsmanager.h"
#include "jsonfile.h"
#include "layoutformat.h"
#include "names.h"
#include "windowmanager.h"
#include "windowsdialog.h"
#include "layoutstyledialog.h"
#include "uistate.h"

#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QClipboard>
#include <QAction>
#include <QMenuBar>
#include <QSet>

namespace {
// The built-in widget types, registered like any plugin's would be (the
// registry is idempotent: a taken id is skipped, so a registration that
// ran earlier — e.g. in a test — is not clobbered).
void registerBuiltinWidgets()
{
    WidgetRegistry* registry = WidgetRegistry::getInstance();
    registry->registerWidget(SplitterWidget::id, SplitterWidget::title,
                             [](WidgetConfiguration &) { return new SplitterWidget; },
                             SplitterWidget::tag);
    registry->registerWidget(TabsWidget::id, TabsWidget::title,
                             [](WidgetConfiguration &) { return new TabsWidget; },
                             TabsWidget::tag);
    registry->registerWidget(LibraryWidget::id, LibraryWidget::title,
                             [](WidgetConfiguration &) { return new LibraryWidget; },
                             LibraryWidget::tag);
    registry->registerWidget(CurrentPlaylistWidget::id, CurrentPlaylistWidget::title,
                             [](WidgetConfiguration &) { return new CurrentPlaylistWidget; },
                             CurrentPlaylistWidget::tag);
    registry->registerWidget(PlaylistsWidget::id, PlaylistsWidget::title,
                             [](WidgetConfiguration &) { return new PlaylistsWidget; },
                             PlaylistsWidget::tag);
    registry->registerWidget(PlaylistListWidget::id, PlaylistListWidget::title,
                             [](WidgetConfiguration &) { return new PlaylistListWidget; },
                             PlaylistListWidget::tag);
    registry->registerWidget(PlaybackControlWidget::id, PlaybackControlWidget::title,
                             [](WidgetConfiguration &) { return new PlaybackControlWidget; },
                             PlaybackControlWidget::tag);
    registry->registerWidget(PositionSliderWidget::id, PositionSliderWidget::title,
                             [](WidgetConfiguration &) { return new PositionSliderWidget; },
                             PositionSliderWidget::tag);
    registry->registerWidget(VolumeControlWidget::id, VolumeControlWidget::title,
                             [](WidgetConfiguration &) { return new VolumeControlWidget; },
                             VolumeControlWidget::tag);
    registry->registerWidget(CurrentTrackWidget::id, CurrentTrackWidget::title,
                             [](WidgetConfiguration &) { return new CurrentTrackWidget; },
                             CurrentTrackWidget::tag);
    registry->registerWidget(TrackLabelWidget::id, TrackLabelWidget::title,
                             [](WidgetConfiguration &) { return new TrackLabelWidget; },
                             TrackLabelWidget::tag);
    registry->registerWidget(PeakMeterWidget::id, PeakMeterWidget::title,
                             [](WidgetConfiguration &) { return new PeakMeterWidget; },
                             PeakMeterWidget::tag);
    registry->registerWidget(OscillogramWidget::id, OscillogramWidget::title,
                             [](WidgetConfiguration &) { return new OscillogramWidget; },
                             OscillogramWidget::tag);
    registry->registerWidget(SpectrumWidget::id, SpectrumWidget::title,
                              [](WidgetConfiguration &) { return new SpectrumWidget; },
                              SpectrumWidget::tag);
    registry->registerWidget(SpectrogramWidget::id, SpectrogramWidget::title,
                              [](WidgetConfiguration &) { return new SpectrogramWidget; },
                              SpectrogramWidget::tag);
    registry->registerWidget(EqualizerWidget::id, EqualizerWidget::title,
                             [](WidgetConfiguration &) { return new EqualizerWidget; },
                             EqualizerWidget::tag);
    registry->registerWidget(WindowToggleWidget::id, WindowToggleWidget::title,
                             [](WidgetConfiguration &) { return new WindowToggleWidget; },
                             WindowToggleWidget::tag);
    registry->registerWidget(MenuButtonWidget::id, MenuButtonWidget::title,
                             [](WidgetConfiguration &) { return new MenuButtonWidget; },
                             MenuButtonWidget::tag);
    registry->registerWidget(TitleBarWidget::id, TitleBarWidget::title,
                             [](WidgetConfiguration &) { return new TitleBarWidget; },
                             TitleBarWidget::tag);

    // The "x-" style property docs (the style sheet editor's help): the
    // custom-painted widgets', registered here with the widgets, so the
    // set is complete even for a type that never lands on screen.
    StyleManager* styles = StyleManager::getInstance();
    styles->registerStyleProperties(PeakMeterWidget::styleProperties());
    styles->registerStyleProperties(OscillogramWidget::styleProperties());
    styles->registerStyleProperties(SpectrumWidget::styleProperties());
    styles->registerStyleProperties(SpectrogramWidget::styleProperties());
    styles->registerStyleProperties(TitleBarWidget::styleProperties());
}

// The unmodified check: two documents are equivalent when they are
// byte-identical after dropping the display keys ("title", "origin")
// the live document does not carry. A QJsonDocument byte-compare is
// valid for equal key sets (QJsonObject's hash order is deterministic).
bool documentsEquivalent(const QJsonObject &a, const QJsonObject &b)
{
    QJsonObject da = a;
    da.remove("title");
    da.remove("origin");
    QJsonObject db = b;
    db.remove("title");
    db.remove("origin");
    return QJsonDocument(da).toJson() == QJsonDocument(db).toJson();
}

bool isKnownWidgetType(const QString &type,
                       const QList<WidgetRegistry::WidgetOption> &options)
{
    return std::any_of(options.begin(), options.end(),
                       [&type](const WidgetRegistry::WidgetOption &option) {
                           return option.id == type;
                       });
}

// Walks a serialized container tree — any nesting: "containers", "tabs",
// whatever a plugin's container stores — and records every widget type
// this build cannot create: a map with a non-empty "type" that is not a
// registered widget id (a missing plugin, a typo, a newer version).
// First-occurrence order, deduplicated.
void collectUnknownWidgetTypes(const QVariant &value,
                               const QList<WidgetRegistry::WidgetOption> &options,
                               QStringList &unknown)
{
    if (value.typeId() == QMetaType::QVariantMap) {
        const QVariantMap map = value.toMap();
        const QString type = map.value("type").toString();
        if (!type.isEmpty() && !isKnownWidgetType(type, options)
            && !unknown.contains(type))
            unknown.append(type);
        for (auto it = map.cbegin(); it != map.cend(); ++it)
            collectUnknownWidgetTypes(it.value(), options, unknown);
    } else if (value.typeId() == QMetaType::QVariantList) {
        const QVariantList list = value.toList();
        for (const QVariant &item : list)
            collectUnknownWidgetTypes(item, options, unknown);
    }
}
}

// The content operations of the layouts' store (see layoutmanager.h).
QJsonObject LayoutManager::LayoutContent::read(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    return doc.isObject() ? doc.object() : QJsonObject();
}

QByteArray LayoutManager::LayoutContent::write(const QJsonObject &content, const QString &origin)
{
    // The atomic write is the store's job (jsonfile); this is only the
    // encoding. The origin rides in the document when it is set (the
    // provenance of a fork).
    QJsonObject out = content;
    if (!origin.isEmpty())
        out["origin"] = origin;
    return QJsonDocument(out).toJson();
}

QString LayoutManager::LayoutContent::origin(const QString &path)
{
    return read(path).value("origin").toString();
}

QString LayoutManager::LayoutContent::title(const QJsonObject &content)
{
    return content.value("title").toString();
}

bool LayoutManager::LayoutContent::equivalent(const QJsonObject &a, const QJsonObject &b)
{
    return documentsEquivalent(a, b);
}

QJsonObject LayoutManager::LayoutContent::live(const QJsonObject &content)
{
    return content;
}

QJsonObject LayoutManager::LayoutContent::empty()
{
    QJsonObject doc;
    doc["container"] = QJsonObject();
    doc["version"] = kLayoutVersion;
    return doc;
}

LayoutManager::LayoutManager(QObject *parent)
    : QObject{parent},
      m_store{EntryStore::Config{
          [] { return Settings::instance()->layoutsDir(); },
          [] { return Settings::instance()->presetLayoutsDir(); },
          QStringLiteral(".json"),
          QStringLiteral("layout.current"),
          [](const QString &dir, const QString &name) {
              return names::uniqueFile(QDir(dir), name + QStringLiteral(".modified"),
                                       QStringLiteral(".json"));
          },
      }}
{
    m_window = nullptr;

    // The app-level press recorder (takePressOnPopup): an app-object
    // filter sees every press, including the ones a popup's mouse grab
    // delivers to the popup itself (the container filters can't).
    qApp->installEventFilter(this);

    // The settings (like the other registrations, idempotent): the
    // stored value, else the default.
    SettingsManager* settings = SettingsManager::getInstance();
    settings->registerSetting(
        { QStringLiteral("layout.edit_mode"),
          QStringLiteral("Edit mode"),
          QStringLiteral("Widgets can be rearranged, resized and "
                         "replaced when on (the View menu toggle)."),
          true,
          QString(), });
    // The session state, registered like any other setting: the active
    // layout's name (empty = no current) and the widget auto-naming
    // counter.
    settings->registerSetting(
        { QStringLiteral("layout.current"),
          QStringLiteral("Current layout"),
          QStringLiteral("The active layout's name (the user's file it "
                         "lives in; empty = no current, the empty "
                         "window)."),
          QString(),
          QString(), });
    settings->registerSetting(
        { QStringLiteral("widgets.name_counter"),
          QStringLiteral("Widget name counter"),
          QStringLiteral("The next number of the auto-named widget "
                         "instances (\"<id>-<n>\"), one counter for all "
                         "widget ids."),
          1,
          QString(), });
    m_editMode = settings->value("layout.edit_mode").toBool();

    // The built-in widgets join the registry (a plugin's widgets do the
    // same from its initialize()).
    registerBuiltinWidgets();

    // The layout is saved by the main window's closeEvent (the app's
    // exit), not from aboutToQuit: the quit tears the windows down
    // before the signal (the close cascades to the docked children and
    // QApplication closes the top-levels), and a save at that point
    // would record every window as closed.
}

void LayoutManager::show() {
    m_window = new MainWindow();
    m_window->show();

    // Automatically restore the current layout and style (a no-op on
    // first run: there is nothing to restore).
    loadLayout();
}

// The store accessors: the shared core (entrystore.h) owns the paths,
// the "layout.current" setting and the listing.
QString LayoutManager::currentLayout() const
{
    return m_store.current();
}

void LayoutManager::setCurrentLayout(const QString &name)
{
    m_store.setCurrent(name);
}

QJsonObject LayoutManager::layoutDocument(const QString &name) const
{
    // The user's file, else the preset, else an invalid document (stale
    // names).
    return m_store.content(name);
}

bool LayoutManager::isPreset(const QString &name) const
{
    return m_store.isPreset(name);
}

QString LayoutManager::originOf(const QString &name) const
{
    return m_store.originOf(name);
}

bool LayoutManager::firstRun() const
{
    return m_store.firstRun();
}

QList<LayoutManager::LayoutInfo> LayoutManager::layouts() const
{
    return m_store.list();
}

QJsonObject LayoutManager::buildLayoutDocument() const
{
    if (m_window == nullptr)
        return {};

    // The document root holds the layout's parts as siblings: the root
    // container under its own "container" key, the window state and the
    // child windows. The "version" key marks the format (the "origin"
    // key, when there is one, is added by the file writer — it belongs
    // to the file, not to the live state).
    WidgetConfiguration saved;
    saved["container"] = m_window->mainContainer()->storeContainer();

    // The window's size and position are stored at the root of the
    // document ("window" is a reserved key). A maximized window stores
    // its normal geometry plus the flag, so a restart does not inherit a
    // full-screen "normal" size.
    const QRect geo = m_window->normalGeometry();
    QVariantMap window;
    window["x"] = geo.x();
    window["y"] = geo.y();
    window["width"] = geo.width();
    window["height"] = geo.height();
    window["maximized"] = m_window->isMaximized();

    // The window chrome (the widget-toolbar convention: each stored
    // only when non-default — a missing key takes its default on
    // restore, so no format migration). The menu bar's default is
    // shown (hidden is the non-default; the platform gate — on macOS
    // a per-window hide is a no-op, so the key never fires there);
    // the frame's default is framed (frameless is the non-default).
    if (m_window->menuBarSupported() && m_window->menuBar()->isHidden())
        window["menu_bar"] = false;
    if (m_window->isFrameless())
        window["frameless"] = true;

    // The child windows (standalone and docked) ride in the document
    // like the window state: omitted when there are none.
    const QVariantList windows = WindowManager::getInstance()->storeWindows();
    if (!windows.isEmpty())
        saved.insert("windows", windows);

    // The dock arrangement (the main window's saveState) is part of the
    // main window's state — written when there is a docked window. It
    // joins the "window" map before the map is copied into the document
    // (a later insert on the local map would not reach the copy).
    const QByteArray dockState = WindowManager::getInstance()->dockState();
    if (!dockState.isEmpty())
        window.insert("dock_state", QString::fromLatin1(dockState.toBase64()));

    saved.insert("window", window);

    QJsonObject doc = widgetConfigurationToJson(saved);
    doc["version"] = kLayoutVersion;
    return doc;
}

bool LayoutManager::applyLayoutDocument(const QJsonObject &doc, bool strict,
                                        const QString &source)
{
    if (m_window == nullptr)
        return false;

    // The version check and the migrations run first: a document
    // outlives the build that wrote it, so a stale one is brought up to
    // the current shape in memory (the file is never touched — the
    // migrated document lands in the new format on the next save). A
    // failed step rejects the document, like a failed strict import. The
    // migration works on a copy: the caller's document is untouched.
    QJsonObject migrated = doc;
    int from = 0;
    QString error;
    if (!migrateDocument(migrated, from, error)) {
        qWarning("LayoutManager::applyLayoutDocument: %s: %s",
                 qPrintable(source), qPrintable(error));
        return false;
    }
    if (from < kLayoutVersion)
        qWarning("LayoutManager::applyLayoutDocument: %s migrated from version %d",
                 qPrintable(source), from);

    // The root container sits under its own "container" key, a sibling
    // of "window" and "windows". A document without the key is invalid:
    // there is nothing to restore.
    if (!migrated.contains("container"))
        return false;
    WidgetConfiguration cfg = migrated.value("container").toObject().toVariantMap();

    // A widget type this build cannot create (a missing plugin, a typo,
    // a newer version) is found by walking the whole serialized tree.
    // Strict (the import) rejects the file on any of them; lenient (the
    // automatic loads) warns and keeps going, and the offending
    // subtrees are skipped on the restore (createWidget returns
    // nullptr for them). An empty type (an empty main container) is a
    // valid saved state. The child windows' containers are walked like
    // the main one.
    const QList<WidgetRegistry::WidgetOption> options = widgetOptions();
    QStringList unknown;
    collectUnknownWidgetTypes(cfg, options, unknown);
    const QVariantList windows = migrated.value("windows").toVariant().toList();
    for (const QVariant &entry : windows)
        collectUnknownWidgetTypes(entry.toMap().value("container"), options, unknown);
    for (const QString &type : unknown)
        qWarning("LayoutManager::applyLayoutDocument: %s references unknown widget type '%s'",
                 qPrintable(source), qPrintable(type));
    if (strict && !unknown.isEmpty())
        return false;

    // An unknown root leaves nothing to restore, even in lenient mode:
    // keep the current layout.
    const QString type = cfg.value("type").toString();
    if (!type.isEmpty() && !isKnownWidgetType(type, options))
        return false;

    // The window state (read once, applied before the container and the
    // geometry): the chrome keys first — the frameless flag before the
    // geometry restore, because the frame change alters the window's
    // size on X11 (the geometry set after the flag wins). Each key is
    // stored only when non-default: a missing key takes its default
    // (shown menu bar, framed window).
    const QVariantMap window = migrated.value("window").toObject().toVariantMap();
    if (m_window->framelessSupported())
    {
        const bool frameless = window.value("frameless", false).toBool();
        if (m_window->isFrameless() != frameless)
            m_window->setFrameless(frameless);
    }
    if (m_window->menuBarSupported())
    {
        const bool shown = window.value("menu_bar", true).toBool();
        if (m_window->menuBar()->isVisible() != shown)
            m_window->setMenuBarVisible(shown);
    }

    m_window->mainContainer()->restoreContainer(cfg);

    // The child windows are torn down and rebuilt; the dock arrangement
    // (in the main window's state, base64 in the document) is applied
    // once the docks are back, so it also restores which docks were
    // hidden.
    WindowManager::getInstance()->restoreWindows(
        windows,
        QByteArray::fromBase64(
            window.value("dock_state").toByteArray()));

    // Restore the window size/position stored at the document root;
    // a missing, degenerate or oversized size keeps the default.
    const QRect geo = UiState::restoredGeometry(window);
    if (geo.isValid()) {
        m_window->setGeometry(geo);
        if (window.value("maximized", false).toBool())
            m_window->showMaximized();
    }
    return true;
}

QString LayoutManager::saveCurrentLayout()
{
    // The save (see the class comment): the live layout into the
    // current layout's file — a plain write for a user file, a fork
    // for a stale preset current, a no-op for an unset current (there
    // is no file to write to).
    if (m_window == nullptr)
        return currentLayout();
    return m_store.saveCurrent(buildLayoutDocument());
}

void LayoutManager::loadCurrentLayout()
{
    const QString name = m_store.resolveCurrent();
    if (name.isEmpty())
        return; // no current (or none left): the window starts empty
    const QJsonObject doc = layoutDocument(name);
    if (doc.isEmpty())
        return; // unreadable: keep the current layout
    applyLayoutDocument(doc, false, name);
}

bool LayoutManager::switchLayout(const QString &name)
{
    if (name == currentLayout())
        return true; // already on it (an unset current is not a target)
    // The target is validated before anything is saved: a refused
    // switch leaves the current (and its save) untouched. A preset
    // target must also be readable (an unreadable preset would leave
    // the current half-saved).
    if (!m_store.available(name))
        return false;
    if (m_store.isPreset(name) && layoutDocument(name).isEmpty())
        return false;
    saveCurrentLayout(); // the previous layout is saved first (a no-op
                         // when there is no current)
    if (isPreset(name))
        return forkLayout(name); // a preset is used as a copy
    applyLayoutDocument(layoutDocument(name), false, name);
    setCurrentLayout(name);
    return true;
}

bool LayoutManager::saveLayoutAs(const QString &name)
{
    // The live layout under `name`, made current — the current
    // layout's provenance carried over (the store's operation).
    return m_store.saveAs(name, buildLayoutDocument());
}

bool LayoutManager::newLayout(const QString &name)
{
    // A fresh empty layout under `name`: the document is just the
    // (empty) container and the version key, applied and made current.
    // The current layout is saved as-is first (a no-op for an unset
    // current), so the arrangement the user was working on is never
    // lost.
    if (m_window == nullptr)
        return false;
    return m_store.create(name, buildLayoutDocument(), [this] {
        WidgetConfiguration empty;
        m_window->mainContainer()->restoreContainer(empty);
        WindowManager::getInstance()->restoreWindows(QVariantList(), QByteArray());
    });
}

bool LayoutManager::forkLayout(const QString &name)
{
    // Taking a copy of a preset: the user's copy lives at
    // "<name>.modified" (its provenance names the preset). An existing
    // copy is adopted (switched to, as it stands) rather than
    // duplicated; a missing one is created from the preset's own
    // document (not the live state).
    const QString fork = name + QStringLiteral(".modified");
    const QString forkPath = Settings::instance()->layoutsDir()
        + QLatin1Char('/') + fork + QStringLiteral(".json");
    if (!QFile::exists(forkPath)) {
        const QJsonObject doc = layoutDocument(name);
        if (doc.isEmpty())
            return false;
        if (!atomicWrite(forkPath,
                         LayoutManager::LayoutContent::write(doc, QStringLiteral("preset:%1").arg(name)),
                         "LayoutManager::forkLayout"))
            return false;
    }
    setCurrentLayout(fork);
    applyLayoutDocument(layoutDocument(fork), false, fork);
    return true;
}

bool LayoutManager::isModified(const QString &name) const
{
    // The "modified" marker: the user file records an origin preset and
    // its content differs from that preset's (the same equivalence the
    // save of a stale preset current uses).
    return m_store.isModified(name);
}

bool LayoutManager::deleteLayout(const QString &name)
{
    // User files only: the presets are read-only.
    QString applyName;
    if (!m_store.remove(name, &applyName))
        return false;
    if (applyName.isEmpty()) {
        // The deleted layout was the current and no user file is left:
        // the current is unset and the widgets cleared.
        WidgetConfiguration empty;
        m_window->mainContainer()->restoreContainer(empty);
        WindowManager::getInstance()->restoreWindows(QVariantList(), QByteArray());
    } else {
        // The first remaining user file takes over as the current.
        applyLayoutDocument(layoutDocument(applyName), false, applyName);
    }
    return true;
}

void LayoutManager::saveLayout() {
    // The app's exit: the save of the current layout and the current
    // style (a no-op for an unset current or an unedited preset
    // current — there is no file to write to).
    saveCurrentLayout();
    StyleManager::getInstance()->saveCurrentStyle();
}

void LayoutManager::loadLayout() {
    // The automatic load is lenient: a saved layout that references a
    // widget whose plugin is missing skips those subtrees with a warning
    // instead of losing the whole layout (the import stays strict).
    loadCurrentLayout();
    StyleManager::getInstance()->loadCurrentStyle();
}

bool LayoutManager::saveLayoutTo(const QString &path) const {
    // The export: the live layout under the picked file, the current
    // layout's provenance carried over.
    return m_store.exportTo(path, buildLayoutDocument());
}

bool LayoutManager::loadLayoutFrom(const QString &path, bool strict) {
    // The import: read the file and apply its document. The caller
    // (the menu slot) is responsible for the file dialog and, for the
    // import into the user's layouts directory, the switch.
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return false; // nothing saved yet: keep the default layout
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject() || doc.object().isEmpty())
        return false; // invalid or empty: keep the default layout
    return applyLayoutDocument(doc.object(), strict, path);
}

bool LayoutManager::exportLayout() {
    if (m_window == nullptr)
        return false;

    const QString picked = QFileDialog::getSaveFileName(m_window, "Export layout",
                                                        QString(),
                                                        "Layout files (*.json)");
    if (picked.isEmpty())
        return false;

    // A name without an extension is stored as .json.
    const QString path = picked.endsWith(".json", Qt::CaseInsensitive)
        ? picked
        : picked + ".json";
    if (!saveLayoutTo(path)) {
        QMessageBox::warning(m_window, "Export layout", "Could not write the layout file.");
        return false;
    }
    return true;
}

bool LayoutManager::importLayout() {
    if (m_window == nullptr)
        return false;

    const QString path = QFileDialog::getOpenFileName(m_window, "Import layout",
                                                      QString(),
                                                      "Layout files (*.json)");
    if (path.isEmpty())
        return false;

    // Strict: a document that references a widget type this build cannot
    // create (a missing plugin, a typo) is refused whole.
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(m_window, "Import layout", "Could not read the layout file.");
        return false;
    }
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject() || doc.object().isEmpty()
        || !applyLayoutDocument(doc.object(), true, path)) {
        QMessageBox::warning(m_window, "Import layout", "Could not read the layout file.");
        return false;
    }

    // The import lands in the user's layouts directory under the
    // picked file's name (a name collision overwrites) and becomes the
    // current layout.
    const QString name = QFileInfo(path).completeBaseName();
    if (name.isEmpty()) {
        QMessageBox::warning(m_window, "Import layout", "Could not read the layout file.");
        return false;
    }
    atomicWrite(Settings::instance()->layoutsDir() + QLatin1Char('/')
                    + name + QStringLiteral(".json"),
                LayoutManager::LayoutContent::write(doc.object(), doc.object().value("origin").toString()),
                "LayoutManager::importLayout");
    setCurrentLayout(name);
    return true;
}

void LayoutManager::fillLayoutMenu(QMenu *menu)
{
    // The dynamic list: the user's layouts — the files in the layouts
    // directory. The current one is checked, a click switches (the
    // previous is saved by the store; a preset is switched as a copy).
    menu->clear();
    const QString current = currentLayout();

    auto addEntry = [this, menu, current](const QString &name,
                                          const QString &title) {
        QAction *action = menu->addAction(title);
        action->setCheckable(true);
        action->setChecked(name == current);
        connect(action, &QAction::triggered, this, [this, name] {
            switchLayout(name);
        });
    };

    // The user's files (the presets are not listed — they are taken as
    // copies from the dialog), in the listing's title-sorted order.
    for (const LayoutInfo &info : layouts())
        if (!info.readOnly)
            addEntry(info.name, info.title);

    menu->addSeparator();

    // The windows editor: the layout's child windows (standalone and
    // docked) — add, remove, rename, convert and show/hide.
    QAction *manageWindows = menu->addAction("Manage windows");
    connect(manageWindows, &QAction::triggered, this, &LayoutManager::editWindows);

    menu->addSeparator();

    // The management of the store (the picker dialog lists everything,
    // presets included: delete needs a selection there; the New/Copy
    // to/Import/Export operations are its per-pane buttons). The
    // dialog opens on the Layout tab.
    QAction *manageLayouts = menu->addAction("Manage layouts");
    connect(manageLayouts, &QAction::triggered, this, [this] {
        showLayoutStyleDialog(0);
    });
}

void LayoutManager::showLayoutStyleDialog(int store) {
    if (m_window == nullptr)
        return;

    // Non-modal (the previews and the windows the picker can show must
    // be interactable at once) and single-instance (a second trigger
    // raises the open dialog and switches to the requested tab), like
    // the windows editor.
    if (m_layoutStyleDialog) {
        m_layoutStyleDialog->showTab(
            store == 0 ? LayoutStyleDialog::Store::Layout
                       : LayoutStyleDialog::Store::Style);
        m_layoutStyleDialog->raise();
        m_layoutStyleDialog->activateWindow();
        return;
    }
    m_layoutStyleDialog = new LayoutStyleDialog(LayoutStyleDialog::Mode::Picker, m_window);
    m_layoutStyleDialog->showTab(
        store == 0 ? LayoutStyleDialog::Store::Layout
                   : LayoutStyleDialog::Store::Style);
    m_layoutStyleDialog->setAttribute(Qt::WA_DeleteOnClose);
    m_layoutStyleDialog->show();
}

void LayoutManager::firstRunDialog() {
    if (m_window == nullptr || !firstRun())
        return;

    // The first start: the picker above the empty window, modal — no
    // Cancel: a selection is committed (Use) or fresh empty files are
    // created ("Start empty"), so after the dialog the currents are
    // files and the picker is not repeated.
    LayoutStyleDialog dialog(LayoutStyleDialog::Mode::FirstRun, m_window);
    dialog.exec();
}

void LayoutManager::editStyleSheet() {
    if (m_window == nullptr)
        return;

    // The dialog applies its result to the StyleManager itself (OK
    // commits — including the save to the current style's file —,
    // Cancel reverts).
    StyleSheetDialog dialog(m_window);
    dialog.exec();
}

void LayoutManager::editWindows() {
    if (m_window == nullptr)
        return;

    // The dialog is a live view over the WindowManager: its changes
    // apply immediately (like the edit-mode widget changes) and land in
    // the layout document on the next save. It is shown non-modal,
    // because a window shown from it must be interactable at once (a
    // modal dialog would lock the user out of a window big enough to
    // cover it), and it is a single instance (a second trigger raises
    // the open one).
    if (m_windowsDialog) {
        m_windowsDialog->raise();
        m_windowsDialog->activateWindow();
        return;
    }
    m_windowsDialog = new WindowsDialog(WindowsDialog::Mode::Editor, m_window);
    m_windowsDialog->setAttribute(Qt::WA_DeleteOnClose);
    m_windowsDialog->show();
}

bool LayoutManager::exportStyle() {
    if (m_window == nullptr)
        return false;

    const QString picked = QFileDialog::getSaveFileName(m_window, "Export style",
                                                        QString(),
                                                        "Style files (*.qss)");
    if (picked.isEmpty())
        return false;

    const QString path = picked.endsWith(".qss", Qt::CaseInsensitive)
        ? picked
        : picked + ".qss";
    if (!StyleManager::getInstance()->exportStyle(path)) {
        QMessageBox::warning(m_window, "Export style", "Could not write the style file.");
        return false;
    }
    return true;
}

bool LayoutManager::importStyle() {
    if (m_window == nullptr)
        return false;

    const QString path = QFileDialog::getOpenFileName(m_window, "Import style",
                                                      QString(),
                                                      "Style files (*.qss)");
    if (path.isEmpty())
        return false;

    if (!StyleManager::getInstance()->importStyle(path)) {
        QMessageBox::warning(m_window, "Import style", "Could not read the style file.");
        return false;
    }
    return true;
}

void LayoutManager::showPreferences() {
    if (m_window == nullptr)
        return;

    // The dialog lists the core's and the plugins' settings topics and
    // shows the selected one's page; the pages own their state.
    PreferencesDialog dialog(m_window);
    dialog.exec();
}

bool LayoutManager::eventFilter(QObject *obj, QEvent *event)
{
    Q_UNUSED(obj)
    // Every press records whether a popup was open under it (the
    // active popup widget holds the mouse grab): the state a dismissal
    // leaves behind is overwritten by the next press, whatever it is.
    if (event->type() == QEvent::MouseButtonPress)
        m_pressOnPopup = QApplication::activePopupWidget() != nullptr;
    return false;
}

bool LayoutManager::takePressOnPopup()
{
    const bool onPopup = m_pressOnPopup;
    m_pressOnPopup = false;
    return onPopup;
}

void LayoutManager::mouseEnteredContainer(WidgetContainer* container)
{
    // A re-entry into the same container (moving between its child
    // widgets) is not a new entry: the flip-flop keeps alternating there.
    if (m_mouseContainer.data() == container)
        return;
    // An Enter on an ANCESTOR container is not the mouse moving to a new
    // widget either: when a popup (the edit menu, a native menu) closes,
    // Qt re-enters the whole chain from the outermost container down to
    // the widget under the cursor — every container in the chain gets its
    // Enter, and the mouse is in the leaf (the LAST one). Resetting on
    // the ancestor Enters would undo the "native next" the just-closed
    // menu set (the flip-flop would never reach the native menu).
    for (QWidget *w = m_mouseContainer.data(); w != nullptr;
         w = w->parentWidget())
        if (w == container)
            return;
    m_mouseContainer = container;
    // Entering a different container: the first right-click on it must be
    // the edit menu, not the "native next" a previous widget left behind.
    setEditRightClickPassThrough(false);
}

void LayoutManager::setEditMode(bool enabled) {
    m_editMode = enabled;
    // The right-click flip-flop resets with the toggle: a stale "on"
    // must not eat the first right-click after re-entering edit mode. The
    // mouse-entry tracker goes with it, so the next entry is a fresh one.
    m_editRightClickPassThrough = false;
    m_mouseContainer = nullptr;
    // Write-through: the toggle is the user setting (the SettingsManager's
    // changed() signal is the settings page's refresh).
    SettingsManager::getInstance()->setValue("layout.edit_mode", enabled);
    emit editModeChanged(enabled);
}

LayoutManager* LayoutManager::getInstance(QObject *parent) {
    if (m_layoutManager == nullptr) {
        m_layoutManager = new LayoutManager(parent);
    }
    return m_layoutManager;
}


void LayoutManager::popupContainerMenu(WidgetContainer* container)
{
    m_lastContainer = container;

    QMenu* m_menu = new QMenu();
    BaseWidget *w = container->getWidget();

    QAction *set_widget_action = m_menu->addAction("Set widget...");
    connect(set_widget_action, SIGNAL(triggered(bool)), this, SLOT(onMenuSetWidget(bool)) );

    m_menu->addSeparator();

    QAction *copy_action = m_menu->addAction("Copy widget");
    copy_action->setEnabled(w != nullptr);
    connect(copy_action, &QAction::triggered, this, &LayoutManager::onMenuCopyWidget);

    QAction *cut_action = m_menu->addAction("Cut widget");
    cut_action->setEnabled(w != nullptr);
    connect(cut_action, &QAction::triggered, this, &LayoutManager::onMenuCutWidget);

    QAction *delete_action = m_menu->addAction("Delete widget");
    delete_action->setEnabled(w != nullptr);
    connect(delete_action, &QAction::triggered, this, &LayoutManager::onMenuDeleteWidget);

    QClipboard* clipboard = QGuiApplication::clipboard();
    QAction *paste_action = m_menu->addAction("Paste widget");
    paste_action->setEnabled(clipboard != nullptr && isSerializedWidget(clipboard->text()));
    connect(paste_action, &QAction::triggered, this, &LayoutManager::onMenuPasteWidget);

    if (w != nullptr)
    {
        QMenu *context = container->getWidget()->widgetContextMenu();
        if (context != nullptr)
        {
            m_menu->addSeparator();
            //QString wid = w->widgetId();
            context->setTitle(QStringLiteral("Edit ") + w->widgetTitle());
            m_menu->addMenu( context );
            connect(m_menu, SIGNAL(aboutToHide()), context, SLOT(deleteLater()));
        }
    }
    connect(m_menu, SIGNAL(aboutToHide()), this, SLOT(onMenuHide()));
    connect(m_menu, SIGNAL(aboutToHide()), m_menu, SLOT(deleteLater()));
    m_menu->popup(QCursor::pos());
}

void LayoutManager::onMenuHide()
{
    // The edit menu closed: the flip-flop flips to "native next" (the
    // pass-through flips it back — see editRightClickPassThrough).
    setEditRightClickPassThrough(true);
    if (m_lastContainer)
    {
        m_lastContainer->getOverlayWidget()->grayed(false);
    }
}

QList<WidgetRegistry::WidgetOption> LayoutManager::widgetOptions()
{
    return WidgetRegistry::getInstance()->options();
}

void LayoutManager::onMenuSetWidget(bool checked)
{
    if (m_lastContainer == nullptr)
        return;
    QString selected("");
    if (m_lastContainer->getWidget() != nullptr)
    {
        selected = m_lastContainer->getWidget()->widgetId();
    }
    SelectWidgetDialog dialog(widgetOptions(), selected);
    if (dialog.exec() == QDialog::Accepted)
    {
        QString newWidgetId = dialog.value();
        if (selected != newWidgetId)
        {
            WidgetConfiguration wc;
            m_lastContainer->setWidget( createWidget(newWidgetId, wc) );
        }
    }
}

void LayoutManager::onMenuCopyWidget()
{
    if (m_lastContainer == nullptr || m_lastContainer->getWidget() == nullptr)
        return;

    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard != nullptr)
        clipboard->setText(serializeContainer(m_lastContainer));
}

void LayoutManager::onMenuCutWidget()
{
    if (m_lastContainer == nullptr || m_lastContainer->getWidget() == nullptr)
        return;

    // Serialize first: once the widget is removed there is nothing left to copy.
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard != nullptr)
        clipboard->setText(serializeContainer(m_lastContainer));

    m_lastContainer->setWidget(nullptr);
}

void LayoutManager::onMenuDeleteWidget()
{
    if (m_lastContainer == nullptr || m_lastContainer->getWidget() == nullptr)
        return;

    // Just remove the widget; unlike cut there is no clipboard round-trip.
    m_lastContainer->setWidget(nullptr);
}

void LayoutManager::onMenuPasteWidget()
{
    if (!m_lastContainer)
        return;

    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr)
        return;

    const QString text = clipboard->text();
    if (!isSerializedWidget(text))
        return;

    m_lastContainer->setWidget(createWidgetFromSerializedText(text));
}

QString LayoutManager::serializeContainer(WidgetContainer* container)
{
    const WidgetConfiguration config = container->storeContainer();
    return QString::fromUtf8(QJsonDocument(widgetConfigurationToJson(config)).toJson(QJsonDocument::Compact));
}

bool LayoutManager::isSerializedWidget(const QString& text) const
{
    if (text.trimmed().isEmpty())
        return false;

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    const QString type = doc.object().value("type").toString();
    if (type.isEmpty())
        return false;

    // Only widget ids offered in the "Set widget..." dialog can be pasted,
    // so garbage clipboard content never wipes the container.
    const QList<WidgetRegistry::WidgetOption> options = widgetOptions();
    return std::any_of(options.begin(), options.end(),
                       [&type](const WidgetRegistry::WidgetOption& option) {
                           return option.id == type;
                       });
}

BaseWidget* LayoutManager::createWidgetFromSerializedText(const QString& text)
{
    if (!isSerializedWidget(text))
        return nullptr;

    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    WidgetConfiguration config = widgetConfigurationFromJson(doc.object());
    return createWidget(config.value("type").toString(), config);
}

BaseWidget* LayoutManager::createWidget(const QString& widgetId, WidgetConfiguration& config)
{
    BaseWidget* widget = WidgetRegistry::getInstance()->create(widgetId, config);
    if (widget == nullptr)
        return nullptr;
    widget->restoreWidget(config);
    return widget;
}

QString LayoutManager::freeInstanceName(const QString &widgetId)
{
    // One counter for all widget ids (the id prefixes the name anyway);
    // the live checks below keep it correct even when the counter was
    // reset (it restarts at 1).
    SettingsManager* settings = SettingsManager::getInstance();
    int counter = std::max(settings->value("widgets.name_counter").toInt(), 1);

    // Every objectName a live layout widget carries (a copy-paste keeps
    // the name, so a fresh instance must not take it) — in the main
    // container and in the windows' containers alike.
    QSet<QString> taken;
    if (m_window != nullptr) {
        QList<WidgetContainer*> containers;
        containers.append(m_window->mainContainer());
        containers.append(WindowManager::getInstance()->containers());
        for (WidgetContainer* container : containers) {
            const QList<BaseWidget*> widgets =
                container->findChildren<BaseWidget*>(Qt::FindChildrenRecursively);
            for (BaseWidget* widget : widgets)
                if (!widget->objectName().isEmpty())
                    taken.insert(widget->objectName());
        }
    }
    StyleManager* styles = StyleManager::getInstance();

    QString name;
    do {
        name = widgetId + QLatin1Char('-') + QString::number(counter);
        ++counter;
    } while (taken.contains(name) || styles->ruleReferences(name));

    settings->setValue("widgets.name_counter", counter);
    return name;
}
