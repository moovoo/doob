#include <QtTest/QtTest>

#include <QApplication>
#include <QFont>
#include <QListWidget>
#include <QPushButton>

#include "selectwidgetdialog.h"

namespace {

// The dialog's options, the way the registry hands them out: the tags
// are the core's (the dialog's group order is
// layout, playback, visualization, library, other).
QList<WidgetRegistry::WidgetOption> options()
{
    QList<WidgetRegistry::WidgetOption> items;
    items.append(WidgetRegistry::WidgetOption{
        QStringLiteral("splitter"), QStringLiteral("Splitter"),
        widgetTags::layout, });
    items.append(WidgetRegistry::WidgetOption{
        QStringLiteral("tabs"), QStringLiteral("Tabs"),
        widgetTags::layout, });
    items.append(WidgetRegistry::WidgetOption{
        QStringLiteral("library"), QStringLiteral("Library"),
        widgetTags::library, });
    return items;
}

// The list's rows: text, selectable?, the widget id (empty for a
// heading).
struct Row
{
    QString text;
    bool selectable;
    QString id;
};

QList<Row> rows(SelectWidgetDialog &dialog)
{
    QListWidget *list = dialog.findChild<QListWidget*>("list");
    QList<Row> out;
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem *item = list->item(i);
        out.append(Row{ item->text(),
                        static_cast<bool>(item->flags() & Qt::ItemIsSelectable),
                        item->data(Qt::UserRole).toString(), });
    }
    return out;
}

class SelectWidgetDialogTest : public QObject
{
    Q_OBJECT

private slots:
    void testNoPreselectionSelectsFirstWidgetRow();
    void testPreselectionIsConfirmedBySelect();
    void testPreselectionIsCurrentWithoutTouchingTheList();
    void testDoubleClickedItemIsAccepted();
    void testDoubleClickedHeadingDoesNotAccept();
    void testCancelRejects();
    void testGroupsAreSortedWithOtherLast();
    void testEmptyGroupsAreHidden();
    void testEmptyTagFallsBackToOther();

private:
    QListWidget* list(SelectWidgetDialog& dialog) const
    {
        return dialog.findChild<QListWidget*>("list");
    }
    QPushButton* button(SelectWidgetDialog& dialog, const QString& name) const
    {
        return dialog.findChild<QPushButton*>(name);
    }
    // The synthetic double-click (the QMetaObject::activate form — this
    // Qt's activate exposes only the void** overloads).
    void doubleClick(SelectWidgetDialog& dialog, int row) const
    {
        QModelIndex idx = list(dialog)->model()->index(row, 0);
        std::array<void*, 2> args{ nullptr, &idx };
        QMetaObject::activate(list(dialog),
                              list(dialog)->metaObject()->indexOfSignal(
                                  "doubleClicked(QModelIndex)"),
                              args.data());
    }
};

void SelectWidgetDialogTest::testNoPreselectionSelectsFirstWidgetRow()
{
    // The first row is a group heading, not a widget: without a
    // pre-selection the first *widget* row is the current one.
    SelectWidgetDialog dialog(options(), QString());
    dialog.show();
    QApplication::processEvents();

    button(dialog, "okButton")->click();

    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
    QCOMPARE(dialog.value(), QString("splitter"));
}

void SelectWidgetDialogTest::testPreselectionIsConfirmedBySelect()
{
    // The pre-selected item is the container's current widget: clicking
    // "Select" without touching the list must confirm it, not the first
    // row of the list.
    SelectWidgetDialog dialog(options(), QStringLiteral("tabs"));
    dialog.show();
    QApplication::processEvents();

    button(dialog, "okButton")->click();

    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
    QCOMPARE(dialog.value(), QString("tabs"));
}

void SelectWidgetDialogTest::testPreselectionIsCurrentWithoutTouchingTheList()
{
    SelectWidgetDialog dialog(options(), QStringLiteral("library"));
    dialog.show();
    QApplication::processEvents();

    QCOMPARE(list(dialog)->currentItem()->data(Qt::UserRole).toString(),
             QString("library"));
    QVERIFY(list(dialog)->currentItem()->isSelected());
}

void SelectWidgetDialogTest::testDoubleClickedItemIsAccepted()
{
    SelectWidgetDialog dialog(options(), QString());
    dialog.show();
    QApplication::processEvents();

    // The rows: "Layout", Splitter, Tabs, "Library", Library.
    list(dialog)->setCurrentRow(4);
    doubleClick(dialog, 4);

    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
    QCOMPARE(dialog.value(), QString("library"));
}

