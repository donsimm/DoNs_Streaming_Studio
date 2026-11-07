#pragma once

#include "overlay.h"
#include "protocol.h"
#include "systemstats.h"

#include <QList>
#include <QString>
#include <QUuid>

#include <optional>

// Aufbereitung der Daten vom Kern für die Anzeige im Fenster (P07, Teil F). Nur Qt Core
// (CLAUDE.md Regel 9): Das Fenster setzt nur die Texte und Werte, die hier entstehen.

namespace dss {

// Kopfbereich, Werte wie auf docs/bilder/oberflaeche.png; «–», wenn der Wert fehlt.
QString cpuText(const std::optional<double> &percent);                     // «1 %»
QString ramText(const std::optional<qint64> &usedKiB,
                const std::optional<qint64> &totalKiB);                    // «0.8 / 7.8 GiB»
QString lanText(const std::optional<LanStats> &lan);                       // «↓ 0 ↑ 0 kbit/s»
QString lanToolTip(const std::optional<LanStats> &lan);                    // «LAN über enp3s0 …»

// Verlauf der letzten Werte (eine Messung pro Sekunde, Standard 60). Ein fehlender Wert
// bleibt als Lücke stehen.
class ValueHistory
{
public:
    explicit ValueHistory(int capacity = 60);

