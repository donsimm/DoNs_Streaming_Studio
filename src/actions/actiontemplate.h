#pragma once

#include "actionmodel.h"

#include <QList>
#include <QString>
#include <QUuid>

// Mitgelieferte Aktionen (P14 f1, Plan 7): Vorlage src/actions/vorlage/aktionen.json als
// Qt-Ressource. Nur Qt Core (Regel 9 und 14).
//
// - Fehlt aktionen.json, legt der Kern den Stand der Vorlage an (Hauptschalter aus); nie über eine
//   vorhandene Datei.
// - Jede mitgelieferte Aktion trägt eine feste template_id. «Zurücksetzen» (action_reset mit
//   action_id) setzt Auslöser, Schritte und Einstellungen nach der Vorlage, die Kennung der
//   Aktion bleibt. Gelöschte legt action_reset mit template_id wieder an (Menü der Liste).
// - Fehlen dabei Befehl, Zeitnachricht oder Warteschlange der Vorlage, kommen sie mit; vorhandene
//   bleiben unverändert. Eine Warteschlange mit gleichem Namen ersetzt die der Vorlage.
// - Beim Zurücksetzen und Wiederherstellen bekommen Auslöser und Schritte neue Kennungen (sie
//   müssen über die Datei eindeutig sein, Kopien der Vorlage sind möglich).

namespace dss::actions {

// Overlay «Chat-Bot» und seine Elemente (Plan 7). Die Schritte der Vorlage verweisen darauf;
// das Overlay selbst legt Abschnitt f2 mit genau diesen Kennungen an.
inline constexpr char kChatBotOverlayId[] = "d5580000-0000-4000-8000-000000000001";

struct ChatBotElement
{
    const char *name;
    const char *id;
};

inline constexpr ChatBotElement kChatBotElements[] = {
    {"Alert Follow", "d5590000-0000-4000-8000-000000000001"},
    {"Alert Sub", "d5590000-0000-4000-8000-000000000002"},
    {"Alert Geschenk", "d5590000-0000-4000-8000-000000000003"},
    {"Alert Cheer", "d5590000-0000-4000-8000-000000000004"},
    {"Alert Raid", "d5590000-0000-4000-8000-000000000005"},
    {"Begrüssung Bild", "d5590000-0000-4000-8000-000000000006"},
    {"Begrüssung Name", "d5590000-0000-4000-8000-000000000007"},
    {"Shoutout Bild", "d5590000-0000-4000-8000-000000000008"},
    {"Shoutout Name", "d5590000-0000-4000-8000-000000000009"},
};

// Pfad der Vorlage in den Ressourcen
inline constexpr char kActionTemplateResource[] = ":/dss/vorlage/aktionen.json";

// Inhalt der Vorlage, wie die Datei es liest (geprüft). Eine ungültige Vorlage ist ein Fehler im
// Bau; dann ist der Stand leer und error gesetzt.
const ActionsConfig &actionTemplate(QString *error = nullptr);

// Mitgelieferte Aktion zur template_id; nullptr, wenn es sie in der Vorlage nicht gibt.
const Action *templateAction(const QUuid &templateId);

// template_id der mitgelieferten Aktionen, die in config fehlen, in der Reihenfolge der Vorlage
QList<QUuid> missingTemplates(const ActionsConfig &config);

// action_reset mit action_id: Aktion nach der Vorlage, Kennung bleibt. !ok mit Meldung auf Deutsch.
bool resetActionToTemplate(ActionsConfig *config, const QUuid &actionId, QString *error);
// action_reset mit template_id: gelöschte mitgelieferte Aktion am Ende der Liste wieder anlegen.
// created: Kennung der Aktion (die der Vorlage, wenn frei).
bool restoreTemplateAction(ActionsConfig *config, const QUuid &templateId, QUuid *created, QString *error);

} // namespace dss::actions