void SelectWidgetDialogTest::testDoubleClickedHeadingDoesNotAccept()
{
    // A heading is not a widget: a double-click on it is a no-op (the
    // dialog stays open).
    SelectWidgetDialog dialog(options(), QString());
    dialog.show();
    QApplication::processEvents();

    doubleClick(dialog, 0);  // the "Layout" heading

    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Rejected));
}

void SelectWidgetDialogTest::testCancelRejects()
{
    SelectWidgetDialog dialog(options(), QString());
    dialog.show();
    QApplication::processEvents();

    button(dialog, "cancelButton")->click();

    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Rejected));
}

void SelectWidgetDialogTest::testGroupsAreSortedWithOtherLast()
{
    // The core's groups in their order (layout, playback,
    // visualization, library), an unknown group (a plugin's own) sorted
    // in before "other", and "other" last.
    QList<WidgetRegistry::WidgetOption> options;
    options.append({ QStringLiteral("meter"), QStringLiteral("Meter"),
                     widgetTags::visualization, });
    options.append({ QStringLiteral("funky"), QStringLiteral("Funky"),
                     QStringLiteral("foo"), });
    options.append({ QStringLiteral("playback"), QStringLiteral("Playback"),
                     widgetTags::playback, });
    options.append({ QStringLiteral("library"), QStringLiteral("Library"),
                     widgetTags::library, });
    options.append({ QStringLiteral("splitter"), QStringLiteral("Splitter"),
                     widgetTags::layout, });

    SelectWidgetDialog dialog(options, QString());
    dialog.show();
    QApplication::processEvents();

    const QList<Row> r = rows(dialog);
    QCOMPARE(r.size(), 10);
    QCOMPARE(r.at(0).text, QStringLiteral("Layout"));
    QCOMPARE(r.at(1).id, QStringLiteral("splitter"));
    QCOMPARE(r.at(2).text, QStringLiteral("Playback"));
    QCOMPARE(r.at(3).id, QStringLiteral("playback"));
    QCOMPARE(r.at(4).text, QStringLiteral("Visualization"));
    QCOMPARE(r.at(5).id, QStringLiteral("meter"));
    QCOMPARE(r.at(6).text, QStringLiteral("Library"));
    QCOMPARE(r.at(7).id, QStringLiteral("library"));
    QCOMPARE(r.at(8).text, QStringLiteral("Foo"));
    QCOMPARE(r.at(9).id, QStringLiteral("funky"));
    // The headings are group markers, not rows to pick, and they are
    // visually set apart (bold, underlined).
    for (int i = 0; i < r.size(); i += 2) {
        QVERIFY(!r.at(i).selectable);
        const QFont font = list(dialog)->item(i)->font();
        QVERIFY(font.bold());
        QVERIFY(font.underline());
    }
    for (int i = 1; i < r.size(); i += 2)
        QVERIFY(r.at(i).selectable);
}

void SelectWidgetDialogTest::testEmptyGroupsAreHidden()
{
    // A group with no widget is not listed: the options cover only
    // "layout" and "library", so the other headings are absent.
    SelectWidgetDialog dialog(options(), QString());
    dialog.show();
    QApplication::processEvents();

    const QList<Row> r = rows(dialog);
    QCOMPARE(r.size(), 5);
    for (const Row &row : r)
        QVERIFY(row.text != QStringLiteral("Playback")
                && row.text != QStringLiteral("Visualization")
                && row.text != QStringLiteral("Other"));
}

void SelectWidgetDialogTest::testEmptyTagFallsBackToOther()
{
    // A widget that names no tag (a plugin that does not pass one)
    // lands in the "other" group.
    QList<WidgetRegistry::WidgetOption> options;
    options.append({ QStringLiteral("splitter"), QStringLiteral("Splitter"),
                     widgetTags::layout, });
    options.append({ QStringLiteral("funky"), QStringLiteral("Funky"),
                     QString(), });

    SelectWidgetDialog dialog(options, QString());
    dialog.show();
    QApplication::processEvents();

    const QList<Row> r = rows(dialog);
    QCOMPARE(r.size(), 4);
    QCOMPARE(r.at(0).text, QStringLiteral("Layout"));
    QCOMPARE(r.at(1).id, QStringLiteral("splitter"));
    QCOMPARE(r.at(2).text, QStringLiteral("Other"));
    QCOMPARE(r.at(3).id, QStringLiteral("funky"));
}

}

QTEST_MAIN(SelectWidgetDialogTest)
#include "selectwidgetdialog_test.moc"
