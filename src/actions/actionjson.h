#pragma once

#include "actionmodel.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

// Datei <Datenordner>/aktionen.json (P14 a, Plan 2.2 bis 2.8 und 2.11). Nur Qt Core.
//
// Lesen: Die Felder des Plans sind Pflicht (Ausnahme: weight eines Schritts, Standard 0) und
// werden streng geprüft (Typ, Bereich, erlaubte Werte, eindeutige Kennungen, Aufbau von «Wenn»).
// Unbekannte Felder sind erlaubt und bleiben in extra erhalten (Plan 2.1). Eine ungültige Datei
// liest sich nicht; die Meldung nennt das Feld, zum Beispiel «actions[0].steps[2].text».
// Schreiben: eingerückt, Felder alphabetisch (QJsonDocument), wie medien.json.

namespace dss::actions {

inline constexpr int kActionsFormatVersion = 1;
inline constexpr char kActionsFileName[] = "aktionen.json";

QByteArray serializeActions(const ActionsConfig &config);
QJsonObject actionsToJson(const ActionsConfig &config);
bool parseActions(const QByteArray &data, ActionsConfig *config, QString *error);
bool actionsFromJson(const QJsonObject &object, ActionsConfig *config, QString *error);

// Einzelne Teile, zum Beispiel für action_put (Plan 5: der Editor schickt die ganze Aktion).
// Kennungen müssen innerhalb des Teils eindeutig sein; gegen den Rest der Datei prüft der Kern.
QJsonObject actionToJson(const Action &action);
bool actionFromJson(const QJsonObject &object, Action *action, QString *error);
QJsonObject commandToJson(const Command &command);
bool commandFromJson(const QJsonObject &object, Command *command, QString *error);
QJsonObject timerToJson(const Timer &timer);
bool timerFromJson(const QJsonObject &object, Timer *timer, QString *error);
QJsonObject queueToJson(const Queue &queue);
bool queueFromJson(const QJsonObject &object, Queue *queue, QString *error);
QJsonObject userGroupToJson(const UserGroup &group);
bool userGroupFromJson(const QJsonObject &object, UserGroup *group, QString *error);

} // namespace dss::actions
