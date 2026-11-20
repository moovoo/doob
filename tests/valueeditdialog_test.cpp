#include <QtTest>

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QMetaType>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>

#include "valueeditdialog.h"

namespace {

class ValueEditDialogTest : public QObject
{
    Q_OBJECT

private slots:
    void editorMatchesTheValueType();
    void stringRoundTrip();
    void boolRoundTrip();
    void intRoundTrip();
    void listRoundTripDropsEmptyLines();
    void mapRoundTrip();
    void mapRejectsInvalidJson();
    void cancelLeavesResultInvalid();

private:
    static void clickOk(ValueEditDialog &dialog);
};

void ValueEditDialogTest::clickOk(ValueEditDialog &dialog)
{
    QPushButton* ok = dialog.findChild<QPushButton*>("okButton");
    QVERIFY(ok != nullptr);
    QTest::mouseClick(ok, Qt::LeftButton);
}

void ValueEditDialogTest::editorMatchesTheValueType()
{
    {
        ValueEditDialog dialog("t", true);
        QVERIFY(dialog.findChild<QCheckBox*>("valueEditor") != nullptr);
    }
    {
        ValueEditDialog dialog("t", 5);
        QVERIFY(dialog.findChild<QSpinBox*>("valueEditor") != nullptr);
    }
    {
        ValueEditDialog dialog("t", 1.5);
        QVERIFY(dialog.findChild<QDoubleSpinBox*>("valueEditor") != nullptr);
    }
    {
        ValueEditDialog dialog("t", "s");
        QVERIFY(dialog.findChild<QLineEdit*>("valueEditor") != nullptr);
    }
    {
        ValueEditDialog dialog("t", QStringList() << "a");
        QVERIFY(dialog.findChild<QPlainTextEdit*>("valueEditor") != nullptr);
    }
    {
        ValueEditDialog dialog("t", QVariantMap{ { "k", "v" } });
        QVERIFY(dialog.findChild<QPlainTextEdit*>("valueEditor") != nullptr);
    }
}

void ValueEditDialogTest::stringRoundTrip()
{
    ValueEditDialog dialog("t", "old");
    QLineEdit* editor = dialog.findChild<QLineEdit*>("valueEditor");
    QVERIFY(editor != nullptr);
    QCOMPARE(editor->text(), QString("old"));

    editor->setText("new");
    clickOk(dialog);

    QVERIFY(dialog.editedValue().isValid());
    QCOMPARE(dialog.editedValue().toString(), QString("new"));
}

void ValueEditDialogTest::boolRoundTrip()
{
    ValueEditDialog dialog("t", false);
    QCheckBox* editor = dialog.findChild<QCheckBox*>("valueEditor");
    QVERIFY(editor != nullptr);
    QVERIFY(!editor->isChecked());

    editor->click();  // a synthetic press+release
    clickOk(dialog);

    QCOMPARE(dialog.editedValue().toBool(), true);
}

void ValueEditDialogTest::intRoundTrip()
{
    ValueEditDialog dialog("t", 1);
    QSpinBox* editor = dialog.findChild<QSpinBox*>("valueEditor");
    QVERIFY(editor != nullptr);
    QCOMPARE(editor->value(), 1);

    editor->setValue(42);
    clickOk(dialog);

    QCOMPARE(dialog.editedValue().toInt(), 42);
}

void ValueEditDialogTest::listRoundTripDropsEmptyLines()
{
    ValueEditDialog dialog("t", QStringList() << "a" << "b");
    QPlainTextEdit* editor = dialog.findChild<QPlainTextEdit*>("valueEditor");
    QVERIFY(editor != nullptr);
    QCOMPARE(editor->toPlainText(), QString("a\nb"));

    editor->setPlainText("x\n\n  y  \n");
    clickOk(dialog);

    QCOMPARE(dialog.editedValue().toStringList(), QStringList() << "x" << "y");
}

void ValueEditDialogTest::mapRoundTrip()
{
    ValueEditDialog dialog("t", QVariantMap{ { "k", "v" } });
    QPlainTextEdit* editor = dialog.findChild<QPlainTextEdit*>("valueEditor");
    QVERIFY(editor != nullptr);

    editor->setPlainText(R"({"k2": "v2"})");
    clickOk(dialog);

    QCOMPARE(dialog.editedValue().toMap().value("k2").toString(), QString("v2"));
}

void ValueEditDialogTest::mapRejectsInvalidJson()
{
    ValueEditDialog dialog("t", QVariantMap());
    QPlainTextEdit* editor = dialog.findChild<QPlainTextEdit*>("valueEditor");
    QVERIFY(editor != nullptr);

    editor->setPlainText("not json");
    clickOk(dialog);

    // Not accepted: the dialog kept its old value and shows a hint.
    QVERIFY(!dialog.editedValue().isValid());
    QCOMPARE(dialog.findChild<QLabel*>("hintLabel")->text(),
             QString("Not a valid JSON object."));

    editor->setPlainText(R"({"ok": 1})");
    clickOk(dialog);

    QVERIFY(dialog.editedValue().isValid());
    QCOMPARE(dialog.editedValue().toMap().value("ok").toInt(), 1);
}

void ValueEditDialogTest::cancelLeavesResultInvalid()
{
    ValueEditDialog dialog("t", "s");
    QPushButton* cancel = dialog.findChild<QPushButton*>("cancelButton");
    QVERIFY(cancel != nullptr);
    QTest::mouseClick(cancel, Qt::LeftButton);

    QVERIFY(!dialog.editedValue().isValid());
}

} // namespace

QTEST_MAIN(ValueEditDialogTest)
#include "valueeditdialog_test.moc"
