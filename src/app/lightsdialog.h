#pragma once

#include "cameraview.h"

#include <QDialog>

namespace dss {
class CoreClient;
}
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

// P15 e3: Dialog «Lampen» (Plan 8, Entwurf Seite 5, Checkliste «Dialoge»). Liste der Lampen mit Name,
// Adresse und Zustand; Name und Adresse der gewählten Lampe direkt änderbar, «Hinzufügen», «Lampe
// löschen» (Entscheid 4 des Nutzers), «Verbindung prüfen». Die Adressen kommen aus GET export (Datei
// licht.json), der Zustand aus den Daten des Reiters. Änderungen gelten erst mit OK: Der Dialog schickt
// die Anfragen aus lightsDialogCalls nacheinander und schliesst nach der letzten; lehnt der Dienst
// eine ab, bleibt er offen, zeigt die Meldung und liest den Stand neu. Danach baut der Reiter die
// Bereiche der Lampen neu (Plan 8).
class LightsDialog : public QDialog
{
    Q_OBJECT

public:
    LightsDialog(dss::CoreClient *client, const dss::CameraData *data, QWidget *parent = nullptr);

    // Zustand der Lampen nach einem Ereignis lights
    void refresh();

    void accept() override;

private:
    void load();
    void fillList();
    int currentRow() const;
    void onResult(const dss::CameraCall &call, const dss::CameraResult &result);
    void sendNext();

    dss::CoreClient *m_client;
    const dss::CameraData *m_data;
    QList<dss::LightEntry> m_before;  // Stand aus licht.json
    QList<dss::LightEntry> m_entries; // Stand im Dialog
    bool m_loaded = false;
    qint64 m_exportRequest = 0;
    qint64 m_checkRequest = 0;
    QString m_checkId;
    qint64 m_request = 0; // laufende Anfrage nach OK
    QList<dss::CameraCall> m_queue;

    QTreeWidget *m_list = nullptr;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_host = nullptr;
    QPushButton *m_add = nullptr;
    QPushButton *m_remove = nullptr;
    QPushButton *m_check = nullptr;
    QLabel *m_checkResult = nullptr;
    QLabel *m_problem = nullptr;
    QPushButton *m_ok = nullptr;
};
