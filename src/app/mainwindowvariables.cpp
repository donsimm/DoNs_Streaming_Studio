// P14 e3-7: Unterreiter «Variablen» im MainWindow (Checkliste P14 e3, Abschnitt 3 «Variablen»). Zeilen,
// Prüfmeldungen und Bearbeitungen nur über dss::actionview; die Bearbeitungen gehen nacheinander an
// den Kern wie bei «Befehle» (mainwindowcommands.cpp). Nach einem erfolgreichen variable_set oder
// variable_remove holt der CoreClient die Variablen selbst neu, das Fenster nicht zusätzlich.

#include "mainwindow.h"

#include "chatbotpage.h"
#include "coreclient.h"
#include "dialogs.h"
#include "variablespage.h"

#include <QSignalBlocker>

namespace {

// Kennung einer Zeile: gleicher Name kann dauerhaft und flüchtig vorkommen
QString variableKey(const dss::VariableRow &row)
{
    return (row.persisted ? QStringLiteral("p/") : QStringLiteral("f/")) + row.userId + QLatin1Char('/') + row.name;
}

const dss::VariableRow *findVariableRow(const QList<dss::VariableRow> &rows, const QString &key)
{
    for (const dss::VariableRow &row : rows)
        if (variableKey(row) == key)
            return &row;
    return nullptr;
}

} // namespace

void MainWindow::connectVariables()
{
    VariablesPage *page = m_chatBot->variablesPage();
    // Holen beim Zeigen, nach actions.run_finished und nach dem Wiederverbinden: requestShownVariables
    // (mainwindowcommands.cpp); hier nur neu zeichnen. Die Zuschauer kommen nach den Variablen.
    connect(m_client, &dss::CoreClient::variablesChanged, this, &MainWindow::refreshVariables);
    connect(m_client, &dss::CoreClient::viewersChanged, this, &MainWindow::refreshVariables);
    connect(m_client, &dss::CoreClient::connectedChanged, this, &MainWindow::refreshVariables);
    connect(page, &VariablesPage::searchChanged, this, &MainWindow::refreshVariables);
    connect(page, &VariablesPage::filterChanged, this, &MainWindow::refreshVariables);
    connect(page, &VariablesPage::selectionChanged, this, [this](VariablesPage::Area area) {
        // Andere Zeile gewählt: Prüfzeile leeren wie bei «Befehle»; selectionChanged kommt auch nach
        // jedem Neufüllen, darum nur bei echtem Wechsel. Die Meldung aus fileError bleibt stehen.
        VariablesPage *page = m_chatBot->variablesPage();
        const QString key = page->selectedRow(area);
        if (key != m_variableSelection[area]) {
            m_variableSelection[area] = key;
            if (m_variableFileError.isEmpty() || page->problem(area) != m_variableFileError)
                page->setProblem(area, QString());
        }
        refreshVariableButtons();
    });

    connect(page, &VariablesPage::valueEdited, this,
            [this](VariablesPage::Area area, const QString &key, const QString &text) {
                const dss::VariableRow *row = findVariableRow(m_variableRows[area], key);
                if (!row) {
                    refreshVariables();
                    return;
                }
                const dss::VariableRow edited = *row;
                runVariableEdits(dss::setVariableValue(edited, text), area, edited.name);
            });
    connect(page, &VariablesPage::removeClicked, this, [this](VariablesPage::Area area) {
        // Kopie: Während die Rückfrage offen ist, können neue Zeilen kommen.
        const dss::VariableRow *found =
            findVariableRow(m_variableRows[area], m_chatBot->variablesPage()->selectedRow(area));
        if (!found)
            return;
        const dss::VariableRow row = *found;
        if (!confirm(this, QStringLiteral("Variable löschen?"), dss::removeVariableQuestion(row),
                     QStringLiteral("Löschen"), QStringLiteral("Abbrechen")))
            return;
        const std::optional<dss::VariablesInfo> info = m_client->variables();
        if (info)
            runVariableEdits(dss::removeVariable(info->persisted, info->volatileGlobal, info->volatileUsers, row),
                             area, row.name, true);
    });

    // Antworten: beim ersten Fehler aufhören, Meldung unter der Tabelle und wieder den Stand des
    // Kerns zeigen; nach der letzten «Gespeichert: <Name>» bzw. «Gelöscht: <Name>».
    connect(m_client, &dss::CoreClient::changeFinished, this, [this](const dss::ChangeResult &result) {
        if (m_variableEditRequest == 0 || result.requestId != m_variableEditRequest)
            return;
        m_variableEditRequest = 0;
        if (!result.ok) {
            m_variableEdits.clear();
            m_chatBot->variablesPage()->setProblem(VariablesPage::Area(m_variableEditArea), result.message);
            refreshVariables();
            return;
        }
        if (!m_variableEdits.isEmpty()) {
            sendNextVariableEdit();
            return;
        }
        showStatus(m_variableEditDeleted ? dss::deletedStatusText(m_variableEditSaved)
                                         : dss::savedStatusText(m_variableEditSaved));
    });
    refreshVariables();
}

