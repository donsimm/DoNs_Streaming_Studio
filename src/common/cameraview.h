#pragma once

#include "cameraprotocol.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

// Ansichtslogik des Reiters «Kameras» (P15 e1, Plan P15 Abschnitt 8 mit den Antworten des Nutzers 1,
// 2 und 5: eigener Reiter, Variante 1B «Alles sichtbar»). Zustände, ausgegraute Teile, Halten mit
// Wiederholung, Bündeln der Regler, Kelvin/Mired und die feste Liste der Bildwerte. Nur Qt Core,
// ohne Fenster getestet (Regel 9, tst_kameraansicht). Das Fenster baut aus den Anfragen (CameraCall
// ohne requestId) die Nachricht camera_call und zeigt die Texte unverändert.
//
// Felder und Pfade des Dienstes: docs/09-kamera-dienst.md.

namespace dss {

// Steuerkreuz und Zoom: Halten wiederholt «move» alle 200 ms (Totmann des Dienstes: 500 ms,
// docs/09-kamera-dienst.md, «Bewegen»). Ein Klick (Loslassen vor kCameraHoldDelayMs) ist ein
// Schritt; die Wartezeit wie autoRepeatDelay von QAbstractButton (Standard 300 ms,
// https://doc.qt.io/qt-6/qabstractbutton.html#autoRepeatDelay-prop).
inline constexpr int kCameraHoldDelayMs = 300;
inline constexpr int kCameraHoldRepeatMs = 200;
// Regler senden höchstens alle 100 ms den letzten Wert (Plan 8).
inline constexpr int kCameraBundleMs = 100;

// Licht (Plan 2.6): Helligkeit 3–100 %, Farbtemperatur 143–344 Mired, im Fenster 2900–7000 K in
// Schritten von 50 K (1 000 000 / Mired, gerundet).
inline constexpr int kLightBrightnessMin = 3;
inline constexpr int kLightBrightnessMax = 100;
inline constexpr int kLightMiredMin = 143;
inline constexpr int kLightMiredMax = 344;
inline constexpr int kLightKelvinMin = 2900;
inline constexpr int kLightKelvinMax = 7000;
inline constexpr int kLightKelvinStep = 50;
// Kelvin auf 50 K gerundet, aus dem begrenzten Mired-Wert
int lightKelvinFromMired(int mired);
// Mired für den Dienst, begrenzt auf 143–344
int lightMiredFromKelvin(int kelvin);
int lightBrightnessClamped(int brightness);
QString lightKelvinText(int kelvin);       // «4500 K»
QString lightBrightnessText(int brightness); // «40 %»

// Kopfzeile rechts: Zustand des Dienstes in zwei Zeilen (Platz für beide von Anfang an, wie die
// Zeile «OBS»). state für die Farbe im Theme wie chatBotHeaderView: ok, warning, error, off.
// usable: Steuerung, Bild und Licht bedienbar (nur connected). blockedHint: Hinweis über den
// Spalten, solange der Dienst nicht antwortet (leer, wenn nichts zu sagen ist).
struct CameraServiceView
{
    QString text;
    QString detail;
    QString state = QStringLiteral("off");
    bool usable = false;
    QString blockedHint;

    bool operator==(const CameraServiceView &) const = default;
};
// Ohne Kern (nullopt): «Kamera-Dienst: –»
CameraServiceView cameraServiceView(const std::optional<CameraServiceInfo> &info);

// Eine Kamera aus «cameras» von GET status bzw. dem Ereignis status
struct CameraEntry
{
    QString id;
    QString name;
    QString model;
    QString state; // awake, asleep, privacy, absent, unknown
    bool present = false;
    bool ptz = false;

