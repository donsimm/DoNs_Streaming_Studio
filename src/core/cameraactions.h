#pragma once

#include "actionservices.h"

namespace dss::camera {
class CameraServiceClient;
}

// Schritte «Kamera-Position» und «Licht-Preset» im Kern (P15 f1, Plan P15 9.): POST
// camera-presets/<id>/recall bzw. light-presets/<id>/recall über den Kamera-Client des Kerns,
// derselbe Weg wie camera_call (feste Liste, checkCameraCall). Nur mit DSS_CORE_CAMERA gebaut.
//
// - Fehler im Lauf: Dienst aus oder nicht erreichbar, Kamera fehlt (409), Preset gelöscht (404
//   not_found), keine Lampe antwortet (502); Meldung des Dienstes, sonst «Fehler <Status>».
// - Licht: Status 200 mit Einträgen in errors (Lampe ohne Antwort, Wert nicht übernommen, Lampe
//   gelöscht) gilt ebenfalls als Fehler mit den Meldungen des Dienstes; die anderen Lampen sind
//   gesetzt.
// - Protokoll ohne Inhalt: nur Pfad, Status und Code, wie camera_call.
//
// Quelle: docs/09-kamera-dienst.md («Kamera-Presets», «Licht-Presets», «Fehlercodes»).
class CameraActions : public dss::actions::CameraControl
{
public:
    // client lebt länger als dieses Objekt
    explicit CameraActions(dss::camera::CameraServiceClient *client);

    void recallPreset(dss::actions::PresetKind kind, const QString &presetId, dss::actions::StepDone done) override;
    QString presetName(dss::actions::PresetKind kind, const QString &presetId) const override;

private:
    dss::camera::CameraServiceClient *m_client;
};
