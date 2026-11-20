#include "valueeditdialog.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMetaType>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

ValueEditDialog::ValueEditDialog(const QString &title, const QVariant &value,
                                 QWidget *parent)
    : QDialog(parent)
    , m_initial(value)
{
    setObjectName(QStringLiteral("valueEditDialog"));
    setWindowTitle(QStringLiteral("Edit %1").arg(title));

    QVBoxLayout* layout = new QVBoxLayout(this);

    switch (value.type()) {
    case QMetaType::Bool: {
        QCheckBox* editor = new QCheckBox(this);
        editor->setChecked(value.toBool());
        m_editor = editor;
        break;
    }
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong: {
        QSpinBox* editor = new QSpinBox(this);
        editor->setRange(INT_MIN, INT_MAX);
        editor->setValue(static_cast<int>(value.toLongLong()));
        m_editor = editor;
        break;
    }
    case QMetaType::Double: {
        QDoubleSpinBox* editor = new QDoubleSpinBox(this);
        editor->setDecimals(6);
        editor->setRange(-1e15, 1e15);
        editor->setValue(value.toDouble());
        m_editor = editor;
        break;
    }
    case QMetaType::QStringList: {
        QPlainTextEdit* editor = new QPlainTextEdit(this);
        editor->setPlainText(value.toStringList().join('\n'));
        m_editor = editor;
        break;
    }
    case QMetaType::QVariantList:
    case QMetaType::QVariantMap: {
        QPlainTextEdit* editor = new QPlainTextEdit(this);
        editor->setPlainText(QString::fromUtf8(
            QJsonDocument::fromVariant(value).toJson(QJsonDocument::Indented)));
        m_editor = editor;
        break;
    }
    default: {
        QLineEdit* editor = new QLineEdit(value.toString(), this);
        m_editor = editor;
        break;
    }
    }
    m_editor->setObjectName(QStringLiteral("valueEditor"));
    layout->addWidget(m_editor, 1);

    m_hint = new QLabel(this);
    m_hint->setObjectName(QStringLiteral("hintLabel"));
    layout->addWidget(m_hint);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch(1);
    QPushButton* ok = new QPushButton(QStringLiteral("OK"), this);
    ok->setObjectName(QStringLiteral("okButton"));
    ok->setDefault(true);
    QPushButton* cancel = new QPushButton(QStringLiteral("Cancel"), this);
    cancel->setObjectName(QStringLiteral("cancelButton"));
    buttonLayout->addWidget(ok);
    buttonLayout->addWidget(cancel);
    layout->addLayout(buttonLayout);

    connect(ok, &QPushButton::clicked, this,
            [this]() {
        const QVariant value = currentValue();
        // The object editor must hold a JSON object (an array or a bare
        // value is not a setting value): keep the dialog open.
        if (m_initial.userType() == QMetaType::QVariantMap
                && !value.isValid()) {
            m_hint->setText(QStringLiteral("Not a valid JSON object."));
            return;
        }
        m_accepted = true;
        accept();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    resize(420, 200);
}

QVariant ValueEditDialog::currentValue() const
{
    if (QCheckBox* editor = qobject_cast<QCheckBox*>(m_editor))
        return editor->isChecked();
    if (QSpinBox* editor = qobject_cast<QSpinBox*>(m_editor))
        return editor->value();
    if (QDoubleSpinBox* editor = qobject_cast<QDoubleSpinBox*>(m_editor))
        return editor->value();
    if (QLineEdit* editor = qobject_cast<QLineEdit*>(m_editor))
        return editor->text();
    if (QPlainTextEdit* editor = qobject_cast<QPlainTextEdit*>(m_editor)) {
        if (m_initial.userType() == QMetaType::QStringList) {
            QStringList out;
            for (const QString &line : editor->toPlainText().split('\n')) {
                const QString trimmed = line.trimmed();
                if (!trimmed.isEmpty())
                    out.append(trimmed);
            }
            return out;
        }
        // The object editor: an unparseable (or non-object) text yields an
        // invalid value (the caller keeps the dialog open).
        const QJsonDocument doc =
            QJsonDocument::fromJson(editor->toPlainText().toUtf8());
        return doc.isObject() ? doc.object().toVariantMap() : QVariant();
    }
    return {};
}

QVariant ValueEditDialog::editedValue() const
{
    return m_accepted ? currentValue() : QVariant();
}
