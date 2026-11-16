#pragma once

#include "actionmodel.h"
#include "actionrefs.h"
#include "variables.h"
#include "viewerdata.h"

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QUuid>

// Ansichtslogik des Editors im Reiter «Chat-Bot» (P14 e1, Plan 6 und 8): Unterreiter «Aktionen»,
// «Befehle», «Zeitnachrichten» und «Variablen». Nur Qt Core (CLAUDE.md Regel 9): Das Fenster (e2,
// e3) zeigt die Zeilen und Texte, die hier entstehen, und schickt die Änderungsbefehle an den Kern.
//
// Der Editor speichert sofort (Plan 5): Jede Bearbeitung ergibt entweder Änderungsbefehle (die ganze
// Aktion, der ganze Befehl, die ganze Zeitnachricht) oder eine Prüfmeldung am Feld, nie beides. Was
// gilt, steht danach in actions_config vom Kern; die Ansicht hält keinen eigenen Stand.

namespace dss {

// actionprotocol.h bindet diese Datei ein; deshalb nur deklariert (Testen, e2-7)
struct ActionsStatus;
struct TwitchStatusInfo;

// --- Änderungsbefehle (Plan 5) ---

// Nachricht an den Kern ohne request_id. type wie im Plan 5 (action_put, action_remove, action_copy,
// action_reset, command_put, command_remove, timer_put, timer_remove, variable_set,
// variable_remove); fields die Felder der Nachricht. Festgelegt seit b3 in actionprotocol.h und
// docs/08, «Aktionen» (actionsEditMessage setzt type und request_id dazu):
// action_put {action}, command_put {command}, timer_put {timer} (Inhalt wie in aktionen.json),
// *_remove, action_copy, action_reset {action_id | command_id | timer_id},
// variable_set {scope, user_id, name, persisted, value}, variable_remove {scope, user_id, name, persisted}.
struct ActionsEdit
{
    QString type;
    QJsonObject fields;

