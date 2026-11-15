#include "settingsdialog.h"

#include "coreclient.h"
#include "windowdata.h"

#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>
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

    // P13, Bereich «Twitch»: wie der Bereich «OBS» (Plan P13, 2.7). Client Secret wie das Passwort
    // von OBS: verdeckt, gilt mit «OK», «Secret löschen» ebenso.
    auto *twitchTitle = new QLabel(QStringLiteral("Twitch"), this);
    twitchTitle->setObjectName(QStringLiteral("propertySectionTitle"));
    form->addRow(twitchTitle);
    auto *twitchLine = new QFrame(this);
    twitchLine->setObjectName(QStringLiteral("propertySectionLine"));
    twitchLine->setFixedHeight(1);
    form->addRow(twitchLine);
    m_twitchEnabled = new QCheckBox(QStringLiteral("Mit Twitch verbinden"), this);
    m_twitchEnabled->setObjectName(QStringLiteral("settingsTwitchEnabled"));
    m_twitchClientId = new QLineEdit(this);
    m_twitchClientId->setObjectName(QStringLiteral("settingsTwitchClientId"));
    m_twitchClientId->setPlaceholderText(QStringLiteral("aus der Twitch-Konsole, kein Passwort"));
    m_twitchSecret = new QLineEdit(this);
    m_twitchSecret->setObjectName(QStringLiteral("settingsTwitchClientSecret"));
    m_twitchSecret->setEchoMode(QLineEdit::Password);
    m_twitchDeleteSecret = new QPushButton(QStringLiteral("Secret löschen"), this);
    m_twitchDeleteSecret->setObjectName(QStringLiteral("settingsTwitchDeleteSecret"));
    m_twitchDeleteSecret->setAutoDefault(false);
    m_twitchDeleteSecret->setToolTip(QStringLiteral("Löscht das gespeicherte Secret mit «OK»"));
    auto *secretRow = new QHBoxLayout;
    secretRow->addWidget(m_twitchSecret, 1);
    secretRow->addWidget(m_twitchDeleteSecret);
    form->addRow(QString(), m_twitchEnabled);
    form->addRow(QStringLiteral("Client-ID"), m_twitchClientId);
    auto *secretLabel = new QLabel(QStringLiteral("Client Secret"), this);
    secretLabel->setBuddy(m_twitchSecret);
    form->addRow(secretLabel, secretRow);
    form->addRow(QStringLiteral("Kanal"),
                 createTwitchAccountRow(&m_twitchChannel, QStringLiteral("channel"), QStringLiteral("settingsTwitchChannel")));
    form->addRow(QStringLiteral("Bot"),
                 createTwitchAccountRow(&m_twitchBot, QStringLiteral("bot"), QStringLiteral("settingsTwitchBot")));
    // Zustand von EventSub, Abos und Abzeichen: Platz für drei Zeilen von Anfang an (Regel 9).
    m_twitchStatus = new QLabel(this);
    m_twitchStatus->setObjectName(QStringLiteral("settingsTwitchStatus"));
    m_twitchStatus->setWordWrap(true);
    m_twitchStatus->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_twitchStatus->setFixedHeight(fontMetrics().lineSpacing() * 3);
    form->addRow(QStringLiteral("Zustand"), m_twitchStatus);
    column->addLayout(form);

    connect(m_obsDeletePassword, &QPushButton::clicked, this, [this] {
        m_deleteObsPassword = true;
        m_obsPassword->clear();
        updateState();
    });
    connect(m_twitchDeleteSecret, &QPushButton::clicked, this, [this] {
        m_deleteTwitchSecret = true;
        m_twitchSecret->clear();
        updateState();
    });
    // Das eingegebene Passwort bleibt nicht im Fenster, auch nicht nach «Abbrechen» (Regel 12),
    // ebenso das Secret.
    connect(this, &QDialog::finished, m_obsPassword, &QLineEdit::clear);
    connect(this, &QDialog::finished, m_twitchSecret, &QLineEdit::clear);
    m_twitchTimer = new QTimer(this);
    m_twitchTimer->setInterval(1000);
    connect(m_twitchTimer, &QTimer::timeout, this, &SettingsDialog::updateTwitchRemaining);
    connect(this, &QDialog::finished, m_twitchTimer, &QTimer::stop);

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
            m_twitchRequest = 0;
        }
        updateState();
    });
    connect(m_client, &dss::CoreClient::twitchStatusChanged, this, &SettingsDialog::onTwitchStatusChanged);
    connect(m_client, &dss::CoreClient::twitchRequestFinished, this, [this](const dss::ChangeResult &result) {
        if (m_twitchRequest == 0 || result.requestId != m_twitchRequest)
            return;
        m_twitchRequest = 0;
        if (!result.ok)
            m_problem->setText(result.message);
        updateState();
    });
    onTwitchStatusChanged();
}

