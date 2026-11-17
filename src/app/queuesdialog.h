#pragma once

#include "actionview.h"

#include <QDialog>
#include <QList>
#include <QUuid>

class QLabel;
class QPushButton;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace dss {
class CoreClient;
struct ChangeResult;
}

// Dialog «Warteschlangen» (P14 e3-8, Checkliste P14 e3, Abschnitt 3), geöffnet über «Warteschlangen
// verwalten …» in «Aktionen». Zeilen, Prüfmeldungen und Bearbeitungen nur über dss::actionview
// (queueRows, newQueue, renameQueue, setQueueBlocking, removeQueue); jede Änderung geht sofort an den
// Kern (Entscheid des Nutzers, 2026-11-16), unten steht nur «Schliessen». Die Meldung des Kerns
// steht im Dialog, nicht in der Statuszeile (wie SettingsDialog, Punkt 76). Nicht blockierend (open()).
class QueuesDialog : public QDialog
{
    Q_OBJECT

public:
    QueuesDialog(dss::CoreClient *client, QWidget *parent = nullptr);

    // Zeigt den Stand des Kerns und den Dialog.
    void showFromCore();

private:
    void refresh();
    void updateButtons();
    const dss::QueueRow *selectedQueue() const;
    void askRename(QTreeWidgetItem *item, int column);
    void renameSelected();
    void askRemove();
    void showMenu(const QPoint &position);
    void runEdits(const dss::EditResult &result, const QString &savedName, bool deleted = false);
    void sendNextEdit();
    void onChangeFinished(const dss::ChangeResult &result);

    dss::CoreClient *m_client;
    QTreeWidget *m_list = nullptr;
    QToolButton *m_add = nullptr;
    QToolButton *m_remove = nullptr;
    QPushButton *m_pause = nullptr; // «Anhalten», «Fortsetzen», «Leeren»: ausgegraut bis P14-2
    QPushButton *m_resume = nullptr;
    QPushButton *m_clear = nullptr;
    QLabel *m_problem = nullptr;
    QString m_fileError;          // zuletzt gezeigter Grund der ungültigen Datei
    QList<dss::QueueRow> m_rows;  // Zeilen in der Reihenfolge der Liste
    QUuid m_selected;             // zuletzt gewählte Warteschlange (null = «Standard»)
    bool m_hasSelection = false;
    bool m_filling = false;
    QUuid m_wanted;               // neue Warteschlange, zu wählen, sobald ihre Zeile da ist
    QList<dss::ActionsEdit> m_edits; // Bearbeitungen, die noch an den Kern gehen
    qint64 m_editRequest = 0;        // Anfrage unterwegs
    QString m_editSaved;             // Name für «Gespeichert: …» bzw. «Gelöscht: …»
    bool m_editDeleted = false;
    QUuid m_editCreated;             // aus newQueue
};
