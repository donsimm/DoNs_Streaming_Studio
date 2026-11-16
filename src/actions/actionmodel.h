#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>

// Modell des Aktionssystems (P14 a, Plan 2.2 bis 2.8): Aktionen mit Auslösern und Schritten,
// Befehle, Zeitnachrichten, Warteschlangen, Benutzergruppen. Nur Qt Core, kein Qt Gui, kein Netz,
// kein protocol.h (CLAUDE.md Regel 9 und 14). Format der Datei: actionjson.h.
//
// Felder und Aufbau wie Streamer.bot (Fassung 24), Namen in snake_case. Jedes Objekt behält
// unbekannte Felder in extra (Plan 2.1): Sie werden beim Lesen gesammelt und beim Schreiben
// unverändert wieder ausgegeben. Nichts am Aussehen steht im Modell, Schritte verweisen nur auf
// Overlays und Elemente (Plan 2.1, Abschnitt 8).

namespace dss::actions {

// Schritt (Sub-Action). Die Felder des Typs stehen in fields und sind gegen steptypes.h geprüft;
// so trägt ein Typ neue Felder ohne neue Struktur. Gruppe, Wenn, Dann und Sonst tragen steps
// (verschachtelt wie Fassung 24); «Wenn» hat genau die Kinder «Dann» (if_true) und «Sonst» (if_false).
struct Step
{
    QUuid id;
    QString type;
    bool enabled = true;
    double weight = 0;    // Gewicht bei «zufällig einen» (random_action, Gruppe mit random)
    QJsonObject fields;   // Felder des Typs (steptypes.h)
    QList<Step> steps;    // nur bei Typen mit hasSteps
    QJsonObject extra;

    // ausserhalb definiert: Step ist hier für QList<Step>::operator== noch unvollständig
    bool operator==(const Step &other) const;
};

// Auslöser (Trigger). Die Filter stehen in fields, geprüft gegen triggertypes.h.
struct Trigger
{
    QUuid id;
    QString type;
    bool enabled = true;
    QJsonObject fields;
    QJsonObject extra;

    bool operator==(const Trigger &) const = default;
};

struct Action
{
    QUuid id;
    QString name;
    QString group;             // nur zum Ordnen, leer = ohne Gruppe
    bool enabled = true;
    QUuid queue;               // null = «Standard» (auch die UUID aus lauter Nullen in der Datei)
    bool excludeFromHistory = false;
    bool randomAction = false;
    bool concurrent = false;   // gespeichert, nicht ausgeführt (Plan 2.3)
    bool alwaysRun = false;
    QUuid templateId;          // mitgelieferte Aktion (Plan 7), null = eigene
    QList<Trigger> triggers;
    QList<Step> steps;
    QJsonObject extra;

    bool operator==(const Action &) const = default;
};

// Zuschauer in Benutzergruppen und Rechten, über die Twitch-Benutzer-ID (Plan 2.9)
struct UserRef
{
    QString userId;
    QJsonObject extra;

    bool operator==(const UserRef &) const = default;
};

struct Permissions
{
    QString grant = QStringLiteral("allow"); // allow, deny
    QStringList roles;                        // subscriber, vip, moderator
    QList<QUuid> groups;                      // Benutzergruppen
    QList<UserRef> users;
    QJsonObject extra;

    bool operator==(const Permissions &) const = default;
};

struct Command
{
    QUuid id;
    QString name;
    bool enabled = true;
    QString group;
    QStringList commands;              // ein Befehl je Eintrag, zum Beispiel «!discord»
    QString mode = QStringLiteral("basic");      // basic; regex gespeichert, nicht ausgeführt
    QString location = QStringLiteral("start");  // start, exact, anywhere
    bool caseSensitive = false;
    bool ignoreBotAccount = true;
    bool ignoreInternal = true;
    QStringList sources{QStringLiteral("twitch_message")};
    int globalCooldownS = 0;
    int userCooldownS = 0;
    bool persistCounter = false;
    bool persistUserCounter = false;
    Permissions permissions;
    QJsonObject extra;

    bool operator==(const Command &) const = default;
};

struct Timer
{
    QUuid id;
    QString name;
    bool enabled = true;
    bool repeat = true;
    int intervalS = 1200;
    bool random = false;
    int randomMinS = 0;
    int randomMaxS = 0;
    int lines = 0;          // so viele Chat-Nachrichten seit dem letzten Lauf nötig
    bool onlyLive = true;   // eigene Erweiterung, Streamer.bot hat das Feld nicht (Plan 2.7)
    QJsonObject extra;

    bool operator==(const Timer &) const = default;
};

struct Queue
{
    QUuid id;
    QString name;
    bool blocking = false;
    QJsonObject extra;

    bool operator==(const Queue &) const = default;
};

struct UserGroup
{
    QUuid id;
    QString name;
    QList<UserRef> users;
    QJsonObject extra;

    bool operator==(const UserGroup &) const = default;
};

// Inhalt von aktionen.json
struct ActionsConfig
{
    bool enabled = false; // Hauptschalter «Chat-Bot aktiv», beim ersten Start aus
    QList<Queue> queues;  // ohne «Standard»: der gilt immer und steht nicht in der Datei
    QList<UserGroup> groups;
    QList<Action> actions;
    QList<Command> commands;
    QList<Timer> timers;
    QJsonObject extra;

    bool operator==(const ActionsConfig &) const = default;

    const Action *action(const QUuid &id) const;
    const Command *command(const QUuid &id) const;
    const Timer *timer(const QUuid &id) const;
    const Queue *queue(const QUuid &id) const;
    const UserGroup *group(const QUuid &id) const;
};

// Mindestabstand einer Zeitnachricht (Plan 2.7)
inline constexpr int kMinTimerIntervalS = 60;
// Höchste Tiefe der Verschachtelung von Schritten. Streamer.bot nutzt beim Nutzer bis 3
// (Plan, Befunde); die Grenze schützt das Lesen vor beliebig tiefen Dateien.
inline constexpr int kMaxStepDepth = 32;

// Neuer Schritt mit neuer Kennung und den Standardwerten des Typs (steptypes.h). «Wenn» bekommt
// gleich seine Kinder «Dann» und «Sonst». Unbekannter Typ: Schritt mit leerer Kennung.
Step newStep(const QString &type);
// Neuer Auslöser mit neuer Kennung und den Standardwerten der Filter (triggertypes.h).
Trigger newTrigger(const QString &type);

// Ruft f für jeden Schritt in steps und allen Unterlisten auf, in der Reihenfolge der Anzeige
// (Tiefe zuerst). path: Nummern ab 1 je Ebene, zum Beispiel {2, 1, 3}.
template <typename F>
void forEachStep(const QList<Step> &steps, F &&f, QList<int> path = {})
{
    for (qsizetype i = 0; i < steps.size(); ++i) {
        path.append(int(i) + 1);
        f(steps.at(i), path);
        forEachStep(steps.at(i).steps, f, path);
        path.removeLast();
    }
}

} // namespace dss::actions
