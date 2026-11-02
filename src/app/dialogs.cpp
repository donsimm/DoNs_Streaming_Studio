#include "dialogs.h"

#include "overlay.h"
#include "windowedit.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>

bool askNewOverlay(QWidget *parent, QString *name, int *width, int *height)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Overlay anlegen"));
    auto *form = new QFormLayout(&dialog);
    auto *nameEdit = new QLineEdit(dss::newOverlayName(), &dialog);
    nameEdit->selectAll();
    auto *widthBox = new QSpinBox(&dialog);
    widthBox->setRange(dss::kOverlayMinSize, dss::kOverlayMaxWidth);
    widthBox->setValue(dss::kNewOverlayWidth);
    widthBox->setSuffix(QStringLiteral(" px"));
    auto *heightBox = new QSpinBox(&dialog);
    heightBox->setRange(dss::kOverlayMinSize, dss::kOverlayMaxHeight);
    heightBox->setValue(dss::kNewOverlayHeight);
    heightBox->setSuffix(QStringLiteral(" px"));
    form->addRow(QStringLiteral("Name"), nameEdit);
    form->addRow(QStringLiteral("Breite"), widthBox);
    form->addRow(QStringLiteral("Höhe"), heightBox);
    auto *buttons = new QDialogButtonBox(&dialog);
    QPushButton *ok = buttons->addButton(QStringLiteral("Anlegen"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QStringLiteral("Abbrechen"), QDialogButtonBox::RejectRole);
    ok->setDefault(true);
    form->addRow(buttons);
    // Leerer Name lässt sich nicht bestätigen (gleiche Prüfung wie im Kern).
    QObject::connect(nameEdit, &QLineEdit::textChanged, ok, [ok](const QString &text) {
        ok->setEnabled(dss::validateOverlayName(text).isEmpty());
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    *name = nameEdit->text();
    *width = widthBox->value();
    *height = heightBox->value();
    return true;
}

bool askText(QWidget *parent, const QString &title, const QString &label, QString *value)
{
    QInputDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setLabelText(label);
    dialog.setTextValue(*value);
    dialog.setOkButtonText(QStringLiteral("OK"));
    dialog.setCancelButtonText(QStringLiteral("Abbrechen"));
    if (dialog.exec() != QDialog::Accepted || dialog.textValue().trimmed().isEmpty())
        return false;
    *value = dialog.textValue();
    return true;
}

bool confirm(QWidget *parent, const QString &title, const QString &text, const QString &acceptText,
             const QString &rejectText)
{
    QMessageBox box(parent);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(title);
    box.setText(text);
    QPushButton *accept = box.addButton(acceptText, QMessageBox::AcceptRole);
    QPushButton *reject = box.addButton(rejectText, QMessageBox::RejectRole);
    box.setDefaultButton(reject);
    box.setEscapeButton(reject);
    box.exec();
    return box.clickedButton() == accept;
}
