#include "metatransformdialog.h"
#include "dialogplumbing.h"
#include "metadatadialog.h"
#include "metatransform.h"
#include "mediaitemstore.h"
#include "uistate.h"

#include <array>

#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

MetaTransformDialog::MetaTransformDialog(QStandardItemModel *model,
                                         const QList<quint64> &ids,
                                         const QList<int> &rows,
                                         QWidget *parent)
    : QDialog(parent)
    , m_model(model)
    , m_ids(ids)
    , m_rows(rows)
{
    setWindowTitle(QStringLiteral("Transform %1 row(s)").arg(rows.size()));

    QComboBox *columnCombo = new QComboBox(this);
    columnCombo->setObjectName("columnCombo");
    const QStringList labels = MetaDataDialog::columnLabels();
    for (int column : MetaDataDialog::editableColumns())
        columnCombo->addItem(labels.at(column), column);
    m_columnCombo = columnCombo;

    QLineEdit *expression = new QLineEdit(this);
    expression->setObjectName("expressionEdit");
    expression->setPlaceholderText(
        QStringLiteral("%filename% match(\"^(\\\\d+) - (.+)$\", 2)"));
    m_expression = expression;

    QPushButton *help = new QPushButton("Help", this);
    help->setObjectName("helpButton");
    connect(help, &QPushButton::clicked, this, &MetaTransformDialog::onHelp);

    QHBoxLayout *columnRow = new QHBoxLayout;
    columnRow->addWidget(new QLabel("Column:", this));
    columnRow->addWidget(columnCombo);
    columnRow->addStretch();

    QHBoxLayout *exprRow = new QHBoxLayout;
    exprRow->addWidget(new QLabel("Expression:", this));
    exprRow->addWidget(expression);
    exprRow->addWidget(help);

    m_preview = new QTableWidget(0, 2, this);
    m_preview->setObjectName("previewTable");
    m_preview->setHorizontalHeaderLabels({ "Row", "Result" });
    m_preview->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_preview->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_preview->verticalHeader()->setVisible(false);
    m_preview->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_preview->setSelectionBehavior(QAbstractItemView::SelectRows);

    m_status = new QLabel(this);
    m_status->setObjectName("statusLabel");

    QHBoxLayout *buttons = new QHBoxLayout;
    dialog::dialogButtons(this, buttons, "Close", "closeButton", "Apply",
                          "applyButton", &MetaTransformDialog::onApply);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addLayout(columnRow);
    layout->addLayout(exprRow);
    layout->addWidget(m_preview);
    layout->addWidget(m_status);
    layout->addLayout(buttons);

    connect(columnCombo, &QComboBox::currentIndexChanged, this,
            &MetaTransformDialog::updatePreview);
    connect(expression, &QLineEdit::textChanged, this,
            &MetaTransformDialog::updatePreview);

    updatePreview();
    resize(640, 420);

    // The dialog's own state: its size and position (saved on every
    // hide) and the preview's row column (the result stretches).
    m_state =
        dialog::dialogUiState(this, QStringLiteral("dialogs.metatransform"));
    m_state->restoreColumns(m_preview->horizontalHeader(),
                             { "row", "result" });
}

void MetaTransformDialog::hideEvent(QHideEvent *event)
{
    // The buttons close through reject/accept, which hide the dialog
    // without a closeEvent — the columns are saved on every hide, like
    // the geometry (an unchanged save is a no-op).
    m_state->saveColumns(m_preview->horizontalHeader(),
                             { "row", "result" });
    QDialog::hideEvent(event);
}

int MetaTransformDialog::column() const
{
    return m_columnCombo->currentData().toInt();
}

QString MetaTransformDialog::expression() const
{
    return m_expression->text();
}

void MetaTransformDialog::updatePreview()
{
    m_preview->setRowCount(0);

    const QString expr = m_expression->text().trimmed();
    if (expr.isEmpty())
    {
        m_status->setText(QStringLiteral("Type an expression to preview the results"));
        return;
    }

    const int target = column();

    // A syntax-level error is the same for every row: detect it once.
    const TransformResult first = MetaTransform::evaluate(
        expr, MetaTransform::variablesFor(MediaItemStore::resolve(m_ids.at(0)),
                                          m_model->item(m_rows.at(0), target)->text()));
    if (!first.ok)
    {
        m_status->setText(first.error);
        return;
    }

    m_preview->setRowCount(static_cast<int>(m_rows.size()));
    for (int i = 0; i < m_rows.size(); ++i)
    {
        const int row = m_rows.at(i);
        const MediaItem item = MediaItemStore::resolve(m_ids.at(row));
        const TransformResult result = MetaTransform::evaluate(
            expr, MetaTransform::variablesFor(item, m_model->item(row, target)->text()));

        m_preview->setItem(i, 0, new QTableWidgetItem(
            QStringLiteral("%1. %2")
                .arg(row + 1)
                .arg(m_model->item(row, MetaDataDialog::Title)->text())));
        m_preview->setItem(i, 1, new QTableWidgetItem(result.value));
    }
    m_status->setText(QString());
}

