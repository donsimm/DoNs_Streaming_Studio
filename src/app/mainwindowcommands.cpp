// P14 e3-5: Unterreiter «Befehle» im MainWindow (Checkliste P14 e3, Abschnitt 3 «Befehle»). Zeilen,
// Werte und Bearbeitungen nur über dss::actionview; das Fenster schickt die Bearbeitungen
// nacheinander an den Kern wie bei «Aktionen» (runActionEdits).

#include "mainwindow.h"

#include "chatbotpage.h"
#include "commandspage.h"
#include "coreclient.h"
#include "dialogs.h"
#include "listdetailpage.h"

#include <QSignalBlocker>

using dss::actions::Command;

void MainWindow::connectCommands()
{
    ListDetailPage *page = m_chatBot->commandsPage();
    CommandForm *form = m_chatBot->commandForm();
    connect(m_client, &dss::CoreClient::actionsConfigChanged, this, &MainWindow::refreshCommandList);
    connect(m_client, &dss::CoreClient::variablesChanged, this, &MainWindow::refreshCommandList);
    connect(m_client, &dss::CoreClient::actionsStatusChanged, this, &MainWindow::refreshCommandTest);
    connect(m_client, &dss::CoreClient::twitchStatusChanged, this, &MainWindow::refreshCommandTest);
    // Nach dem Wiederverbinden sind die Variablen leer (CoreClient): neu holen, wenn sichtbar
    connect(m_client, &dss::CoreClient::connectedChanged, this, [this](bool connected) {
        refreshCommandList();
        if (connected)
            requestShownVariables();
    });
    connect(m_chatBot, &ChatBotPage::subTabChanged, this, &MainWindow::requestShownVariables);
    // Ein beendeter Lauf kann Zähler geändert haben (Spalte «Benutzt»)
    connect(m_client, &dss::CoreClient::eventReceived, this, [this](const dss::events::Event &event) {
        if (event.type == QLatin1String("actions.run_finished"))
            requestShownVariables();
    });

    connect(page, &ListDetailPage::searchChanged, this, &MainWindow::refreshCommandList);
    connect(page, &ListDetailPage::filterChanged, this, &MainWindow::refreshCommandList);
    connect(page, &ListDetailPage::selectionChanged, this, [this] { refreshCommandDetail(); });
    connect(page, &ListDetailPage::enabledToggled, this, [this](const QUuid &id, bool enabled) {
        const std::optional<dss::ActionsConfigInfo> info = m_client->actionsConfig();
        const Command *before = info && info->config ? info->config->command(id) : nullptr;
        if (!before)
            return;
        Command after = *before;
        after.enabled = enabled;
        runCommandEdits(dss::putCommand(*before, after), before->name);
    });
    connect(page, &ListDetailPage::addClicked, this, [this] {
        const std::optional<dss::ActionsConfigInfo> info = m_client->actionsConfig();
        if (info && info->config)
            runCommandEdits(dss::newCommand(*info->config), QString());
    });
    connect(page, &ListDetailPage::copyClicked, this, [this] {
        const std::optional<dss::ActionsConfigInfo> info = m_client->actionsConfig();
        if (info && info->config)
            runCommandEdits(dss::copyCommand(*info->config, m_chatBot->commandsPage()->selectedRow()), QString());
    });
    connect(page, &ListDetailPage::removeClicked, this, [this] {
        // Kopie: Während die Rückfrage offen ist, kann eine neue Liste kommen.
        const std::optional<dss::ActionsConfigInfo> info = m_client->actionsConfig();
        const QUuid id = m_chatBot->commandsPage()->selectedRow();
        const Command *command = info && info->config ? info->config->command(id) : nullptr;
        if (!command)
            return;
        const QString name = command->name;
        const QString question = dss::removeCommandQuestion(*info->config, id);
        if (!question.isEmpty()
            && !confirm(this, QStringLiteral("Befehl löschen?"), question, QStringLiteral("Löschen"),
                        QStringLiteral("Abbrechen")))
            return;
        const std::optional<dss::ActionsConfigInfo> now = m_client->actionsConfig();
        if (now && now->config)
            runCommandEdits(dss::removeCommand(*now->config, id), name, CommandForm::NoSection, true);
    });

    // Formular rechts: jede Änderung als command_put des ganzen Befehls (putCommand)
    connect(form, &CommandForm::nameEdited, this, [this](const QString &name) {
        editShownCommand(CommandForm::CommandSection, [name](Command &c) { c.name = name; });
    });
    connect(form, &CommandForm::groupEdited, this, [this](const QString &group) {
        editShownCommand(CommandForm::CommandSection, [group](Command &c) { c.group = group; });
    });
    connect(form, &CommandForm::commandsEdited, this, [this](const QString &text) {
        editShownCommand(CommandForm::CommandSection, [text](Command &c) { c.commands = dss::commandsFromText(text); });
    });
    connect(form, &CommandForm::locationChosen, this, [this](const QString &location) {
        editShownCommand(CommandForm::CommandSection, [location](Command &c) { c.location = location; });
    });
    connect(form, &CommandForm::enabledToggled, this, [this](bool enabled) {
        editShownCommand(CommandForm::CommandSection, [enabled](Command &c) { c.enabled = enabled; });
    });
    connect(form, &CommandForm::permissionsEdited, this, [this](const dss::PermissionChoice &choice) {
        editShownCommand(CommandForm::PermissionSection, [choice](Command &c) {
            c.permissions = dss::permissionsFromChoice(c.permissions, choice);
        });
    });
    connect(form, &CommandForm::ignoreBotToggled, this, [this](bool ignore) {
        editShownCommand(CommandForm::PermissionSection, [ignore](Command &c) { c.ignoreBotAccount = ignore; });
    });
    connect(form, &CommandForm::globalCooldownEdited, this, [this](int seconds) {
        editShownCommand(CommandForm::CooldownSection, [seconds](Command &c) { c.globalCooldownS = seconds; });
    });
    connect(form, &CommandForm::userCooldownEdited, this, [this](int seconds) {
        editShownCommand(CommandForm::CooldownSection, [seconds](Command &c) { c.userCooldownS = seconds; });
    });
    connect(form, &CommandForm::actionChosen, this, [this](const QUuid &actionId) {
        const std::optional<dss::ActionsConfigInfo> info = m_client->actionsConfig();
        const QUuid id = m_chatBot->commandForm()->shownCommand();
        const Command *command = m_client->isConnected() && info && info->config ? info->config->command(id) : nullptr;
        if (command)
            runCommandEdits(dss::setCommandAction(*info->config, id, actionId), command->name, CommandForm::RunSection);
    });
    connect(form, &CommandForm::openActionClicked, this, &MainWindow::openAction);
    connect(form, &CommandForm::testChosen, this, &MainWindow::startCommandTest);

    // Antworten: beim ersten Fehler aufhören (die Meldung zeigt onChangeFinished, aus dem Formular
    // dazu unter dem Abschnitt) und wieder den Stand des Kerns zeigen; nach der letzten
    // «Gespeichert: <Name>» bzw. «Gelöscht: <Name>».
    connect(m_client, &dss::CoreClient::changeFinished, this, [this](const dss::ChangeResult &result) {
        if (m_commandEditRequest == 0 || result.requestId != m_commandEditRequest)
            return;
        m_commandEditRequest = 0;
        if (!result.ok) {
            m_commandEdits.clear();
            m_chatBot->commandForm()->setSectionProblem(m_commandEditSection, result.message);
            refreshCommandList();
            refreshCommandDetail(true);
            return;
        }
        if (!m_commandEdits.isEmpty()) {
            sendNextCommandEdit();
            return;
        }
        if (!m_commandEditCreated.isNull())
            m_chatBot->commandsPage()->selectRow(m_commandEditCreated);
        if (!m_commandEditSaved.isEmpty())
            showStatus(m_commandEditDeleted ? dss::deletedStatusText(m_commandEditSaved)
                                            : dss::savedStatusText(m_commandEditSaved));
    });
    // «▶ Testen als …»: je Aktion ein action_test (Probelauf); «Probelauf gestartet: <Name>» nach
    // der letzten Antwort, Ablehnungen zeigt onChangeFinished.
    connect(m_client, &dss::CoreClient::changeFinished, this, [this](const dss::ChangeResult &result) {
        if (m_commandTestRequest == 0 || result.requestId != m_commandTestRequest)
            return;
        m_commandTestRequest = 0;
        if (!result.ok) {
            m_commandTests.clear();
            return;
        }
        if (!m_commandTests.isEmpty()) {
            sendNextCommandTest();
            return;
        }
        showStatus(dss::testStartedStatusText(m_commandTestName, false));
    });
    refreshCommandList();
}

