#ifndef STYLEMANAGER_H
#define STYLEMANAGER_H

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "entrystore.h"
#include "styleproperty.h"

class QObject;
class QMenu;

// The application's style: the app-wide Qt style sheet (QSS), held as
// text and applied live (setStyleSheet() re-skins the whole UI at once,
// which is what the style editor dialog's preview relies on), plus the
// store it lives in — the user's styles directory (styles/<name>.qss)
// and the read-only presets — parallel to the layouts' store.
//
// The active style is the "style.current" setting:
// the name of a user file or a preset (an unset current means no style
// — the first start before the picker has been answered, or a start
// with everything deleted; the platform style, the empty sheet).
// Unlike a layout, picking a style changes no window state, so a
// preset is used as-is: selecting it applies the preset's text and
// makes the read-only preset itself current — no copy is taken. The
// copy is taken only on write (the copy-on-write): a live sheet that
// has diverged from the preset's (an edit) is forked into the user's
// "<name>.modified.qss" on the next save (the style editor's OK, a
// switch, the app's exit), the origin names the preset, an existing
// copy is written over with the live sheet (the sheet the user just
// approved is the state), and the copy becomes current. A style's
// provenance rides in its file as a leading
// "/* doob-origin: preset:<name> */" comment (set by the fork, shown
// as "(from preset X)").
//
// Saving the current style (a switch, the app's exit, a new style, the
// editor's OK) writes the sheet into the current style's file — a
// plain write for a user file, the copy-on-write fork for an edited
// preset current (an unedited preset current stays a no-op — the
// read-only preset is never rewritten), a no-op for an unset current
// (there is no file to write to).
//
// The sheet also carries "x-" custom properties for custom-painted
// widgets (e.g. "x-box-size: 3px"). Qt does not understand them:
// it warns "Unknown property" and applies the rest of the rule, so the
// text is passed to Qt unmodified and a linear scan keeps the "x-"
// declarations here in document order. Widgets read them back through
// styleValue(), which resolves a declaration the way CSS custom
// properties do: the widget itself or one of its ancestors (a "#name"
// selector by objectName, a bare name by class name) — so a value set
// on a container cascades down to custom-painted descendants — and
// within one node the best matching rule wins: a "#name" selector
// beats a bare class name regardless of document order, at equal
// specificity the later rule in the sheet wins (the same resolution
// Qt applies to the native properties, so a per-instance "#name" rule
// overrides the class rule in both halves of the styling). The
// widget's coded default stands in when no rule matches.
//
// The "x-" properties are also *documented*: a widget that reads any
// declares them as StyleProperty records (name, what it does, the
// coded default, the unit and the range when it has one) and registers
// them here (registerStyleProperties — keyed by name, idempotent). The
// core's widgets register theirs from the LayoutManager's builtin
// registration, so the set is complete even for widgets that are not
// on screen; the style sheet editor's help button renders them
// (styleProperties, the sections) and nothing else.
class StyleManager
{
public:
    // The content operations of the styles' store (the store's
    // ContentOps, entrystore.h): the entry's content is the QSS text,
    // the origin rides in the file as a leading
    // "/* doob-origin: ... */" comment (display metadata — the live
    // sheet never carries it).
    struct StyleContent {
        using Value = QString;
        // The text of the file at `path` ("" when unreadable).
        static Value read(const QString &path);
        // `content` + the origin comment when `origin` names a preset,
        // as the file's bytes.
        static QByteArray write(const Value &content, const QString &origin);
        // The origin a file records ("" when it has none).
        static QString origin(const QString &path);
        // The listing's title: the name (the content carries none).
        static QString title(const Value &content) { Q_UNUSED(content); return {}; }
        // The unmodified check: the texts are equal (the origin comment
        // is stripped from the entry's side by live() before the call).
        static bool equivalent(const Value &a, const Value &b) { return a == b; }
        // A text's live form: the file's text without its provenance
        // comment (the comment is metadata of the file, never of the
        // sheet).
        static Value live(const Value &content);
        // A fresh empty text (the empty sheet).
        static Value empty() { return {}; }
    };

    using EntryStore = ::EntryStore<StyleContent>;

    // A style in the listing: its name (the file's stem), the title
    // the menus and dialogs show, and whether it is read-only (a
    // preset).
    using Info = EntryStore::Info;

    static StyleManager* getInstance();
    static void reset(); // for tests: drop the instance

    QString stylesheet() const { return m_stylesheet; }

    // Apply the sheet to the application immediately (an empty string
    // restores the platform style).
    void setStyleSheet(const QString &text);

