#include "queuesdialog.h"

#include "coreclient.h"
#include "dialogs.h"
#include "panels.h"

#include <QDialogButtonBox>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

constexpr int kQueueIdRole = Qt::UserRole; // Kennung der Warteschlange, null = «Standard»
constexpr int kNameColumn = 0;
constexpr int kBlockingColumn = 1;

} // namespace

QueuesDialog::QueuesDialog(dss::CoreClient *client, QWidget *parent)
    : QDialog(parent)
    , m_client(client)
{
    setObjectName(QStringLiteral("queuesDialog"));
    setWindowTitle(QStringLiteral("Warteschlangen"));
    auto *column = new QVBoxLayout(this);

    // Liste wie die Tabellen der Unterreiter (ListDetailPage): Spaltenkopf, feste Breite aus der
    // Schrift, letzte Spalte gedehnt. Doppelklick benennt nur um.
    m_list = new QTreeWidget(this);
    m_list->setObjectName(QStringLiteral("queuesList"));
    m_list->setAccessibleName(QStringLiteral("Warteschlangen"));
    m_list->setColumnCount(2);
    m_list->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Nacheinander")});
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setExpandsOnDoubleClick(false);
    m_list->header()->setStretchLastSection(true);
    m_list->setColumnWidth(kNameColumn, m_list->fontMetrics().horizontalAdvance(QLatin1Char('0')) * 24);
    connect(m_list, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *current) {
        if (m_filling)
            return;
        // Andere Zeile gewählt: Meldung leeren; nach dem Neufüllen kommt dieselbe Kennung wieder
        const QUuid id = current ? current->data(kNameColumn, kQueueIdRole).toUuid() : QUuid();
        if (current && (!m_hasSelection || id != m_selected) && m_problem->text() != m_fileError)
            m_problem->clear();
        m_hasSelection = current != nullptr;
        m_selected = id;
        updateButtons();
    });
    connect(m_list, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
        if (m_filling || column != kBlockingColumn)
            return;
        const std::optional<dss::ActionsConfigInfo> info = m_client->actionsConfig();
        if (!info || !info->config) {
            refresh();
            return;
        }
        const QUuid id = item->data(kNameColumn, kQueueIdRole).toUuid();
        runEdits(dss::setQueueBlocking(*info->config, id, item->checkState(kBlockingColumn) == Qt::Checked),
                 item->text(kNameColumn));
    });
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, &QueuesDialog::askRename);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QWidget::customContextMenuRequested, this, &QueuesDialog::showMenu);

    m_add = symbolButton(QStringLiteral("hinzufuegen.svg"), QStringLiteral("+"),
                         QStringLiteral("Warteschlange hinzufügen"), nullptr);
    m_add->setObjectName(QStringLiteral("queuesAdd"));
    m_remove = symbolButton(QStringLiteral("entfernen.svg"), QStringLiteral("–"),
                            QStringLiteral("Warteschlange löschen"), nullptr);
    m_remove->setObjectName(QStringLiteral("queuesRemove"));
    // Steuern kommt mit P14-2: bis dahin ausgegraut sichtbar (Entscheid des Nutzers, 2026-11-16)
    m_pause = new QPushButton(QStringLiteral("Anhalten"));
    m_pause->setObjectName(QStringLiteral("queuesPause"));
    m_resume = new QPushButton(QStringLiteral("Fortsetzen"));
    m_resume->setObjectName(QStringLiteral("queuesResume"));
    m_clear = new QPushButton(QStringLiteral("Leeren"));
    m_clear->setObjectName(QStringLiteral("queuesClear"));
    for (QPushButton *button : {m_pause, m_resume, m_clear}) {
        button->setAutoDefault(false);
        button->setEnabled(false);
    }
    connect(m_add, &QToolButton::clicked, this, [this] {
        const std::optional<dss::ActionsConfigInfo> info = m_client->actionsConfig();
        if (info && info->config)
            runEdits(dss::newQueue(*info->config), QString()); // wie «Befehl hinzufügen»: neue Zeile gewählt
    });
    connect(m_remove, &QToolButton::clicked, this, &QueuesDialog::askRemove);

    column->addWidget(m_list, 1);
    column->addWidget(buttonBar({m_add, m_remove}, {m_pause, m_resume, m_clear}, this));

    // Meldung des Kerns: Platz für drei Zeilen von Anfang an, damit das Setzen des Textes das
    // Layout nicht ändert (Regel 9, wie SettingsDialog).
    m_problem = new QLabel(this);
    m_problem->setObjectName(QStringLiteral("queuesProblem"));
    m_problem->setWordWrap(true);
    m_problem->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_problem->setFixedHeight(fontMetrics().lineSpacing() * 3);
    column->addWidget(m_problem);

    // Jede Änderung gilt sofort: nur «Schliessen» (Text deutsch wie in dialogs.h)
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    QPushButton *close = buttons->button(QDialogButtonBox::Close);
    close->setObjectName(QStringLiteral("queuesClose"));
    close->setText(QStringLiteral("Schliessen"));
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_client, &dss::CoreClient::actionsConfigChanged, this, &QueuesDialog::refresh);
    connect(m_client, &dss::CoreClient::connectedChanged, this, [this](bool connected) {
        if (!connected) {
            m_editRequest = 0;
            m_edits.clear();
        }
        refresh();
    });
    connect(m_client, &dss::CoreClient::changeFinished, this, &QueuesDialog::onChangeFinished);
    refresh();
}

