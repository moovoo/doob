#include "stylemanager.h"

#include "jsonfile.h"
#include "layoutmanager.h"
#include "names.h"
#include "settings.h"
#include "settingsmanager.h"

#include <QApplication>
#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QObject>

#include <algorithm>

namespace {

// The custom-property prefix. Qt's style engine does not understand
// these declarations (it warns "Unknown property" and applies the rest
// of the rule), so the scan records them here and the widgets read
// them back through StyleManager::styleValue().
const QLatin1String kCustomPrefix{"x-"};
const QLatin1String kImportant{"!important"};

// The index of the closing '/' of the "/*" comment that starts at
// `start`; an unterminated comment runs to the end of the text.
qsizetype skipComment(const QString &text, qsizetype start)
{
    qsizetype i = start + 2;
    while (i + 1 < text.size()) {
        if (text.at(i) == QLatin1Char('*') && text.at(i + 1) == QLatin1Char('/'))
            return i + 1;
        ++i;
    }
    return text.size();
}

// The "x-" declarations of a rule body: each declaration ends at a ';'
// (or at the end of the body), is split at its first ':' and kept when
// its name carries the prefix ("!important" stripped from the value).
QHash<QString, QString> customProps(const QString &body)
{
    QHash<QString, QString> props;
    for (const QString& part : body.split(QLatin1Char(';'))) {
        const QString trimmed = part.trimmed();
        if (trimmed.isEmpty())
            continue;
        const int colon = static_cast<int>(trimmed.indexOf(QLatin1Char(':')));
        if (colon <= 0)
            continue;
        const QString name = trimmed.left(colon).trimmed();
        if (!name.startsWith(kCustomPrefix))
            continue;
        QString value = trimmed.mid(colon + 1).trimmed();
        if (value.endsWith(kImportant, Qt::CaseInsensitive))
            value = value.left(value.size() - kImportant.size()).trimmed();
        props.insert(name, value);
    }
    return props;
}

// The selector list of a rule, comma-split and trimmed.
QStringList selectorParts(const QString &selector)
{
    QStringList parts;
    for (const QString& part : selector.split(QLatin1Char(','))) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty())
            parts.append(trimmed);
    }
    return parts;
}

// The file's text without its leading "/* doob-origin: ... */" comment
// (and its separator): the provenance is display metadata, not content
// — the live sheet carries no comment, and the "modified" compare
// strips it from the user's file before looking at the preset's text
// (a comment-only difference is not a modification).
QString withoutOriginComment(const QString &text)
{
    const QString trimmed = text.trimmed();
    const QLatin1String prefix{"/* doob-origin: "};
    if (!trimmed.startsWith(prefix))
        return text;
    const qsizetype end = trimmed.indexOf(QStringLiteral("*/"));
    if (end < 0)
        return text;
    return text.mid(end + 2).trimmed();
}

// `text` + the origin comment when `origin` names a preset: the
// provenance comment leads the file (a valid QSS comment, shown as
// "(from preset X)"); only a preset's origin is recorded.
QByteArray fileBytes(const QString &text, const QString &origin)
{
    if (origin.startsWith(QStringLiteral("preset:"), Qt::CaseSensitive))
        return QStringLiteral("/* doob-origin: %1 */\n").arg(origin).toUtf8()
            + text.toUtf8();
    return text.toUtf8();
}

} // namespace

// The content operations of the styles' store (see stylemanager.h).
QString StyleManager::StyleContent::read(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll());
}

QByteArray StyleManager::StyleContent::write(const QString &content, const QString &origin)
{
    return fileBytes(content, origin);
}

QString StyleManager::StyleContent::origin(const QString &path)
{
    // The leading "/* doob-origin: ... */" comment, when the file has
    // one (the first non-whitespace of the file must open it).
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QString head = QString::fromUtf8(f.readAll()).trimmed().left(128);
    const QLatin1String prefix{"/* doob-origin: "};
    if (!head.startsWith(prefix))
        return {};
    const qsizetype end = head.indexOf(QLatin1String("*/"));
    if (end < prefix.size())
        return {};
    return head.mid(prefix.size(), end - prefix.size()).trimmed();
}

