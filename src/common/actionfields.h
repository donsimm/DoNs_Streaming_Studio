#pragma once

#include "actionmodel.h"
#include "fieldspec.h"

#include <QList>
#include <QPair>
#include <QString>

// Felder der rechten Spalte im Unterreiter «Aktionen» (P14 e2-8, Plan 4 und 6): Beschriftung,
// Reihenfolge und Abschnitte der Felder eines Schritts. Nur Qt Core (CLAUDE.md Regel 9): Das Fenster
// zeigt die Abschnitte und Zeilen, wie sie hier stehen, und erfindet keine eigenen Texte.
//
// Jedes Feld aus StepTypeInfo::fields (steptypes.h) kommt in stepForm genau einmal vor; Art und
// Grenzen stammen von dort (Zeiten umgerechnet in Sekunden). Die Elemente des Overlay-Schritts sind eine Zeile der Art ElementList;
// den Inhalt je Element beschreibt elementForm.
//
// Auslöser (P14 e2-9, Plan 2.5): triggerForm enthält jeden Filter aus TriggerTypeInfo::filters
// (triggertypes.h) genau einmal, ebenso mit Art und Grenzen von dort. Die Testwerte des Auslösers
// «Test» sind eine Zeile der Art StringMap.

namespace dss {

struct FieldRow
{
    QString name;  // Feld in aktionen.json
    QString label; // Beschriftung im Fenster
    actions::FieldKind kind = actions::FieldKind::String;
    QList<QPair<QString, QString>> values; // Wert und deutsche Bezeichnung (Enum, Vorschläge bei Text)
    // Grenzen, Einheit und Nachkommastellen gelten für den angezeigten Wert (P14 e2-9b): Zeiten in ms
    // zeigt das Fenster in Sekunden (scale 1000, suffix «s», 3 Nachkommastellen). Umrechnen nur mit
    // toDisplay und fromDisplay, das Fenster rechnet nicht selbst.
    double min = 0;
    double max = 0;
    double scale = 1; // gespeicherter Wert = angezeigter Wert × scale
    int decimals = 0; // Nachkommastellen des angezeigten Werts
    QString suffix;  // Einheit hinter dem Feld («s», «%»), leer = keine
    QString choices; // Auswahl aus den Daten: overlay, source, media, action, queue, command, timer; leer = keine
    // Leer = wirkt. Sonst sichtbar, aber ausgegraut, mit diesem Text als Tooltip (P14 e2-9b).
    QString inactiveReason;
};

// Knopf eines Abschnitts ohne eigenes Feld, z. B. «Anhören» beim Ton (Plan 6)
struct FieldButton
{
    QString name;           // Kennung für das Fenster
    QString label;          // Beschriftung
    QString inactiveReason; // wie FieldRow::inactiveReason
};

struct FieldSection
{
    QString title;
    QList<FieldRow> rows;
    QList<FieldButton> buttons = {}; // unter den Zeilen
    QString hint = {};               // Hinweis unter dem Abschnitt (grau), meist leer
};

struct ActionViewNames;

// Eintrag einer Auswahl aus den Daten (FieldRow::choices), P14 e2-16
struct FieldChoice
{
    QString value; // Kennung in Kleinbuchstaben ohne Klammern; leer = null («Keine»)
    QString label;
    bool operator==(const FieldChoice &) const = default;
};
// Auswahl zu FieldRow::choices mit Kennungen (overlay, source, action, command, timer, queue):
// «Keine», dann die Einträge nach Name. Fehlt current in den Daten, steht er als «<Kennung> (fehlt)»
// am Ende, damit das Feld den Wert des Kerns zeigen kann. Andere Arten (media, leer): leer.
QList<FieldChoice> fieldChoices(const QString &choices, const actions::ActionsConfig &config,
                                const ActionViewNames &names, const QString &current);

// Gespeicherter Wert (aus aktionen.json) in den angezeigten Wert umrechnen, z. B. 8500 ms → 8,5 s.
double toDisplay(const FieldRow &row, double stored);
// Angezeigten Wert zurück in den gespeicherten; bei Art Int auf ganze Zahlen gerundet.
double fromDisplay(const FieldRow &row, double shown);

// Feld für einen Abstand der Zeitnachricht (interval_s, random_min_s, random_max_s), P14 e3-2:
// gespeichert in Sekunden, angezeigt in Minuten (scale 60, suffix «min», 1 Nachkommastelle, Art Int),
// kleinster Wert 1 min (= kMinTimerIntervalS), grösster 1440 min (= kMaxTimerIntervalS). Beschriftung vom Aufrufer.
FieldRow minutesRow(const QString &name, const QString &label);

// Abschnitte zum Schritttyp; leer bei Typen ohne Felder (Dann, Sonst, Abbruch) und unbekannten.
QList<FieldSection> stepForm(const QString &type);
// Inhalt je Element des Overlay-Schritts (Felder source, text, image eines Eintrags in elements).
FieldSection elementForm();
// «Schritt «<Art>»»; Art = StepTypeInfo::label, bei unbekanntem Typ der Typ selbst.
QString stepTitle(const actions::Step &step);
// Abschnitte zum Auslösertyp; leer bei Typen ohne Filter (Chat-Nachricht, Erste Nachricht, Follow,
// Stream offline) und unbekannten.
QList<FieldSection> triggerForm(const QString &type);
// «Auslöser «<Art>»»; Art = TriggerTypeInfo::label, bei unbekanntem Typ der Typ selbst.
QString triggerTitle(const actions::Trigger &trigger);
// Grauer Hinweis rechts für Arten, deren Form leer ist (Entscheid des Nutzers, 2026-11-16).
QString emptyFormHint();
// Titel des Abschnitts mit dem Feld (Schritt oder Auslöser), für die Prüfmeldung darunter; leer, wenn
// das Feld fehlt.
QString fieldSection(const QList<FieldSection> &form, const QString &fieldName);

} // namespace dss
