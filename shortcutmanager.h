#ifndef SHORTCUTMANAGER_H
#define SHORTCUTMANAGER_H

#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QPointer>
#include <QVariant>
#include <QWidget>

#include <functional>

class QShortcut;

// One registered global action: the id it is stored under (the SettingsManager's
// keys are "shortcuts.<id>" and "shortcuts.<id>.global"), the human-
// readable title and category the preferences page shows, the default key
// sequence (empty = disabled by default) and the default scope (global =
// app-wide, non-global = the attached window only). invoke is the action
// itself: the manager owns the binding, the owner owns the action.
struct ShortcutAction
{
    QString id;
    QString title;
    QString category;
    QKeySequence defaultSequence;
    bool global = true;
    std::function<void()> invoke;
};

// The registry of global keyboard actions. Components register their
// actions from their singletons' ctors (like their settings), the
// ShortcutPage (the "Shortcuts" preferences topic) is a thin view over
// the registrations, and the SettingsManager's two keys per action persist the
// user's choices (write-through, stored values survive unregistration —
// a removed action's keys stay, and a re-added action finds them).
//
// A registered action gets one QShortcut, (re)created when the manager is
// attached to a window (MainWindow's ctor): a global action is app-wide
// (Qt::ApplicationShortcut — every window, dialog and focused text field
// included), a non-global one is scoped to the window
// (Qt::WindowShortcut). The SettingsManager's changed() signal rebinds live, so
// the preferences page needs no extra plumbing.
class ShortcutManager : public QObject
{
    Q_OBJECT
public:
    static ShortcutManager* getInstance();
    static void reset(); // for tests: drop the instance

    // The SettingsManager's keys of an action.
    static QString sequenceKey(const QString &id)
    {
        return QStringLiteral("shortcuts.") + id;
    }
    static QString globalKey(const QString &id)
    {
        return QStringLiteral("shortcuts.") + id + QStringLiteral(".global");
    }
    // The action id a settings key names (the inverse of
    // sequenceKey/globalKey): the key without the "shortcuts." prefix
    // and the ".global" scope suffix — empty when the key is not one
    // of the manager's.
    static QString actionIdForKey(const QString &key);

    // Idempotent in the other registries' sense: a taken id (or an empty
    // id) is refused (false, a qWarning). Registers the action's two
    // settings with the SettingsManager (defaults = the action's defaults) and,
    // when attached, creates its shortcut.
    bool registerAction(const ShortcutAction &action);
    void unregisterAction(const QString &id);
    // All registered actions, registration order.
    QList<ShortcutAction> actions() const { return m_actions; }

    // The effective values (stored -> default).
    QKeySequence sequence(const QString &id) const;
    bool isGlobal(const QString &id) const;

    // Runs the action (the shortcut's target).
    void trigger(const QString &id);

    // Binds the actions to the window: (re)creates the shortcuts, parented
    // to it. Re-attaching (a new main window) re-creates them.
    void attach(QWidget *window);
    QWidget* window() const { return m_window; }

    // Test seam: the live shortcut of an action (nullptr when unattached
    // or unregistered).
    QShortcut* shortcut(const QString &id) const;

signals:
    void actionsChanged();

private:
    explicit ShortcutManager();
    ~ShortcutManager() override;

    int actionIndex(const QString &id) const;
    // Creates (attached) or refreshes the action's shortcut from the
    // effective SettingsManager values, warning on a sequence clash with another
    // action.
    void applyAction(int index);

    QList<ShortcutAction> m_actions;
    QHash<QString, QShortcut*> m_shortcuts;
    QPointer<QWidget> m_window;
    inline static ShortcutManager* m_instance = nullptr;
};

#endif // SHORTCUTMANAGER_H
