#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

// Nachrichten zum Kamera-Dienst zwischen Kern und Fenster (P15 d2, Plan P15 Abschnitt 7;
// Schnittstelle 19). Eigene Datei neben protocol.h wie actionprotocol.h. Beschreibung:
// docs/08-kern-schnittstelle.md, «Kamera-Dienst». Nur Qt Core (Regel 9).
//
// Der Kern reicht Anfragen des Fensters an den Dienst weiter (camera_call → camera_result), aber
// nur Methode und Pfad aus der festen Liste (checkCameraCall). Den Inhalt prüft der Dienst
// (docs/09-kamera-dienst.md); kommt dort ein Feld dazu, bleibt diese Schnittstelle gleich.

namespace dss {

namespace msg {
// Kern → Fenster: nach hello und bei jeder Änderung an alle
inline constexpr char kCameraService[] = "camera_service";
// Kern → Fenster: Ereignis des Dienstes, an alle
inline constexpr char kCameraEvent[] = "camera_event";
// Fenster → Kern und Antwort nur an den Absender
inline constexpr char kCameraCall[] = "camera_call";
inline constexpr char kCameraResult[] = "camera_result";
} // namespace msg

// Zustand der Verbindung zum Dienst. unavailable: Kern ohne Kamera-Dienst gebaut (DSS_CORE_CAMERA aus).
inline constexpr char kCameraStateOff[] = "off";
inline constexpr char kCameraStateConnecting[] = "connecting";
inline constexpr char kCameraStateConnected[] = "connected";
inline constexpr char kCameraStateError[] = "error";
inline constexpr char kCameraStateUnavailable[] = "unavailable";
QStringList cameraServiceStates();

// camera_service: Zustand, Meldung (leer, wenn verbunden) und letzter Stand von GET status des
// Dienstes, nachgeführt mit seinen Ereignissen status, presets und lights (leer, solange keiner).
struct CameraServiceInfo
{
    QString state = QString::fromLatin1(kCameraStateOff);
    QString message;
    QJsonObject status;

    bool operator==(const CameraServiceInfo &) const = default;
};
QJsonObject cameraServiceMessage(const CameraServiceInfo &info);
bool parseCameraService(const QJsonObject &message, CameraServiceInfo *info, QString *error);

// camera_event: name wie beim Dienst (status, config, controls, presets, lights), data unverändert.
struct CameraEventInfo
{
    QString name;
    QJsonObject data;

    bool operator==(const CameraEventInfo &) const = default;
};
QJsonObject cameraEventMessage(const CameraEventInfo &event);
bool parseCameraEvent(const QJsonObject &message, CameraEventInfo *event, QString *error);

// camera_call: method GET, PUT, POST oder DELETE; path relativ zu /api/v1/ («camera-presets/totale/
// recall»); body ein Objekt oder ohne Inhalt (fehlt oder null).
struct CameraCall
{
    qint64 requestId = 0;
    QString method;
    QString path;
    std::optional<QJsonObject> body;

    bool operator==(const CameraCall &) const = default;
};
QJsonObject cameraCallMessage(const CameraCall &call);
bool parseCameraCall(const QJsonObject &message, CameraCall *call, QString *error);

// camera_result: status HTTP-Status des Dienstes; 0 = keine Antwort (nicht erlaubt, nicht verbunden,
// Zeitgrenze), dann steht der Grund in error und body ist leer. Fehler des Dienstes (404, 409, …)
// kommen mit seinem body (ok, code, message) und ohne error.
struct CameraResult
{
    qint64 requestId = 0;
    int status = 0;
    QJsonObject body;
    QString error;

    bool operator==(const CameraResult &) const = default;
};
QJsonObject cameraResultMessage(const CameraResult &result);
bool parseCameraResult(const QJsonObject &message, CameraResult *result, QString *error);

// Feste Liste erlaubter Anfragen (Plan P15, Abschnitt 4; docs/09-kamera-dienst.md, «Anfragen»), ohne
// GET events (den Strom hält der Kern selbst). «*» steht für genau einen Teil des Pfads: Kennung
// einer Kamera, eines Presets oder einer Lampe aus Buchstaben, Ziffern und . _ ~ : % -, nicht «.»
// oder «..». Leer = erlaubt, sonst die Meldung auf Deutsch. Für den Kern und dss-cli.
QString checkCameraCall(const QString &method, const QString &path);
// Muster der festen Liste («GET status», «POST camera-presets/*/recall», …) für docs und Tests.
QStringList allowedCameraCalls();
// Pfad für camera_call aus einer Eingabe von Hand: «/api/v1/status» und «status» geben «status».
QString cameraApiPath(const QString &input);

} // namespace dss
