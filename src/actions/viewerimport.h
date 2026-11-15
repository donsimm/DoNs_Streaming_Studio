#pragma once

#include "actionmodel.h"
#include "variables.h"
#include "viewerdata.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

// Import der Zuschauer-Daten aus Streamer.bot (P14 z1a, Plan 9.1 bis 9.4). Nur Qt Core.
//
// Liest den Export von scripts/streamerbot-export/ (z0), ordnet ihn nach Plan 9.3 zu und meldet in
// einem Probelauf, was wohin käme. Endgültig: zuerst Sicherung der betroffenen Dateien, dann
// schreiben. Die Kern-Anfrage viewer_import und dss-cli zuschauer-import kommen mit z1b; dort
// schreibt nur der Kern (K-03).
//
// Format des Exports (z0): je LiteDB-Datei ein Ordner ohne Endung, darin je Sammlung
// <sammlung>.json (Array der Dokumente; Datum ISO 8601 in UTC, ObjectId und Guid als Text). Die
// JSON-Texte quotes.dat und users.dat liegen unverändert als quotes.json und users.json im Export.
//
// Datenschutz (Plan 2.10, 9.1): Der Bericht nennt nur Anzahlen und Namen von Variablen, keine Namen
// oder IDs von Zuschauern und keine Werte. Ordner und Dateien, deren Name mit «auth» beginnt, werden
// nie gelesen.

namespace dss::actions {

// --- Export lesen ---

struct ExportCollection
{
    QString file;       // Datei in Streamer.bot, zum Beispiel «globals.db»
    QString collection; // Sammlung, zum Beispiel «user_globals»
    QJsonArray documents;
};

struct StreamerbotExport
{
    QList<ExportCollection> collections;  // sortiert nach Datei und Sammlung
    std::optional<QJsonObject> quotes;    // quotes.dat
    std::optional<QJsonObject> users;     // users.dat
    QMap<QString, QJsonObject> otherJson; // weitere JSON-Texte, «<name>.dat» → Inhalt
    QStringList skipped;                  // nie gelesen (auth*) oder kein JSON, Name im Export
};

// Liest den Ordner …/export/. false bei fehlendem Ordner oder ungültigem JSON (Meldung ohne Inhalt).
bool readStreamerbotExport(const QString &dir, StreamerbotExport *out, QString *error);

// --- Stand im Studio ---

// Die Dateien, die der Import liest und schreibt. actions fehlt, wenn aktionen.json fehlt (dann
// werden keine Benutzergruppen übernommen; der Kern legt die Datei beim Start an, Plan 2.11).
struct ImportState
{
    VariablesData variables;
    ViewersData viewers;
    QuotesData quotes;
    ArchiveData archive;
    std::optional<ActionsConfig> actions;

    bool operator==(const ImportState &) const = default;
};

// Liest die Dateien aus dem Datenordner. Fehlende Dateien gelten als leer. Eine ungültige Datei
// bricht ab (sie wird nicht überschrieben, Plan 2.11).
bool loadImportState(const QString &dataDir, ImportState *state, QString *error);

// --- Zuordnung und Probelauf ---

struct ImportOptions
{
    bool overwrite = false; // --ueberschreiben: auch im Studio geänderte Werte ersetzen
    QDateTime now;          // UTC; Zeitpunkt für Werte ohne Datum in Streamer.bot
};

struct ImportCount
{
    int added = 0;       // neu
    int replaced = 0;    // ersetzt (Wert aus Streamer.bot)
    int unchanged = 0;   // gleicher Wert schon da
    int kept = 0;        // im Studio geändert, bleibt (ohne overwrite)
    int overwritten = 0; // im Studio geändert, mit overwrite ersetzt

    bool operator==(const ImportCount &) const = default;
};

struct ImportReport
{
    ImportCount globalVariables;
    ImportCount userVariables;
    ImportCount commandCounters; // Zähler über alle und je Zuschauer
    ImportCount viewers;
    ImportCount quotes;
    ImportCount groups;

    QMap<QString, int> userVariableNames;   // Name → Anzahl Zuschauer
    QStringList globalVariableNames;
    QMap<QString, int> keptVariableNames;   // im Studio geändert, bleibt: Name → Anzahl
    QMap<QString, int> overwrittenVariableNames;

    int withoutId = 0;   // Dokumente mit Zuschauer, aber ohne Twitch-ID (ins Archiv)
    int duplicates = 0;  // doppelte Einträge desselben Zuschauers (der neuere gilt)
    bool groupsSkipped = false; // aktionen.json fehlt: Gruppen nicht übernommen
    QMap<QString, int> archived;            // «datei/sammlung» → Anzahl ins Archiv
    QStringList emptyCollections;           // «datei/sammlung»
    QStringList skipped;                    // nie gelesen
    QMap<QString, int> userTypes;           // users.dat: type → Anzahl Einträge
};

struct ImportResult
{
    ImportState state; // Stand nach dem Import
    ImportReport report;
};

// Reine Funktion: schreibt nichts (Probelauf). Werte aus Streamer.bot ersetzen gleichnamige; Werte
// mit Herkunft «studio» bleiben, ausser mit overwrite. Nur im Studio vorhandene bleiben immer.
ImportResult planViewerImport(const StreamerbotExport &source, const ImportState &current,
                              const ImportOptions &options);

// Bericht auf Deutsch, mehrere Zeilen, ohne Namen oder IDs von Zuschauern und ohne Werte
QString importReportText(const ImportReport &report, bool final);

// --- Endgültig ---

// Name des Ordners der Sicherung, zum Beispiel «sicherung-20261115-120000»
QString importBackupDirName(const QDateTime &now);

// Schreibt die Dateien, die sich gegenüber before ändern. Zuerst kommen die bisherigen Fassungen
// dieser Dateien nach backupDir (darf noch nicht bestehen); erst dann wird geschrieben (QSaveFile).
// *written: Namen der geschriebenen Dateien.
bool writeViewerImport(const QString &dataDir, const QString &backupDir, const ImportState &before,
                       const ImportState &after, QStringList *written, QString *error);

} // namespace dss::actions
