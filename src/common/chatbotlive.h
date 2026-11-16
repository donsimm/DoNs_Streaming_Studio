#pragma once

#include "actionengine.h"

#include <QList>
#include <QString>
#include <QUuid>

// P14 e2-5: Ansichtslogik des Unterreiters «Live» im Reiter «Chat-Bot» (Plan 3.5), ohne Fenster
// (Regel 9). windowdata.h bleibt für P13.

namespace dss {

// --- Bereich «Läufe der Aktionen» ---

// Eine Zeile: Zeit (lokal, hh:mm:ss, Eintreffen des Laufs), Aktion, Auslöser (Bezeichnung der Art),
// Ergebnis. state ist der Zustand für die Farbe: "ok", "warning", "error" oder leer (wartet, läuft).
struct RunRow
{
    QUuid runId;
    QUuid actionId;
    QString time;
    QString action;
    QString trigger;
    QString result;
    QString state;
    bool cancellable = false; // «Lauf abbrechen»: nur wartend und laufend
    bool isError = false;     // state == "error", für «Nur Fehler»

    bool operator==(const RunRow &) const = default;
};
RunRow runRow(const actions::RunRecord &record);
// Ergebnistext, z. B. «läuft, Schritt 2 von 5», «Probelauf · fertig (1.5 s)»
QString runResultText(const actions::RunRecord &record);
// Bezeichnung des Auslösers («Befehl», «Test», «Aktion ausführen», …); unbekannte Art unverändert
QString runTriggerText(const QString &triggerType);

// Liste der Läufe, älteste zuerst, höchstens kMaxRows. Ein Lauf hat genau eine Zeile; Meldungen
// zum selben Lauf (actions.run_started, run_changed, run_finished) ersetzen sie.
class RunRows
{
public:
    static constexpr int kMaxRows = 200;

    // Was das Fenster an der sichtbaren Liste (visibleRows) ändern muss, in dieser Reihenfolge:
    // erst removedFront Zeilen vorne entfernen, dann bei appended eine Zeile an index einfügen,
    // sonst die Zeile an index ersetzen. index -1: sichtbar ändert sich nichts. Eingefügt wird am
    // Ende, ausser bei «Nur Fehler», wenn ein bestehender Lauf zum Fehler wird (dann an seiner Stelle).
    struct Change
    {
        int index = -1;
        bool appended = false;
        int removedFront = 0;

        bool operator==(const Change &) const = default;
    };

    // Ersetzt die Liste (action_runs); von einem längeren Verlauf bleiben die neuesten.
    void reset(const QList<actions::RunRecord> &records);
    Change apply(const actions::RunRecord &record);
    // «Nur Fehler»: sichtbar sind nur Zeilen mit isError. Danach baut das Fenster die Liste neu.
    void setOnlyErrors(bool onlyErrors);
    bool onlyErrors() const { return m_onlyErrors; }
    const QList<RunRow> &rows() const { return m_rows; }
    QList<RunRow> visibleRows() const;
    // Zeile zum Lauf, nullptr wenn nicht (mehr) in der Liste
    const RunRow *row(const QUuid &runId) const;

private:
    bool visible(const RunRow &row) const { return !m_onlyErrors || row.isError; }
    int visibleIndex(int index) const; // Stelle in visibleRows für m_rows[index]

    QList<RunRow> m_rows;
    bool m_onlyErrors = false;
};

} // namespace dss
