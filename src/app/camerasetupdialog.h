#pragma once

#include <QDialog>
#include <QJsonObject>

namespace dss {
class CameraData;
class CoreClient;
}
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTreeWidget;

// P15 e2: Dialog «Kamera einrichten – <Name>» (Plan 8, Entwurf Seite 5, Checkliste «Dialoge»).
// Felder aus GET cameras/<id>/config (config, ranges); OK schickt nur die geänderten Felder als
// PUT …/config und schliesst erst nach der Antwort des Dienstes (Meldung sonst im Dialog). Eine
// leere Grenze («Standard») schickt null. «Vergessen» setzt einen gemerkten Wert sofort auf den
// Standard des Geräts (PUT …/controls mit null). Den Stand liest der Dialog aus den Daten des
// Reiters; der Reiter ruft refresh() nach neuen Bildwerten oder Einstellungen.
class CameraSetupDialog : public QDialog
{
    Q_OBJECT

public:
    CameraSetupDialog(dss::CoreClient *client, const dss::CameraData *data, const QString &cameraId,
                      QWidget *parent = nullptr);

    void refresh();

    void accept() override;

private:
    void fill();
    QJsonObject formValues() const;
    QDoubleSpinBox *limitBox(const char *objectName);
    void setLimit(QDoubleSpinBox *box, const QString &key);

    dss::CoreClient *m_client;
    const dss::CameraData *m_data;
    QString m_cameraId;
    QJsonObject m_before; // Felder beim Füllen, im Format von formValues
    bool m_filled = false;
    qint64 m_request = 0;       // PUT …/config nach OK
    qint64 m_forgetRequest = 0; // PUT …/controls nach «Vergessen»

    QLineEdit *m_name = nullptr;
    QDoubleSpinBox *m_panMin = nullptr;
    QDoubleSpinBox *m_panMax = nullptr;
    QDoubleSpinBox *m_tiltMin = nullptr;
    QDoubleSpinBox *m_tiltMax = nullptr;
    QCheckBox *m_invertPan = nullptr;
    QCheckBox *m_invertTilt = nullptr;
    QSpinBox *m_speed = nullptr;
    QDoubleSpinBox *m_step = nullptr;
    QCheckBox *m_presetsWake = nullptr;
    QCheckBox *m_aiOff = nullptr;
    QTreeWidget *m_remembered = nullptr;
    QPushButton *m_forget = nullptr;
    QLabel *m_problem = nullptr;
    QPushButton *m_ok = nullptr;
};