void QueuesDialog::showFromCore()
{
    m_problem->clear();
    m_fileError.clear();
    m_wanted = QUuid();
    refresh();
    open();
}

void QueuesDialog::refresh()
{
    const std::optional<dss::ActionsConfigInfo> info = m_client->actionsConfig();
    const QList<dss::QueueRow> rows = info && info->config ? dss::queueRows(*info->config) : QList<dss::QueueRow>();
    QList<QUuid> before;
    for (const dss::QueueRow &row : std::as_const(m_rows))
        before.append(row.id);
    QList<QUuid> after;
    for (const dss::QueueRow &row : rows)
        after.append(row.id);

    m_filling = true;
    m_rows = rows;
    m_list->clear();
    QHash<QUuid, QTreeWidgetItem *> items;
    for (const dss::QueueRow &row : rows) {
        auto *item = new QTreeWidgetItem(m_list);
        item->setText(kNameColumn, row.name);
        item->setData(kNameColumn, kQueueIdRole, row.id);
        // «Standard» ohne Kästchen (blockingEditable aus der Logik)
        if (row.blockingEditable) {
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(kBlockingColumn, row.blocking ? Qt::Checked : Qt::Unchecked);
        }
        items.insert(row.id, item);
    }
    // Auswahl: neue Warteschlange, sonst dieselbe, sonst die nächste (keptSelection; null = «Standard»)
    QTreeWidgetItem *selectItem = nullptr;
    if (!m_wanted.isNull() && items.contains(m_wanted)) {
        selectItem = items.value(m_wanted);
        m_wanted = QUuid();
    } else if (m_hasSelection) {
        selectItem = items.contains(m_selected) ? items.value(m_selected)
                                                : items.value(dss::keptSelection(before, after, m_selected));
    }
    m_list->setCurrentItem(selectItem);
    m_filling = false;
    m_hasSelection = selectItem != nullptr;
    m_selected = selectItem ? selectItem->data(kNameColumn, kQueueIdRole).toUuid() : QUuid();

    // Datei ungültig: Der Kern weist Änderungen ab, der Grund steht in der Meldung. Gilt er nicht
    // mehr, nur diese Meldung löschen; eine Meldung einer Bearbeitung bleibt stehen.
    const QString fileError = info ? info->fileError : QString();
    if (fileError != m_fileError) {
        if (!fileError.isEmpty() || m_problem->text() == m_fileError)
            m_problem->setText(fileError);
        m_fileError = fileError;
    }
    updateButtons();
}

void QueuesDialog::updateButtons()
{
    const std::optional<dss::ActionsConfigInfo> info = m_client->actionsConfig();
    const bool editable =
        m_client->isConnected() && info && info->config && info->fileError.isEmpty() && m_editRequest == 0;
    m_list->setEnabled(m_client->isConnected() && info && info->config);
    m_add->setEnabled(editable);
    const dss::QueueRow *row = selectedQueue();
    m_remove->setEnabled(editable && row && row->removable);
}

const dss::QueueRow *QueuesDialog::selectedQueue() const
{
    if (!m_hasSelection)
        return nullptr;
    for (const dss::QueueRow &row : m_rows)
        if (row.id == m_selected)
            return &row;
    return nullptr;
}

void QueuesDialog::askRename(QTreeWidgetItem *item, int column)
{
    // Doppelklick auf den Namen wie «Gruppe umbenennen» in «Aktionen»
    const QUuid id = item->data(kNameColumn, kQueueIdRole).toUuid();
    const dss::QueueRow *row = selectedQueue();
    if (column != kNameColumn || !row || row->id != id)
        return;
    renameSelected();
}