    void add(const std::optional<double> &value);
    void clear() { m_values.clear(); }
    int capacity() const { return m_capacity; }
    // Älteste zuerst, höchstens capacity() Einträge.
    QList<std::optional<double>> values() const { return m_values; }
    // Grösster vorhandener Wert, 0 ohne Werte.
    double maximum() const;

private:
    int m_capacity;
    QList<std::optional<double>> m_values;
};

// Oberer Rand der Verlaufsfläche: CPU 100 %, RAM gesamt, LAN der grösste Wert im Verlauf
// (mindestens 1 kbit/s, damit eine Linie bei 0 unten liegt).
double lanScaleMaximum(const ValueHistory &history);

// Zeile im Dock «Quellen». Reihenfolge wie in OBS: die vorderste Quelle (letztes Element in
// Overlay::sources) steht oben.
struct SourceRow
{
    QUuid id;
    QString name;
    bool visible = true;
};
QList<SourceRow> sourceRows(const Overlay &overlay);
QString visibilityToolTip(bool visible); // «eingeblendet» / «ausgeblendet»

// Quelle mit der Kennung, nullptr wenn es sie nicht gibt.
const Source *findSource(const Overlay &overlay, const QUuid &sourceId);

// Drehung für das Zahlenfeld in Grad (125 Zehntelgrad → 12.5).
double rotationDegrees(int tenthDegrees);

// Statuszeile rechts.
QString coreStatusText(bool connected); // «Kern-Dienst: verbunden» / «… nicht verbunden»
// P10-vor (Punkt 67): Hinweis, solange der Kern Dateien übernimmt: leer, «Übernehme «a.png» …»,
// «Übernehme 2 Dateien («a.png», «b.mp4») …».
QString importStatusText(const QStringList &fileNames);

// P10-vor, Teil B 1: Dialog «Werkzeuge → Einstellungen …». Nur die Werte, die sich gegenüber dem
// Stand des Kerns geändert haben (Adresse ohne Leerzeichen am Rand); std::nullopt, wenn nichts
// geändert ist. Geprüft wird im Kern, der Dialog zeigt dessen Meldung.
struct SettingsForm
{
    int port = 0;
    QString address;
    // P10c: Verbindung zu OBS
    bool obsEnabled = false;
    QString obsHost;
    int obsPort = kObsDefaultPort;
    QString obsPassword;            // leer = behalten
    bool deleteObsPassword = false; // Knopf «Passwort löschen»; ein neues Passwort geht vor
};
// obsInError: Der OBS-Knopf ist rot. Dann geht auch ein unverändertes Formular an den Kern, denn
// «OK» versucht es nach einem Abbruch durch OBS neu (P10c, Plan 1.7).
std::optional<SettingsUpdate> settingsUpdateFromForm(const CoreSettings &current, const SettingsForm &form,
                                                     bool obsInError);
// Hinweis im Dialog: Die Werte wirken sofort (Server startet neu, P10), nur Adressen dieses PCs.
QString settingsDialogHint();
// Meldung im Dialog: Datei ungültig (settings_error) bzw. ohne Verbindung; sonst leer.
QString settingsDialogProblem(bool connected, const std::optional<SettingsInfo> &info);
// P10c: Platzhalter im Passwortfeld. Der Wert selbst kommt nie vom Kern (Regel 12). Ohne Einstellungen
// vom Kern ist unbekannt, ob ein Passwort gespeichert ist: leer.
QString obsPasswordPlaceholder(const std::optional<SettingsInfo> &info, bool deleting);
// P10c: Tooltip des OBS-Knopfes: «OBS: verbunden (OBS 33.0.1, Szene «Live»)», bei einem Problem die
// Meldung des Kerns, sonst wie die anderen Knöpfe.
QString obsToolTip(ConnectionState state, const std::optional<ObsStatusInfo> &status);
// P10c, E-29: Zeile «OBS» im Dock «Steuerung».
struct ObsSourceRow
{
    QString text;
    bool problem = false;     // roter Text wie #serverMessage
    QString buttonText;       // «In OBS anlegen …» bzw. «In weitere Szene einfügen …» (F6)
    QString buttonToolTip;
    bool canAct = false;      // Knopf frei: Overlay gewählt und OBS verbunden
    bool addsToScene = false; // Quelle da: der Knopf fügt sie in eine weitere Szene ein
};
ObsSourceRow obsSourceRow(bool overlaySelected, ConnectionState obsState, const ObsSourceInfo &source);
// P10c, E-29: Zustand des Fensters «Quelle in OBS anlegen». pending: Eine Anfrage läuft.
struct ObsSourceDialogState
{
    QString problem;        // Meldung des Zustands, sonst leer
    bool canEdit = false;   // Felder frei
    bool canSubmit = false; // Knopf frei; leerer Name lässt sich nicht bestätigen (gleiche Prüfung wie im Kern)
};
ObsSourceDialogState obsSourceDialogState(bool coreConnected, ConnectionState obsState, int sceneCount,
                                          const QString &name, bool pending);

// --- Browserquelle (P10, Teil C) ---

// Feld «Browserquelle» im Dock «Steuerung»: die Adresse mit Token (Antwort auf
// get_browser_source zum gewählten Overlay) oder, solange es keine gibt, ein grauer Platzhalter
// mit dem Grund. canCopy: «Kopieren» ist möglich.
struct BrowserSourceField
{
    QString text;
    QString placeholder;
    bool canCopy = false;
};
BrowserSourceField browserSourceField(const QUuid &selectedOverlay,
                                      const std::optional<BrowserSourceInfo> &info);

// Anzeige im Dock «Steuerung»: «Ausgabe: ein, 1 Browserquelle verbunden», «Ausgabe: aus
// (Browserquelle leer), keine Browserquelle verbunden», «Ausgabe: –» ohne Overlay. pages:
// verbundene Seiten (server_status), leer = unbekannt (dann ohne diesen Teil).
QString outputStateText(const std::optional<bool> &output, const std::optional<int> &pages);
// Schalter links neben dem Eintrag im Dock «Overlays» (E-24).
QString outputToolTip(bool output);

// Meldungen des Overlay-Servers (Port belegt, Adresse fehlt, ungültige Adresse benutzt) für das
// Fenster: text ganz (eine Meldung pro Zeile, im Dock «Steuerung» und als Tooltip), shortText für
// die Statuszeile («Browserquelle: <erster Satz>», höchstens 90 Zeichen, «(+1)» bei weiteren).
// Beide leer, wenn es nichts zu melden gibt oder keine Verbindung besteht.
struct ServerMessages
{
    QString text;
    QString shortText;
};
ServerMessages serverMessages(bool connected, const std::optional<ServerStatusInfo> &info);

} // namespace dss
