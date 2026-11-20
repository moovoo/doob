#include "rowclipboard.h"

#include <QAbstractItemView>
#include <QKeySequence>
#include <QModelIndex>
#include <QShortcut>

void installRowShortcuts(QAbstractItemView *view, QObject *context,
                         const std::function<void()> &onDelete,
                         const std::function<void()> &onCopy,
                         const std::function<void(const QList<quint64> &ids, int index)> &onPaste)
{
    QShortcut *del = new QShortcut(QKeySequence(Qt::Key_Delete), view, // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
                                   nullptr, nullptr, Qt::WidgetShortcut);
    QObject::connect(del, &QShortcut::activated, context, onDelete);
    if (!onCopy)
        return; // a delete-only view (the library)

    QShortcut *copy = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_C), view, // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
                                    nullptr, nullptr, Qt::WidgetShortcut);
    QObject::connect(copy, &QShortcut::activated, context, onCopy);

    QShortcut *paste = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_V), view, // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
                                     nullptr, nullptr, Qt::WidgetShortcut);
    QObject::connect(paste, &QShortcut::activated, context, [view, onPaste] {
        const QList<quint64> ids = RowClipboard::getInstance()->items();
        if (ids.isEmpty())
            return;
        const QModelIndex current = view->currentIndex();
        const int end = view->model() != nullptr ? view->model()->rowCount() : 0;
        const int index = current.isValid() ? current.row() + 1 : end;
        onPaste(ids, index);
    });
}
