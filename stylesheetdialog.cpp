#include "stylesheetdialog.h"

#include "dialogplumbing.h"
#include "stylemanager.h"
#include "uistate.h"

#include <QCheckBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextCursor>
#include <QVBoxLayout>

namespace {
// The pause between the last keystroke and the auto-preview applying
// the text, so typing does not re-style the app per character.
constexpr int AUTO_PREVIEW_DELAY_MS = 300;
}

StyleSheetDialog::StyleSheetDialog(QWidget *parent, const QString &focusSelector,
                                   const QString &revertTo)
    : QDialog(parent)
    , m_editor(nullptr)
    , m_autoPreview(nullptr)
    , m_preview(nullptr)
    , m_help(nullptr)
    , m_timer(this)
{
    setWindowTitle("Style sheet");
    // Cancel reverts to the sheet in place at open — or to `revertTo`
    // when the caller passed one (the "Style this widget..." action
    // passes the sheet from before it appended its rule, so a cancel
    // leaves no empty rule behind). The editor itself always shows the
    // sheet as it is now (the appended rule included).
    m_previous = revertTo.isEmpty() ? StyleManager::getInstance()->stylesheet()
                                    : revertTo;

    QVBoxLayout* layout = new QVBoxLayout(this);

    QLabel* hint = new QLabel("Qt style sheet. Preview applies it to the running app; Cancel reverts.", this);
    layout->addWidget(hint);

    m_editor = new QPlainTextEdit(this);
    m_editor->setObjectName("styleSheet");
    m_editor->setPlainText(StyleManager::getInstance()->stylesheet());
    layout->addWidget(m_editor, 1);

    if (!focusSelector.isEmpty()) {
        const qsizetype pos = m_editor->toPlainText().indexOf(focusSelector);
        if (pos >= 0) {
            QTextCursor cursor(m_editor->document());
            cursor.setPosition(static_cast<int>(pos));
            m_editor->setTextCursor(cursor);
            m_editor->centerCursor();
        }
    }

    m_autoPreview = new QCheckBox("Auto-preview", this);
    m_autoPreview->setObjectName("autoPreview");
    m_autoPreview->setChecked(true);
    layout->addWidget(m_autoPreview);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    m_preview = new QPushButton("Preview", this);
    m_preview->setObjectName("previewButton");
    buttonLayout->addWidget(m_preview);
    m_help = new QPushButton("Help", this);
    m_help->setObjectName("helpButton");
    buttonLayout->addWidget(m_help);
    buttonLayout->addStretch(1);

    // The row's stretch is the helper's (a second one would be
    // layout-identical); the ok's slot applies the editor's final text,
    // so a change typed after the last preview is not lost.
    dialog::dialogButtons(this, buttonLayout, "Cancel", "cancelButton",
                          "OK", "okButton", [this] {
        applyPreview();
        accept();
    });

    layout->addLayout(buttonLayout);

    m_timer.setSingleShot(true);
    m_timer.setInterval(AUTO_PREVIEW_DELAY_MS);
    connect(&m_timer, &QTimer::timeout, this, &StyleSheetDialog::applyPreview);
    connect(m_editor, &QPlainTextEdit::textChanged, this, &StyleSheetDialog::onTextChanged);
    connect(m_autoPreview, &QCheckBox::toggled, this, &StyleSheetDialog::onAutoPreviewToggled);
    connect(m_preview, &QPushButton::clicked, this, &StyleSheetDialog::onPreviewClicked);
    connect(m_help, &QPushButton::clicked, this, &StyleSheetDialog::onHelp);

    onAutoPreviewToggled(true); // initial state of the preview button
    resize(700, 500);

    // The dialog's own state: its size and position (saved on every
    // hide). The sheet being edited is the style store's business.
    m_state =
        dialog::dialogUiState(this, QStringLiteral("dialogs.style_sheet"));
}

void StyleSheetDialog::onTextChanged()
{
    if (m_autoPreview->isChecked())
        m_timer.start();
}

void StyleSheetDialog::onAutoPreviewToggled(bool enabled)
{
    m_preview->setEnabled(!enabled);
    if (enabled)
        m_timer.start(); // pick up whatever is in the editor right now
}

void StyleSheetDialog::onPreviewClicked()
{
    applyPreview();
}

QString StyleSheetDialog::helpHtml()
{
    // The "x-" custom property reference: the widgets' registered docs
    // (one section per widget, the properties' name, unit, coded
    // default and range) and the Qt style sheet reference at the
    // bottom (the native properties the sheet also carries).
    QString html;
    html += QStringLiteral(
        "<h3>Custom properties</h3>"
        "<p>doob's custom-painted widgets read additional <tt>x-</tt> "
        "properties from the same style sheet, alongside the native "
        "Qt properties. Qt does not understand them: it logs an "
        "expected \"Unknown property\" warning for each and applies the "
        "rest of the rule.</p>"
        "<p>They resolve like CSS custom properties: a rule's value "
        "applies to the selector's widget and cascades to its "
        "descendants, a <tt>#name</tt> selector beats a class name, at "
        "equal specificity the later rule in the sheet wins. The "
        "widget's coded default (the table's \"Default\" column) stands "
        "in when no rule matches.</p>");

    const QList<StyleProperty> props =
        StyleManager::getInstance()->styleProperties();
    QString owner;
    for (const StyleProperty &prop : props) {
        if (prop.owner != owner) {
            if (!owner.isEmpty())
                html += QStringLiteral("</table>");
            owner = prop.owner;
            html += QStringLiteral("<h3>%1</h3>").arg(owner);
            html += QStringLiteral(
                "<table cellspacing=\"0\">"
                "<tr><th align=\"left\">Property</th>"
                "<th align=\"left\">Default</th>"
                "<th align=\"left\">What it does</th></tr>");
        }
        QString range;
        if (prop.max > 0)
            range = QStringLiteral(" (%1-%2)").arg(prop.min).arg(prop.max);
        html += QStringLiteral(
                    "<tr><td style=\"white-space:nowrap\"><tt>%1</tt></td>"
                    "<td style=\"white-space:nowrap\">%2</td>"
                    "<td>%3%4</td></tr>")
            .arg(prop.name, prop.defaultValue, prop.description, range);
    }
    if (!owner.isEmpty())
        html += QStringLiteral("</table>");

    html += QStringLiteral(
        "<h3>Reference</h3>"
        "<p>The native properties this sheet uses are Qt's: "
        "<a href=\"https://doc.qt.io/qt-6/stylesheet-reference.html\">"
        "Qt Style Sheet Reference</a>.</p>");
    return html;
}

void StyleSheetDialog::onHelp()
{
    // Rich text in a scrollable browser (the reference outgrows a
    // message box), the same shape as the metadata transform's.
    dialog::showReferenceDialog(this, QStringLiteral("Style sheet reference"),
                                helpHtml(), true);
}

void StyleSheetDialog::applyPreview()
{
    StyleManager::getInstance()->setStyleSheet(m_editor->toPlainText());
}

void StyleSheetDialog::accept()
{
    // The commit: the applied sheet is saved to the current style's
    // file (the save forks an edited preset current — the copy-on-
    // write), so the edit survives the restart without a separate
    // save.
    StyleManager::getInstance()->saveCurrentStyle();
    QDialog::accept();
}

void StyleSheetDialog::reject()
{
    // Revert whatever the preview applied, then refuse.
    StyleManager::getInstance()->setStyleSheet(m_previous);
    QDialog::reject();
}
