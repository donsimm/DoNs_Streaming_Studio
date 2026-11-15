#pragma once

#include <QHash>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

// Platzhalter wie Streamer.bot (P14 a, Plan 2.9). Nur Qt Core.
//
// - %name% setzt das Argument des Laufs ein; ein unbekanntes bleibt wörtlich stehen.
// - ~name~ setzt eine dauerhafte globale Variable ein; eine unbekannte bleibt wörtlich stehen.
// - Ein Name besteht aus Buchstaben A–Z, a–z, Ziffern und «_». Alles andere zwischen zwei
//   Zeichen % ist kein Platzhalter («100% sicher, %user%» ersetzt nur %user%). Damit bleiben auch
//   Formate wie %name:format% wörtlich stehen (nicht in P14).
// - Gross- und Kleinschreibung zählt. Ob Streamer.bot Namen ohne Rücksicht darauf auflöst, ist
//   im Schritt-0-Bericht vom 2026-11-15 nicht geklärt ([OFFEN]).
// - Eingesetzte Werte werden nicht nochmals durchsucht (keine Platzhalter aus Chat-Texten).
// - Nicht in P14: $math()$, $length()$ (kommen beim Nutzer nicht vor).

namespace dss::actions {

using Arguments = QHash<QString, QJsonValue>;
// Wert einer dauerhaften globalen Variable; nullopt = unbekannt
using GlobalLookup = std::function<std::optional<QJsonValue>(const QString &name)>;

QString replacePlaceholders(const QString &text, const Arguments &arguments,
                            const GlobalLookup &globals = {});

// Text eines Werts beim Einsetzen: Text unverändert; ganze Zahlen ohne Nachkommastellen, andere
// Zahlen in kürzester Form mit Punkt; true/false als «True»/«False» wie .NET (Streamer.bot,
// im Schritt-0-Bericht nicht geklärt); null leer; Listen und Objekte als kompaktes JSON.
QString placeholderText(const QJsonValue &value);

// Namen der Platzhalter %name% bzw. ~name~ im Text, in der Reihenfolge des Vorkommens, ohne Doppelte
QStringList argumentNames(const QString &text);
QStringList globalNames(const QString &text);

// «auto_type» (Plan 2.4): Text, der ganz eine Zahl ist (zum Beispiel «12», «-3», «1.5»), wird
// zur Zahl, sonst bleibt er Text. Variablen sind Text oder Zahl (Plan 2.9); true/false bleiben
// darum Text (ob Streamer.bot sie umwandelt, ist im Schritt-0-Bericht nicht geklärt).
QJsonValue autoTypedValue(const QString &text);

} // namespace dss::actions
