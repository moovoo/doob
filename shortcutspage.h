#ifndef SHORTCUTSPAGE_H
#define SHORTCUTSPAGE_H

#include <QWidget>

class QTableWidget;

struct ShortcutAction;

// The "Shortcuts" preferences topic: a table of every registered global
// action — one row per action with its current key sequence (editable,
// QKeySequenceEdit) and its scope (global = app-wide, checked). The page
// is a thin view over the ShortcutManager's registrations and the
// SettingsManager's keys ("shortcuts.<id>", "shortcuts.<id>.global"): edits
// go through the SettingsManager (write-through), and its changed()
// signal syncs a row changed from elsewhere (the Settings browser), so
// the two stay in step for free.
class ShortcutsPage : public QWidget
{
    Q_OBJECT
public:
    explicit ShortcutsPage(QWidget *parent = nullptr);

private slots:
    // A row's sequence edit finished (the new sequence, if any, is
    // committed to the SettingsManager).
    void onSequenceEdited(int row);
    // A row's scope checkbox toggled.
    void onGlobalToggled(int row);
    // The selected row (or every row) back to its defaults.
    void onResetClicked();
    void onResetAllClicked();

private:
    void rebuild();
    // One table row for an action (its sequence edit and scope checkbox).
    void addRow(const ShortcutAction &action, const QString &category);
    // The action id a row holds ("" when none).
    QString actionIdAt(int row) const;
    // The row of an action id (-1 when the page shows no row for it).
    int rowForAction(const QString &id) const;
    // Applies the SettingsManager's current values to a row without retriggering
    // its edits.
    void syncRow(int row);

    QTableWidget* m_table;
};

#endif // SHORTCUTSPAGE_H