QString StyleManager::StyleContent::live(const QString &content)
{
    return withoutOriginComment(content);
}

StyleManager* StyleManager::getInstance()
{
    if (m_instance == nullptr)
        m_instance = new StyleManager();
    return m_instance;
}

void StyleManager::reset()
{
    delete m_instance;
    m_instance = nullptr;
}

StyleManager::StyleManager()
    : m_store{EntryStore::Config{
          [] { return Settings::instance()->stylesDir(); },
          [] { return Settings::instance()->presetStylesDir(); },
          QStringLiteral(".qss"),
          QStringLiteral("style.current"),
          [](const QString &, const QString &name) {
              return name + QStringLiteral(".modified.qss");
          },
      }}
{
    // The session state, registered like any other setting: the active
    // style's name (empty = no current, the platform style).
    SettingsManager::getInstance()->registerSetting(
        { QStringLiteral("style.current"),
          QStringLiteral("Current style"),
          QStringLiteral("The active style's name (the user's file or "
                         "the preset it lives in; empty = no current, "
                         "the platform style)."),
          QString(),
          QString(), });
}

QList<StyleManager::Rule> StyleManager::scanCustomProperties(const QString &text)
{
    // One linear pass (find-only, the text is never modified): outside
    // a block the text accumulates into the current selector, inside a
    // block into the current body; at the closing brace the rule is
    // recorded — its "x-" declarations (when there are any) for
    // styleValue(), its selectors always (ruleReferences() checks
    // plain rules too).
    QList<Rule> rules;
    QString selector;
    QString body;
    bool inBlock = false;

    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        // Skip a /* ... */ comment (an unterminated one runs to the end).
        if (c == QLatin1Char('/')
            && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('*')) {
            i = skipComment(text, i);
            continue;
        }
        if (!inBlock) {
            if (c == QLatin1Char('{')) {
                inBlock = true;
                body.clear();
            } else {
                selector.append(c);
            }
        } else if (c == QLatin1Char('}')) {
            inBlock = false;
            // Keep the rule even when it carries no "x-" declaration:
            // the selector index must see every rule (ruleReferences()
            // checks plain rules too — an empty "#name { }" block is
            // exactly what the style action starts from).
            rules.append(Rule{selectorParts(selector), customProps(body)});
            selector.clear();
            body.clear();
        } else {
            body.append(c);
        }
    }
    return rules;
}

QString StyleManager::styleValue(const QObject *root, const QString &name,
                                 const QString &fallback) const
{
    // The widget itself first, then the ancestors: a value named for
    // the widget beats an inherited one (CSS custom-property
    // semantics). Within one node the best matching rule wins: a
    // "#name" selector (specificity 1) beats a bare class name
    // (specificity 0) regardless of document order, and at equal
    // specificity the later rule in the sheet wins — the same
    // resolution Qt applies to the native properties of the same
    // sheet, so a per-instance "#name" rule overrides the class rule
    // in both halves of the styling.
    const QObject *node = root;
    while (node != nullptr) {
        const QString objectName = node->objectName();
        const QString className = node->metaObject()->className();
        int bestSpecificity = -1;
        QString value;
        for (const Rule& rule : m_rules) {
            const auto it = rule.props.constFind(name);
            if (it == rule.props.constEnd())
                continue;
            for (const QString& selector : rule.selectors) {
                const int specificity = selector.startsWith(QLatin1Char('#')) ? 1 : 0;
                // Each part of a comma list scores on its own: a rule
                // that matches this node by its class part carries
                // class specificity. An unnamed node matches no "#"
                // part.
                const bool matches = specificity == 1
                    ? (!objectName.isEmpty() && selector.mid(1) == objectName)
                    : selector == className;
                if (matches && specificity >= bestSpecificity) {
                    bestSpecificity = specificity;
                    value = it.value();
                }
            }
        }
        if (bestSpecificity >= 0)
            return value;
        node = node->parent();
    }
    return fallback;
}

