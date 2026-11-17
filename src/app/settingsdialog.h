#pragma once

#include <QDateTime>
#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTimer;

namespace dss {
class CoreClient;
struct ChangeResult;
}

// «Werkzeuge → Einstellungen …» (P10-vor, Teil B 1): Port und Adresse im Heimnetz des Kerns, seit
// P10c die Verbindung zu OBS, seit P13 Twitch, seit P15 der Kamera-Dienst. Liest die Werte vom Kern (get_settings), schickt Änderungen als
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
    // P13: Fokus auf den Bereich «Twitch» («Mit Twitch verbinden»), nach showFromCore.
    void focusTwitchSection();

private:
    void accept() override;
    void resetToDefaults(); // P10, Punkt 77
    void onSettingsChanged();
    void onChangeFinished(const dss::ChangeResult &result);
    void updateState();
    // P13: Bereich «Twitch»
    struct TwitchAccountRow
    {
        QString account; // channel, bot
        QStackedWidget *stack = nullptr; // Seite 0: Zustand, Seite 1: Anmeldung unterwegs
        QLabel *state = nullptr;
        QPushButton *login = nullptr;
        QPushButton *logout = nullptr;
        QLabel *code = nullptr;
        QLabel *uri = nullptr;
        QPushButton *open = nullptr;
        QPushButton *copy = nullptr;
        QLabel *remaining = nullptr;
        QPushButton *cancel = nullptr;
        QDateTime expiresAt;
    };
    QWidget *createTwitchAccountRow(TwitchAccountRow *row, const QString &account, const QString &objectPrefix);
    void sendTwitchRequest(const char *type, const QString &account);
    void onTwitchStatusChanged();
    void updateTwitchRemaining();

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
    // P13: Anbindung an Twitch
    QCheckBox *m_twitchEnabled = nullptr;
    QLineEdit *m_twitchClientId = nullptr;
    QLineEdit *m_twitchSecret = nullptr;
    QPushButton *m_twitchDeleteSecret = nullptr;
    TwitchAccountRow m_twitchChannel;
    TwitchAccountRow m_twitchBot;
    QLabel *m_twitchStatus = nullptr;
    QTimer *m_twitchTimer = nullptr; // Restzeit des Codes, solange der Dialog offen ist
    // P15 d3: Verbindung zum Kamera-Dienst
    QCheckBox *m_cameraEnabled = nullptr;
    QLineEdit *m_cameraHost = nullptr;
    QSpinBox *m_cameraPort = nullptr;
    qint64 m_twitchRequest = 0;      // twitch_login_* oder twitch_logout unterwegs
    bool m_deleteTwitchSecret = false; // «Secret löschen» gedrückt, gilt erst mit «OK»
    QLabel *m_problem = nullptr;
    QPushButton *m_ok = nullptr;
    QPushButton *m_reset = nullptr; // nur bei ungültiger kern.ini frei
    QString m_stateProblem;      // zuletzt gezeigte Meldung aus updateState
    qint64 m_pendingRequest = 0; // update_settings oder reset_settings unterwegs
    bool m_pendingReset = false; // die Anfrage unterwegs ist reset_settings
    bool m_filled = false;       // Felder aus dem Kern gefüllt (danach nicht mehr überschreiben)
    bool m_deleteObsPassword = false; // «Passwort löschen» gedrückt, gilt erst mit «OK»
};