void MainWindow::refreshCommandList()
{
    ListDetailPage *page = m_chatBot->commandsPage();
    const std::optional<dss::ActionsConfigInfo> info = m_client->actionsConfig();
    const bool loaded = info && info->config;
    QStringList values{QString()};
    if (loaded)
        values = dss::commandGroupChoices(*info->config);
    QStringList labels;
    for (const QString &group : std::as_const(values))
        labels.append(dss::commandGroupLabel(group));
    {
        // Fehlt die gewählte Gruppe, gilt «Alle Gruppen»; die Zeilen kommen gleich unten
        const QSignalBlocker block(page);
        page->setFilterChoices(values, labels);
    }
    QList<ListDetailPage::Row> rows;
    if (loaded) {
        // [OFFEN] e2-14: Prüfmeldungen (checkReferences) fehlen noch wie in refreshActionList
        const std::optional<dss::VariablesInfo> variables = m_client->variables();
        const QList<dss::CommandRow> commandRows = dss::commandRows(
            *info->config, page->searchText(), page->filterValue(), variables ? &variables->persisted : nullptr, {});
        for (const dss::CommandRow &r : commandRows)
            rows.append({r.id, r.enabled,
                         {r.commands, r.group, r.permissions, r.globalCooldown, r.userCooldown, r.actions, r.used},
                         r.warning});
    }
    page->setHint(loaded ? dss::commandListHint(*info->config) : QString());
    page->setRows(rows); // danach selectionChanged → refreshCommandDetail
}

