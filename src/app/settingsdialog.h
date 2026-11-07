#pragma once

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace dss {
class CoreClient;
struct ChangeResult;
}

// «Werkzeuge → Einstellungen …» (P10-vor, Teil B 1): Port und Adresse im Heimnetz des Kerns, seit
// P10c die Verbindung zu OBS. Liest die Werte vom Kern (get_settings), schickt Änderungen als
// update_settings und zeigt die Meldung des Kerns. Keine Prüfung hier (CLAUDE.md Regel 9): was sich
// geändert hat, steht in windowdata.h (settingsUpdateFromForm), geprüft wird im Kern. Nicht
// blockierend (open()).
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    SettingsDialog(dss::CoreClient *client, QWidget *parent = nullptr);

    // Fragt die Werte neu beim Kern an und zeigt den Dialog.
    void showFromCore();

private:
    void accept() override;
    void resetToDefaults(); // P10, Punkt 77
    void onSettingsChanged();
    void onChangeFinished(const dss::ChangeResult &result);
    void updateState();

    dss::CoreClient *m_client;
    QSpinBox *m_port = nullptr;
    QLineEdit *m_address = nullptr;
    QLabel *m_file = nullptr;
    // P10c: Verbindung zu OBS
    QCheckBox *m_obsEnabled = nullptr;
    QLineEdit *m_obsHost = nullptr;
    QSpinBox *m_obsPort = nullptr;
    QLineEdit *m_obsPassword = nullptr;
    QPushButton *m_obsDeletePassword = nullptr;
    QLabel *m_problem = nullptr;
    QPushButton *m_ok = nullptr;
    QPushButton *m_reset = nullptr; // nur bei ungültiger kern.ini frei
    QString m_stateProblem;      // zuletzt gezeigte Meldung aus updateState
    qint64 m_pendingRequest = 0; // update_settings oder reset_settings unterwegs
    bool m_pendingReset = false; // die Anfrage unterwegs ist reset_settings
    bool m_filled = false;       // Felder aus dem Kern gefüllt (danach nicht mehr überschreiben)
    bool m_deleteObsPassword = false; // «Passwort löschen» gedrückt, gilt erst mit «OK»
};
