#pragma once

#include "fieldspec.h"

#include <QList>
#include <QString>

// Typen der Schritte (P14 a, Plan 2.4) mit der Nummer bei Streamer.bot, damit der spätere Import
// der Aktionen Bekanntes ohne Umbau übernimmt (Plan 2.1). Nur Qt Core (CLAUDE.md Regel 9).
//
// Quellen der Nummern: Plan P14, Abschnitt 2.4 (Recherche von Lotse vom 2026-11-09 mit
// https://docs.streamer.bot/api/sub-actions und den Daten des Nutzers, Fassung 24).

namespace dss::actions {

// In welchem Teil der Schritt ausgeführt wird (Plan 2.4, Spalte «Teil»)
enum class StepPart {
    P14_1,
    P14_2,
    Never, // script und unsupported: gelesen und gespeichert, nie ausgeführt
};

struct StepTypeInfo
{
    QString key;            // Typ in aktionen.json
    QString label;          // Bezeichnung auf Deutsch (Meldungen; vorläufig, Editor in e1)
    int sbType = 0;         // Nummer bei Streamer.bot; 0 = keine Entsprechung oder Typnummer aus z0
    StepPart part = StepPart::P14_1;
    bool hasSteps = false;  // trägt eine eigene Liste steps (Gruppe, Wenn, Dann, Sonst)
    bool creatable = true;  // im Editor anlegbar (nicht: Dann, Sonst, script, unsupported)
    QList<FieldSpec> fields; // Felder des Typs ohne die gemeinsamen (id, type, enabled, weight, steps)
};

inline constexpr char kStepComment[] = "comment";
inline constexpr char kStepGroup[] = "group";
inline constexpr char kStepIf[] = "if";
inline constexpr char kStepIfTrue[] = "if_true";
inline constexpr char kStepIfFalse[] = "if_false";
inline constexpr char kStepBreak[] = "break";
inline constexpr char kStepDelay[] = "delay";
inline constexpr char kStepRunAction[] = "run_action";
inline constexpr char kStepSetArgument[] = "set_argument";
inline constexpr char kStepGlobalGet[] = "global_get";
inline constexpr char kStepGlobalSet[] = "global_set";
inline constexpr char kStepChatSend[] = "chat_send";
inline constexpr char kStepUserInfo[] = "user_info";
inline constexpr char kStepTwitchShoutout[] = "twitch_shoutout";
inline constexpr char kStepTwitchAnnounce[] = "twitch_announce";
inline constexpr char kStepPlaySound[] = "play_sound";
inline constexpr char kStepOverlaySource[] = "overlay_source";
inline constexpr char kStepObsSourceVisible[] = "obs_source_visible";
inline constexpr char kStepCameraPreset[] = "camera_preset"; // P15 f1
inline constexpr char kStepLightPreset[] = "light_preset";   // P15 f1
inline constexpr char kStepScript[] = "script";
inline constexpr char kStepUnsupported[] = "unsupported";

// Pause höchstens 1 h (Plan 2.4)
inline constexpr int kMaxDelayMs = 3600 * 1000;

// Alle Typen in der Reihenfolge des Plans
const QList<StepTypeInfo> &stepTypes();
// nullptr, wenn der Typ unbekannt ist
const StepTypeInfo *stepType(const QString &key);
// Typ zur Nummer bei Streamer.bot; jede unbekannte Nummer (auch 0) ergibt «unsupported».
QString stepTypeForSbType(int sbType);

// Vergleiche des Schritts «Wenn» (Plan 2.4). regex wird gespeichert, aber nicht ausgeführt
// (Regex später); der Verweis-Check meldet es.
QStringList ifOperations();

} // namespace dss::actions
