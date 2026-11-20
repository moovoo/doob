#include "urlwizard.h"
#include "urlimportdialog.h"

UrlWizard::UrlWizard(QWidget *owner)
    : QObject(owner)
    , m_owner(owner)
{
}

void UrlWizard::setSink(const std::function<void(const QList<QUrl> &sources,
                                                 const UrlImportDialog::SourceMeta &metadata)> &sink)
{
    m_sink = sink;
}

void UrlWizard::open(const QStringList &lines)
{
    if (m_dialog) {
        // The wizard is already mid-flow: raise it, do not restart it.
        m_dialog->show();
        m_dialog->raise();
        m_dialog->activateWindow();
        return;
    }
    m_dialog = new UrlImportDialog(m_owner);
    connect(m_dialog, &UrlImportDialog::sourcesConfirmed, this,
            [this](const QList<QUrl> &sources, const UrlImportDialog::SourceMeta &metadata) {
                if (m_sink)
                    m_sink(sources, metadata);
            });
    m_dialog->show();
    if (!lines.isEmpty())
        m_dialog->start(lines);
}