QWidget *SettingsDialog::createTwitchAccountRow(TwitchAccountRow *row, const QString &account,
                                                const QString &objectPrefix)
{
    row->account = account;
    row->stack = new QStackedWidget(this);
    row->stack->setObjectName(objectPrefix + QStringLiteral("Stack"));

    // Seite 0: Zustand mit «Anmelden …» und «Abmelden». Zustand mit Platz für drei Zeilen (Name,
    // Meldung, fehlende Rechte), damit das Setzen des Textes das Layout nicht ändert (Regel 9).
    auto *idle = new QWidget(row->stack);
    auto *idleColumn = new QVBoxLayout(idle);
    idleColumn->setContentsMargins(0, 0, 0, 0);
    row->state = new QLabel(idle);
    row->state->setObjectName(objectPrefix + QStringLiteral("State"));
    row->state->setWordWrap(true);
    row->state->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    row->state->setFixedHeight(fontMetrics().lineSpacing() * 3);
    row->login = new QPushButton(QStringLiteral("Anmelden …"), idle);
    row->login->setObjectName(objectPrefix + QStringLiteral("Login"));
    row->login->setAutoDefault(false);
    row->logout = new QPushButton(QStringLiteral("Abmelden"), idle);
    row->logout->setObjectName(objectPrefix + QStringLiteral("Logout"));
    row->logout->setAutoDefault(false);
    auto *idleButtons = new QHBoxLayout;
    idleButtons->addWidget(row->login);
    idleButtons->addWidget(row->logout);
    idleButtons->addStretch(1);
    idleColumn->addWidget(row->state);
    idleColumn->addLayout(idleButtons);
    row->stack->addWidget(idle);

    // Seite 1: Anmeldung unterwegs, Code gross, Adresse von Twitch, Restzeit und «Abbrechen».
    auto *pending = new QWidget(row->stack);
    auto *pendingColumn = new QVBoxLayout(pending);
    pendingColumn->setContentsMargins(0, 0, 0, 0);
    row->code = new QLabel(pending);
    row->code->setObjectName(objectPrefix + QStringLiteral("Code"));
    row->code->setTextInteractionFlags(Qt::TextSelectableByMouse);
    row->uri = new QLabel(pending);
    row->uri->setObjectName(objectPrefix + QStringLiteral("Uri"));
    row->uri->setTextInteractionFlags(Qt::TextSelectableByMouse);
    row->open = new QPushButton(QStringLiteral("Im Browser öffnen"), pending);
    row->open->setObjectName(objectPrefix + QStringLiteral("Open"));
    row->open->setAutoDefault(false);
    row->copy = new QPushButton(QStringLiteral("Code kopieren"), pending);
    row->copy->setObjectName(objectPrefix + QStringLiteral("Copy"));
    row->copy->setAutoDefault(false);
    row->remaining = new QLabel(pending);
    row->remaining->setObjectName(objectPrefix + QStringLiteral("Remaining"));
    row->cancel = new QPushButton(QStringLiteral("Abbrechen"), pending);
    row->cancel->setObjectName(objectPrefix + QStringLiteral("Cancel"));
    row->cancel->setAutoDefault(false);
    auto *pendingButtons = new QHBoxLayout;
    pendingButtons->addWidget(row->open);
    pendingButtons->addWidget(row->copy);
    pendingButtons->addWidget(row->remaining);
    pendingButtons->addStretch(1);
    pendingButtons->addWidget(row->cancel);
    pendingColumn->addWidget(row->code);
    pendingColumn->addWidget(row->uri);
    if (account == QLatin1String("bot")) {
        auto *hint = new QLabel(dss::twitchBotLoginHint(), pending);
        hint->setObjectName(QStringLiteral("settingsTwitchBotHint"));
        hint->setWordWrap(true);
        pendingColumn->addWidget(hint);
    }
    pendingColumn->addLayout(pendingButtons);
    row->stack->addWidget(pending);

    connect(row->login, &QPushButton::clicked, this,
            [this, account] { sendTwitchRequest(dss::msg::kTwitchLoginStart, account); });
    connect(row->logout, &QPushButton::clicked, this,
            [this, account] { sendTwitchRequest(dss::msg::kTwitchLogout, account); });
    connect(row->cancel, &QPushButton::clicked, this,
            [this, account] { sendTwitchRequest(dss::msg::kTwitchLoginCancel, account); });
    connect(row->open, &QPushButton::clicked, this, [row] { QDesktopServices::openUrl(QUrl(row->uri->text())); });
    connect(row->copy, &QPushButton::clicked, this, [row] { QGuiApplication::clipboard()->setText(row->code->text()); });
    return row->stack;
}

