#pragma once

#include "actionmodel.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QUuid>

#include <optional>

// Verweise in aktionen.json prüfen (P14 a, Plan 2.11): Warteschlange, Befehl und Zeitnachricht
// eines Auslösers, Ziel von «Aktion ausführen», Benutzergruppen der Rechte, Overlay und Elemente,
// Mediendateien. Ein fehlender Verweis macht die Datei nicht ungültig, er ist eine Warnung im
// Editor; der Schritt meldet ihn im Lauf (b1). Dazu Hinweise auf Gespeichertes, das nicht
// ausgeführt wird (Modus regex, Vergleich regex) und auf doppelte Befehle (Plan 2.6).
//
// Overlays und Mediendateien kennt die Bibliothek nicht (kein dss_common); der Kern gibt sie im
// ReferenceContext mit (b3).

namespace dss::actions {

struct ReferenceContext
{
    // Overlay → Kennungen seiner Quellen; nullopt = nicht prüfen
    std::optional<QHash<QUuid, QSet<QUuid>>> overlays;
    // Verweise «media/<SHA-256>.<Endung>» der Mediathek; nullopt = nicht prüfen
    std::optional<QSet<QString>> mediaFiles;
};

struct ReferenceWarning
{
    QUuid objectId;  // Aktion, Befehl oder Zeitnachricht
    QUuid itemId;    // Schritt oder Auslöser; null, wenn das Objekt selbst gemeint ist
    QString message; // auf Deutsch mit Ort, zum Beispiel «Aktion «Begrüssung», Schritt 2.1 (Pause): …»

    bool operator==(const ReferenceWarning &) const = default;
};

QList<ReferenceWarning> checkReferences(const ActionsConfig &config, const ReferenceContext &context);

} // namespace dss::actions