    bool operator==(const CameraEntry &) const = default;
};
QList<CameraEntry> cameraEntries(const QJsonArray &cameras);
// Nur die Tiny 2 kennt Schlafen, Wecken und die Gruppe «OBSBOT» (Modell wie obsbot.MODEL im Dienst).
bool cameraIsTiny2(const CameraEntry &camera);
// «wach», «schläft», «Privatsphäre», «fehlt», «unbekannt»
QString cameraStateText(const QString &state);
// Eintrag der Auswahl: «OBSBOT Tiny 2 · wach»
QString cameraChoiceText(const CameraEntry &camera);
// Gewählte Kamera nach einer neuen Liste: dieselbe Kennung, sonst die erste; leer bei leerer Liste.
QString cameraChoiceAfterUpdate(const QList<CameraEntry> &cameras, const QString &currentId);

// Was im Reiter bedienbar ist, für die gewählte Kamera (nullptr = keine). Die Anordnung bleibt in
// jedem Zustand gleich (Entwurf Seite 4): Ausgegraut heisst sichtbar, aber nicht bedienbar.
// Hinweis über «Bewegen» mit hintState wie CameraServiceView::state («warning» gelb, «» grau);
// hintWake: im Hinweis steht der Knopf «Wecken».
struct CameraControlView
{
    bool chooserEnabled = false;
    QString wakeText = QStringLiteral("Wecken"); // feste Breite für den längeren Text im Fenster
    bool wakeEnabled = false;
    QString wakeToolTip;
    bool setupEnabled = false; // «Einrichten …»

    bool moveEnabled = false; // Steuerkreuz, Mitte, Zoom
    QString moveHint;
    QString moveHintState;
    bool moveHintWake = false;

    // KI-Verfolgung aus dem Wert «obsbot_ai» (P15 e1b): bedienbar bei wacher Tiny 2. aiMenu wie
    // vom Dienst («Aus», «Oberkörper», bei einem nicht geprüften Modus dazu «Unbekannt (m/u)»).
    bool aiEnabled = false;
    QString aiHint;
    int aiValue = 0;
    QList<std::pair<int, QString>> aiMenu;

    bool presetsEnabled = false; // Liste «Positionen»
    bool recallEnabled = false;  // braucht eine gewählte Position
    QString recallHint;
    bool saveEnabled = false;    // «Aktuelle Position speichern …», «Überschreiben» (wach)
    bool editEnabled = false;    // «Umbenennen», «Löschen»

    bool imageEnabled = false; // mittlere Spalte «Bildwerte»

