#include "actionfields.h"

#include "actionview.h"
#include "steptypes.h"
#include "triggertypes.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dss {

using namespace actions;

namespace {

using Values = QList<QPair<QString, QString>>;

// Zeile ohne Art und Grenzen; die kommen in stepForm aus StepTypeInfo::fields.
FieldRow row(const QString &name, const QString &label, const Values &values = {},
             const QString &suffix = QString(), const QString &choices = QString())
{
    FieldRow r;
    r.name = name;
    r.label = label;
    r.values = values;
    r.suffix = suffix;
    r.choices = choices;
    return r;
}

// Zeit, gespeichert in ms, angezeigt in Sekunden mit 3 Nachkommastellen (jede ms bleibt erhalten).
// Grenzen rechnet withSpecs um.
FieldRow secondsRow(const QString &name, const QString &label)
{
    FieldRow r = row(name, label, {}, QStringLiteral("s"));
    r.scale = 1000;
    r.decimals = 3;
    return r;
}

// Sichtbar, aber ausgegraut, bis die Funktion wirkt
FieldRow inactive(FieldRow r, const QString &reason)
{
    r.inactiveReason = reason;
    return r;
}

// Gleiche Texte wie die Kurzinhalte der Schritte in actionview.cpp
Values ifOperationValues()
{
    return {{QStringLiteral("equals"), QStringLiteral("ist gleich")},
            {QStringLiteral("not_equals"), QStringLiteral("ist nicht gleich")},
            {QStringLiteral("equals_ignore_case"), QStringLiteral("ist gleich (ohne Gross-/Kleinschreibung)")},
            {QStringLiteral("not_equals_ignore_case"), QStringLiteral("ist nicht gleich (ohne Gross-/Kleinschreibung)")},
            {QStringLiteral("contains"), QStringLiteral("enthält")},
            {QStringLiteral("less_than"), QStringLiteral("ist kleiner als")},
            {QStringLiteral("greater_than"), QStringLiteral("ist grösser als")},
            {QStringLiteral("is_empty"), QStringLiteral("ist leer")},
            {QStringLiteral("not_exists"), QStringLiteral("existiert nicht")},
            {QStringLiteral("regex"), QStringLiteral("passt zum Regex")}};
}

// Variable lesen und setzen: Name, für wen, dauerhaft (Plan 4.6; Texte wie Unterreiter «Variablen»)
FieldSection variableSection()
{
    return {QStringLiteral("Variable"),
            {row(QStringLiteral("name"), QStringLiteral("Name")),
             row(QStringLiteral("scope"), QStringLiteral("Gilt"),
                 {{QStringLiteral("global"), QStringLiteral("für alle")},
                  {QStringLiteral("user"), QStringLiteral("je Zuschauer")}}),
             row(QStringLiteral("user"), QStringLiteral("Zuschauer"),
                 {{QStringLiteral("trigger"), QStringLiteral("auslösende Person")},
                  {QStringLiteral("target"), QStringLiteral("Ziel")}}),
             row(QStringLiteral("persisted"), QStringLiteral("dauerhaft"))}};
}

// Abschnitte je Typ ohne Art und Grenzen (Plan 2.4, 4.1–4.10 und 6)
QList<FieldSection> sectionsFor(const QString &type)
{
    const QString media = QStringLiteral("media");
    if (type == QLatin1String(kStepComment))
        return {{QStringLiteral("Kommentar"),
                 {row(QStringLiteral("text"), QStringLiteral("Text")),
                  row(QStringLiteral("color"), QStringLiteral("Farbe"))}}};
    if (type == QLatin1String(kStepGroup))
        return {{QStringLiteral("Gruppe"),
                 {row(QStringLiteral("name"), QStringLiteral("Name")),
                  row(QStringLiteral("random"), QStringLiteral("zufällig einen"))}}};
    if (type == QLatin1String(kStepIf))
        return {{QStringLiteral("Vergleich"),
                 {row(QStringLiteral("input"), QStringLiteral("Eingabe")),
                  row(QStringLiteral("operation"), QStringLiteral("Vergleich"), ifOperationValues()),
                  row(QStringLiteral("value"), QStringLiteral("Wert")),
                  row(QStringLiteral("auto_type"), QStringLiteral("Zahlen erkennen"))}}};
    if (type == QLatin1String(kStepDelay))
        return {{QStringLiteral("Pause"),
                 {secondsRow(QStringLiteral("value_ms"), QStringLiteral("Dauer")),
                  row(QStringLiteral("random"), QStringLiteral("zufällig")),
                  secondsRow(QStringLiteral("max_value_ms"), QStringLiteral("bis"))}}};
    if (type == QLatin1String(kStepRunAction))
        return {{QStringLiteral("Aktion"),
                 {row(QStringLiteral("action_id"), QStringLiteral("Aktion"), {}, {}, QStringLiteral("action")),
                  row(QStringLiteral("immediately"), QStringLiteral("sofort, im selben Lauf"))}}};
    if (type == QLatin1String(kStepSetArgument))
        return {{QStringLiteral("Argument"),
                 {row(QStringLiteral("name"), QStringLiteral("Name")),
                  row(QStringLiteral("value"), QStringLiteral("Wert")),
                  row(QStringLiteral("auto_type"), QStringLiteral("Zahlen erkennen"))}}};
    if (type == QLatin1String(kStepGlobalGet))
        return {variableSection(),
                {QStringLiteral("Ergebnis"),
                 {row(QStringLiteral("destination"), QStringLiteral("in Argument")),
                  row(QStringLiteral("default"), QStringLiteral("Wert, wenn sie fehlt"))}}};
    if (type == QLatin1String(kStepGlobalSet))
        return {variableSection(),
                {QStringLiteral("Änderung"),
                 {row(QStringLiteral("operation"), QStringLiteral("Änderung"),
                      {{QStringLiteral("set"), QStringLiteral("Setzen")},
                       {QStringLiteral("increment"), QStringLiteral("Erhöhen")},
                       {QStringLiteral("decrement"), QStringLiteral("Verringern")},
                       {QStringLiteral("delete"), QStringLiteral("Löschen")}}),
                  row(QStringLiteral("value"), QStringLiteral("Wert"))}}};
    if (type == QLatin1String(kStepChatSend))
        return {{QStringLiteral("Nachricht"),
                 {row(QStringLiteral("text"), QStringLiteral("Text")),
                  row(QStringLiteral("reply"), QStringLiteral("als Antwort"))}},
                {QStringLiteral("Konto"),
                 {row(QStringLiteral("use_bot"), QStringLiteral("als Bot")),
                  row(QStringLiteral("fallback"), QStringLiteral("als Kanal, wenn der Bot nicht angemeldet ist"))}}};
    if (type == QLatin1String(kStepUserInfo))
        return {{QStringLiteral("Benutzer"), {row(QStringLiteral("login"), QStringLiteral("Benutzer"))}}};
    if (type == QLatin1String(kStepTwitchShoutout))
        return {{QStringLiteral("Shoutout"), {row(QStringLiteral("login"), QStringLiteral("Benutzer"))}}};
    // Farben laut Schritt 0 (B 17), siehe steptypes.cpp; das Feld bleibt Text
    if (type == QLatin1String(kStepTwitchAnnounce))
        return {{QStringLiteral("Ansage"),
                 {row(QStringLiteral("text"), QStringLiteral("Text")),
                  row(QStringLiteral("color"), QStringLiteral("Farbe"),
                      {{QStringLiteral("primary"), QStringLiteral("Farbe des Kanals")},
                       {QStringLiteral("blue"), QStringLiteral("blau")},
                       {QStringLiteral("green"), QStringLiteral("grün")},
                       {QStringLiteral("orange"), QStringLiteral("orange")},
                       {QStringLiteral("purple"), QStringLiteral("violett")}})}},
                {QStringLiteral("Konto"), {row(QStringLiteral("use_bot"), QStringLiteral("als Bot"))}}};
    // «Anhören» ist ein Knopf (Plan 6), kein Feld; ausgegraut bis P14 e3 (Entscheid Nutzer, 2026-11-16)
    if (type == QLatin1String(kStepPlaySound))
        return {{QStringLiteral("Ton"),
                 {row(QStringLiteral("file"), QStringLiteral("Datei"), {}, {}, media),
                  row(QStringLiteral("volume"), QStringLiteral("Lautstärke"), {}, QStringLiteral("%")),
                  row(QStringLiteral("wait"), QStringLiteral("Warten, bis zu Ende"))},
                 {{QStringLiteral("listen"), QStringLiteral("Anhören"),
                   QStringLiteral("Anhören kommt mit einer späteren Version.")}}}};
    if (type == QLatin1String(kStepOverlaySource)) {
        const Values transition{{QStringLiteral("none"), QStringLiteral("ohne")},
                                {QStringLiteral("fade"), QStringLiteral("weich")}};
        // Ein- und Ausblenden wirkt erst ab P14-2: sichtbar, aber ausgegraut (Entscheid Nutzer, 2026-11-16)
        const QString notYet = QStringLiteral("Wirkt noch nicht: weiches Ein- und Ausblenden kommt mit einer "
                                              "späteren Version.");
        return {{QStringLiteral("Overlay"),
                 {row(QStringLiteral("overlay"), QStringLiteral("Overlay"), {}, {}, QStringLiteral("overlay")),
                  row(QStringLiteral("mode"), QStringLiteral("Wirkung"),
                      {{QStringLiteral("show"), QStringLiteral("zeigen")},
                       {QStringLiteral("hide"), QStringLiteral("verstecken")},
                       {QStringLiteral("toggle"), QStringLiteral("umschalten")}})}},
                {QStringLiteral("Elemente"),
                 {row(QStringLiteral("elements"), QStringLiteral("Elemente"), {}, {}, QStringLiteral("source"))}},
                {QStringLiteral("Dauer"),
                 {secondsRow(QStringLiteral("duration_ms"), QStringLiteral("Dauer")),
                  row(QStringLiteral("wait"), QStringLiteral("Warten, bis ausgeblendet"))}},
                {QStringLiteral("Einblenden/Ausblenden"),
                 {inactive(row(QStringLiteral("transition_in"), QStringLiteral("Einblenden"), transition), notYet),
                  inactive(row(QStringLiteral("transition_out"), QStringLiteral("Ausblenden"), transition), notYet),
                  inactive(secondsRow(QStringLiteral("transition_ms"), QStringLiteral("Dauer weich")), notYet)}}};
    }
    if (type == QLatin1String(kStepObsSourceVisible))
        return {{QStringLiteral("OBS-Quelle"),
                 {row(QStringLiteral("scene_name"), QStringLiteral("Szene")),
                  row(QStringLiteral("source_name"), QStringLiteral("Quelle")),
                  row(QStringLiteral("state"), QStringLiteral("Zustand"),
                      {{QStringLiteral("on"), QStringLiteral("ein")},
                       {QStringLiteral("off"), QStringLiteral("aus")},
                       {QStringLiteral("toggle"), QStringLiteral("umschalten")}})}}};
    if (type == QLatin1String(kStepScript))
        return {{QStringLiteral("Skript"),
                 {row(QStringLiteral("language"), QStringLiteral("Sprache")),
                  row(QStringLiteral("code"), QStringLiteral("Code"))}}};
    if (type == QLatin1String(kStepUnsupported))
        return {{QStringLiteral("Streamer.bot"),
                 {row(QStringLiteral("sb_type"), QStringLiteral("Typ bei Streamer.bot")),
                  row(QStringLiteral("original"), QStringLiteral("Rohdaten"))}}};
    return {}; // Dann, Sonst, Abbruch: keine Felder
}

// Bereich «von–bis» einer Anzahl mit Einheit (Plan 2.5; Einheiten wie triggerDetail in actionview.cpp)
FieldSection countSection(const QString &title, const QString &unit)
{
    return {title, {row(QStringLiteral("min"), QStringLiteral("von"), {}, unit),
                    row(QStringLiteral("max"), QStringLiteral("bis"), {}, unit)}};
}

// Stufen der Abos; leere Auswahl = alle (triggertypes.cpp). «Stufe 1» wie triggerDetail.
FieldSection tiersSection()
{
    Values values;
    for (const QString &tier : subscriptionTiers())
        values.append({tier, QStringLiteral("Stufe %1").arg(tier.toInt() / 1000)});
    return {QStringLiteral("Stufe"), {row(QStringLiteral("tiers"), QStringLiteral("Stufen"), values)}};
}

// Abschnitte je Auslösertyp ohne Art und Grenzen (Plan 2.5)
QList<FieldSection> triggerSectionsFor(const QString &type)
{
    if (type == QLatin1String(kTriggerCommand))
        return {{QStringLiteral("Befehl"),
                 {row(QStringLiteral("command_id"), QStringLiteral("Befehl"), {}, {}, QStringLiteral("command"))}}};
    if (type == QLatin1String(kTriggerTimer))
        return {{QStringLiteral("Zeitnachricht"),
                 {row(QStringLiteral("timer_id"), QStringLiteral("Zeitnachricht"), {}, {}, QStringLiteral("timer"))}}};
    if (type == QLatin1String(kTriggerSubscribe) || type == QLatin1String(kTriggerGiftSub))
        return {tiersSection()};
    if (type == QLatin1String(kTriggerResubscribe))
        return {tiersSection(),
                {QStringLiteral("Monate"),
                 {row(QStringLiteral("min_months"), QStringLiteral("ab"), {}, QStringLiteral("Monaten"))}}};
    if (type == QLatin1String(kTriggerGiftBomb))
        return {tiersSection(), countSection(QStringLiteral("Anzahl"), QStringLiteral("Abos"))};
    if (type == QLatin1String(kTriggerCheer)) {
        // Bei Cheer heisst «nur max» genau dieser Wert (Plan 2.5, triggerdispatcher.cpp)
        FieldSection bits = countSection(QStringLiteral("Bits"), QStringLiteral("Bits"));
        bits.hint = QStringLiteral("Nur «bis»: genau dieser Wert");
        return {bits};
    }
    if (type == QLatin1String(kTriggerRaid))
        return {countSection(QStringLiteral("Zuschauer"), QStringLiteral("Zuschauer"))};
    // Gleicher Text wie triggerDetail in actionview.cpp
    if (type == QLatin1String(kTriggerStreamOnline))
        return {{QStringLiteral("Start"),
                 {row(QStringLiteral("skip_at_start"),
                      QStringLiteral("nicht beim Start des Studios im laufenden Stream"))}}};
    if (type == QLatin1String(kTriggerTest))
        return {{QStringLiteral("Testwerte"), {row(QStringLiteral("variables"), QStringLiteral("Testwerte"))}}};
    return {}; // Chat-Nachricht, Erste Nachricht, Follow, Stream offline: keine Filter
}

// Art und Grenzen der Zeilen aus der Feldbeschreibung; Grenzen als angezeigter Wert
QList<FieldSection> withSpecs(QList<FieldSection> form, const QList<FieldSpec> &specs)
{
    for (FieldSection &section : form) {
        for (FieldRow &r : section.rows) {
            for (const FieldSpec &spec : specs) {
                if (spec.name != r.name)
                    continue;
                r.kind = spec.kind;
                if (spec.kind == FieldKind::Int || spec.kind == FieldKind::Number) {
                    r.min = toDisplay(r, spec.min);
                    r.max = toDisplay(r, spec.max);
                }
            }
        }
    }
    return form;
}

} // namespace

