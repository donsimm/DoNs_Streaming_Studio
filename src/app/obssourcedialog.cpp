#include "obssourcedialog.h"

#include "coreclient.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

ObsSourceDialog::ObsSourceDialog(dss::CoreClient *client, QWidget *parent)
    : QDialog(parent)
    , m_client(client)
{
    setObjectName(QStringLiteral("obsSourceDialog"));
    auto *form = new QFormLayout(this);
    m_name = new QLineEdit(this);
    m_name->setObjectName(QStringLiteral("obsSourceName"));
    m_scene = new QComboBox(this);
    m_scene->setObjectName(QStringLiteral("obsSourceScene"));
    form->addRow(QStringLiteral("Name"), m_name);
    form->addRow(QStringLiteral("Szene"), m_scene);
    // Meldung des Kerns: Platz für drei Zeilen von Anfang an (Regel 9), wie im Dialog «Einstellungen»
    m_problem = new QLabel(this);
    m_problem->setObjectName(QStringLiteral("obsSourceProblem"));
    m_problem->setWordWrap(true);
    m_problem->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_problem->setFixedHeight(fontMetrics().lineSpacing() * 3);
    form->addRow(m_problem);
    auto *buttons = new QDialogButtonBox(this);
    m_ok = buttons->addButton(QStringLiteral("Anlegen"), QDialogButtonBox::AcceptRole);
    m_ok->setObjectName(QStringLiteral("obsSourceOk"));
    buttons->addButton(QStringLiteral("Abbrechen"), QDialogButtonBox::RejectRole);
    m_ok->setDefault(true);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &ObsSourceDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_name, &QLineEdit::textChanged, this, &ObsSourceDialog::updateState);
    connect(m_client, &dss::CoreClient::obsStatusChanged, this, [this] {
        refreshScenes();
        updateState();
    });
    connect(m_client, &dss::CoreClient::connectionStatesChanged, this, &ObsSourceDialog::updateState);
    connect(m_client, &dss::CoreClient::obsRequestFinished, this, &ObsSourceDialog::onResult);
    connect(m_client, &dss::CoreClient::connectedChanged, this, [this](bool connected) {
        if (!connected)
            m_pendingRequest = 0;
        updateState();
    });
}

void ObsSourceDialog::showFor(const QUuid &overlayId, const QString &name, bool addToScene)
{
    m_overlayId = overlayId;
    m_addToScene = addToScene;
    m_pendingRequest = 0;
    setWindowTitle(addToScene ? QStringLiteral("Quelle in weitere Szene einfügen") : QStringLiteral("Quelle in OBS anlegen"));
    m_ok->setText(addToScene ? QStringLiteral("Einfügen") : QStringLiteral("Anlegen"));
    m_name->setText(name);
    m_name->setReadOnly(addToScene);
    m_problem->clear();
    m_stateProblem.clear();
    // Vorgewählt ist die aktuelle Szene (F2).
    m_scene->clear();
    refreshScenes();
    const std::optional<dss::ObsStatusInfo> status = m_client->obsStatus();
    if (status) {
        const int current = m_scene->findData(status->currentScene.uuid);
        if (current >= 0)
            m_scene->setCurrentIndex(current);
    }
    updateState();
    open();
    if (!addToScene) {
        m_name->setFocus();
        m_name->selectAll();
    }
}

void ObsSourceDialog::refreshScenes()
{
    const std::optional<dss::ObsStatusInfo> status = m_client->obsStatus();
    const QString selected = m_scene->currentData().toString();
    m_scene->clear();
    if (status) {
        for (const dss::ObsSceneInfo &scene : status->scenes)
            m_scene->addItem(scene.name, scene.uuid);
    }
    const int index = m_scene->findData(selected);
    if (index >= 0)
        m_scene->setCurrentIndex(index);
}

void ObsSourceDialog::accept()
{
    const QString scene = m_scene->currentData().toString();
    m_problem->clear();
    m_pendingRequest = m_addToScene ? m_client->sendObsAddToScene(m_overlayId, scene)
                                    : m_client->sendObsCreateSource(m_overlayId, scene, m_name->text());
    updateState();
}

void ObsSourceDialog::onResult(const dss::ChangeResult &result)
{
    if (m_pendingRequest == 0 || result.requestId != m_pendingRequest)
        return;
    m_pendingRequest = 0;
    if (result.ok) {
        QDialog::accept();
        emit succeeded(m_addToScene ? QStringLiteral("Quelle «%1» in die Szene «%2» eingefügt").arg(m_name->text().trimmed(), m_scene->currentText())
                                    : QStringLiteral("Quelle «%1» in OBS angelegt").arg(m_name->text().trimmed()));
        return;
    }
    m_problem->setText(result.message);
    updateState();
}

void ObsSourceDialog::updateState()
{
    const bool connected = m_client->connectionStates().value(dss::Connection::Obs) == dss::ConnectionState::Connected;
    const QString problem = connected ? QString() : QStringLiteral("OBS ist nicht verbunden.");
    const bool idle = m_pendingRequest == 0;
    m_name->setEnabled(idle);
    m_scene->setEnabled(idle && connected);
    m_ok->setEnabled(idle && connected && m_scene->count() > 0 && !m_name->text().trimmed().isEmpty());
    // Wie im Dialog «Einstellungen»: Meldung des Zustands setzen bzw. wieder entfernen; eine Meldung
    // des Kerns zu einer Anfrage bleibt stehen.
    if (!problem.isEmpty() && m_problem->text().isEmpty())
        m_problem->setText(problem);
    else if (problem.isEmpty() && !m_stateProblem.isEmpty() && m_problem->text() == m_stateProblem)
        m_problem->clear();
    m_stateProblem = problem;
}
