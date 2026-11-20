#include <QtTest/QtTest>

#include <QApplication>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QPushButton>

#include "peakmeterwidget.h"
#include "stylesheetdialog.h"
#include "stylemanager.h"

namespace {

class StyleSheetDialogTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void testEditorOpensWithCurrentSheet();
    void testAutoPreviewEnabledByDefault();
    void testPreviewAppliesEditorText();
    void testAutoPreviewAppliesAfterTyping();
    void testAutoPreviewToggleEnablesButton();
    void testOkCommitsEditorText();
    void testOkAppliesTextTypedAfterPreview();
    void testCancelRevertsPreviousSheet();
    void testRejectRevertsPreviousSheet();
    void testFocusSelectorLandsCursor();
    void testRevertToRestoresPreActionSheet();
    void testHelpButtonExists();
    void testHelpHtmlListsRegisteredProperties();
    void testHelpHtmlEmptyRegistry();

private:
    QPlainTextEdit* editor(StyleSheetDialog& dialog) const;
    QCheckBox* autoPreview(StyleSheetDialog& dialog) const;
    QPushButton* button(StyleSheetDialog& dialog, const QString& name) const;
};

void StyleSheetDialogTest::init()
{
    StyleManager::reset();
    qApp->setStyleSheet(QString());
}

void StyleSheetDialogTest::cleanup()
{
    StyleManager::reset();
    qApp->setStyleSheet(QString());
}

QPlainTextEdit* StyleSheetDialogTest::editor(StyleSheetDialog& dialog) const
{
    return dialog.findChild<QPlainTextEdit*>("styleSheet");
}

QCheckBox* StyleSheetDialogTest::autoPreview(StyleSheetDialog& dialog) const
{
    return dialog.findChild<QCheckBox*>("autoPreview");
}

QPushButton* StyleSheetDialogTest::button(StyleSheetDialog& dialog, const QString& name) const
{
    return dialog.findChild<QPushButton*>(name);
}

void StyleSheetDialogTest::testEditorOpensWithCurrentSheet()
{
    StyleManager::getInstance()->setStyleSheet("QLabel { color: red; }");

    StyleSheetDialog dialog;

    QCOMPARE(editor(dialog)->toPlainText(), QString("QLabel { color: red; }"));
}

void StyleSheetDialogTest::testAutoPreviewEnabledByDefault()
{
    StyleSheetDialog dialog;

    QVERIFY(autoPreview(dialog)->isChecked());
    QVERIFY(!button(dialog, "previewButton")->isEnabled());
}

void StyleSheetDialogTest::testPreviewAppliesEditorText()
{
    StyleManager::getInstance()->setStyleSheet("A");
    StyleSheetDialog dialog;
    autoPreview(dialog)->setChecked(false); // manual mode
    editor(dialog)->setPlainText("QPushButton { background: green; }");

    button(dialog, "previewButton")->click();

    QCOMPARE(StyleManager::getInstance()->stylesheet(), QString("QPushButton { background: green; }"));
    QCOMPARE(qApp->styleSheet(), QString("QPushButton { background: green; }"));
}

void StyleSheetDialogTest::testAutoPreviewAppliesAfterTyping()
{
    StyleSheetDialog dialog;
    editor(dialog)->setPlainText("QLabel { color: blue; }");

    // The auto-preview timer has to fire; spin the event loop.
    QTRY_COMPARE_WITH_TIMEOUT(qApp->styleSheet(), QString("QLabel { color: blue; }"), 1000);
}

void StyleSheetDialogTest::testAutoPreviewToggleEnablesButton()
{
    StyleSheetDialog dialog;
    QCheckBox* check = autoPreview(dialog);
    QPushButton* preview = button(dialog, "previewButton");
    QVERIFY(!preview->isEnabled());

    check->setChecked(false);
    QVERIFY(preview->isEnabled());

    check->setChecked(true);
    QVERIFY(!preview->isEnabled());
}

void StyleSheetDialogTest::testOkCommitsEditorText()
{
    StyleManager::getInstance()->setStyleSheet("A");
    StyleSheetDialog dialog;
    editor(dialog)->setPlainText("B");

    button(dialog, "okButton")->click();

    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
    QCOMPARE(StyleManager::getInstance()->stylesheet(), QString("B"));
}

void StyleSheetDialogTest::testOkAppliesTextTypedAfterPreview()
{
    StyleManager::getInstance()->setStyleSheet("A");
    StyleSheetDialog dialog;
    autoPreview(dialog)->setChecked(false); // manual mode, so typing does not auto-apply
    editor(dialog)->setPlainText("B");
    button(dialog, "previewButton")->click();
    editor(dialog)->setPlainText("C");

    button(dialog, "okButton")->click();

    // OK applies the editor's final text, not the last previewed one.
    QCOMPARE(StyleManager::getInstance()->stylesheet(), QString("C"));
}

