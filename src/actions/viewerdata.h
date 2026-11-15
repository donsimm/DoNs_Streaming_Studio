#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>

#include <functional>

// Weitere Dateien mit Zuschauer-Daten (P14 a, Plan 2.10): zuschauer.json, zitate.json,
// streamerbot-archiv.json. Nur Qt Core. Gefüllt vom Import aus Streamer.bot (z1), zuschauer.json
// im Betrieb nachgeführt aus Chat und «Benutzerinfo» (b, c).
//
// Datenschutz: Die Dateien liegen nur im Datenordner, nie im Repo, im Protokoll, in Berichten oder
// in Tests (Tests nutzen erfundene Daten).
//
// Die Felder aus Streamer.bot stehen erst nach Schritt 0 von z0 fest (Struktur der Dateien). Darum
// sind hier nur die Felder Pflicht, die der Plan nennt; alle weiteren bleiben unverändert erhalten.

namespace dss::actions {

// --- zuschauer.json ---

inline constexpr int kViewersFormatVersion = 1;
inline constexpr char kViewersFileName[] = "zuschauer.json";

struct Viewer
{
    QString login;        // Kleinbuchstaben, nicht leer
    QString displayName;  // Anzeigename, darf leer sein
    QJsonObject extra;    // weitere Felder aus Streamer.bot, Namen unverändert

    bool operator==(const Viewer &) const = default;
};

struct ViewersData
{
    QMap<QString, Viewer> viewers; // Twitch-Benutzer-ID → Zuschauer

    bool operator==(const ViewersData &) const = default;
};

// {"format_version": 1, "viewers": {"<Twitch-ID>": {"login": …, "display_name": …, …}}}
QByteArray serializeViewers(const ViewersData &data);
bool parseViewers(const QByteArray &bytes, ViewersData *data, QString *error);

// Nachführen aus einer Chat-Nachricht oder «Benutzerinfo» (nur Login und Anzeigename, Plan 2.10).
// Weitere Felder bleiben. true, wenn sich etwas geändert hat; leere ID oder leerer Login ändern nichts.
bool noteViewer(ViewersData *data, const QString &userId, const QString &login, const QString &displayName);

// --- zitate.json ---

inline constexpr int kQuotesFormatVersion = 1;
inline constexpr char kQuotesFileName[] = "zitate.json";

struct QuotesData
{
    QList<QJsonObject> quotes; // Felder aus quotes.dat, unverändert (Schritt 0 von z0)

    bool operator==(const QuotesData &) const = default;
};

// {"format_version": 1, "quotes": [{…}, …]}
QByteArray serializeQuotes(const QuotesData &data);
bool parseQuotes(const QByteArray &bytes, QuotesData *data, QString *error);

// --- streamerbot-archiv.json ---

inline constexpr int kArchiveFormatVersion = 1;
inline constexpr char kArchiveFileName[] = "streamerbot-archiv.json";

struct ArchiveData
{
    // Datei aus Streamer.bot (zum Beispiel «kick_data.db») → Sammlung → Inhalt, unverändert als JSON
    QMap<QString, QJsonObject> files;

    bool operator==(const ArchiveData &) const = default;
};

// {"format_version": 1, "files": {"<Datei>": {"<Sammlung>": <JSON>}}}
QByteArray serializeArchive(const ArchiveData &data);
bool parseArchive(const QByteArray &bytes, ArchiveData *data, QString *error);

// --- alle Dateien des Aktionssystems ---

// Für dss-core --format-pruefen (Plan 2.11): Name im Datenordner, aktuelle Version und strenges
// Lesen. Reihenfolge: aktionen.json, variablen.json, zuschauer.json, zitate.json, Archiv.
struct DataFileFormat
{
    QString fileName;
    int version = 0;
    std::function<bool(const QByteArray &, QString *)> check;
};
QList<DataFileFormat> actionDataFiles();

} // namespace dss::actions