void MetaTransformDialog::onApply()
{
    const QString expr = m_expression->text().trimmed();
    if (expr.isEmpty())
        return;

    const int target = column();
    const bool numeric = MetaDataDialog::isNumericColumn(target);

    // A failing expression fails for every row: show the reason, not
    // "applied to 0 row(s)".
    const TransformResult first = MetaTransform::evaluate(
        expr, MetaTransform::variablesFor(MediaItemStore::resolve(m_ids.at(0)),
                                          m_model->item(m_rows.at(0), target)->text()));
    if (!first.ok)
    {
        m_status->setText(first.error);
        return;
    }

    int applied = 0;
    int skipped = 0;
    for (int row : m_rows)
    {
        const MediaItem item = MediaItemStore::resolve(m_ids.at(row));
        const TransformResult result = MetaTransform::evaluate(
            expr, MetaTransform::variablesFor(item, m_model->item(row, target)->text()));
        if (!result.ok)
            continue;
        if (numeric)
        {
            bool ok = false;
            result.value.trimmed().toInt(&ok);
            if (!ok)
            {
                ++skipped;
                continue;
            }
        }
        m_model->item(row, target)->setText(result.value);
        ++applied;
    }

    // The summary stays in the status (a re-run of the preview would
    // wipe it; the preview's values are unchanged by the apply).
    QString status = QStringLiteral("Applied to %1 row(s)").arg(applied);
    if (skipped > 0)
        status += QStringLiteral(" — %1 skipped (not a number)").arg(skipped);
    m_status->setText(status);
}

void MetaTransformDialog::onHelp()
{
    // The reference lines come from the engine's own docs, so the help can
    // never drift from what evaluates. Rich text in a scrollable browser
    // (the reference is longer than a message box is pleasant).
    const std::array<QPair<const char*, const char*>, 5> kExamples = { {
        { R"("My album")", "the constant, every row" },
        { R"(%filename% match("^(\\d+) - (.+)$", 1))", "the number before \" - \"" },
        { R"(%filename% match("^(\\d+) - (.+)$", 2))", "everything after \" - \"" },
        { R"(%filename% match("^(.*)\\.[^.]+$", 1))", "strip the file extension" },
        { R"(%artist% upper())", "A-LICE COOPER" },
        },
    };

    QString html;
    html += QStringLiteral(
        "<h3>Syntax</h3>"
        "<p><tt>%macro% op(args) op(args) ...</tt> — read left to right, "
        "each op consumes the previous result.</p>"
        "<p>A value can also be a literal: <tt>\"My album\"</tt> (or "
        "<tt>My album</tt> without quotes when no ops follow) — sets the "
        "column to exactly that.</p>"
        "<h3>Examples</h3>"
        "<table cellspacing=\"0\">");
    for (const QPair<const char*, const char*> &example : kExamples)
        html += QStringLiteral("<tr><td style=\"white-space:nowrap\"><tt>%1</tt></td><td>%2</td></tr>")
                    .arg(example.first, example.second);
    html += QStringLiteral("</table>");

    html += QStringLiteral(
        "<h3>Macros</h3>"
        "<p>The metadata fields are the values this dialog opened with; "
        "the file parts are empty for non-local items.</p>"
        "<table cellspacing=\"0\">"
        "<tr><th align=\"left\">Macro</th><th align=\"left\">Value</th></tr>");
    for (const QPair<QString, QString> &doc : MetaTransform::macroDocs())
        html += QStringLiteral("<tr><td style=\"white-space:nowrap\"><tt>%1</tt></td><td>%2</td></tr>")
                    .arg(doc.first, doc.second);
    html += QStringLiteral("</table>");

    html += QStringLiteral(
        "<h3>Ops</h3>"
        "<table cellspacing=\"0\">"
        "<tr><th align=\"left\">Op</th><th align=\"left\">What it does</th></tr>");
    for (const QPair<QString, QString> &doc : MetaTransform::opDocs())
        html += QStringLiteral("<tr><td style=\"white-space:nowrap\"><tt>%1</tt></td><td>%2</td></tr>")
                    .arg(doc.first, doc.second);
    html += QStringLiteral("</table>");

    dialog::showReferenceDialog(this, QStringLiteral("Transform reference"),
                                html);
}