void MainWindow::refreshVariables()
{
    VariablesPage *page = m_chatBot->variablesPage();
    const std::optional<dss::VariablesInfo> info = m_client->variables();
    QStringList names{QString()};
    if (info)
        names = dss::userVariableNames(info->persisted, info->volatileUsers);
    QStringList labels;
    for (const QString &name : names)
        labels << dss::variableNameLabel(name);
    {
        // Kein filterChanged: Die Zeilen folgen hier ohnehin
        const QSignalBlocker blocker(page);
        page->setFilterChoices(names, labels);
    }

    m_variableRows[VariablesPage::Global].clear();
    m_variableRows[VariablesPage::User].clear();
    if (info) {
        m_variableRows[VariablesPage::Global] =
            dss::globalVariableRows(info->persisted, info->volatileGlobal, page->searchText(VariablesPage::Global));
        m_variableRows[VariablesPage::User] =
            dss::userVariableRows(info->persisted, info->volatileUsers, m_client->viewers(),
                                  page->searchText(VariablesPage::User), page->filterValue());
    }
    page->setEditable(m_client->isConnected() && info && info->fileError.isEmpty());
    // Datei ungültig: Der Kern weist Änderungen ab; der Grund steht unter beiden Tabellen. Gilt er
    // nicht mehr, nur diese Meldung löschen; eine Meldung einer Bearbeitung bleibt stehen.
    const QString fileError = info ? info->fileError : QString();
    if (fileError != m_variableFileError) {
        for (const auto area : {VariablesPage::Global, VariablesPage::User})
            if (!fileError.isEmpty() || page->problem(area) == m_variableFileError)
                page->setProblem(area, fileError);
        m_variableFileError = fileError;
    }

    QList<VariablesPage::Row> global;
    for (const dss::VariableRow &r : m_variableRows[VariablesPage::Global])
        global.append({variableKey(r), {r.name, r.valueText, r.persistedText, r.origin}});
    QList<VariablesPage::Row> user;
    for (const dss::VariableRow &r : m_variableRows[VariablesPage::User])
        user.append({variableKey(r), {r.viewer, r.name, r.valueText, r.origin}});
    page->setRows(VariablesPage::Global, global); // danach selectionChanged → refreshVariableButtons
    page->setRows(VariablesPage::User, user);
    refreshVariableButtons();
}

void MainWindow::refreshVariableButtons()
{
    VariablesPage *page = m_chatBot->variablesPage();
    const std::optional<dss::VariablesInfo> info = m_client->variables();
    for (int i = 0; i < 2; ++i) {
        const auto area = VariablesPage::Area(i);
        const dss::VariableRow *row = findVariableRow(m_variableRows[area], page->selectedRow(area));
        // Frei nur, wenn removeVariable die gewählte Variable löschen würde
        page->setRemoveEnabled(area, m_client->isConnected() && info && info->fileError.isEmpty() && row
                                         && !dss::removeVariable(info->persisted, info->volatileGlobal,
                                                                 info->volatileUsers, *row).edits.isEmpty());
    }
}

void MainWindow::runVariableEdits(const dss::EditResult &result, int area, const QString &savedName, bool deleted)
{
    // Prüfmeldung unter der Tabelle; eine neue Bearbeitung löscht die alte Meldung
    m_chatBot->variablesPage()->setProblem(VariablesPage::Area(area), result.problem);
    if (!result.problem.isEmpty() || result.edits.isEmpty() || m_variableEditRequest != 0) {
        // Abgewiesen, nichts zu tun oder eine Bearbeitung noch unterwegs: Zelle wieder wie im Kern
        refreshVariables();
        return;
    }
    m_variableEdits = result.edits;
    m_variableEditArea = area;
    m_variableEditSaved = savedName;
    m_variableEditDeleted = deleted;
    sendNextVariableEdit();
}

void MainWindow::sendNextVariableEdit()
{
    m_variableEditRequest = m_client->sendActionsEdit(m_variableEdits.takeFirst());
    if (m_variableEditRequest == 0) {
        m_variableEdits.clear();
        refreshVariables(); // nicht verbunden: Zelle wieder wie im Kern
    }
}