    bool operator==(const ActionsEdit &) const = default;
};

// Ergebnis einer Bearbeitung. edits leer und problem leer: nichts geändert. problem: Prüfmeldung
// am Feld, dann ohne edits. created: Kennung eines neu angelegten Objekts (Aktion, Auslöser,
// Schritt, Befehl, Zeitnachricht), damit das Fenster es auswählt; sonst null.
struct EditResult
{
    QList<ActionsEdit> edits;
    QString problem;
    QUuid created;
};

// Auswahl nach einer neuen Liste: die bisherige, wenn es sie noch gibt, sonst die nächste
// danach (in der Reihenfolge von before), sonst die davor, sonst keine.
QUuid keptSelection(const QList<QUuid> &before, const QList<QUuid> &after, const QUuid &selected);

// Statuszeile nach einer gespeicherten Änderung: «Gespeichert: <Name>»
QString savedStatusText(const QString &name);
// Statuszeile nach dem Löschen einer Aktion oder Gruppe: «Gelöscht: <Name>» (Entscheid des
// Nutzers, 2026-11-16)
QString deletedStatusText(const QString &name);

// Name einer Kopie: «<Name> (Kopie)», bei Bedarf «<Name> (Kopie 2)» usw., eindeutig in names.
QString copyName(const QString &name, const QStringList &names);

// Prüfmeldungen aus checkReferences, eine je Zeile; leer = keine. itemId null: alle zum Objekt
// (Aktion, Befehl, Zeitnachricht) samt seinen Schritten und Auslösern; sonst nur zu diesem Schritt
// bzw. Auslöser.
QString warningText(const QList<actions::ReferenceWarning> &warnings, const QUuid &objectId,
                    const QUuid &itemId = QUuid());

// Namen für die Kurzinhalte, vom Fenster aus dem Stand des Kerns (Overlays, Quellen, Medien).
// Fehlt ein Name, steht «fehlt» bzw. der Verweis.
struct ActionViewNames
{
    QHash<QUuid, QString> overlays;
    QHash<QUuid, QString> sources;
    QHash<QString, QString> media; // Verweis «media/…» → Name der Datei
};

// Dauer für Kurzinhalte: «500 ms», «1.5 s», «10 s», «20 min»
QString durationText(qint64 ms);

// --- Unterreiter «Aktionen», links: Gruppen → Aktionen ---

// Zeile im Baum. Reihenfolge wie in aktionen.json (danach laufen die Aktionen, Plan 3.2): eine
// Gruppe steht dort, wo ihre erste Aktion steht, mit allen ihren Aktionen; Aktionen ohne Gruppe
// stehen selbst auf der obersten Ebene.
struct ActionListRow
{
    bool isGroup = false;
    QString group;   // Gruppe (bei Aktionen deren Gruppe, leer = ohne)
    QUuid actionId;  // nur bei Aktionen
    QString text;    // Name der Gruppe bzw. der Aktion
    QString detail;  // Gruppe: «(5)»; Aktion: Auslöser kurz, zum Beispiel «!discord, Raid»
    bool enabled = true; // Kästchen «aktiv» (nur Aktionen)
    int depth = 0;       // 1 bei Aktionen in einer Gruppe
    QString warning;     // Prüfmeldungen der Aktion und ihrer Teile
};
// search: ohne Gross- und Kleinschreibung in Name, Gruppe und Auslösern; eine Gruppe, deren Name
// passt, zeigt alle ihre Aktionen. Gruppen ohne Rücksicht auf Gross- und Kleinschreibung (Entscheid
// des Nutzers, 2026-11-16): Gruppenzeile und ihre Aktionen tragen die Schreibweise der ersten Aktion
// der Gruppe in der Datei.
QList<ActionListRow> actionListRows(const actions::ActionsConfig &config, const QString &search,
                                    const QList<actions::ReferenceWarning> &warnings);
// Auslöser einer Aktion in einer Zeile: aktive Auslöser, Befehle mit ihrem ersten Befehl,
// Zeitnachrichten mit Abstand, sonst die Bezeichnung des Typs.
QString actionTriggerSummary(const actions::Action &action, const actions::ActionsConfig &config);

// Knopfleiste: «Neu» (in der Gruppe der Auswahl), «Neue Gruppe», «Kopieren», «Löschen»,
// «Zurücksetzen» (nur mitgelieferte, Plan 7). Gruppennamen sind ohne Rücksicht auf Gross- und
// Kleinschreibung eindeutig: newAction übernimmt die bestehende Schreibweise der Gruppe.
EditResult newAction(const actions::ActionsConfig &config, const QString &group);
EditResult newActionGroup(const actions::ActionsConfig &config);
EditResult copyAction(const actions::ActionsConfig &config, const QUuid &actionId);
EditResult removeAction(const actions::ActionsConfig &config, const QUuid &actionId);
bool actionResettable(const actions::Action &action);
EditResult resetAction(const actions::ActionsConfig &config, const QUuid &actionId);
// Rückfrage vor «Zurücksetzen» (Plan 7)
QString resetActionQuestion(const actions::Action &action);

// Gruppe umbenennen: je Aktion der Gruppe ein action_put mit dem neuen Namen (ohne Leerzeichen am
// Rand). Gruppen gibt es nur über ihre Aktionen (Plan 2.3); mitgelieferte Aktionen werden mit
// umbenannt. group und Vergleiche ohne Rücksicht auf Gross- und Kleinschreibung: Prüfmeldung bei
// leerem Namen und beim Namen einer anderen Gruppe in jeder Schreibweise, mit deren Schreibweise
// («Die Gruppe «Shoutout» gibt es schon.», kein Zusammenführen); eine andere Schreibweise derselben
// Gruppe ist ein Umbenennen. Gleicher Name (alle Aktionen schon so) oder unbekannte Gruppe: nichts.
EditResult renameActionGroup(const actions::ActionsConfig &config, const QString &group, const QString &name);
// Gruppe löschen: je Aktion der Gruppe (jede Schreibweise) ein action_remove, in der Reihenfolge der
// Datei. Das Fenster schickt sie nacheinander. Unbekannte Gruppe: nichts.
EditResult removeActionGroup(const actions::ActionsConfig &config, const QString &group);

// Rückfragen vor dem Löschen. Leerer Text: keine Rückfrage nötig (nichts darin, keine Verweise, oder
// das Objekt gibt es nicht). Verweise zählen, auch abgeschaltete: Befehle und Zeitnachrichten, die
// eine Aktion über ihre Auslöser startet, und Schritte «Aktion ausführen» anderer Aktionen; beim
// Befehl und bei der Zeitnachricht die Aktionen mit einem Auslöser darauf. Der Kern entfernt die
// Verweise nicht, sie erscheinen danach als Prüfmeldung «fehlt» bzw. ohne Aktion.
// «Aktion «Shoutout ausführen» löschen? Sie wird von 1 Aktion benutzt.»
QString removeActionQuestion(const actions::ActionsConfig &config, const QUuid &actionId);
// Immer eine Rückfrage, wenn es die Gruppe gibt (sie löscht alle ihre Aktionen); Verweise nur von
// ausserhalb der Gruppe. group in jeder Schreibweise, im Text die Schreibweise der ersten Aktion.
// «Gruppe «Shoutout» mit 2 Aktionen löschen? Sie werden von 1 Befehl benutzt.»
QString removeActionGroupQuestion(const actions::ActionsConfig &config, const QString &group);

// --- «Aktionen», Mitte: Bereich «Aktion» ---

QString actionTitle(const actions::Action &action); // «Aktion «Begrüssung»»

struct QueueChoice
{
    QUuid id;            // null = «Standard»
    QString label;       // «Standard», «Alerts (nacheinander)»
    bool manage = false; // letzter Eintrag «Warteschlangen verwalten …»
    bool operator==(const QueueChoice &) const = default;
};
// «Standard», die Warteschlangen nach Name, bei einer fehlenden Warteschlange von current
// «Warteschlange fehlt», zuletzt «Warteschlangen verwalten …».
QList<QueueChoice> queueChoices(const actions::ActionsConfig &config, const QUuid &current);
int queueChoiceIndex(const QList<QueueChoice> &choices, const QUuid &queue);
// Gruppen aller Aktionen für die Auswahl «Gruppe», nach Name, ohne leere; jede Gruppe einmal in der
// Schreibweise der ersten Aktion in der Datei.
QStringList actionGroupChoices(const actions::ActionsConfig &config);
// Hinweis bei mitgelieferten Aktionen: «mitgeliefert» bzw. «mitgeliefert, geändert», wenn die
// Fassung der Vorlage (templateAction, kommt mit f) bekannt ist und abweicht; leer bei eigenen.
QString templateHint(const actions::Action &action, const actions::Action *templateAction);

// Speichert die bearbeitete Aktion after (action_put). Name und Gruppe ohne Leerzeichen am Rand,
// Name nicht leer, sonst wie der Kern beim Lesen (actionFromJson). Gleich wie before: nichts.
EditResult putAction(const actions::Action &before, const actions::Action &after);
// Für das Feld «Gruppe» im Fenster: wie oben, gibt es die Gruppe schon bei einer anderen Aktion in
// anderer Schreibweise («shoutout» bei «Shoutout»), übernimmt after deren Schreibweise.
EditResult putAction(const actions::ActionsConfig &config, const actions::Action &before,
                     const actions::Action &after);

// --- «Aktionen», Mitte: «▶ Testen» (Plan 3.6, P14 e2-7) ---

// Eintrag der Auswahl «Testwerte: Benutzer» (Entscheid des Nutzers, 2026-11-16): «Testzuschauer»
// (erfunden, fester Login und Kennung) und «eigener Kanal» (Login und Kennung aus dem Twitch-Status).
struct TestUserChoice
{
    QString label;    // «Testzuschauer», «eigener Kanal»
    QString user;     // Argument user (Anzeigename)
    QString userName; // Argument userName (Login)
    QString userId;   // Argument userId (Twitch-ID)
    bool operator==(const TestUserChoice &) const = default;
};
// Erst «Testzuschauer», dann «eigener Kanal». Ohne Login oder Kennung des Kanals (nicht angemeldet,
// Kern ohne Twitch) fehlt «eigener Kanal»; der Status nennt nur den Login, er gilt auch als Anzeigename.
QList<TestUserChoice> testUserChoices(const TwitchStatusInfo &twitch);
// «▶ Testen» bedienbar: Aktion gespeichert und Aktionssystem gebaut und lesbar (nicht
// unavailable, nicht file_error). Auch bei abgeschaltetem Hauptschalter, wie im Kern.
bool testAllowed(const actions::Action &action, const ActionsStatus &status);
// «echt ausführen» bedienbar: wie testAllowed, aber nicht stumm (Version im Einsatz läuft, Plan
// 3.4); sonst nur Probelauf, das Kästchen ist ausgegraut.
bool realTestAllowed(const actions::Action &action, const ActionsStatus &status);
// Anfrage action_test {action_id, arguments, dry_run = !real}. arguments: die Testwerte des
// Auslösers «Test» der Aktion (erster solcher Auslöser), darüber user, userName und userId des
// gewählten Benutzers. Prüft nicht, ob der Test erlaubt ist (testAllowed, realTestAllowed).
EditResult testAction(const actions::Action &action, const TestUserChoice &user, bool real);
// Statuszeile nach der Antwort: «Probelauf gestartet: <Name>» bzw. «Gestartet: <Name>»
QString testStartedStatusText(const QString &name, bool real);

// --- «Aktionen», Mitte: Bereich «Auslöser» ---

struct TriggerRow
{
    QUuid id;
    QString text;    // «Twitch · Erste Nachricht», «Befehl · !discord, !dc», «Zeitnachricht · Info»
    QString detail;  // Filter: «Stufe 1, 2», «ab 100», «1 bis 10», «nicht beim Start des Studios …»
    bool enabled = true;
    QString warning;
};
QList<TriggerRow> triggerRows(const actions::Action &action, const actions::ActionsConfig &config,
                              const QList<actions::ReferenceWarning> &warnings);

// Eintrag in einem Menü «hinzufügen» (Auslöser, Schritt), nach Abschnitten
struct TypeChoice
{
    QString key;
    QString label;
    QString section; // Abschnitt des Menüs
    bool operator==(const TypeChoice &) const = default;
};
QList<TypeChoice> triggerChoices();

EditResult addTrigger(const actions::Action &action, const QString &type);
EditResult removeTrigger(const actions::Action &action, const QUuid &triggerId);
EditResult setTriggerEnabled(const actions::Action &action, const QUuid &triggerId, bool enabled);
// Filter eines Auslösers (triggertypes.h); Prüfmeldung wie checkFieldValue
EditResult setTriggerField(const actions::Action &action, const QUuid &triggerId, const QString &field,
                           const QJsonValue &value);

// --- «Aktionen», Mitte: Bereich «Schritte» ---

struct StepRow
{
    QUuid id;
    int depth = 0;
    QString number;  // «2.1» wie in den Prüfmeldungen
    QString type;    // Bezeichnung des Typs, bei Zweigen «Dann» bzw. «Sonst»
    QString summary; // Kurzinhalt
    bool enabled = true;   // Kästchen
    bool inactive = false; // selbst oder darüber abgeschaltet: rot durchgestrichen (Plan 3.1)
    bool branch = false;   // «Dann»/«Sonst»: Zeile ohne Kästchen, nicht verschieb- oder löschbar
    bool hasChildren = false;
    QString warning;
};
QList<StepRow> stepRows(const actions::Action &action, const actions::ActionsConfig &config,
                        const ActionViewNames &names, const QList<actions::ReferenceWarning> &warnings);
QString stepSummary(const actions::Step &step, const actions::ActionsConfig &config,
                    const ActionViewNames &names);

// Menü «Schritt hinzufügen»: anlegbare Typen, die in P14-1 laufen, nach Art
QList<TypeChoice> stepChoices();

// Neuer Schritt: in eine gewählte Gruppe, «Dann» oder «Sonst» ans Ende, sonst nach dem gewählten
// Schritt auf derselben Ebene, ohne Auswahl ans Ende.
EditResult addStep(const actions::Action &action, const QUuid &selected, const QString &type);
// Entfernt den Schritt mit allem darin; «Dann» und «Sonst» nicht.
EditResult removeStep(const actions::Action &action, const QUuid &stepId);
// Rückfrage vor removeStep, nur wenn der Schritt Schritte enthält (Gruppe, Wenn mit Dann/Sonst;
// gezählt ohne die Zweige selbst), sonst leer. «Schritt «Gruppe» samt 3 Schritten darin löschen?»
QString removeStepQuestion(const actions::Action &action, const QUuid &stepId);
// direction -1 = hoch, +1 = runter, innerhalb derselben Liste
EditResult moveStep(const actions::Action &action, const QUuid &stepId, int direction);
EditResult setStepEnabled(const actions::Action &action, const QUuid &stepId, bool enabled);
// Feld eines Schritts (steptypes.h); Prüfmeldung wie checkFieldValue
EditResult setStepField(const actions::Action &action, const QUuid &stepId, const QString &field,
                        const QJsonValue &value);

struct StepButtons
{
    bool canRemove = false;
    bool canMoveUp = false;
    bool canMoveDown = false;
};
StepButtons stepButtons(const actions::Action &action, const QUuid &selected);

// --- Ausführen: Aktion zu einem Befehl oder einer Zeitnachricht ---

struct ActionChoice
{
    QUuid id;      // null = «Keine»
    QString label; // «Keine», «Info: Discord»
    bool operator==(const ActionChoice &) const = default;
};
// «Keine», dann alle Aktionen nach Name
QList<ActionChoice> actionChoices(const actions::ActionsConfig &config);
// Erste Aktion (Reihenfolge der Datei) mit einem Auslöser «Befehl» auf commandId bzw.
// «Zeitnachricht» auf timerId; null, wenn keine.
QUuid commandAction(const actions::ActionsConfig &config, const QUuid &commandId);
QUuid timerAction(const actions::ActionsConfig &config, const QUuid &timerId);
// «Ausführen: Aktion» wählen (Plan 2.6): Die bisherige Aktion (commandAction bzw. timerAction)
// gibt ihren Auslöser ab, die neue bekommt ihn. Null = keine Aktion. Je ein action_put.
EditResult setCommandAction(const actions::ActionsConfig &config, const QUuid &commandId, const QUuid &actionId);
EditResult setTimerAction(const actions::ActionsConfig &config, const QUuid &timerId, const QUuid &actionId);

// --- Unterreiter «Befehle» ---

struct CommandRow
{
    QUuid id;
    bool enabled = true;
    QString commands;       // «!discord, !dc»
    QString group;
    QString permissions;    // «Alle», «Abonnenten, VIPs», «Moderatoren»
    QString globalCooldown; // «30 s», «—» bei 0
    QString userCooldown;
    QString actions;        // Aktionen mit diesem Befehl, «—» ohne
    QString used;           // Zähler über alle (dauerhaft), «—» ohne
    QString warning;
};
// group leer = alle Gruppen. search: ohne Gross- und Kleinschreibung in Name, Befehlen, Gruppe und
// Aktionen. variables: für die Spalte «Benutzt», nullptr = noch nicht geholt.
QList<CommandRow> commandRows(const actions::ActionsConfig &config, const QString &search,
                              const QString &group, const actions::VariablesData *variables,
                              const QList<actions::ReferenceWarning> &warnings);
// Filter «Gruppe»: erster Eintrag leer («Alle Gruppen»), dann die Gruppen nach Name
QStringList commandGroupChoices(const actions::ActionsConfig &config);
QString commandGroupLabel(const QString &group); // leer → «Alle Gruppen»
// Zeile unter der Liste: «10 Befehle, 1 abgeschaltet · Der Kanal darf immer und hat keine Abklingzeit.»
QString commandListHint(const actions::ActionsConfig &config);
QString commandTitle(const actions::Command &command); // «Befehl «!discord»»
QString permissionsText(const actions::Permissions &permissions, const actions::ActionsConfig &config);

// Ort im Befehl (Plan 2.6)
struct LocationChoice
{
    QString key;   // start, exact, anywhere
    QString label;
    bool operator==(const LocationChoice &) const = default;
};
QList<LocationChoice> commandLocations();

// «Wer darf» (P14-1: Rollen und Gruppen; «verbieten» und einzelne Zuschauer bleiben erhalten)
struct PermissionChoice
{
    bool all = true;
    bool subscriber = false;
    bool vip = false;
    bool moderator = false;
    QList<QUuid> groups;
    bool operator==(const PermissionChoice &) const = default;
};
PermissionChoice permissionChoice(const actions::Permissions &permissions);
// Neue Rechte aus der Auswahl; grant und users bleiben. «Alle» leert Rollen und Gruppen.
actions::Permissions permissionsFromChoice(const actions::Permissions &before, const PermissionChoice &choice);

// Feld «Auslöser»: ein Befehl je Zeile ↔ Liste (ohne Leerzeichen am Rand, ohne leere Zeilen)
QString commandsText(const QStringList &commands);
QStringList commandsFromText(const QString &text);

EditResult newCommand(const actions::ActionsConfig &config);
EditResult copyCommand(const actions::ActionsConfig &config, const QUuid &commandId);
EditResult removeCommand(const actions::ActionsConfig &config, const QUuid &commandId);
// Rückfrage (siehe removeActionQuestion): «Befehl «!so» löschen? Er wird von 1 Aktion benutzt.»
QString removeCommandQuestion(const actions::ActionsConfig &config, const QUuid &commandId);
// command_put; Name nicht leer, Befehle ohne Leerzeichen am Rand und ohne leere, Abklingzeiten
// ab 0, sonst wie der Kern beim Lesen (commandFromJson). Gleich wie before: nichts.
EditResult putCommand(const actions::Command &before, const actions::Command &after);

// --- Unterreiter «Zeitnachrichten» ---

struct TimerRow
{
    QUuid id;
    bool enabled = true;
    QString name;
    QString interval; // «20 min», «5 bis 10 min, zufällig», «einmal nach 20 min»
    QString lines;    // «5», «—» bei 0
    QString onlyLive; // «ja», «nein»
    QString actions;  // «—» ohne
    QString warning;
};
QList<TimerRow> timerRows(const actions::ActionsConfig &config, const QList<actions::ReferenceWarning> &warnings);
QString timerTitle(const actions::Timer &timer); // «Zeitnachricht «Info»»

EditResult newTimer(const actions::ActionsConfig &config);
EditResult copyTimer(const actions::ActionsConfig &config, const QUuid &timerId);
EditResult removeTimer(const actions::ActionsConfig &config, const QUuid &timerId);
// Rückfrage (siehe removeActionQuestion): «Zeitnachricht «Info» löschen? Sie wird von 1 Aktion benutzt.»
QString removeTimerQuestion(const actions::ActionsConfig &config, const QUuid &timerId);
// timer_put; Name nicht leer, Abstand mindestens 60 s, bei Zufall Bereich ab 60 s und von ≤ bis,
// sonst wie der Kern beim Lesen (timerFromJson). Gleich wie before: nichts.
EditResult putTimer(const actions::Timer &before, const actions::Timer &after);

// --- Dialog «Warteschlangen» ---

struct QueueRow
{
    QUuid id;                      // null = «Standard»
    QString name;
    bool blocking = false;         // «nacheinander»
    bool isDefault = false;        // «Standard»
    bool removable = true;         // «Standard» nicht
    bool blockingEditable = true;  // «Standard» ohne Kästchen
    bool operator==(const QueueRow &) const = default;
};
// Zuerst «Standard» (nicht blockierend wie im Kern), dann die Warteschlangen nach Name ohne Rücksicht
// auf Gross- und Kleinschreibung. Der Laufzustand (angehalten, wartend) kommt erst mit P14-2.
QList<QueueRow> queueRows(const actions::ActionsConfig &config);
// queue_put mit «Warteschlange 1», sonst der nächsten freien Nummer (ohne Rücksicht auf Schreibweise),
// nicht blockierend; created = neue Kennung.
EditResult newQueue(const actions::ActionsConfig &config);
// Name ohne Leerzeichen am Rand. Prüfmeldung bei leerem Namen und bei «Standard» oder dem Namen einer
// anderen Warteschlange in jeder Schreibweise («Die Warteschlange «Alerts» gibt es schon.», wie bei
// den Gruppen). Gleicher Name oder unbekannte Warteschlange: nichts.
EditResult renameQueue(const actions::ActionsConfig &config, const QUuid &queueId, const QString &name);
// queue_put; gleicher Wert, «Standard» oder unbekannte Warteschlange: nichts.
EditResult setQueueBlocking(const actions::ActionsConfig &config, const QUuid &queueId, bool blocking);
// queue_remove; «Standard» oder unbekannte Warteschlange: nichts.
EditResult removeQueue(const actions::ActionsConfig &config, const QUuid &queueId);
// Rückfrage (siehe removeActionQuestion), nur wenn Aktionen die Warteschlange benutzen; diese laufen
// danach in «Standard» (actionengine.cpp). «Warteschlange «Alerts» löschen? Sie wird von 2 Aktionen
// benutzt. Diese laufen danach in «Standard».»
QString removeQueueQuestion(const actions::ActionsConfig &config, const QUuid &queueId);

// --- Unterreiter «Variablen» ---

// Flüchtige Werte (nur im Speicher des Kerns, ohne Herkunft)
using VolatileValues = QMap<QString, QJsonValue>;              // Name → Wert
using VolatileUserValues = QMap<QString, VolatileValues>;      // Twitch-ID → Name → Wert

struct VariableRow
{
    actions::VariableScope scope = actions::VariableScope::Global;
    QString userId;   // nur je Zuschauer
    QString viewer;   // Anzeigename aus zuschauer.json, sonst Login, sonst «unbekannt (<ID>)»
    QString name;
    QJsonValue value;
    QString valueText;
    bool persisted = true;
    QString persistedText; // «dauerhaft», «flüchtig»
    QString origin;        // «Studio», «Streamer.bot», bei flüchtigen «—»

