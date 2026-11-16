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
// Grenzen stammen von dort. Die Elemente des Overlay-Schritts sind eine Zeile der Art ElementList;
// den Inhalt je Element beschreibt elementForm.

namespace dss {

struct FieldRow
{
    QString name;  // Feld in aktionen.json
    QString label; // Beschriftung im Fenster
    actions::FieldKind kind = actions::FieldKind::String;
    QList<QPair<QString, QString>> values; // Wert und deutsche Bezeichnung (Enum, Vorschläge bei Text)
    double min = 0;
    double max = 0;
    QString suffix;  // Einheit hinter dem Feld («ms», «%»), leer = keine
    QString choices; // Auswahl aus den Daten: overlay, source, media, action, queue; leer = keine
};

struct FieldSection
{
    QString title;
    QList<FieldRow> rows;
};

// Abschnitte zum Schritttyp; leer bei Typen ohne Felder (Dann, Sonst, Abbruch) und unbekannten.
QList<FieldSection> stepForm(const QString &type);
// Inhalt je Element des Overlay-Schritts (Felder source, text, image eines Eintrags in elements).
FieldSection elementForm();
// «Schritt «<Art>»»; Art = StepTypeInfo::label, bei unbekanntem Typ der Typ selbst.
QString stepTitle(const actions::Step &step);
// Titel des Abschnitts mit dem Feld, für die Prüfmeldung darunter; leer, wenn das Feld fehlt.
QString fieldSection(const QList<FieldSection> &form, const QString &fieldName);

} // namespace dss
