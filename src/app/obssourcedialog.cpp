#include "obssourcedialog.h"

#include "coreclient.h"
#include "windowdata.h"

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
    // Meldung des Kerns: Platz für drei gesetzte Zeilen von Anfang an (Regel 9)
    m_problem = new QLabel(this);
    m_problem->setObjectName(QStringLiteral("obsSourceProblem"));
    m_problem->setWordWrap(true);
    m_problem->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_problem->setFixedHeight(fontMetrics().size(0, QStringLiteral("a\nb\nc")).height());
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
    m_scene->clear();
    m_selectedScene.clear();
    refreshScenes();
    updateState();
    open();
    if (addToScene) {
        m_scene->setFocus();
    } else {
        m_name->setFocus();
        m_name->selectAll();
    }
}

void ObsSourceDialog::refreshScenes()
{
    const std::optional<dss::ObsStatusInfo> status = m_client->obsStatus();
    // Ohne OBS ist die Liste leer; die Wahl bleibt für das Wiederverbinden gemerkt.
    if (m_scene->count() > 0)
        m_selectedScene = m_scene->currentData().toString();
    m_scene->clear();
    if (status) {
        for (const dss::ObsSceneInfo &scene : status->scenes)
            m_scene->addItem(scene.name, scene.uuid);
    }
    int index = m_scene->findData(m_selectedScene);
    // Vorgewählt ist die aktuelle Szene (F2).
    if (index < 0 && status)
        index = m_scene->findData(status->currentScene.uuid);
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
    const dss::ObsSourceDialogState state = dss::obsSourceDialogState(
        m_client->isConnected(), m_client->connectionStates().value(dss::Connection::Obs), m_scene->count(),
        m_name->text(), m_pendingRequest != 0);
    m_name->setEnabled(state.canEdit);
    m_scene->setEnabled(state.canEdit);
    m_ok->setEnabled(state.canSubmit);
    // Meldung des Zustands setzen, ersetzen oder entfernen. Anders als im Dialog «Einstellungen» ersetzt
    // sie eine Meldung des Kerns nicht: «Verbindung zu OBS abgebrochen …» kommt zusammen mit dem
    // Zustand und sagt, was aus der Anfrage wurde.
    if (m_problem->text().isEmpty() || m_problem->text() == m_stateProblem)
        m_problem->setText(state.problem);
    m_stateProblem = state.problem;
}