void MainWindow::refreshCommandDetail(bool resetFields)
{
    ListDetailPage *page = m_chatBot->commandsPage();
    const std::optional<dss::ActionsConfigInfo> info = m_client->actionsConfig();
    const bool editable = m_client->isConnected() && info && info->config;
    const Command *command = info && info->config ? info->config->command(page->selectedRow()) : nullptr;
    page->setListButtons(editable, editable && command);
    CommandForm::Detail detail;
    if (command) {
        const dss::actions::ActionsConfig &config = *info->config;
        detail.id = command->id;
        detail.editable = editable;
        detail.name = command->name;
        detail.group = command->group;
        detail.groups = dss::commandGroupChoices(config).mid(1); // ohne «Alle Gruppen»
        detail.commandsText = dss::commandsText(command->commands);
        detail.locations = dss::commandLocations();
        detail.location = command->location;
        detail.enabled = command->enabled;
        detail.permissions = dss::permissionChoice(command->permissions);
        detail.userGroups = dss::userGroupChoices(config, command->permissions);
        detail.ignoreBot = command->ignoreBotAccount;
        detail.globalCooldown = command->globalCooldownS;
        detail.userCooldown = command->userCooldownS;
        detail.actions = dss::actionChoices(config);
        detail.action = dss::commandAction(config, command->id);
        page->setDetailTitle(dss::commandTitle(*command));
    } else {
        page->setDetailTitle(QStringLiteral("Befehl"));
    }
    page->showDetail(command != nullptr);
    m_chatBot->commandForm()->setDetail(detail, resetFields);
    refreshCommandTest();
}

void MainWindow::refreshCommandTest()
{
    const std::optional<dss::ActionsConfigInfo> info = m_client->actionsConfig();
    const std::optional<dss::ActionsStatus> status = m_client->actionsStatus();
    QStringList users;
    for (const dss::TestUserChoice &user : dss::testUserChoices(m_client->twitchStatus().value_or(dss::TwitchStatusInfo())))
        users.append(user.label);
    const bool allowed = m_client->isConnected() && info && info->config && status
        && dss::commandTestAllowed(*info->config, m_chatBot->commandForm()->shownCommand(), *status);
    m_chatBot->commandForm()->setTestState(users, allowed);
}

