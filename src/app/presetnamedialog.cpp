#include "presetnamedialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

PresetNameDialog::PresetNameDialog(const QString &title, const QString &hint, bool withControlsBox, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("presetNameDialog"));
    setWindowTitle(title);
    auto *column = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_name = new QLineEdit(this);
    m_name->setObjectName(QStringLiteral("presetName"));
    form->addRow(QStringLiteral("Name"), m_name);
    if (withControlsBox) {
        m_withControls = new QCheckBox(QStringLiteral("Bildwerte mitspeichern"), this);
        m_withControls->setObjectName(QStringLiteral("presetWithControls"));
        form->addRow(QString(), m_withControls);
    }
    column->addLayout(form);
    auto *hintLabel = new QLabel(hint, this);
    hintLabel->setObjectName(QStringLiteral("hintLabel"));
    hintLabel->setWordWrap(true);
    column->addWidget(hintLabel);
    column->addStretch(1);

    auto *buttons = new QDialogButtonBox(this);
    m_ok = buttons->addButton(QStringLiteral("OK"), QDialogButtonBox::AcceptRole);
    m_ok->setObjectName(QStringLiteral("presetNameOk"));
    m_ok->setDefault(true);
    m_ok->setEnabled(false);
    buttons->addButton(QStringLiteral("Abbrechen"), QDialogButtonBox::RejectRole);
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_name, &QLineEdit::textChanged, this, [this] { m_ok->setEnabled(!name().isEmpty()); });
    resize(fontMetrics().averageCharWidth() * 60, sizeHint().height());
}

QString PresetNameDialog::name() const
{
    return m_name->text().trimmed();
}

bool PresetNameDialog::withControls() const
{
    return m_withControls && m_withControls->isChecked();
}