double toDisplay(const FieldRow &row, double stored)
{
    return stored / row.scale;
}

double fromDisplay(const FieldRow &row, double shown)
{
    const double stored = shown * row.scale;
    // 8,5 s × 1000 ist in double nicht immer genau 8500; Int-Felder auf ganze ms runden
    return row.kind == FieldKind::Int ? std::round(stored) : stored;
}

FieldRow minutesRow(const QString &name, const QString &label)
{
    FieldRow r = row(name, label, {}, QStringLiteral("min"));
    r.kind = FieldKind::Int;
    r.scale = 60;
    r.decimals = 1;
    r.min = double(kMinTimerIntervalS) / r.scale;
    // Obergrenze wie timerFromJson (int in Sekunden)
    r.max = double(std::numeric_limits<int>::max()) / r.scale;
    return r;
}

QList<FieldSection> stepForm(const QString &type)
{
    const StepTypeInfo *info = stepType(type);
    if (!info)
        return {};
    return withSpecs(sectionsFor(type), info->fields);
}

QList<FieldSection> triggerForm(const QString &type)
{
    const TriggerTypeInfo *info = triggerType(type);
    if (!info)
        return {};
    return withSpecs(triggerSectionsFor(type), info->filters);
}

QString triggerTitle(const Trigger &trigger)
{
    const TriggerTypeInfo *info = triggerType(trigger.type);
    return QStringLiteral("Auslöser «%1»").arg(info ? info->label : trigger.type);
}