void QueuesDialog::renameSelected()
{
    // «Standard» bleibt
    const dss::QueueRow *row = selectedQueue();
    if (!row || row->isDefault || !m_add->isEnabled())
        return;
    const QUuid id = row->id;
    const QString before = row->name;
    QString name = before;
    if (!askText(this, QStringLiteral("Warteschlange umbenennen"), QStringLiteral("Neuer Name"), &name)
        || name == before)
        return;
    // Stand nach dem Dialog: Während er offen war, kann eine neue Liste gekommen sein.
    const std::optional<dss::ActionsConfigInfo> now = m_client->actionsConfig();
    if (now && now->config)
        runEdits(dss::renameQueue(*now->config, id, name), name.trimmed());
}

void QueuesDialog::askRemove()
{
    const dss::QueueRow *found = selectedQueue();
    const std::optional<dss::ActionsConfigInfo> info = m_client->actionsConfig();
    if (!found || !info || !info->config)
        return;
    // Kopie: Während die Rückfrage offen ist, können neue Zeilen kommen.
    const dss::QueueRow row = *found;
    const QString question = dss::removeQueueQuestion(*info->config, row.id);
    if (!question.isEmpty()
        && !confirm(this, QStringLiteral("Warteschlange löschen?"), question, QStringLiteral("Löschen"),
                    QStringLiteral("Abbrechen")))
        return;
    const std::optional<dss::ActionsConfigInfo> now = m_client->actionsConfig();
    if (now && now->config)
        runEdits(dss::removeQueue(*now->config, row.id), row.name, true);
}

void QueuesDialog::showMenu(const QPoint &position)
{
    // Wie das Kontextmenü der Gruppen in «Aktionen»: ohne Kern (oder während einer Bearbeitung)
    // kein Menü, an «Standard» keines. Gleicher Weg wie Doppelklick und Knopf.
    if (!m_add->isEnabled())
        return;
    QTreeWidgetItem *item = m_list->itemAt(position);
    if (!item)
        return;
    const QUuid id = item->data(kNameColumn, kQueueIdRole).toUuid();
    if (id.isNull())
        return;
    m_list->setCurrentItem(item);
    QMenu menu(this);
    menu.addAction(QStringLiteral("Umbenennen …"), this, [this, id] {
        const dss::QueueRow *row = selectedQueue();
        if (row && row->id == id)
            renameSelected();
    });
    menu.addAction(QStringLiteral("Löschen …"), this, [this, id] {
        const dss::QueueRow *row = selectedQueue();
        if (row && row->id == id)
            askRemove();
    });
    menu.exec(m_list->viewport()->mapToGlobal(position));
}

void QueuesDialog::runEdits(const dss::EditResult &result, const QString &savedName, bool deleted)
{
    // Prüfmeldung im Dialog; eine neue Bearbeitung löscht die alte Meldung
    m_problem->setText(result.problem);
    if (!result.problem.isEmpty() || result.edits.isEmpty() || m_editRequest != 0) {
        // Abgewiesen, nichts zu tun oder eine Bearbeitung noch unterwegs: Liste wieder wie im Kern
        refresh();
        return;
    }
    m_edits = result.edits;
    m_editSaved = savedName;
    m_editDeleted = deleted;
    m_editCreated = result.created;
    sendNextEdit();
}

void QueuesDialog::sendNextEdit()
{
    m_editRequest = m_client->sendActionsEdit(m_edits.takeFirst());
    if (m_editRequest == 0) {
        m_edits.clear();
        refresh(); // nicht verbunden: Liste wieder wie im Kern
        return;
    }
    updateButtons();
}

void QueuesDialog::onChangeFinished(const dss::ChangeResult &result)
{
    // Antworten: beim ersten Fehler aufhören, Meldung im Dialog und wieder den Stand des Kerns zeigen;
    // nach der letzten «Gespeichert: <Name>» bzw. «Gelöscht: <Name>», ebenfalls im Dialog.
    if (m_editRequest == 0 || result.requestId != m_editRequest)
        return;
    m_editRequest = 0;
    if (!result.ok) {
        m_edits.clear();
        m_problem->setText(result.message);
        refresh();
        return;
    }
    if (!m_edits.isEmpty()) {
        sendNextEdit();
        return;
    }
    if (!m_editSaved.isEmpty())
        m_problem->setText(m_editDeleted ? dss::deletedStatusText(m_editSaved)
                                         : dss::savedStatusText(m_editSaved));
    if (!m_editCreated.isNull()) {
        m_wanted = m_editCreated;
        m_editCreated = QUuid();
    }
    refresh();
}
