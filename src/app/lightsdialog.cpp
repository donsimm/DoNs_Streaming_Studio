#include "lightsdialog.h"

#include "coreclient.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

enum Column { kNameColumn, kHostColumn, kStateColumn };

// Farbe über die Eigenschaft state im Theme (wie camerapage.cpp)
void setState(QWidget *w, const QString &state)
{
    if (w->property("state").toString() == state)
        return;
    w->setProperty("state", state);
    w->style()->unpolish(w);
    w->style()->polish(w);
}

} // namespace

LightsDialog::LightsDialog(dss::CoreClient *client, const dss::CameraData *data, QWidget *parent)
    : QDialog(parent), m_client(client), m_data(data)
{
    setObjectName(QStringLiteral("lightsDialog"));
    setWindowTitle(QStringLiteral("Lampen"));
    auto *column = new QVBoxLayout(this);
    column->setSpacing(6);

    m_list = new QTreeWidget(this);
    m_list->setObjectName(QStringLiteral("lightsList"));
    m_list->setAccessibleName(QStringLiteral("Lampen"));
    m_list->setColumnCount(3);
    m_list->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Adresse"), QStringLiteral("Zustand")});
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    const int digit = m_list->fontMetrics().horizontalAdvance(QLatin1Char('0'));
    m_list->header()->setStretchLastSection(true);
    m_list->setColumnWidth(kNameColumn, digit * 26);
    m_list->setColumnWidth(kHostColumn, digit * 26);
    m_list->setMinimumHeight(m_list->fontMetrics().lineSpacing() * 7);
    connect(m_list, &QTreeWidget::itemSelectionChanged, this, [this] {
        const int row = currentRow();
        const QSignalBlocker blockName(m_name);
        const QSignalBlocker blockHost(m_host);
        m_name->setText(row >= 0 ? m_entries.at(row).name : QString());
        m_host->setText(row >= 0 ? m_entries.at(row).host : QString());
        m_checkResult->clear();
        refresh();
    });
    column->addWidget(m_list, 1);

    // Name und Adresse der gewählten Lampe, direkt änderbar (Entwurf 8)
    auto *form = new QFormLayout;
    m_name = new QLineEdit(this);
    m_name->setObjectName(QStringLiteral("lightsName"));
    m_name->setMaxLength(40); // wie die Namen der Kameras
    m_host = new QLineEdit(this);
    m_host->setObjectName(QStringLiteral("lightsHost"));
    m_host->setPlaceholderText(QStringLiteral("Adresse oder Name, wahlweise mit «:Port»"));
    form->addRow(QStringLiteral("Name"), m_name);
    form->addRow(QStringLiteral("Adresse"), m_host);
    column->addLayout(form);
    connect(m_name, &QLineEdit::textEdited, this, [this](const QString &text) {
        const int row = currentRow();
        if (row < 0)
            return;
        m_entries[row].name = text;
        m_list->topLevelItem(row)->setText(kNameColumn, text);
    });
    connect(m_host, &QLineEdit::textEdited, this, [this](const QString &text) {
        const int row = currentRow();
        if (row < 0)
            return;
        m_entries[row].host = text;
        m_list->topLevelItem(row)->setText(kHostColumn, text);
        m_checkResult->clear();
        refresh();
    });

    m_add = new QPushButton(QStringLiteral("Hinzufügen"), this);
    m_add->setObjectName(QStringLiteral("lightsAdd"));
    m_remove = new QPushButton(QStringLiteral("Lampe löschen"), this);
    m_remove->setObjectName(QStringLiteral("lightsRemove"));
    m_check = new QPushButton(QStringLiteral("Verbindung prüfen"), this);
    m_check->setObjectName(QStringLiteral("lightsCheck"));
    m_checkResult = new QLabel(this);
    m_checkResult->setObjectName(QStringLiteral("lightsCheckResult"));
    m_checkResult->setTextFormat(Qt::PlainText);
    m_checkResult->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(6);
    buttonRow->addWidget(m_add);
    buttonRow->addWidget(m_remove);
    buttonRow->addWidget(m_check);
    buttonRow->addWidget(m_checkResult, 1);
    column->addLayout(buttonRow);
    connect(m_add, &QPushButton::clicked, this, [this] {
        m_entries.append({QString(), QStringLiteral("Neue Lampe"), QString()});
        fillList();
        m_list->setCurrentItem(m_list->topLevelItem(int(m_entries.size()) - 1));
        m_name->setFocus();
        m_name->selectAll();
    });
    connect(m_remove, &QPushButton::clicked, this, [this] {
        const int row = currentRow();
        if (row < 0)
            return;
        m_entries.removeAt(row);
        fillList();
    });
    connect(m_check, &QPushButton::clicked, this, [this] {
        const int row = currentRow();
        if (row < 0 || m_entries.at(row).id.isEmpty())
            return;
        m_checkId = m_entries.at(row).id;
        setState(m_checkResult, QString());
        m_checkResult->setText(QStringLiteral("Prüfe …"));
        m_checkRequest = m_client->callCamera(dss::cameraStatusGetCall());
    });

    auto *hint = new QLabel(QStringLiteral("Die Adresse der Lampe im Heimnetz, am besten am Router fest zugeteilt. "
                                           "Eine Suche im Netz gibt es nicht."),
                            this);
    hint->setObjectName(QStringLiteral("hintLabel"));
    hint->setWordWrap(true);
    column->addWidget(hint);

    // Meldung des Dienstes: Platz für zwei Zeilen von Anfang an (wie «Kamera einrichten», Regel 9)
    m_problem = new QLabel(this);
    m_problem->setObjectName(QStringLiteral("lightsProblem"));
    m_problem->setWordWrap(true);
    m_problem->setTextFormat(Qt::PlainText);
    m_problem->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_problem->setFixedHeight(fontMetrics().lineSpacing() * 2);
    column->addWidget(m_problem);

    auto *buttons = new QDialogButtonBox(this);
    m_ok = buttons->addButton(QStringLiteral("OK"), QDialogButtonBox::AcceptRole);
    m_ok->setObjectName(QStringLiteral("lightsOk"));
    m_ok->setDefault(true);
    buttons->addButton(QStringLiteral("Abbrechen"), QDialogButtonBox::RejectRole);
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &LightsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_client, &dss::CoreClient::cameraResultReceived, this, &LightsDialog::onResult);
    load();
    resize(fontMetrics().averageCharWidth() * 90, sizeHint().height());
}