    bool operator==(const CameraControlView &) const = default;
};
// controls: Antwort von GET cameras/<id>/controls bzw. CameraState::controls (für die KI-Verfolgung).
CameraControlView cameraControlView(const CameraServiceView &service, const CameraEntry *camera,
                                    bool presetSelected = false, const QJsonArray &controls = {});
// Titel der mittleren Spalte: «Bildwerte · OBSBOT Tiny 2», ohne Kamera «Bildwerte»
QString cameraImageTitle(const CameraEntry *camera);

// Feste Liste der bekannten Bildwerte (Plan 8, Regel 9): Bereiche und Zeilen stehen immer da, ein
// Ereignis ändert nur Werte. Schlüssel wie beim Dienst (Name des Treibers klein, «_»), Namen
// deutsch. Pan, Tilt und Zoom fehlen (nur über Bewegen); Werte, die der Dienst meldet und die nicht
// in der Liste stehen, zeigt der Reiter nicht.
enum class CameraControlKind { Slider, Check, Choice, Text };
struct CameraControlSpec
{
    QString key;
    QString label;
    QString section;
    CameraControlKind kind = CameraControlKind::Slider;
};
// «Bild», «Belichtung», «Fokus», «Weissabgleich», «Weitere», «OBSBOT»
QStringList cameraControlSections();
const QList<CameraControlSpec> &cameraControlSpecs();

// Eine Zeile mit dem Stand vom Dienst. available: die Kamera hat den Wert; enabled: bedienbar
// (Kamera da, nicht inaktiv wegen Automatik, nicht nur lesbar, «OBSBOT» nur wach). valueText fest
// für die Anzeige rechts («4600 K», «nie», «–» ohne Wert). menu: Wert und Text bei Choice.
struct CameraControlRow
{
    CameraControlSpec spec;
    bool available = false;
    bool enabled = false;
    bool remembered = false; // Marke «gemerkt», Kontextmenü «Zurücksetzen»
    int minimum = 0;
    int maximum = 0;
    int step = 1;
    int value = 0;
    QList<std::pair<int, QString>> menu;
    QString valueText = QStringLiteral("–");
    QString toolTip;
};
QList<CameraControlRow> cameraControlRows(const QJsonArray &controls, const CameraEntry *camera,
                                          const CameraServiceView &service);
// Hinweis eines Bereichs ohne einen Wert der Kamera («Diese Kamera hat das nicht.»), sonst leer
QString cameraSectionHint(const QList<CameraControlRow> &rows, const QString &section);
// «Zurücksetzen» in der Titelzeile eines Bereichs: nur, wenn dort ein bedienbarer Wert gemerkt ist.
bool cameraSectionResettable(const QList<CameraControlRow> &rows, const QString &section);
// Zoom über dem Steuerkreuz: «35 %» im Bereich des Geräts, ohne Wert «–»
QString cameraZoomText(const QJsonArray &controls);

// Zeilen der Liste «Positionen» der gewählten Kamera. presets aus dem Ereignis presets (alle Felder);
// Einträge nur mit id und name (aus GET status) haben noch keine Kamera und stehen bei jeder
// Kamera, mit Position «–». position: «18° · −14° · Zoom 35 %»; withControls «✔» oder «–».
struct CameraPresetRow
{
    QString id;
    QString name;
    QString position;
    QString withControls;

    bool operator==(const CameraPresetRow &) const = default;
};
QList<CameraPresetRow> cameraPresetRows(const QJsonArray &presets, const QString &cameraId,
                                        const QJsonArray &controls);

// Eine Lampe im rechten Bereich (Entwurf Seite 1B und 4 F). Bereiche entstehen nur nach dem Dialog
// «Lampen» neu (Plan 8); lightLayoutKey vergleicht dafür Kennungen und Namen.
struct LightRow
{
    QString id;
    QString name;
    bool reachable = false;
    bool enabled = false; // Dienst verbunden und Lampe erreichbar
    bool on = false;
    int brightness = kLightBrightnessMin;
    int kelvin = kLightKelvinMin;
    QString stateText; // «erreichbar», «nicht erreichbar»
    QString stateKey;  // «ok», «error» für die Farbe
    QString brightnessText = QStringLiteral("–");
    QString kelvinText = QStringLiteral("–");

    bool operator==(const LightRow &) const = default;
};
QList<LightRow> lightRows(const QJsonArray &lights, const CameraServiceView &service);
QStringList lightLayoutKey(const QJsonArray &lights);

// Liste «Licht-Presets»: Name und je Lampe «ein · 40 % · 4500 K», «aus» oder «–» (nicht ändern),
// Spalten in der Reihenfolge von lights.
struct LightPresetRow
{
    QString id;
    QString name;
    QStringList cells;

