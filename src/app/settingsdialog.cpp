#include "settingsdialog.h"

#include "coreclient.h"
#include "windowdata.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(dss::CoreClient *client, QWidget *parent)
    : QDialog(parent)
    , m_client(client)
{
    setObjectName(QStringLiteral("settingsDialog"));
    setWindowTitle(QStringLiteral("Einstellungen"));
    auto *column = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_port = new QSpinBox(this);
    m_port->setObjectName(QStringLiteral("settingsPort"));
    // Bereich aus der Nachricht des Kerns (port_min, port_max); bis dahin der ganze Bereich.
    m_port->setRange(1, 65535);
    m_address = new QLineEdit(this);
    m_address->setObjectName(QStringLiteral("settingsAddress"));
    m_address->setPlaceholderText(QStringLiteral("leer = beim nächsten Start ermitteln"));
    m_file = new QLabel(this);
    m_file->setObjectName(QStringLiteral("settingsFile"));
    m_file->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QStringLiteral("Port des Overlay-Servers"), m_port);
    form->addRow(QStringLiteral("Adresse im Heimnetz (IPv4)"), m_address);
    form->addRow(QStringLiteral("Datei"), m_file);
    column->addLayout(form);

    auto *hint = new QLabel(dss::settingsDialogHint(), this);
    hint->setObjectName(QStringLiteral("settingsHint"));
    hint->setWordWrap(true);
    column->addWidget(hint);
    // Meldung des Kerns: Platz für drei Zeilen von Anfang an, damit das Setzen des Textes das
    // Layout nicht ändert (Regel 9).
    m_problem = new QLabel(this);
    m_problem->setObjectName(QStringLiteral("settingsProblem"));
    m_problem->setWordWrap(true);
    m_problem->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_problem->setFixedHeight(fontMetrics().lineSpacing() * 3);
    column->addWidget(m_problem);

    auto *buttons = new QDialogButtonBox(this);
    m_ok = buttons->addButton(QStringLiteral("OK"), QDialogButtonBox::AcceptRole);
    m_ok->setObjectName(QStringLiteral("settingsOk"));
    buttons->addButton(QStringLiteral("Abbrechen"), QDialogButtonBox::RejectRole);
    m_ok->setDefault(true);
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_client, &dss::CoreClient::settingsChanged, this, &SettingsDialog::onSettingsChanged);
    connect(m_client, &dss::CoreClient::changeFinished, this, &SettingsDialog::onChangeFinished);
    connect(m_client, &dss::CoreClient::connectedChanged, this, [this](bool connected) {
        if (connected && isVisible())
            m_client->requestSettings();
        else if (!connected)
            m_pendingRequest = 0;
        updateState();
    });
    updateState();
}

void SettingsDialog::showFromCore()
{
    m_filled = false;
    m_pendingRequest = 0;
    m_problem->clear();
    m_client->requestSettings();
    onSettingsChanged();
    open();
}

void SettingsDialog::accept()
{
    const std::optional<dss::SettingsInfo> info = m_client->settings();
    if (!info) {
        updateState();
        return;
    }
    const std::optional<dss::SettingsUpdate> update =
        dss::settingsUpdateFromForm(info->settings, m_port->value(), m_address->text());
    if (!update) {
        QDialog::accept(); // nichts geändert
        return;
    }
    m_problem->clear();
    m_pendingRequest = m_client->sendSettingsUpdate(*update);
    updateState();
}

void SettingsDialog::onSettingsChanged()
{
    const std::optional<dss::SettingsInfo> info = m_client->settings();
    if (info) {
        m_port->setRange(info->portMin, info->portMax);
        m_file->setText(info->file);
        // Eigene Eingaben nicht überschreiben, wenn ein anderes Fenster etwas ändert.
        if (!m_filled) {
            m_port->setValue(info->settings.port);
            m_address->setText(info->settings.homeAddress);
            m_filled = true;
        }
    }
    updateState();
}

void SettingsDialog::onChangeFinished(const dss::ChangeResult &result)
{
    if (m_pendingRequest == 0 || result.requestId != m_pendingRequest)
        return;
    m_pendingRequest = 0;
    if (result.ok) {
        QDialog::accept();
        return;
    }
    m_problem->setText(result.message);
    updateState();
}

void SettingsDialog::updateState()
{
    const std::optional<dss::SettingsInfo> info = m_client->settings();
    const QString problem = dss::settingsDialogProblem(m_client->isConnected(), info);
    const bool usable = problem.isEmpty() && info.has_value();
    m_port->setEnabled(usable && m_pendingRequest == 0);
    m_address->setEnabled(usable && m_pendingRequest == 0);
    m_ok->setEnabled(usable && m_pendingRequest == 0);
    // Meldung des Zustands (ohne Verbindung, Datei ungültig) setzen bzw. wieder entfernen; eine
    // Meldung des Kerns zu einer Änderung bleibt stehen.
    if (!problem.isEmpty())
        m_problem->setText(problem);
    else if (!m_stateProblem.isEmpty() && m_problem->text() == m_stateProblem)
        m_problem->clear();
    m_stateProblem = problem;
}
