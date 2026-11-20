#ifndef UISTATE_H
#define UISTATE_H

#include <QRect>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

class QHeaderView;
class QWidget;

// The generic UI state of a dialog (or any widget that should remember
// how the user arranged it): the window's size and position, the table
// column widths, and whatever else the widget declares (a filter text,
// a selected tab — free fields).
//
// The state lives in the central SettingsManager as ONE setting per
// state scope (a key like "dialogs.url_import"), a QVariantMap of
// fields, registered on demand by the first UiState that names it
// (later ones just share it). So the state shows up in the settings
// browser like any other setting, is write-through, and survives
// restarts. Dialog state is a user preference, not layout: it does not
// travel with a layout document (the layout owns only what is on the
// main window's screen).
//
// The standard fields have a fixed shape:
//   "geometry" — {x, y, width, height, maximized}: the same shape as
//       the layout document's "window" key. A maximized window stores
//       its normal geometry plus the flag. The restore sanitizes the
//       saved state (restoredGeometry): a missing, degenerate or
//       oversized size (one no screen can hold) keeps the caller's
//       default, a position off every screen is re-centered on the
//       primary screen (the size is kept).
//   "columns"  — {stable column name: width}: one entry per logical
//       column the caller names. A stale name (a column that no longer
//       exists) is ignored on restore, a missing one keeps the view's
//       default size.
//
// Plugging a dialog in takes three lines (in the ctor):
//     m_state = new UiState(QStringLiteral("dialogs.foo"), this);
//     m_state->track(this);           // saves the geometry on every hide
//     m_state->restoreGeometry(this);
// A table inside the dialog adds its columns explicitly — the names
// are the model's stable column keys, one per logical column, in
// order, and the argument is the view's horizontal header:
//     m_state->restoreColumns(view->horizontalHeader(), names);  // ctor
//     m_state->saveColumns(view->horizontalHeader(), names);     // close
class UiState : public QObject
{
    Q_OBJECT
public:
    explicit UiState(QString key, QObject *parent = nullptr);

    // The whole state (an empty map when the key is unset).
    QVariantMap load() const;
    // The whole state, write-through.
    void save(const QVariantMap &state) const;
    // One free field (the widget declares its own names).
    QVariant field(const QString &name) const;
    void set(const QString &name, const QVariant &value) const;

    // The standard fields (see the class comment for their shapes). The
    // column functions take the view's horizontal header (a table or
    // tree view's `horizontalHeader()` / `header()`): the names must
    // number its sections, one per logical column, in order.
    void saveGeometry(const QWidget *window) const;
    void restoreGeometry(QWidget *window);
    void saveColumns(QHeaderView *header, const QStringList &names) const;
    void restoreColumns(QHeaderView *header, const QStringList &names)
        const;

    // The dialog's plug: watches the window and saves the geometry on
    // every hide (a close is a hide — the state of the last session is
    // what a fresh instance restores), applying a stored maximized flag
    // on the first show (a maximized window cannot be maximized before
    // it is shown).
    void track(QWidget *window);

    // The window's position may land off every screen (a monitor was
    // unplugged, the resolution changed): true when it intersects any
    // screen's geometry.
    static bool onAnyScreen(const QRect &geo);

    // A saved size may outgrow every screen (the resolution was
    // lowered, the file is corrupt): true when some screen is at least
    // this big (a window may cover a screen whole, so the full screen
    // geometry is the bound).
    static bool fitsSomeScreen(const QSize &size);

    // The restore rule for a saved window geometry (the "geometry" /
    // "window" map, the same shape in the dialogs' state and the
    // layout document): a missing or degenerate size keeps the
    // caller's default (an invalid rect is returned), a size no screen
    // can hold keeps it too (Qt applies a setGeometry as-is — nothing
    // shrinks a window to the screen), and a position off every screen
    // is re-centered on the primary screen (the size is kept).
    static QRect restoredGeometry(const QVariantMap &geo);

    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QString m_key;
    QWidget* m_window = nullptr;
    // The stored maximized flag, applied on the tracked window's first
    // show (see track).
    bool m_pendingMaximized = false;
};

#endif // UISTATE_H
