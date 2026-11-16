// P14 e3-6: Unterreiter «Zeitnachrichten» im MainWindow (Checkliste P14 e3, Abschnitt 3
// «Zeitnachrichten»). Wie «Befehle» (mainwindowcommands.cpp): Zeilen, Werte und Bearbeitungen nur
// über dss::actionview, die Bearbeitungen gehen nacheinander an den Kern.

#include "mainwindow.h"

#include "chatbotpage.h"
#include "coreclient.h"
#include "dialogs.h"
#include "listdetailpage.h"
#include "timerspage.h"

using dss::actions::Timer;

void MainWindow::connectTimers()
{
    ListDetailPage *page = m_chatBot->timersPage();
    TimerForm *form = m_chatBot->timerForm();
    connect(m_client, &dss::CoreClient::actionsConfigChanged, this, &MainWindow::refreshTimerList);
    connect(m_client, &dss::CoreClient::connectedChanged, this, &MainWindow::refreshTimerList);

    connect(page, &ListDetailPage::selectionChanged, this, [this] { refreshTimerDetail(); });
    connect(page, &ListDetailPage::enabledToggled, this, [this](const QUuid &id, bool enabled) {
        const std::optional<dss::ActionsConfigInfo> info = m_client->actionsConfig();
        const Timer *before = info && info->config ? info->config->timer(id) : nullptr;
        if (!before)
            return;
        Timer after = *before;
        after.enabled = enabled;
        runTimerEdits(dss::putTimer(*before, after), before->name);
    });
    connect(page, &ListDetailPage::addClicked, this, [this] {
        const std::optional<dss::ActionsConfigInfo> info = m_client->actionsConfig();
        if (info && info->config)
            runTimerEdits(dss::newTimer(*info->config), QString());
    });
    connect(page, &ListDetailPage::copyClicked, this, [this] {
        const std::optional<dss::ActionsConfigInfo> info = m_client->actionsConfig();
        if (info && info->config)
            runTimerEdits(dss::copyTimer(*info->config, m_chatBot->timersPage()->selectedRow()), QString());
    });
    connect(page, &ListDetailPage::removeClicked, this, [this] {
        // Kopie: Während die Rückfrage offen ist, kann eine neue Liste kommen.
        const std::optional<dss::ActionsConfigInfo> info = m_client->actionsConfig();
        const QUuid id = m_chatBot->timersPage()->selectedRow();
        const Timer *timer = info && info->config ? info->config->timer(id) : nullptr;
        if (!timer)
            return;
        const QString name = timer->name;
        const QString question = dss::removeTimerQuestion(*info->config, id);
        if (!question.isEmpty()
            && !confirm(this, QStringLiteral("Zeitnachricht löschen?"), question, QStringLiteral("Löschen"),
                        QStringLiteral("Abbrechen")))
            return;
        const std::optional<dss::ActionsConfigInfo> now = m_client->actionsConfig();
        if (now && now->config)
            runTimerEdits(dss::removeTimer(*now->config, id), name, TimerForm::NoSection, true);
    });

    // Formular rechts: jede Änderung als timer_put der ganzen Zeitnachricht (putTimer)
    connect(form, &TimerForm::nameEdited, this, [this](const QString &name) {
        editShownTimer(TimerForm::TimerSection, [name](Timer &t) { t.name = name; });
    });
    connect(form, &TimerForm::enabledToggled, this, [this](bool enabled) {
        editShownTimer(TimerForm::TimerSection, [enabled](Timer &t) { t.enabled = enabled; });
    });
    connect(form, &TimerForm::repeatToggled, this, [this](bool repeat) {
        editShownTimer(TimerForm::TimerSection, [repeat](Timer &t) { t.repeat = repeat; });
    });
    connect(form, &TimerForm::intervalEdited, this, [this](int seconds) {
        editShownTimer(TimerForm::IntervalSection, [seconds](Timer &t) { t.intervalS = seconds; });
    });
    connect(form, &TimerForm::randomToggled, this, [this](bool random) {
        editShownTimer(TimerForm::IntervalSection, [random](Timer &t) { dss::setTimerRandom(t, random); });
    });
    connect(form, &TimerForm::randomMinEdited, this, [this](int seconds) {
        editShownTimer(TimerForm::IntervalSection, [seconds](Timer &t) { t.randomMinS = seconds; });
    });
    connect(form, &TimerForm::randomMaxEdited, this, [this](int seconds) {
        editShownTimer(TimerForm::IntervalSection, [seconds](Timer &t) { t.randomMaxS = seconds; });
    });
    connect(form, &TimerForm::linesEdited, this, [this](int lines) {
        editShownTimer(TimerForm::IntervalSection, [lines](Timer &t) { t.lines = lines; });
    });
    connect(form, &TimerForm::onlyLiveToggled, this, [this](bool onlyLive) {
        editShownTimer(TimerForm::IntervalSection, [onlyLive](Timer &t) { t.onlyLive = onlyLive; });
    });
    connect(form, &TimerForm::actionChosen, this, [this](const QUuid &actionId) {
        const std::optional<dss::ActionsConfigInfo> info = m_client->actionsConfig();
        const QUuid id = m_chatBot->timerForm()->shownTimer();
        const Timer *timer = m_client->isConnected() && info && info->config ? info->config->timer(id) : nullptr;
        if (timer)
            runTimerEdits(dss::setTimerAction(*info->config, id, actionId), timer->name, TimerForm::RunSection);
    });
    connect(form, &TimerForm::openActionClicked, this, &MainWindow::openAction);

    // Antworten: beim ersten Fehler aufhören (die Meldung zeigt onChangeFinished, aus dem Formular
    // dazu unter dem Abschnitt) und wieder den Stand des Kerns zeigen; nach der letzten
    // «Gespeichert: <Name>» bzw. «Gelöscht: <Name>».
    connect(m_client, &dss::CoreClient::changeFinished, this, [this](const dss::ChangeResult &result) {
        if (m_timerEditRequest == 0 || result.requestId != m_timerEditRequest)
            return;
        m_timerEditRequest = 0;
        if (!result.ok) {
            m_timerEdits.clear();
            m_chatBot->timerForm()->setSectionProblem(m_timerEditSection, result.message);
            refreshTimerList();
            refreshTimerDetail(true);
            return;
        }
        if (!m_timerEdits.isEmpty()) {
            sendNextTimerEdit();
            return;
        }
        if (!m_timerEditCreated.isNull())
            m_chatBot->timersPage()->selectRow(m_timerEditCreated);
        if (!m_timerEditSaved.isEmpty())
            showStatus(m_timerEditDeleted ? dss::deletedStatusText(m_timerEditSaved)
                                          : dss::savedStatusText(m_timerEditSaved));
    });
    refreshTimerList();
}