    bool operator==(const VariableRow &) const = default;
};
// Text einer Variablen: Zahlen ohne Nachkommastellen, wenn ganz; Text unverändert
QString variableValueText(const QJsonValue &value);
// «Für alle», nach Name. search: ohne Gross- und Kleinschreibung in Name und Wert.
QList<VariableRow> globalVariableRows(const actions::VariablesData &data, const VolatileValues &volatileValues,
                                      const QString &search);
// «Je Zuschauer», nach Zuschauer und Name. search zusätzlich im Zuschauer (Anzeigename, Login);
// nameFilter leer = alle Variablen.
QList<VariableRow> userVariableRows(const actions::VariablesData &data, const VolatileUserValues &volatileValues,
                                    const actions::ViewersData &viewers, const QString &search,
                                    const QString &nameFilter);
// Filter nach Variablenname: erster Eintrag leer («Alle Variablen»), dann die Namen je Zuschauer
QStringList userVariableNames(const actions::VariablesData &data, const VolatileUserValues &volatileValues);
QString variableNameLabel(const QString &name); // leer → «Alle Variablen»

// Wert in der Zelle geändert: Eine Zahl bleibt eine Zahl (sonst Prüfmeldung), Text bleibt Text.
EditResult setVariableValue(const VariableRow &row, const QString &text);
// Löschen nur, wenn es die Variable in data (dauerhaft) bzw. in den flüchtigen Werten noch gibt.
EditResult removeVariable(const actions::VariablesData &data, const VolatileValues &volatileValues,
                          const VolatileUserValues &volatileUserValues, const VariableRow &row);
// Rückfrage vor dem Löschen, immer (der Wert geht verloren): «Variable «firsts» löschen?», je Zuschauer
// «Variable «firsts» von «Anna» löschen?» (VariableRow::viewer).
QString removeVariableQuestion(const VariableRow &row);

} // namespace dss