void MainWindow::runCommandEdits(const dss::EditResult &result, const QString &savedName,
                                 CommandForm::Section section, bool deleted)
{
    // Aus dem Formular: Prüfmeldung unter dem Abschnitt; eine neue Bearbeitung löscht die alte Meldung
    m_chatBot->commandForm()->setSectionProblem(section, result.problem);
    if (!result.problem.isEmpty()) {
        if (section == CommandForm::NoSection)
            showStatus(result.problem);
        refreshCommandList();
        refreshCommandDetail(true);
        return;
    }
    if (result.edits.isEmpty() || m_commandEditRequest != 0) {
        // Nichts zu tun oder eine Bearbeitung noch unterwegs: Kästchen und Felder wieder wie im Kern
        refreshCommandList();
        refreshCommandDetail(true);
        return;
    }
    m_commandEdits = result.edits;
    m_commandEditCreated = result.created;
    m_commandEditSection = section;
    m_commandEditDeleted = deleted;
    // Ohne Namen (neuer oder kopierter Befehl): der Name aus command_put
    m_commandEditSaved = savedName.isEmpty()
        ? result.edits.first().fields.value(QStringLiteral("command")).toObject().value(QStringLiteral("name")).toString()
        : savedName;
    sendNextCommandEdit();
}

void MainWindow::sendNextCommandEdit()
{
    m_commandEditRequest = m_client->sendActionsEdit(m_commandEdits.takeFirst());
    if (m_commandEditRequest == 0)
        m_commandEdits.clear();
}

void MainWindow::editShownCommand(CommandForm::Section section, const std::function<void(Command &)> &change)
{
    const std::optional<dss::ActionsConfigInfo> info = m_client->actionsConfig();
    if (!m_client->isConnected() || !info || !info->config)
        return;
    const Command *before = info->config->command(m_chatBot->commandForm()->shownCommand());
    if (!before)
        return;
    Command after = *before;
    change(after);
    // Nach dem Umbenennen steht der neue Name in der Statuszeile
    const QString name = after.name.trimmed().isEmpty() ? before->name : after.name.trimmed();
    runCommandEdits(dss::putCommand(*before, after), name, section);
}

void MainWindow::startCommandTest(int userIndex)
{
    // Alte Prüfzeile weg, auch wenn der Test gleich abbricht (Befund e3-5b)
    m_chatBot->commandForm()->setSectionProblem(CommandForm::RunSection, QString());
    const std::optional<dss::ActionsConfigInfo> info = m_client->actionsConfig();
    const std::optional<dss::ActionsStatus> status = m_client->actionsStatus();
    const QUuid id = m_chatBot->commandForm()->shownCommand();
    const Command *command =
        m_client->isConnected() && info && info->config && status ? info->config->command(id) : nullptr;
    if (!command || !dss::commandTestAllowed(*info->config, id, *status) || m_commandTestRequest != 0)
        return;
    const QList<dss::TestUserChoice> users =
        dss::testUserChoices(m_client->twitchStatus().value_or(dss::TwitchStatusInfo()));
    if (userIndex < 0 || userIndex >= users.size())
        return;
    // Immer Probelauf (Entscheid des Nutzers, 2026-11-16); ohne passende Aktion die Prüfmeldung
    // unter «Ausführen»
    const dss::EditResult result = dss::testCommand(*info->config, id, users.at(userIndex), false);
    m_chatBot->commandForm()->setSectionProblem(CommandForm::RunSection, result.problem);
    if (!result.problem.isEmpty() || result.edits.isEmpty())
        return;
    m_commandTests = result.edits;
    m_commandTestName = command->name;
    sendNextCommandTest();
}

void MainWindow::sendNextCommandTest()
{
    m_commandTestRequest = m_client->sendActionsEdit(m_commandTests.takeFirst());
    if (m_commandTestRequest == 0)
        m_commandTests.clear();
}

void MainWindow::requestShownVariables()
{
    if (m_client && m_chatBot && m_client->isConnected() && m_chatBot->commandsPage()->isVisible())
        m_client->requestVariables();
}
