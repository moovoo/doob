#ifndef URLWIZARD_H
#define URLWIZARD_H

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QUrl>
#include <functional>

#include "urlimportdialog.h"

// The "Add by URL..." plumbing shared by the widgets that offer it: a
// single UrlImportDialog per owner widget — a second trigger raises the
// open one instead of starting a fresh flow (the wizard is non-modal: a
// list expansion can run a while) — and the confirmed sources plus their
// metadata routed to the widget's import sink. The wizard is parented to
// the owner widget (findable under it) and deletes itself on close, so a
// QPointer tracks it. open(lines) creates a fresh wizard only when none
// is open, and starts its flow with the lines then; an already-open
// wizard is raised untouched (its flow belongs to the trigger that
// started it).
class UrlWizard : public QObject
{
    Q_OBJECT
public:
    // The helper is parented to the owner widget (owned through it); it
    // parents nothing itself — the wizard belongs to the owner too, so
    // findChild finds it under the widget.
    explicit UrlWizard(QWidget *owner);

    // The sink the confirmed sources land in (set before the first open()).
    void setSink(const std::function<void(const QList<QUrl> &sources,
                                          const UrlImportDialog::SourceMeta &metadata)> &sink);

    // Runs the wizard: raises an open one, else creates a fresh one, shows
    // it and (when lines are given) starts its flow with them.
    void open(const QStringList &lines = QStringList());

    // The open wizard, null when none is.
    UrlImportDialog *dialog() const { return m_dialog; }

private:
    QPointer<UrlImportDialog> m_dialog;
    std::function<void(const QList<QUrl> &sources,
                       const UrlImportDialog::SourceMeta &metadata)> m_sink;
    QWidget *m_owner;
};

#endif // URLWIZARD_H
