#ifndef STYLESHEETDIALOG_H
#define STYLESHEETDIALOG_H

#include <QDialog>
#include <QTimer>

class QCheckBox;
class QPlainTextEdit;
class QPushButton;
class UiState;

// Edits the application's Qt style sheet with a live preview. In
// auto-preview mode (the default) the text is applied to the running
// app a short while after typing stops; in manual mode the "Preview"
// button applies it on demand. OK commits the editor's final text;
// Cancel re-applies the sheet that was in place when the dialog opened,
// so experimenting is free.
//
// With `focusSelector` the cursor lands in that rule (the "Style this
// widget..." action seeds it with "#name"), and with `revertTo` Cancel
// restores that sheet instead of the one in place at open — the action
// passes the sheet from before it appended the instance's rule, so a
// pure cancel leaves no empty rule behind.
//
// The Help button opens the "x-" custom property reference (the
// widgets' registered docs, StyleManager) with the Qt style sheet
// reference linked at the bottom.
class StyleSheetDialog : public QDialog
{
    Q_OBJECT

public:
    explicit StyleSheetDialog(QWidget *parent = nullptr,
                              const QString &focusSelector = QString(),
                              const QString &revertTo = QString());

    // Commits the sheet: the applied text is saved to the current
    // style's file (the save forks a stale preset current).
    void accept() override;
    // Reverts the previewed sheet. Cancel, Esc and the window close button
    // all funnel through here, so every way out of the dialog reverts.
    void reject() override;

    // The help dialog's content (see onHelp): the "x-" property docs
    // of the registered widgets (StyleManager) and the Qt style sheet
    // reference — the test seam.
    static QString helpHtml();

private slots:
    void onTextChanged();
    void onAutoPreviewToggled(bool enabled);
    void onPreviewClicked();
    void onHelp();

private:
    void applyPreview();

    UiState* m_state = nullptr;
    QPlainTextEdit *m_editor;
    QCheckBox *m_autoPreview;
    QPushButton *m_preview;
    QPushButton *m_help;
    QTimer m_timer;
    QString m_previous; // the sheet in place when the dialog opened
};

#endif // STYLESHEETDIALOG_H
