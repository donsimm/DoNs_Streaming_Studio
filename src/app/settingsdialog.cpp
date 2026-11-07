#include "settingsdialog.h"

#include "coreclient.h"
#include "windowdata.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
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
    m_address->setPlaceholderText(QStringLiteral("leer = aus der Standardroute ermitteln"));
    m_file = new QLabel(this);
    m_file->setObjectName(QStringLiteral("settingsFile"));
    m_file->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QStringLiteral("Port des Overlay-Servers"), m_port);
    form->addRow(QStringLiteral("Adresse im Heimnetz (IPv4)"), m_address);
    form->addRow(QStringLiteral("Datei"), m_file);
    auto *hint = new QLabel(dss::settingsDialogHint(), this);
    hint->setObjectName(QStringLiteral("settingsHint"));
    hint->setWordWrap(true);
    form->addRow(hint);

    // P10c, Bereich «OBS»: Titel und Linie wie die Bereiche im Dock «Eigenschaften». In demselben
    // Formular, damit die Felder bündig stehen.
    auto *obsTitle = new QLabel(QStringLiteral("OBS"), this);
    obsTitle->setObjectName(QStringLiteral("propertySectionTitle"));
    form->addRow(obsTitle);
    auto *obsLine = new QFrame(this);
    obsLine->setObjectName(QStringLiteral("propertySectionLine"));
    obsLine->setFixedHeight(1);
    form->addRow(obsLine);
    m_obsEnabled = new QCheckBox(QStringLiteral("Mit OBS verbinden"), this);
    m_obsEnabled->setObjectName(QStringLiteral("settingsObsEnabled"));
    m_obsHost = new QLineEdit(this);
    m_obsHost->setObjectName(QStringLiteral("settingsObsHost"));
    m_obsHost->setPlaceholderText(QStringLiteral("192.168.1.116"));
    m_obsPort = new QSpinBox(this);
    m_obsPort->setObjectName(QStringLiteral("settingsObsPort"));
    m_obsPort->setRange(dss::kObsPortMin, dss::kObsPortMax);
    m_obsPort->setValue(dss::kObsDefaultPort);
    m_obsPassword = new QLineEdit(this);
    m_obsPassword->setObjectName(QStringLiteral("settingsObsPassword"));
    m_obsPassword->setEchoMode(QLineEdit::Password);
    m_obsDeletePassword = new QPushButton(QStringLiteral("Passwort löschen"), this);
    m_obsDeletePassword->setObjectName(QStringLiteral("settingsObsDeletePassword"));
    m_obsDeletePassword->setAutoDefault(false);
    m_obsDeletePassword->setToolTip(QStringLiteral("Löscht das gespeicherte Passwort mit «OK»"));
    auto *passwordRow = new QHBoxLayout;
    passwordRow->addWidget(m_obsPassword, 1);
    passwordRow->addWidget(m_obsDeletePassword);
    form->addRow(QString(), m_obsEnabled);
    form->addRow(QStringLiteral("Adresse von OBS"), m_obsHost);
    form->addRow(QStringLiteral("Port von obs-websocket"), m_obsPort);
    // addRow mit Layout verknüpft die Beschriftung nicht von selbst mit dem Feld.
    auto *passwordLabel = new QLabel(QStringLiteral("Passwort"), this);
    passwordLabel->setBuddy(m_obsPassword);
    form->addRow(passwordLabel, passwordRow);
    column->addLayout(form);

    connect(m_obsDeletePassword, &QPushButton::clicked, this, [this] {
        m_deleteObsPassword = true;
        m_obsPassword->clear();
        updateState();
    });
    // Das eingegebene Passwort bleibt nicht im Fenster, auch nicht nach «Abbrechen» (Regel 12).
    connect(this, &QDialog::finished, m_obsPassword, &QLineEdit::clear);

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
    // P10, Punkt 77: immer da, nur bei ungültiger kern.ini frei (keine Änderung der Anordnung).
    m_reset = buttons->addButton(QStringLiteral("Auf Standardwerte zurücksetzen"), QDialogButtonBox::ResetRole);
    m_reset->setObjectName(QStringLiteral("settingsReset"));
    m_reset->setToolTip(QStringLiteral("Die ungültige kern.ini wird umbenannt (kern.ini.ungueltig-<Datum>) "
                                       "und neu mit den Standardwerten geschrieben."));
    m_ok->setDefault(true);
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_reset, &QPushButton::clicked, this, &SettingsDialog::resetToDefaults);

    connect(m_client, &dss::CoreClient::settingsChanged, this, &SettingsDialog::onSettingsChanged);
    // Punkt 76: Antworten auf Einstellungen kommen nur hier an, nicht in der Statuszeile.
    connect(m_client, &dss::CoreClient::settingsUpdateFinished, this, &SettingsDialog::onChangeFinished);
    connect(m_client, &dss::CoreClient::connectedChanged, this, [this](bool connected) {
        if (connected && isVisible()) {
            m_client->requestSettings();
        } else if (!connected) {
            m_pendingRequest = 0;
            m_pendingReset = false;
        }
        updateState();
    });
    updateState();
}

