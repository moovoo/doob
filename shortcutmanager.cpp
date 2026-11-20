#include "shortcutmanager.h"

#include "settingsmanager.h"

#include <QKeySequence>
#include <QShortcut>
#include <QWidget>

#include <QDebug>

ShortcutManager* ShortcutManager::getInstance()
{
    if (m_instance == nullptr)
        m_instance = new ShortcutManager;
    return m_instance;
}

void ShortcutManager::reset()
{
    delete m_instance;
    m_instance = nullptr;
}

ShortcutManager::~ShortcutManager()
{
    for (QShortcut *shortcut : std::as_const(m_shortcuts))
        delete shortcut;
}

QString ShortcutManager::actionIdForKey(const QString &key)
{
    const QString prefix = QStringLiteral("shortcuts.");
    if (!key.startsWith(prefix))
        return {};
    QString id = key.mid(prefix.size());
    // The ".global" suffix names the scope key; the bare id the
    // sequence key.
    if (id.endsWith(QStringLiteral(".global")))
        id.chop(QStringLiteral(".global").size());
    return id;
}

ShortcutManager::ShortcutManager()
    : QObject(nullptr)
{
    // The user's choices (the preferences page writes them) rebind the
    // live shortcuts; the page needs no extra plumbing.
    connect(SettingsManager::getInstance(), &SettingsManager::changed,
            this, [this](const QString &key, const QVariant &) {
        const QString id = actionIdForKey(key);
        if (id.isEmpty())
            return;
        const int i = actionIndex(id);
        if (i >= 0)
            applyAction(i);
    });
}

int ShortcutManager::actionIndex(const QString &id) const
{
    for (int i = 0; i < m_actions.size(); ++i) {
        if (m_actions.at(i).id == id)
            return i;
    }
    return -1;
}

bool ShortcutManager::registerAction(const ShortcutAction &action)
{
    if (action.id.isEmpty() || actionIndex(action.id) >= 0) {
        qWarning("ShortcutManager::registerAction: id %s is taken or empty",
                 qPrintable(action.id));
        return false;
    }

    SettingsManager* settings = SettingsManager::getInstance();
    // The two keys per action: the sequence (a QKeySequence string, empty
    // disables the action) and the scope (global = app-wide). The defaults
    // double as the types, like every registered setting.
    const bool keysFree =
        settings->registerSetting(
            { sequenceKey(action.id),
              action.title,
              QStringLiteral("The key sequence of the \"%1\" action "
                             "(empty disables it).")
                  .arg(action.title),
              action.defaultSequence.toString(),
              QString(), })
        && settings->registerSetting(
            { globalKey(action.id),
              action.title + QStringLiteral(" (global)"),
              QStringLiteral("Whether the \"%1\" shortcut is app-wide "
                             "(global) or scoped to the main window.")
                  .arg(action.title),
              action.global,
              QString(), });
    if (!keysFree)
        return false;

    m_actions.append(action);
    const int i = actionIndex(action.id);
    applyAction(i);
    emit actionsChanged();
    return true;
}

void ShortcutManager::unregisterAction(const QString &id)
{
    const int i = actionIndex(id);
    if (i < 0)
        return;

    delete m_shortcuts.take(id);
    m_actions.removeAt(i);
    // Drops the metadata, not the stored values: a re-added action (a
    // plugin reloaded) finds its settings intact.
    SettingsManager::getInstance()->unregisterSetting(sequenceKey(id));
    SettingsManager::getInstance()->unregisterSetting(globalKey(id));
    emit actionsChanged();
}

QKeySequence ShortcutManager::sequence(const QString &id) const
{
    const QVariant value =
        SettingsManager::getInstance()->value(sequenceKey(id));
    return { value.toString() };
}

bool ShortcutManager::isGlobal(const QString &id) const
{
    return SettingsManager::getInstance()->value(globalKey(id)).toBool();
}

void ShortcutManager::trigger(const QString &id)
{
    const int i = actionIndex(id);
    if (i >= 0 && m_actions.at(i).invoke)
        m_actions.at(i).invoke();
}

QShortcut* ShortcutManager::shortcut(const QString &id) const
{
    return m_shortcuts.value(id, nullptr);
}

void ShortcutManager::attach(QWidget *window)
{
    if (m_window == window)
        return;

    // The shortcuts are children of the attached window: a re-attach
    // while the old window is alive deletes them, a re-attach after it
    // is gone only clears the map (they died with it, the entries
    // dangle).
    if (m_window.isNull())
        m_shortcuts.clear();
    else
        for (QShortcut *shortcut : std::as_const(m_shortcuts))
            delete shortcut;
    m_shortcuts.clear();
    m_window = window;
    for (int i = 0; i < m_actions.size(); ++i)
        applyAction(i);
}

void ShortcutManager::applyAction(int index)
{
    const ShortcutAction &action = m_actions.at(index);

    // A sequence shared by two actions is ambiguous (Qt fires one of
    // them, the later of the same context): warn, keep both.
    const QKeySequence seq = sequence(action.id);
    if (!seq.isEmpty()) {
        for (const ShortcutAction &other : m_actions) {
            if (other.id != action.id && sequence(other.id) == seq) {
                qWarning("ShortcutManager: %s and %s share %s",
                         qPrintable(action.id), qPrintable(other.id),
                         qPrintable(seq.toString()));
                break;
            }
        }
    }

    if (m_window == nullptr)
        return;

    // QShortcut has no key-sequence setter: a changed sequence (or
    // context) gets a fresh shortcut.
    delete m_shortcuts.take(action.id);
    QShortcut *shortcut = new QShortcut(seq, m_window);
    shortcut->setContext(isGlobal(action.id) ? Qt::ApplicationShortcut
                                             : Qt::WindowShortcut);
    m_shortcuts.insert(action.id, shortcut);
    connect(shortcut, &QShortcut::activated, this,
            [this, id = action.id] { trigger(id); });
}
