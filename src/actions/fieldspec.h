#pragma once

#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>

#include <limits>

// Beschreibung der Felder eines Schritts oder Auslösers (P14 a, Plan 2.4 und 2.5). Nur Qt Core
// (CLAUDE.md Regel 9). Die Tabellen in steptypes.cpp und triggertypes.cpp nutzen sie zum Prüfen
// beim Lesen von aktionen.json und für die Werte eines neu angelegten Schritts.

namespace dss::actions {

enum class FieldKind {
    Bool,
    Int,            // ganze Zahl in [min, max]
    Number,         // Zahl in [min, max], auch mit Nachkommastellen
    String,         // Text, auch leer
    NullableString, // Text oder null
    NullableUuid,   // UUID in Kleinbuchstaben oder null (noch nichts gewählt)
    Enum,           // Text aus values
    StringList,     // Liste von Texten; mit values nur diese, ohne Doppelte
    StringMap,      // Objekt Name → Text (Testwerte)
    Object,         // beliebiges Objekt (Rohdaten, zum Beispiel unsupported.original)
    ElementList,    // Liste der Elemente eines Overlay-Schritts (source, text, image)
};

struct FieldSpec
{
    QString name;
    FieldKind kind = FieldKind::String;
    QJsonValue defaultValue; // Wert bei einem neuen Schritt oder Auslöser
    QStringList values;      // Enum und StringList
    double min = std::numeric_limits<double>::lowest();
    double max = std::numeric_limits<double>::max();
};

// Leerer Text = gültig, sonst die Meldung ohne Feldnamen («muss true oder false sein»).
QString checkFieldValue(const FieldSpec &spec, const QJsonValue &value);

// Text einer UUID wie in allen Dateien des Datenordners (Kleinbuchstaben, ohne Klammern).
QString uuidText(const QUuid &id);
// Liest eine UUID nur in dieser Schreibweise; null-UUID und andere Schreibweisen sind ungültig.
bool parseUuidText(const QString &text, QUuid *id);

} // namespace dss::actions