    // The "x-" custom property `name` for `root` (see the class
    // comment): the value of the best matching rule, or `fallback`.
    QString styleValue(const QObject *root, const QString &name, const QString &fallback) const;

    // True when any rule's selector list names `objectName` (a
    // "#objectName" selector) — the free-name check for auto-named
    // instances.
    bool ruleReferences(const QString &objectName) const;

    // The "x-" property docs (see the class comment): `props` registered
    // under their names — a name re-registered replaces its record, so
    // a registration that ran earlier (a test, a second LayoutManager)
    // is not clobbered.
    void registerStyleProperties(const QList<StyleProperty> &props);
    // The registered properties, sorted by owner then name (the help's
    // stable section order).
    QList<StyleProperty> styleProperties() const;

    // The store.
    // All the styles, sorted by title: the user's files and the
    // read-only presets — a user file shadows a same-named preset.
    QList<Info> styles() const;
    // The active style's name (the "style.current" setting; empty = no
    // current style — the first start before the picker has been
    // answered, the key left unset so first-run detection sees it).
    QString currentStyle() const;
    void setCurrentStyle(const QString &name);
    // The text of the style `name` (the user's file, else the preset,
    // else "" — stale names).
    QString styleText(const QString &name) const;
    // The text of the style `name` as it is applied live: the file's
    // text without its leading provenance comment (the comment is
    // metadata of the file, never of the sheet).
    QString liveText(const QString &name) const;
    // True when `name` is a read-only preset (no user file shadows it).
    bool isPreset(const QString &name) const;
    // The origin ("preset:<name>") a style file records, "" when it has
    // none (or is not a file).
    QString originOf(const QString &name) const;
    // The save (see the class comment): the current sheet into the
    // current style's file — a plain write for a user file, the
    // copy-on-write fork for an edited preset current, a no-op for an
    // unedited preset current and for an unset current. Returns the
    // (possibly new) current name.
    QString saveCurrentStyle();
    // Apply the current style's text ("" for an unset current — the
    // preset's text when the current is a preset); a stale name (no
    // file anywhere) falls back to the first user file, or to no
    // current (the platform style) when there are none.
    void loadCurrentStyle();
    // Switch the active style: the current is saved first (the
    // copy-on-write fork for an edited preset current), then the
    // target is applied and made current — a preset target as-is (its
    // own text, no copy). False for a name without a file.
    bool switchStyle(const QString &name);
    // Fork the current sheet to the user's directory under `name`
    // (an existing file is overwritten) and make it current. The
    // provenance of the current style is carried over. False for a
    // bad name.
    bool saveStyleAs(const QString &name);
    // Create a new empty style (the empty sheet) under `name` (an
    // existing file is overwritten), apply it and make it current —
    // saving the current style as-is first (a no-op for an unset
    // current). False for a bad name.
    bool newStyle(const QString &name);
    // Delete a user style (presets are refused); when the deleted
    // style was the current, the first remaining user file becomes
    // current, or the current is unset (the platform style) when there
    // are none.
    bool deleteStyle(const QString &name);
    // The "modified" marker of the listing: true when the user file
    // records an origin preset and its text (the origin comment
    // aside) differs from that preset's.
    bool isModified(const QString &name) const;

    // The View → Styles menu: clears it and re-fills it whole — all
    // the styles, the user's files and the presets (a preset is usable
    // as-is, so it is listed like any other; the current — which may
    // be a preset — is checked) — a click switches (the previous is
    // saved by the store) — then "Manage styles" (the picker dialog —
    // the store's New/Copy to/Delete/Import/Export operations are its
    // per-pane buttons) and "Edit style sheet" (the live-preview
    // editor). Called on the menu's aboutToShow, so a style added
    // elsewhere shows up on the next open.
    void fillStylesMenu(QMenu *menu);

    // Export/import: the current sheet written to / a file's text read
    // from an explicit path — the import lands in the user's styles
    // directory under the picked file's name and becomes current.
    bool exportStyle(const QString &path) const;
    bool importStyle(const QString &path);

private:
    explicit StyleManager();

    struct Rule {
        QStringList selectors;       // comma-split, trimmed
        QHash<QString, QString> props;  // x-name -> raw value
    };

    static QList<Rule> scanCustomProperties(const QString &text);

    // The store's shared core (the paths, the current, the listing,
    // the save/load/fallback operations — entrystore.h).
    EntryStore m_store;

    QString m_stylesheet;
    QList<Rule> m_rules;
    QHash<QString, StyleProperty> m_styleProps;  // x-name -> the docs
    inline static StyleManager* m_instance;
};

#endif // STYLEMANAGER_H