void MainWindow::refreshTimerList()
{
    const std::optional<dss::ActionsConfigInfo> info = m_client->actionsConfig();
    QList<ListDetailPage::Row> rows;
    if (info && info->config) {
        // [OFFEN] e2-14: Prüfmeldungen (checkReferences) fehlen noch wie in refreshActionList
        for (const dss::TimerRow &r : dss::timerRows(*info->config, {}))
            rows.append({r.id, r.enabled, {r.name, r.interval, r.lines, r.onlyLive, r.actions}, r.warning});
    }
    m_chatBot->timersPage()->setRows(rows); // danach selectionChanged → refreshTimerDetail
}

void MainWindow::refreshTimerDetail(bool resetFields)
{
    ListDetailPage *page = m_chatBot->timersPage();
    const std::optional<dss::ActionsConfigInfo> info = m_client->actionsConfig();
    const bool editable = m_client->isConnected() && info && info->config;
    const Timer *timer = info && info->config ? info->config->timer(page->selectedRow()) : nullptr;
    page->setListButtons(editable, editable && timer);
    TimerForm::Detail detail;
    if (timer) {
        detail.id = timer->id;
        detail.editable = editable;
        detail.name = timer->name;
        detail.enabled = timer->enabled;
        detail.repeat = timer->repeat;
        detail.intervalS = timer->intervalS;
        detail.random = timer->random;
        detail.randomMinS = timer->randomMinS;
        detail.randomMaxS = timer->randomMaxS;
        detail.lines = timer->lines;
        detail.onlyLive = timer->onlyLive;
        detail.actions = dss::actionChoices(*info->config);
        detail.action = dss::timerAction(*info->config, timer->id);
        page->setDetailTitle(dss::timerTitle(*timer));
    } else {
        page->setDetailTitle(QStringLiteral("Zeitnachricht"));
    }
    page->showDetail(timer != nullptr);
    m_chatBot->timerForm()->setDetail(detail, resetFields);
}

void MainWindow::runTimerEdits(const dss::EditResult &result, const QString &savedName, TimerForm::Section section,
                               bool deleted)
{
    // Aus dem Formular: Prüfmeldung unter dem Abschnitt; eine neue Bearbeitung löscht die alte Meldung
    m_chatBot->timerForm()->setSectionProblem(section, result.problem);
    if (!result.problem.isEmpty()) {
        if (section == TimerForm::NoSection)
            showStatus(result.problem);
        refreshTimerList();
        refreshTimerDetail(true);
        return;
    }
    if (result.edits.isEmpty() || m_timerEditRequest != 0) {
        // Nichts zu tun oder eine Bearbeitung noch unterwegs: Kästchen und Felder wieder wie im Kern
        refreshTimerList();
        refreshTimerDetail(true);
        return;
    }
    m_timerEdits = result.edits;
    m_timerEditCreated = result.created;
    m_timerEditSection = section;
    m_timerEditDeleted = deleted;
    // Ohne Namen (neue oder kopierte Zeitnachricht): der Name aus timer_put
    m_timerEditSaved = savedName.isEmpty()
        ? result.edits.first().fields.value(QStringLiteral("timer")).toObject().value(QStringLiteral("name")).toString()
        : savedName;
    sendNextTimerEdit();
}

void MainWindow::sendNextTimerEdit()
{
    m_timerEditRequest = m_client->sendActionsEdit(m_timerEdits.takeFirst());
    if (m_timerEditRequest == 0)
        m_timerEdits.clear();
}

void MainWindow::editShownTimer(TimerForm::Section section, const std::function<void(Timer &)> &change)
{
    const std::optional<dss::ActionsConfigInfo> info = m_client->actionsConfig();
    if (!m_client->isConnected() || !info || !info->config)
        return;
    const Timer *before = info->config->timer(m_chatBot->timerForm()->shownTimer());
    if (!before)
        return;
    Timer after = *before;
    change(after);
    // Nach dem Umbenennen steht der neue Name in der Statuszeile
    const QString name = after.name.trimmed().isEmpty() ? before->name : after.name.trimmed();
    runTimerEdits(dss::putTimer(*before, after), name, section);
}
