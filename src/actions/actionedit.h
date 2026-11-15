#pragma once

#include "actionmodel.h"

#include <QJsonObject>
#include <QString>
#include <QUuid>

// Änderungen an aktionen.json (P14 b3, Plan 5): der Kern wendet die Änderungsbefehle des Fensters
// hier an. Nur Qt Core, kein protocol.h (Regel 9, 14); type und fields wie in docs/08,
// «Aktionen» (action_put {action}, action_remove {action_id}, …).
//
// Nach jeder Änderung wird der ganze Stand wie beim Lesen der Datei geprüft (actionsFromJson:
// eindeutige Kennungen über die ganze Datei, Felder der Typen). Scheitert das, bleibt config
// unverändert.

namespace dss::actions {

struct ConfigEditResult
{
    bool ok = false;
    QString message; // bei !ok, auf Deutsch
    QUuid created;   // neue Aktion (action_put, action_copy), neuer Befehl usw.; sonst null
    QString name;    // Name des geänderten Objekts (Statuszeile «Gespeichert: …», Protokoll)
};

// Typen: actions_set_enabled, action_put, action_remove, action_copy, action_reset,
// command_put, command_remove, timer_put, timer_remove, queue_put, queue_remove, group_put,
// group_remove. Anderer Typ: !ok.
ConfigEditResult applyConfigEdit(ActionsConfig *config, const QString &type, const QJsonObject &fields);
bool isConfigEditType(const QString &type);

// Kopie einer Aktion mit neuen Kennungen (Aktion, Auslöser, Schritte) und Namen «<Name> (Kopie)»,
// bei Bedarf «(Kopie 2)» usw.; ohne Vorlage (template_id null).
Action copiedAction(const ActionsConfig &config, const Action &action);

} // namespace dss::actions