    bool operator==(const LightPresetRow &) const = default;
};
QList<LightPresetRow> lightPresetRows(const QJsonArray &presets, const QJsonArray &lights);

// Anfragen an den Dienst (Pfade relativ zu /api/v1/, alle in allowedCameraCalls)
CameraCall cameraMoveCall(const QString &cameraId, int pan, int tilt, int zoom);
CameraCall cameraStopCall(const QString &cameraId);
CameraCall cameraStepCall(const QString &cameraId, int pan, int tilt, int zoom);
CameraCall cameraHomeCall(const QString &cameraId);
CameraCall cameraWakeCall(const QString &cameraId, bool wake);
CameraCall cameraControlsGetCall(const QString &cameraId);
CameraCall cameraConfigGetCall(const QString &cameraId);
CameraCall cameraControlsPutCall(const QString &cameraId, const QJsonObject &values);
// KI-Verfolgung schalten: PUT controls {"obsbot_ai": mode} (0 «Aus», 1 «Oberkörper»)
CameraCall cameraAiCall(const QString &cameraId, int mode);
// «Zurücksetzen» eines Bereichs: alle bedienbaren Werte mit null (Standard des Geräts, vergessen)
CameraCall cameraSectionResetCall(const QString &cameraId, const QList<CameraControlRow> &rows,
                                  const QString &section);
CameraCall cameraPresetRecallCall(const QString &presetId);
// «Aktuelle Position speichern …» (Dialog: Name, «Bildwerte mitspeichern» Standard aus)
CameraCall cameraPresetCreateCall(const QString &name, const QString &cameraId, bool withControls);
CameraCall cameraPresetOverwriteCall(const QString &presetId, bool withControls);
// P15 e2: Preset einzeln lesen, umbenennen, löschen; Einstellungen der Kamera ändern («Tempo»,
// «Schritt», Dialog «Kamera einrichten»; null = Standard)
CameraCall cameraPresetGetCall(const QString &presetId);
CameraCall cameraPresetRenameCall(const QString &presetId, const QString &name);
CameraCall cameraPresetDeleteCall(const QString &presetId);
CameraCall cameraConfigPutCall(const QString &cameraId, const QJsonObject &values);
// Meldung zu einer Antwort ohne Erfolg (Meldung des Dienstes oder des Kerns), leer bei 2xx
QString cameraResultError(const CameraResult &result);
// Einheit von «Tempo» (Entscheid 2 des Nutzers): Tiny 2 « °/s», sonst « %»
QString cameraSpeedSuffix(const CameraEntry *camera);
// Bereich eines Felds aus «ranges» von GET …/config, sonst fallback
std::pair<double, double> cameraConfigRange(const QJsonObject &config, const QString &key, double fallbackMin,
                                            double fallbackMax);
// Felder aus after, die sich gegenüber before geändert haben (für PUT …/config)
QJsonObject cameraConfigChanges(const QJsonObject &before, const QJsonObject &after);
// Dialog «Kamera einrichten», «Gemerkte Werte»: Werte mit remembered, Anzeige wie in der Mitte
struct CameraRememberedRow
{
    QString key;
    QString label;        // Name des Dienstes («Farbtemperatur»)
    QString value;        // gemerkt
    QString defaultValue; // Standard des Geräts