void SettingsDialog::showFromCore()
{
    m_filled = false;
    m_pendingRequest = 0;
    m_pendingReset = false;
    m_deleteObsPassword = false;
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
    dss::SettingsForm values;
    values.port = m_port->value();
    values.address = m_address->text();
    values.obsEnabled = m_obsEnabled->isChecked();
    values.obsHost = m_obsHost->text();
    values.obsPort = m_obsPort->value();
    values.obsPassword = m_obsPassword->text();
    values.deleteObsPassword = m_deleteObsPassword;
    const bool obsInError = m_client->connectionStates().value(dss::Connection::Obs) == dss::ConnectionState::Error;
    const std::optional<dss::SettingsUpdate> update =
        dss::settingsUpdateFromForm(info->settings, values, obsInError);
    if (!update) {
        QDialog::accept(); // nichts geändert
        return;
    }
    m_problem->clear();
    m_pendingRequest = m_client->sendSettingsUpdate(*update);
    updateState();
}

void SettingsDialog::resetToDefaults()
{
    m_problem->clear();
    m_pendingRequest = m_client->sendSettingsReset();
    m_pendingReset = m_pendingRequest > 0;
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
            m_obsEnabled->setChecked(info->settings.obsEnabled);
            m_obsHost->setText(info->settings.obsHost);
            m_obsPort->setValue(info->settings.obsPort);
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
    const bool wasReset = m_pendingReset;
    m_pendingReset = false;
    if (result.ok && wasReset) {
        // Zurückgesetzt: Dialog bleibt offen mit den neuen Werten und dem Hinweis des Kerns.
        m_filled = false;
        onSettingsChanged();
        m_problem->setText(result.warning);
        return;
    }
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
    for (QWidget *w : std::initializer_list<QWidget *>{m_obsEnabled, m_obsHost, m_obsPort, m_obsPassword})
        w->setEnabled(usable && m_pendingRequest == 0);
    const bool passwordSet = info && info->obsPasswordSet;
    m_obsDeletePassword->setEnabled(usable && m_pendingRequest == 0 && passwordSet && !m_deleteObsPassword);
    m_obsPassword->setPlaceholderText(dss::obsPasswordPlaceholder(info, m_deleteObsPassword));
    m_ok->setEnabled(usable && m_pendingRequest == 0);
    m_reset->setEnabled(m_client->isConnected() && info && !info->error.isEmpty() && m_pendingRequest == 0);
    // Meldung des Zustands (ohne Verbindung, Datei ungültig) setzen bzw. wieder entfernen; eine
    // Meldung des Kerns zu einer Änderung bleibt stehen.
    if (!problem.isEmpty())
        m_problem->setText(problem);
    else if (!m_stateProblem.isEmpty() && m_problem->text() == m_stateProblem)
        m_problem->clear();
    m_stateProblem = problem;
}