void SettingsDialog::sendTwitchRequest(const char *type, const QString &account)
{
    m_problem->clear();
    m_twitchRequest = m_client->sendTwitchAccountRequest(type, account);
    updateState();
}

void SettingsDialog::onTwitchStatusChanged()
{
    const std::optional<dss::TwitchStatusInfo> status = m_client->twitchStatus();
    for (TwitchAccountRow *row : {&m_twitchChannel, &m_twitchBot}) {
        const dss::TwitchAccountView view = dss::twitchAccountView(status, row->account);
        row->state->setText(view.text);
        row->code->setText(view.userCode);
        row->uri->setText(view.verificationUri);
        row->expiresAt = view.expiresAt;
        row->stack->setCurrentIndex(view.loginPending ? 1 : 0);
    }
    m_twitchStatus->setText(dss::twitchStatusText(status));
    updateTwitchRemaining();
    updateState();
}

void SettingsDialog::updateTwitchRemaining()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (TwitchAccountRow *row : {&m_twitchChannel, &m_twitchBot}) {
        if (row->stack->currentIndex() == 1)
            row->remaining->setText(dss::twitchCodeRemainingText(row->expiresAt, now));
    }
}

void SettingsDialog::showFromCore()
{
    m_filled = false;
    m_pendingRequest = 0;
    m_pendingReset = false;
    m_deleteObsPassword = false;
    m_deleteTwitchSecret = false;
    m_twitchRequest = 0;
    m_problem->clear();
    m_client->requestSettings();
    onSettingsChanged();
    onTwitchStatusChanged();
    m_twitchTimer->start();
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
    values.twitchEnabled = m_twitchEnabled->isChecked();
    values.twitchClientId = m_twitchClientId->text();
    values.twitchClientSecret = m_twitchSecret->text();
    values.deleteTwitchClientSecret = m_deleteTwitchSecret;
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
            m_twitchEnabled->setChecked(info->settings.twitchEnabled);
            m_twitchClientId->setText(info->settings.twitchClientId);
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
    // P13: Ohne Twitch im Kern (Schalter DSS_CORE_TWITCH) bleibt der Bereich grau.
    const std::optional<dss::TwitchStatusInfo> twitch = m_client->twitchStatus();
    const bool twitchUsable = usable && m_pendingRequest == 0 && twitch && twitch->enabled;
    for (QWidget *w : std::initializer_list<QWidget *>{m_twitchEnabled, m_twitchClientId, m_twitchSecret})
        w->setEnabled(twitchUsable);
    m_twitchDeleteSecret->setEnabled(twitchUsable && info->twitchClientSecretSet && !m_deleteTwitchSecret);
    m_twitchSecret->setPlaceholderText(dss::twitchSecretPlaceholder(info, m_deleteTwitchSecret));
    const bool accountUsable = m_client->isConnected() && m_twitchRequest == 0;
    for (TwitchAccountRow *row : {&m_twitchChannel, &m_twitchBot}) {
        const dss::TwitchAccountView view = dss::twitchAccountView(twitch, row->account);
        row->login->setEnabled(accountUsable && view.loginEnabled);
        row->logout->setEnabled(accountUsable && view.logoutEnabled);
        row->cancel->setEnabled(accountUsable);
    }
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
