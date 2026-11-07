#pragma once

#include <QDialog>
#include <QUuid>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace dss {
class CoreClient;
struct ChangeResult;
}

// «Quelle in OBS anlegen» und «Quelle in weitere Szene einfügen» (P10c, E-29, Entscheide F2 und F6).
// Nicht blockierend (open()) wie der Dialog «Einstellungen»: Die Antwort kommt später, OBS antwortet
// über das Netz. Bei einer Abweisung bleibt das Fenster mit der Meldung des Kerns offen; die feste
// Höhe der Meldung ändert die Anordnung nicht (Regel 9). Beide Fälle haben dieselben Zeilen, beim
// Einfügen ist der Name nur zu lesen.
class ObsSourceDialog : public QDialog
{
    Q_OBJECT

public:
    ObsSourceDialog(dss::CoreClient *client, QWidget *parent = nullptr);

    // name: beim Anlegen Vorschlag (Name des Overlays), beim Einfügen die vorhandene Quelle.
    void showFor(const QUuid &overlayId, const QString &name, bool addToScene);

signals:
    // Erfolg, Text für die Statuszeile
    void succeeded(const QString &text);

private:
    void accept() override;
    void onResult(const dss::ChangeResult &result);
    void refreshScenes();
    void updateState();

    dss::CoreClient *m_client;
    QLineEdit *m_name = nullptr;
    QComboBox *m_scene = nullptr;
    QLabel *m_problem = nullptr;
    QPushButton *m_ok = nullptr;
    QUuid m_overlayId;
    bool m_addToScene = false;
    qint64 m_pendingRequest = 0;
    QString m_stateProblem;  // zuletzt gezeigte Meldung aus updateState
    QString m_selectedScene; // gewählte Szene, auch solange die Liste ohne OBS leer ist
};