void StyleSheetDialogTest::testCancelRevertsPreviousSheet()
{
    StyleManager::getInstance()->setStyleSheet("A");
    StyleSheetDialog dialog;
    autoPreview(dialog)->setChecked(false);
    editor(dialog)->setPlainText("B");
    button(dialog, "previewButton")->click();
    QCOMPARE(StyleManager::getInstance()->stylesheet(), QString("B")); // the preview applied

    button(dialog, "cancelButton")->click();

    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Rejected));
    QCOMPARE(StyleManager::getInstance()->stylesheet(), QString("A"));
    QCOMPARE(qApp->styleSheet(), QString("A"));
}

void StyleSheetDialogTest::testRejectRevertsPreviousSheet()
{
    // Esc and the window close button go through reject(): they must
    // revert the previewed sheet exactly like Cancel does.
    StyleManager::getInstance()->setStyleSheet("A");
    StyleSheetDialog dialog;
    autoPreview(dialog)->setChecked(false);
    editor(dialog)->setPlainText("B");
    button(dialog, "previewButton")->click();
    QCOMPARE(StyleManager::getInstance()->stylesheet(), QString("B")); // the preview applied

    dialog.reject();

    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Rejected));
    QCOMPARE(StyleManager::getInstance()->stylesheet(), QString("A"));
    QCOMPARE(qApp->styleSheet(), QString("A"));
}

// A focus selector lands the cursor in that rule (the "Style this
// widget..." action seeds it with "#name"); a selector the sheet does
// not contain leaves the cursor at the start.
void StyleSheetDialogTest::testFocusSelectorLandsCursor()
{
    // "QWidget { color: red; }\n" is 24 characters: "#meter-1" starts
    // at 24.
    StyleManager::getInstance()->setStyleSheet(
        QStringLiteral("QWidget { color: red; }\n#meter-1 {\n}"));

    StyleSheetDialog dialog(nullptr, "#meter-1");
    QCOMPARE(editor(dialog)->toPlainText(),
             QStringLiteral("QWidget { color: red; }\n#meter-1 {\n}"));
    QCOMPARE(editor(dialog)->textCursor().position(), 24);

    // A selector the sheet does not carry: the cursor stays at the
    // start.
    StyleSheetDialog noMatch(nullptr, "#other");
    QCOMPARE(editor(noMatch)->textCursor().position(), 0);
}

// An explicit revert target is what Cancel restores — the sheet from
// before the "Style this widget..." action appended its rule, so a
// pure cancel leaves no empty rule behind.
void StyleSheetDialogTest::testRevertToRestoresPreActionSheet()
{
    // The sheet in place when the dialog opens (the appended rule);
    // the pre-action sheet is the one with the plain rule.
    StyleManager::getInstance()->setStyleSheet(
        QStringLiteral("QWidget { color: red; }\n#meter-1 {\n}"));

    StyleSheetDialog dialog(nullptr, "#meter-1", "QWidget { color: red; }");
    autoPreview(dialog)->setChecked(false);
    editor(dialog)->setPlainText("edited");
    button(dialog, "previewButton")->click();
    QCOMPARE(StyleManager::getInstance()->stylesheet(), QString("edited")); // the preview applied

    button(dialog, "cancelButton")->click();

    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Rejected));
    QCOMPARE(StyleManager::getInstance()->stylesheet(), QString("QWidget { color: red; }"));
    QCOMPARE(qApp->styleSheet(), QString("QWidget { color: red; }"));
}

// The Help button is wired (the modal reference it opens is the
// metatransform's pattern; its content is helpHtml's business).
void StyleSheetDialogTest::testHelpButtonExists()
{
    StyleSheetDialog dialog;
    QVERIFY(button(dialog, "helpButton") != nullptr);
}

// The help's html lists the registered "x-" properties (one section
// per widget, the defaults and the ranges) and links the Qt style
// sheet reference.
void StyleSheetDialogTest::testHelpHtmlListsRegisteredProperties()
{
    StyleManager::getInstance()->registerStyleProperties(
        PeakMeterWidget::styleProperties());

    const QString html = StyleSheetDialog::helpHtml();
    QVERIFY(html.contains("Peak meter"));  // the owner's section
    QVERIFY(html.contains("x-bar-margin"));
    QVERIFY(html.contains("x-track-color"));
    QVERIFY(html.contains("x-segments"));
    QVERIFY(html.contains("16"));          // the coded default
    QVERIFY(html.contains("(2-64)"));      // the range
    QVERIFY(html.contains("https://doc.qt.io/qt-6/stylesheet-reference.html"));
}

// Without registered properties the help carries the intro and the
// reference only (no empty widget sections).
void StyleSheetDialogTest::testHelpHtmlEmptyRegistry()
{
    const QString html = StyleSheetDialog::helpHtml();
    QVERIFY(!html.contains("x-segments"));
    QVERIFY(html.contains("https://doc.qt.io/qt-6/stylesheet-reference.html"));
}
}

QTEST_MAIN(StyleSheetDialogTest)
#include "stylesheetdialog_test.moc"