void LightsDialog::load()
{
    m_loaded = false;
    m_exportRequest = m_client->callCamera(dss::cameraExportGetCall());
    if (m_exportRequest == 0)
        m_problem->setText(QStringLiteral("Keine Verbindung zum Kern"));
    refresh();
}

int LightsDialog::currentRow() const
{
    const QList<QTreeWidgetItem *> selected = m_list->selectedItems();
    return selected.isEmpty() ? -1 : m_list->indexOfTopLevelItem(selected.first());
}

void LightsDialog::fillList()
{
    const QSignalBlocker block(m_list);
    m_list->clear();
    for (const dss::LightEntry &e : std::as_const(m_entries))
        m_list->addTopLevelItem(new QTreeWidgetItem({e.name, e.host, QString()}));
    m_name->clear();
    m_host->clear();
    m_checkResult->clear();
    refresh();
}

void LightsDialog::refresh()
{
    // Zustand aus dem Stand des Dienstes; neue Lampen haben noch keinen
    for (int i = 0; i < m_list->topLevelItemCount() && i < m_entries.size(); ++i) {
        QString state = QStringLiteral("–");
        for (const QJsonValue &v : m_data->lights()) {
            const QJsonObject o = v.toObject();
            if (!m_entries.at(i).id.isEmpty() && o.value(QLatin1String("id")).toString() == m_entries.at(i).id)
                state = o.value(QLatin1String("reachable")).toBool() ? QStringLiteral("erreichbar")
                                                                      : QStringLiteral("nicht erreichbar");
        }
        m_list->topLevelItem(i)->setText(kStateColumn, state);
    }
    const bool idle = m_loaded && m_request == 0;
    const int row = currentRow();
    m_list->setEnabled(m_loaded);
    m_add->setEnabled(idle);
    m_name->setEnabled(idle && row >= 0);
    m_host->setEnabled(idle && row >= 0);
    m_remove->setEnabled(idle && row >= 0);
    // Prüfen geht nur mit der gespeicherten Adresse (der Dienst kennt nur sie)
    bool saved = false;
    if (row >= 0 && !m_entries.at(row).id.isEmpty()) {
        for (const dss::LightEntry &old : std::as_const(m_before)) {
            if (old.id == m_entries.at(row).id)
                saved = old.host == m_entries.at(row).host.trimmed();
        }
    }
    m_check->setEnabled(idle && saved && m_checkRequest == 0);
    if (row >= 0 && !saved && m_checkResult->text().isEmpty()) {
        setState(m_checkResult, QString());
        m_checkResult->setText(QStringLiteral("Prüfbar nach OK"));
    }
    m_ok->setEnabled(idle);
}

void LightsDialog::onResult(const dss::CameraCall &call, const dss::CameraResult &result)
{
    const QString error = dss::cameraResultError(result);
    if (call.requestId == m_exportRequest) {
        m_exportRequest = 0;
        if (!error.isEmpty()) {
            m_problem->setText(error);
            return;
        }
        m_before = dss::lightEntriesFromExport(result.body);
        m_entries = m_before;
        m_loaded = true;
        fillList();
    } else if (call.requestId == m_checkRequest) {
        m_checkRequest = 0;
        const auto [text, state] = dss::lightCheckResult(result, m_checkId);
        setState(m_checkResult, state);
        m_checkResult->setText(text);
        refresh();
    } else if (call.requestId == m_request) {
        m_request = 0;
        if (!error.isEmpty()) {
            // Was schon ging, bleibt; den Stand neu lesen
            m_queue.clear();
            m_problem->setText(error);
            load();
            return;
        }
        sendNext();
    }
}

void LightsDialog::accept()
{
    if (!m_loaded || m_request != 0)
        return;
    if (const QString problem = dss::lightsDialogProblem(m_entries); !problem.isEmpty()) {
        m_problem->setText(problem);
        return;
    }
    m_problem->clear();
    m_queue = dss::lightsDialogCalls(m_before, m_entries);
    sendNext();
}

void LightsDialog::sendNext()
{
    if (m_queue.isEmpty()) {
        QDialog::accept();
        return;
    }
    m_request = m_client->callCamera(m_queue.takeFirst());
    if (m_request == 0) {
        m_queue.clear();
        m_problem->setText(QStringLiteral("Keine Verbindung zum Kern, Änderung nicht geschickt"));
    }
    refresh();
}
