#ifndef ROWCLIPBOARD_H
#define ROWCLIPBOARD_H

#include <QList>

#include <functional>

class QAbstractItemView;
class QObject;

// The internal clipboard for playlist rows: the most recently copied
// batch of item ids — one entry per copied row, in row order, duplicates
// preserved (a playlist may hold the same item in several rows, and a row
// copy keeps every copy). A process-wide singleton: copy on one playlist
// view, paste into any other. Deliberately not the OS clipboard: a row is
// a doob-internal item reference and has no portable representation.
class RowClipboard
{
public:
    static RowClipboard* getInstance();
    static void reset(); // for tests

    void copy(const QList<quint64> &ids) { m_ids = ids; }
    QList<quint64> items() const { return m_ids; }
    bool isEmpty() const { return m_ids.isEmpty(); }
    void clear() { m_ids.clear(); }

private:
    RowClipboard() = default;

    QList<quint64> m_ids;
    inline static RowClipboard* m_instance = nullptr;
};

inline RowClipboard* RowClipboard::getInstance()
{
    if (m_instance == nullptr)
        m_instance = new RowClipboard;
    return m_instance;
}

inline void RowClipboard::reset()
{
    delete m_instance;
    m_instance = nullptr;
}

// The per-view row shortcuts the media views install: Delete removes the
// selection, and — when `onCopy` is given — Ctrl+C copies the selection
// and Ctrl+V pastes the clipboard's rows (the paste lands after the view's
// current row, or at the end of a view with no current row — one row per
// copied row, duplicates preserved). The WidgetShortcut context: the
// shortcuts fire only while the view has focus (the default for a parented
// QShortcut is WindowShortcut, which additionally requires the view's
// top-level to be the *active* window; the Wayland compositor owns
// activation and does not reliably report it, so the focus-based context
// is the portable one). `view` owns the shortcuts; `context` keeps the
// callbacks alive.
void installRowShortcuts(QAbstractItemView *view, QObject *context,
                         const std::function<void()> &onDelete,
                         const std::function<void()> &onCopy = {},
                         const std::function<void(const QList<quint64> &ids, int index)> &onPaste = {});

#endif // ROWCLIPBOARD_H
