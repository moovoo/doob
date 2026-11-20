#ifndef CHECKABLELISTDIALOG_H
#define CHECKABLELISTDIALOG_H

#include <QDialog>
#include <QHash>
#include <QList>
#include <QPair>
#include <QStringList>

class QListWidget;
class QListWidgetItem;
class QPushButton;

// Edit-mode dialog for a list of checkable items where the list order is
// the display order and the check state is the visibility (at least one
// item must stay visible). The caller supplies the window title, the hint
// line and the (display label, config key) catalog of every known item —
// e.g. the playback control buttons or the peak meter channels.
class CheckableListDialog : public QDialog
{
    Q_OBJECT
public:
    // The config key of a layout stretcher (the stretchers are not part
    // of the catalog: they are extra list rows, at most one per gap).
    inline static const QString stretcherKey = QStringLiteral("stretch");

    // `catalog` = (display label, config key) pair of every known item;
    // `order` = currently visible keys in display order, the rest are hidden.
    // With `stretchers`, an "Insert stretcher" and a "Remove" button are
    // offered: a stretcher row is a layout stretch, movable like any
    // item and capped at the catalog size + 1 (one per gap). With a
    // non-empty `stateKey`, the dialog's size and position are tracked
    // and restored through the standard UiState wiring under that key
    // (empty = no persistence).
    explicit CheckableListDialog(const QString& title,
                                 const QString& hint,
                                 const QList<QPair<QString, QString>>& catalog,
                                 const QStringList& order,
                                 QWidget* parent = nullptr,
                                 bool stretchers = false,
                                 const QString& stateKey = {});

    // The tri-state variant: the check state is the row's state —
    // unchecked = never, partially checked = when-non-empty, checked =
    // always — and the list order is the display sequence of every key
    // (the state is orthogonal to the order). `states` carries each
    // key's check state (a missing key is partially checked, the
    // default). No stretchers in this mode. `stateKey` as in the
    // binary constructor.
    explicit CheckableListDialog(const QString& title,
                                 const QString& hint,
                                 const QList<QPair<QString, QString>>& catalog,
                                 const QStringList& order,
                                 const QHash<QString, Qt::CheckState>& states,
                                 QWidget* parent = nullptr,
                                 const QString& stateKey = {});

    // Config keys of the checked items, in list order.
    QStringList selectedOrder() const;
    // The full display sequence: the checked items and the stretchers,
    // in list order (a dialog without stretchers is the checked items);
    // in the tri-state mode, every row in list order (the state is
    // orthogonal to the order).
    QStringList sequence() const;
    // The rows' check states (key -> state); empty outside the
    // tri-state mode.
    QHash<QString, Qt::CheckState> states() const;

private:
    // The common work: the two public constructors delegate here after
    // they have set the mode.
    CheckableListDialog(const QString& title,
                        const QString& hint,
                        const QList<QPair<QString, QString>>& catalog,
                        const QStringList& order,
                        QWidget* parent,
                        const QString& stateKey,
                        bool stretchers,
                        const QHash<QString, Qt::CheckState>& states,
                        bool triState);

private slots:
    void onItemChanged(QListWidgetItem* item);
    void onSelectionChanged();
    void onMoveUpClicked();
    void onMoveDownClicked();
    void onInsertStretcherClicked();
    void onRemoveClicked();
    void onResetClicked();

private:
    void populate(const QStringList& order);
    void updateMoveButtons();
    void updateStretcherButtons();
    int checkedCount() const;
    int visibleCount() const;
    int stretcherCount() const;
    bool isStretcher(QListWidgetItem* item) const;

    bool m_triState;
    // The tri-state rows' check states (the binary mode keeps none).
    QHash<QString, Qt::CheckState> m_states;
    QList<QPair<QString, QString>> m_catalog;
    QListWidget* m_list;
    QPushButton* m_moveUp;
    QPushButton* m_moveDown;
    QPushButton* m_insertStretcher;
    QPushButton* m_remove;
    QPushButton* m_reset;
    bool m_populating;
};

#endif // CHECKABLELISTDIALOG_H
