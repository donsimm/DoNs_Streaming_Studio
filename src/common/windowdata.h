#pragma once

#include "actionprotocol.h"
#include "overlay.h"
#include "overlaylive.h"
#include "protocol.h"
#include "systemstats.h"

#include <QDateTime>
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

// P14 d: Bereich «Chat-Bot» im Dock «Eigenschaften» zur gewählten Quelle (Laufzustand vom Kern).
struct LiveRow
{
    QString text;         // «nicht live», «live sichtbar, noch 6 s», ohne Quelle leer
    bool live = false;    // Markierung «live» im Dock «Quellen»
    bool canHide = false; // Knopf «Ausblenden»: live und verbunden
};
LiveRow liveRow(const LiveElements &elements, const QUuid &sourceId, bool connected);
// Abstand bis zur nächsten Anzeige der Restdauer in ms (auf die nächste volle Sekunde, mindestens
// 50 ms); -1, wenn kein Element eine Dauer hat.
int liveRefreshMs(const LiveElements &elements);

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
    // P13: Anbindung an Twitch, Client Secret wie das Passwort von OBS
    bool twitchEnabled = false;
    QString twitchClientId;
    QString twitchClientSecret;            // leer = behalten
    bool deleteTwitchClientSecret = false; // Knopf «Secret löschen»; ein neues Secret geht vor
    // P15 d3: Verbindung zum Kamera-Dienst, wie OBS ohne Passwort
    bool cameraEnabled = false;
    QString cameraHost;
    int cameraPort = kCameraServiceDefaultPort;
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

// P13, Bereich «Twitch» im Dialog «Einstellungen». Platzhalter im Feld «Client Secret» wie beim
// Passwort von OBS; das Secret kommt nie vom Kern (Regel 12).
QString twitchSecretPlaceholder(const std::optional<SettingsInfo> &info, bool deleting);
// Zeile eines Kontos («channel» oder «bot»). Ohne Anmeldung unterwegs: text, «Anmelden …» und
// «Abmelden»; während der Anmeldung: Code, Adresse und «Abbrechen».
struct TwitchAccountView
{
    QString text;              // «nicht angemeldet», «angemeldet als …», Meldung, fehlende Rechte
    bool loginEnabled = false; // «Anmelden …»: Twitch an im Kern, nicht schon gültig angemeldet
    bool logoutEnabled = false; // «Abmelden»: ein Konto ist hinterlegt
    bool loginPending = false;
    QString userCode;
    QString verificationUri;
    QDateTime expiresAt; // UTC
};
TwitchAccountView twitchAccountView(const std::optional<TwitchStatusInfo> &status, const QString &account);
// Restzeit des Codes: «Gültig noch 4:05», danach «Code abgelaufen, neu anmelden».
QString twitchCodeRemainingText(const QDateTime &expiresAt, const QDateTime &now);
// Zustand unter den Konten: Meldung des Kerns, Abos und Abzeichen; ohne Status leer.
QString twitchStatusText(const std::optional<TwitchStatusInfo> &status);
// Hinweis beim Bot während der Anmeldung (Plan P13, 2.1).
QString twitchBotLoginHint();
// P13: Problem des Bots in einem Satz (fehlt, abgelaufen, Fehler); leer, wenn der Bot angemeldet ist
// oder Twitch aus. Ohne Bot sendet der Kanal ersatzweise (Plan 2.7), die Farbe bleibt beim Kanal.
QString twitchBotProblem(const std::optional<TwitchStatusInfo> &status);
// Tooltip des Twitch-Knopfes: wie obsToolTip, dazu das Problem des Bots in einer zweiten Zeile.
QString twitchToolTip(ConnectionState state, const std::optional<TwitchStatusInfo> &status);

// P13, Reiter «Chat-Bot»: Kopfzeile rechts. state für die Farbe im Theme: «ok» (Kanal verbunden,
// Bot angemeldet), «warning» (Kanal verbunden, Bot fehlt), «error» (Kanal mit Fehler), «off».
// P14 e2-4: mit actions_status dazu der Schalter «Chat-Bot aktiv». Reihenfolge: unavailable,
// file_error (Text aus message, error, Schalter aus und ausgegraut), Twitch-Kanal mit Fehler (wie
// ohne actions), off («aus»), muted («stumm: Version im Einsatz läuft», warning), on ohne Bot («Bot
// nicht angemeldet: sendet als Kanal», warning), on («läuft · liest als … · sendet als …», ok).
// Ohne actions_status oder ohne verbundenen Kanal: Text wie ohne actions.
struct ChatBotHeaderView
{
    QString text;
    QString toolTip; // Einzelheiten wie im Bereich «Twitch» (twitchStatusText)
    QString state = QStringLiteral("off");
    bool switchChecked = false; // «Chat-Bot aktiv»
    bool switchEnabled = false;
};
ChatBotHeaderView chatBotHeaderView(ConnectionState channelState, const std::optional<TwitchStatusInfo> &status,
                                    const std::optional<ActionsStatus> &actions = std::nullopt);
// Klick auf «Chat-Bot aktiv»: Anfrage actions_set_enabled
ActionsEdit actionsSetEnabledEdit(bool enabled);

