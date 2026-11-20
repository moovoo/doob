#ifndef CONFIGPAGE_H
#define CONFIGPAGE_H

#include <QMetaObject>
#include <QWidget>

class QCheckBox;
class QHideEvent;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class SettingsManager;
class UiState;

// The "Settings" topic of the preferences dialog: an about:config-style
// browser over the SettingsManager. The registered settings are listed in
// a tree grouped by owner (the core under "Core", a plugin under its
// title), and the stored keys of unregistered settings (a removed
// plugin's leftovers) in an "Orphaned" group. A search box filters by
// key/title/description, a "Modified only" checkbox hides settings at
// their default, double-clicking a setting row opens the type-specific
// value editor, and "Reset to default" drops the selected setting's
// stored value. The tree is always fully expanded (on construction and
// after every rebuild, so a search keystroke re-expands the filtered
// groups).
class ConfigPage : public QWidget
{
    Q_OBJECT
public:
    explicit ConfigPage(QWidget *parent = nullptr);

protected:
    void hideEvent(QHideEvent *event) override;

private slots:
    void onRowDoubleClicked(QTreeWidgetItem *item, int column);
    void onResetClicked();
    // (Re)builds the tree from the SettingsManager. Also its change-slot:
    // every set/reset write refreshes the view.
    void rebuild();

private:
    enum RowKind { SettingRow = 0, OrphanedRow = 1 };

    static QString displayValue(const QVariant &value);
    static QString typeName(const QVariant &value);
    static bool matches(const QString &search, const QString &key,
                        const QString &title, const QString &description);
    void addRow(QTreeWidgetItem *group, const QString &key,
                const QVariant &value, const QVariant &defaultValue,
                int kind);
    // True for an editable setting row (groups and orphaned rows are
    // read-only; a group's unset kind data reads back as SettingRow, so
    // the parent check is what admits rows only).
    bool isSettingRow(const QTreeWidgetItem *item) const;
    void addSettingRows(SettingsManager *settings, const QString &search);
    void addOrphanedRows(SettingsManager *settings, const QString &search);
    QTreeWidgetItem* rowByKey(const QString &key) const;

    QLineEdit* m_search;
    QCheckBox* m_modifiedOnly;
    QCheckBox* m_showOrphaned;
    QTreeWidget* m_tree;
    QPushButton* m_reset;
    SettingsManager* m_settings = nullptr;
    QMetaObject::Connection m_changedConnection;
    // The tree's column widths, in the preferences dialog's state scope
    // (the page is part of the dialog's arrangement — the dialog's own
    // geometry and the page's columns share the "dialogs.preferences"
    // map, each writing its own field).
    UiState* m_state = nullptr;
};

#endif // CONFIGPAGE_H
