#ifndef WINDOWSDIALOG_H
#define WINDOWSDIALOG_H

#include <QDialog>
#include <QHideEvent>
#include <QPair>

#include "windowmanager.h"

class QComboBox;
class QTableWidget;
class QTableWidgetItem;
class UiState;

// The layout's child windows (the View → Layout → Windows... entry).
// In Editor mode the table is a live view over the WindowManager: add,
// remove, rename (double-click the name), convert the kind and
// show/hide apply immediately, like the edit-mode widget changes. The
// editor is shown non-modal by LayoutManager::editWindows (a window
// shown from it must be interactable at once — a modal dialog would
// lock the user out of a window big enough to cover it). In Picker
// mode the table only selects a window (the window toggle widget's
// "Choose window...") and is modal.
class WindowsDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Editor, Picker };

    explicit WindowsDialog(Mode mode, QWidget *parent,
                           const QString &selectedId = QString());

    // Picker mode: the selected window's id (valid when accepted,
    // empty when there is nothing to select).
    QString selectedId() const;

    // The removal confirmation's text (a pure function — the test seam
    // contract carries only the id and the title).
    static QString removeWindowText(const QString &title);

protected:
    // Test seams: the modal sub-dialogs (QDialog is untestable
    // offscreen). promptNewWindow returns (title, kind); an empty title
    // is a cancel.
    virtual QPair<QString, WindowManager::Kind> promptNewWindow();
    virtual bool confirmRemoveWindow(const QString &id, const QString &title);
    void hideEvent(QHideEvent *event) override;

private slots:
    void onAddClicked();
    void onRemoveClicked();
    void onToggleVisibilityClicked();
    void onItemChanged(QTableWidgetItem* item);
    void onWindowsChanged();

private:
    void populate(const QString &selectId);
    // One table row for the window: the name (editable in Editor mode,
    // the id in its UserRole), the type (a combo in Editor mode, plain
    // text in Picker mode) and the visible checkbox.
    void addRow(const WindowManager::Info &info);
    QComboBox* typeCombo(const WindowManager::Info &info);
    QTableWidgetItem* kindItem(WindowManager::Kind kind);
    QString selectedWindowId() const;
    int rowOf(const QString &id) const;

    Mode m_mode;
    QTableWidget* m_table;
    UiState* m_state = nullptr;
    bool m_populating = false;
};

#endif // WINDOWSDIALOG_H
