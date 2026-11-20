#ifndef VALUEEDITDIALOG_H
#define VALUEEDITDIALOG_H

#include <QDialog>
#include <QVariant>

class QLabel;
class QWidget;

// The value editor of the settings browser (the about:config page):
// builds a type-specific editor for the given value (bool -> checkbox,
// number -> spin box, string -> line edit, list -> one per line, object
// -> JSON text) and reports the edited value on OK. result() is valid
// only when the dialog was accepted; a parse failure (a malformed JSON
// object) keeps the dialog open with a hint.
class ValueEditDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ValueEditDialog(const QString &title, const QVariant &value,
                             QWidget *parent = nullptr);

    // The value the editor currently holds (invalid when rejected).
    // (Not named result(): that would shadow QDialog::result().)
    QVariant editedValue() const;

private:
    QVariant currentValue() const;

    QVariant m_initial;
    QWidget* m_editor = nullptr;
    QLabel* m_hint = nullptr;
    bool m_accepted = false;
};

#endif // VALUEEDITDIALOG_H