QString emptyFormHint()
{
    return QStringLiteral("Keine Einstellungen");
}

FieldSection elementForm()
{
    FieldSection s{QStringLiteral("Inhalt der Quellen"),
                   {row(QStringLiteral("source"), QStringLiteral("Element"), {}, {}, QStringLiteral("source")),
                    row(QStringLiteral("text"), QStringLiteral("Text")),
                    row(QStringLiteral("image"), QStringLiteral("Bild"), {}, {}, QStringLiteral("media"))}};
    // Prüfung in fieldspec.cpp (checkElement): source ist eine UUID, text und image Text oder null
    s.rows[0].kind = FieldKind::NullableUuid;
    s.rows[1].kind = FieldKind::NullableString;
    s.rows[2].kind = FieldKind::NullableString;
    return s;
}

QString stepTitle(const Step &step)
{
    const StepTypeInfo *info = stepType(step.type);
    return QStringLiteral("Schritt «%1»").arg(info ? info->label : step.type);
}

QList<FieldChoice> fieldChoices(const QString &choices, const ActionsConfig &config,
                                const ActionViewNames &names, const QString &current)
{
    QList<FieldChoice> entries;
    const auto add = [&entries](const QUuid &id, const QString &label) {
        entries.append({id.toString(QUuid::WithoutBraces), label});
    };
    if (choices == QLatin1String("overlay")) {
        for (auto it = names.overlays.cbegin(); it != names.overlays.cend(); ++it)
            add(it.key(), it.value());
    } else if (choices == QLatin1String("source")) {
        for (auto it = names.sources.cbegin(); it != names.sources.cend(); ++it)
            add(it.key(), it.value());
    } else if (choices == QLatin1String("action")) {
        for (const Action &a : config.actions)
            add(a.id, a.name);
    } else if (choices == QLatin1String("command")) {
        for (const Command &c : config.commands)
            add(c.id, c.name);
    } else if (choices == QLatin1String("timer")) {
        for (const Timer &t : config.timers)
            add(t.id, t.name);
    } else if (choices == QLatin1String("queue")) {
        for (const Queue &q : config.queues)
            add(q.id, q.name);
    } else {
        return {};
    }
    // Nach Name wie actionChoices; gleiche Namen nach Kennung, damit die Reihenfolge fest ist
    std::sort(entries.begin(), entries.end(), [](const FieldChoice &a, const FieldChoice &b) {
        const int c = a.label.localeAwareCompare(b.label);
        return c != 0 ? c < 0 : a.value < b.value;
    });
    entries.prepend({QString(), QStringLiteral("Keine")});
    const QString wanted = current.toLower();
    if (!wanted.isEmpty()
        && std::none_of(entries.cbegin(), entries.cend(), [&](const FieldChoice &e) { return e.value == wanted; }))
        entries.append({wanted, QStringLiteral("%1 (fehlt)").arg(wanted)});
    return entries;
}

QString fieldSection(const QList<FieldSection> &form, const QString &fieldName)
{
    for (const FieldSection &section : form) {
        for (const FieldRow &r : section.rows) {
            if (r.name == fieldName)
                return section.title;
        }
    }
    return QString();
}

} // namespace dss