bool StyleManager::ruleReferences(const QString &objectName) const
{
    // True when any rule's selector list names the objectName — the
    // free-name check that keeps an auto-named instance from picking
    // up a rule left behind by a deleted widget.
    const QString idSelector = QStringLiteral("#") + objectName;
    for (const Rule& rule : m_rules)
        for (const QString& part : rule.selectors)
            if (part == idSelector)
                return true;
    return false;
}

void StyleManager::registerStyleProperties(const QList<StyleProperty> &props)
{
    for (const StyleProperty &prop : props)
        if (!prop.name.isEmpty())
            m_styleProps.insert(prop.name, prop);
}

QList<StyleProperty> StyleManager::styleProperties() const
{
    QList<StyleProperty> list;
    for (auto it = m_styleProps.cbegin(); it != m_styleProps.cend(); ++it)
        list.append(it.value());
    std::sort(list.begin(), list.end(),
              [](const StyleProperty &a, const StyleProperty &b) {
                  if (a.owner != b.owner)
                      return a.owner < b.owner;
                  return a.name < b.name;
              });
    return list;
}

void StyleManager::setStyleSheet(const QString &text)
{
    m_stylesheet = text;
    m_rules = scanCustomProperties(text);
    if (QApplication *app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
        // A truly empty sheet would tear down Qt's QStyleSheetStyle
        // wrapper (and its caches); the wrapper's re-creation leaves
        // previously-styled widgets registered in the fresh caches
        // without their cleanup connection (QTBUG-149945), and a later
        // repolish walks the stale entry and crashes. A comment-only
        // sheet is functionally empty and keeps the wrapper alive.
        app->setStyleSheet(text.isEmpty()
            ? QStringLiteral("/* empty */")
            : text);
    }
}

// The store accessors: the shared core (entrystore.h) owns the paths,
// the "style.current" setting and the listing.
QString StyleManager::currentStyle() const
{
    return m_store.current();
}

void StyleManager::setCurrentStyle(const QString &name)
{
    m_store.setCurrent(name);
}

QString StyleManager::styleText(const QString &name) const
{
    // The user's file, else the preset, else "" (stale names).
    return m_store.content(name);
}

QString StyleManager::liveText(const QString &name) const
{
    // The sheet as it is applied to the app: the file's text without
    // its provenance comment.
    return StyleManager::StyleContent::live(m_store.content(name));
}

bool StyleManager::isPreset(const QString &name) const
{
    return m_store.isPreset(name);
}

QString StyleManager::originOf(const QString &name) const
{
    return m_store.originOf(name);
}

QList<StyleManager::Info> StyleManager::styles() const
{
    return m_store.list();
}

QString StyleManager::saveCurrentStyle()
{
    // The save (see the class comment): the current sheet into the
    // current style's file — a plain write for a user file, the
    // copy-on-write fork for an edited preset current, a no-op for an
    // unedited preset current and for an unset current (there is no
    // file to write to).
    return m_store.saveCurrent(m_stylesheet);
}

void StyleManager::loadCurrentStyle()
{
    const QString name = m_store.resolveCurrent();
    if (name.isEmpty())
        return; // no current (or none left): the platform style
    // The provenance comment is metadata of the file, not of the live
    // sheet: it is stripped on the way in.
    setStyleSheet(StyleManager::StyleContent::live(m_store.content(name)));
}

bool StyleManager::switchStyle(const QString &name)
{
    if (name == currentStyle())
        return true; // already on it (an unset current is not a target)
    // The target is validated before anything is saved: a refused
    // switch leaves the current (and its save) untouched.
    if (!m_store.available(name))
        return false;
    saveCurrentStyle(); // the previous style is saved first (a no-op
                        // when there is no current; the copy-on-write
                        // fork for an edited preset current)
    // A preset target is used as-is (its own text applied, no copy):
    // the copy appears only when the sheet is edited (the save's
    // copy-on-write).
    setStyleSheet(StyleManager::StyleContent::live(m_store.content(name)));
    setCurrentStyle(name);
    return true;
}