    bool operator==(const CameraRememberedRow &) const = default;
};
QList<CameraRememberedRow> cameraRememberedRows(const QJsonArray &controls);

CameraCall lightPutCall(const QString &lightId, const QJsonObject &values);
CameraCall lightsAllCall(bool on); // «Alle ein», «Alle aus»
CameraCall lightPresetRecallCall(const QString &presetId);

// Halten am Steuerkreuz und bei Zoom «−»/«+» (Plan 8): press merkt sich die Richtung, nach
// kCameraHoldDelayMs beginnt die Fahrt (move), danach alle kCameraHoldRepeatMs wieder. Loslassen
// davor gibt einen Schritt (step), danach «stop». cancel vergisst das Halten ohne Anfrage (Kamera
// gewechselt nach einem eigenen stop, Dienst weg). Zeiten in ms einer stetigen Uhr des Fensters.
class CameraHold
{
public:
    void press(const QString &cameraId, int pan, int tilt, int zoom, qint64 nowMs);
    // Fällige Anfrage (erster move nach der Wartezeit, dann die Wiederholung)
    std::optional<CameraCall> tick(qint64 nowMs);
    std::optional<CameraCall> release(qint64 nowMs);
    void cancel();
    bool pressed() const { return m_pressed; }
    bool moving() const { return m_moving; }
    QString cameraId() const { return m_cameraId; }
    // ms bis zur nächsten fälligen Anfrage, -1 = keine
    int nextDueMs(qint64 nowMs) const;

private:
    bool m_pressed = false;
    bool m_moving = false;
    QString m_cameraId;
    int m_pan = 0;
    int m_tilt = 0;
    int m_zoom = 0;
    qint64 m_pressedAt = 0;
    qint64 m_lastSent = 0;
};

// Regler bündeln (Plan 8): PUT je Pfad höchstens alle kCameraBundleMs; dazwischen gilt der letzte
// Wert je Feld. put gibt die Anfrage zurück, wenn sie sofort gehen darf, sonst merkt sie sich die
// Felder für tick.
class CameraBundler
{
public:
    std::optional<CameraCall> put(const QString &path, const QJsonObject &values, qint64 nowMs);
    QList<CameraCall> tick(qint64 nowMs);
    // Alles Offene sofort (Kamera gewechselt, Regler losgelassen)
    QList<CameraCall> flush(qint64 nowMs);
    int nextDueMs(qint64 nowMs) const;

private:
    QHash<QString, qint64> m_lastSent;
    QHash<QString, QJsonObject> m_pending;
};

// Stand des Dienstes im Fenster: Kameras, Lampen und Presets aus camera_service (status), nachgeführt
// mit den Ereignissen; Bildwerte und Einstellungen je Kamera aus dem Ereignis bzw. den Antworten auf
// GET …/controls und GET …/config. Die Rückgabe sagt, welche Teile neu zu zeigen sind.
class CameraData
{
public:
    enum Part { Cameras = 1, Controls = 2, Config = 4, Presets = 8, Lights = 16 };

    int setService(const std::optional<CameraServiceInfo> &info);
    int applyEvent(const CameraEventInfo &event);
    // Antwort auf eine eigene Anfrage (nur GET …/controls, GET …/config und GET camera-presets/<id>
    // tragen Stand; ein Preset ersetzt nur einen bekannten Eintrag gleicher Kennung)
    int applyResult(const CameraCall &call, const CameraResult &result);
    // P15 e2: Kennungen der Kamera-Presets, von denen nur id und name bekannt sind (aus GET status);
    // das Fenster holt sie einzeln mit GET camera-presets/<id>.
    QStringList cameraPresetsWithoutDetails() const;
    // P15 e2 (Entscheid Lotse): Nach dem Senden einer Bewegung (move, step, home einer Kamera,
    // recall eines Kamera-Presets) steht die KI-Verfolgung dieser Kamera auf «Aus», wenn
    // ai_off_before_move gilt (Standard an); der Dienst meldet das nicht. Rückgabe Controls, wenn
    // sich der Wert geändert hat, sonst 0 (auch bei anderen Anfragen).
    int noteCallSent(const CameraCall &call);

    const QList<CameraEntry> &cameras() const { return m_cameras; }
    const CameraEntry *camera(const QString &id) const;
    QJsonArray controls(const QString &cameraId) const { return m_controls.value(cameraId); }
    QJsonObject config(const QString &cameraId) const { return m_config.value(cameraId); }
    const QJsonArray &cameraPresets() const { return m_cameraPresets; }
    const QJsonArray &lightPresets() const { return m_lightPresets; }
    const QJsonArray &lights() const { return m_lights; }

private:
    int setCameras(const QJsonArray &cameras);
    int setPresets(const QJsonArray &camera, const QJsonArray &light, bool full);
    int setCameraPreset(const QJsonObject &preset);

    QList<CameraEntry> m_cameras;
    QHash<QString, QJsonArray> m_controls;
    QHash<QString, QJsonObject> m_config;
    QJsonArray m_cameraPresets;
    QJsonArray m_lightPresets;
    QJsonArray m_lights;
};

} // namespace dss