// P13, Bereich «Ereignisse» im Unterreiter «Live»: was das Fenster bestellt (subscribe_events). Ohne
// Chat-Nachrichten (die zeigt P14 im Bereich «Chat») und ohne Kanalpunkte und OBS (nicht im Entwurf).
QStringList chatBotEventFilters();
// P14 e2-6: Bestellung des Unterreiters «Live» (Bereiche «Chat», «Ereignisse», «Läufe der Aktionen»):
// chatBotEventFilters() und dazu twitch.chat_message, twitch.chat_sent, twitch.chat_dropped und actions.
QStringList chatBotLiveFilters();
// Zeit einer Zeile: lokal, hh:mm:ss; ungültig «–»
QString rowTimeText(const QDateTime &time);
// Eine Zeile: Zeit (lokal, hh:mm:ss), Art, Name, Details. isChat: twitch.chat_message (P14 e2-6).
struct EventRow
{
    QString time;
    QString kind;
    QString name;
    QString details;
    bool isChat = false;

    bool operator==(const EventRow &) const = default;
};
EventRow eventRow(const events::Event &event);
// Was das Fenster an einer sichtbaren Liste ändern muss, wenn eine Zeile hinten dazukommt: erst
// removedFront Zeilen vorne entfernen, dann bei appended die neue Zeile anhängen (P14 e2-6).
// changed: Stelle einer bestehenden Zeile, die sich geändert hat, -1 = keine (P14 Emotes em-3,
// Echo im Chat; EventRows setzt es nie).
struct RowsAppend
{
    bool appended = false;
    int removedFront = 0;
    int changed = -1;

    bool operator==(const RowsAppend &) const = default;
};
// Liste der Zeilen, älteste zuerst, höchstens kMaxRows (Plan 2.7).
class EventRows
{
public:
    static constexpr int kMaxRows = 200;
    // Typen mit Zeile im Bereich «Ereignisse»: chatBotEventFilters() und twitch.chat_message
    static bool shows(const QString &type);
    // Ersetzt die Liste (event_history); von einem längeren Verlauf bleiben die neuesten. Typen
    // ohne Zeile (shows) fallen weg.
    void reset(const QList<events::Event> &events);
    // P14 e2-6: Hängt eine Zeile an, wenn der Typ eine hat (shows); Rückgabe für visibleRows.
    RowsAppend add(const events::Event &event);
    // «Chat-Nachrichten ausblenden» (Standard an): visibleRows ohne isChat. Danach baut das Fenster
    // die Liste neu.
    void setHideChat(bool hideChat) { m_hideChat = hideChat; }
    bool hideChat() const { return m_hideChat; }
    const QList<EventRow> &rows() const { return m_rows; }
    QList<EventRow> visibleRows() const;

private:
    bool visible(const EventRow &row) const { return !m_hideChat || !row.isChat; }

    QList<EventRow> m_rows;
    bool m_hideChat = true;
};
// Statuszeile bei events_dropped (Gegendruck im Kern).
QString eventsDroppedText(qint64 count);

// P13 (Entscheid 2026-11-15): Die Docks des Overlay-Editors sind nur im Reiter «Overlays» zu sehen.
// Beim Verlassen merkt sich das Fenster, welche Docks offen waren, und blendet alle aus; bei der
// Rückkehr kommen genau diese wieder.
class ModuleDocks
{
public:
    // visible: Sichtbarkeit je Dock jetzt. Rückgabe: neue Sichtbarkeit je Dock, leer = nichts ändern.
    QList<bool> switchTo(bool editorModule, const QList<bool> &visible);
    // true im Reiter «Chat-Bot»: Die Anordnung zum Speichern ist die vom Verlassen von «Overlays».
    bool docksHidden() const { return m_hidden; }

private:
    bool m_hidden = false;
    QList<bool> m_remembered;
};
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
// P12b b5 (Entscheid 6 vom 2026-11-12): Zeile «Ton Videos» im Dock «Steuerung». Auswahlliste der
// Ausgänge mit erstem Eintrag «Kein Ausgang» (name leer), Versatz in ms, Hinweis wie die Zeile «OBS».
struct AudioOutputChoice
{
    QString name;  // node.name; leer = kein Ausgang
    QString label; // Anzeige
    bool operator==(const AudioOutputChoice &) const = default;
};
struct AudioRouteRow
{
    QList<AudioOutputChoice> choices; // erster Eintrag immer «Kein Ausgang»
    int current = 0;                  // Eintrag des gewählten Ausgangs
    int offsetMs = 0;
    int offsetMinMs = kAudioOffsetMinMs;
    int offsetMaxMs = kAudioOffsetMaxMs;
    QString text = QStringLiteral("–"); // Zustand
    QString toolTip;
    bool problem = false; // roter Text wie die Zeile «OBS»
    bool canEdit = false; // Einstellungen und Ausgänge vom Kern da
};
// purpose: Zweck aus kAudioPurposes (zuerst kAudioPurposeVideo). Ohne Einstellungen oder Ausgänge
// vom Kern (keine Verbindung) ausgegraut mit «–».
AudioRouteRow audioRouteRow(const std::optional<SettingsInfo> &settings,
                            const std::optional<AudioOutputsInfo> &outputs, const QString &purpose);
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