bool StyleManager::saveStyleAs(const QString &name)
{
    // The current sheet under `name`, made current — the current
    // style's provenance carried over (the store's operation).
    return m_store.saveAs(name, m_stylesheet);
}

bool StyleManager::deleteStyle(const QString &name)
{
    // User files only: the presets are read-only.
    QString applyName;
    if (!m_store.remove(name, &applyName))
        return false;
    // The deleted style was the current: the first remaining user file
    // takes over, or the platform style when there are none.
    setStyleSheet(applyName.isEmpty()
        ? QString()
        : StyleManager::StyleContent::live(m_store.content(applyName)));
    return true;
}

bool StyleManager::newStyle(const QString &name)
{
    // A fresh empty style (the empty sheet) under `name`, applied and
    // made current. The current style is saved as-is first (a no-op
    // for an unset current), so the sheet the user was working on is
    // never lost.
    return m_store.create(name, m_stylesheet, [this] {
        setStyleSheet(QString());
    });
}

bool StyleManager::isModified(const QString &name) const
{
    // The "modified" marker: the user file records an origin preset and
    // its text (the origin comment aside) differs from that preset's.
    return m_store.isModified(name);
}

void StyleManager::fillStylesMenu(QMenu *menu)
{
    // The dynamic list: all the styles — the user's files and the
    // presets (a preset is usable as-is, so it is listed like any
    // other; the current may be a preset). The current one is checked,
    // a click switches (the previous is saved by the store).
    menu->clear();
    const QString current = currentStyle();

    // StyleManager is not a QObject: the context object is the menu
    // (a connection dies with its context, so the menu closing drops
    // it; the store itself is the process-wide singleton).
    auto addEntry = [this, menu, current](const QString &name,
                                          const QString &title) {
        QAction *action = menu->addAction(title);
        action->setCheckable(true);
        action->setChecked(name == current);
        QObject::connect(action, &QAction::triggered, menu, [this, name] {
            switchStyle(name);
        });
    };

    // In the listing's title-sorted order.
    for (const Info &info : styles())
        addEntry(info.name, info.title);

    menu->addSeparator();

    // The management of the store (the picker dialog, opened on the
    // Style tab — the store's New/Copy to/Delete/Import/Export
    // operations are its per-pane buttons).
    QAction *manageStyles = menu->addAction("Manage styles");
    QObject::connect(manageStyles, &QAction::triggered, menu, [this] {
        LayoutManager::getInstance()->showLayoutStyleDialog(1);
    });

    // The live-preview sheet editor.
    menu->addSeparator();

    QAction *editStyle = menu->addAction("Edit style sheet");
    QObject::connect(editStyle, &QAction::triggered, menu, [this] {
        LayoutManager::getInstance()->editStyleSheet();
    });
}

bool StyleManager::exportStyle(const QString &path) const
{
    // The export: the current sheet under the picked file, the current
    // style's provenance carried over.
    return m_store.exportTo(path, m_stylesheet);
}

bool StyleManager::importStyle(const QString &path)
{
    // The import: the file's text (its provenance comment kept) applied
    // and written into the user's styles directory under the picked
    // file's name (a name collision overwrites), made current.
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return false;
    const QString text = QString::fromUtf8(f.readAll());
    f.close();
    const QString name = names::sanitize(QFileInfo(path).completeBaseName());
    if (name.isEmpty())
        return false;
    if (!atomicWrite(m_store.userDir() + QLatin1Char('/') + name
                         + QStringLiteral(".qss"),
                     text.toUtf8(), "StyleManager::importStyle"))
        return false;
    setCurrentStyle(name);
    // The file keeps the provenance comment; the live sheet is clean.
    setStyleSheet(StyleManager::StyleContent::live(text));
    return true;
}
