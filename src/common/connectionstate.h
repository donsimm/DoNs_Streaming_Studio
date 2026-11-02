#pragma once

#include <QString>

// Verbindungen im Kopfbereich und ihre Zustände (docs/01-festlegungen.md, Abschnitt
// «Verbindungsanzeige im Kopfbereich»). Den Zustand setzt später der Kern; das Fenster zeigt
// ihn nur an. Nur Qt Core (CLAUDE.md Regel 9).

namespace dss {

enum class Connection { Twitch, YouTube, Obs, Ndi };

enum class ConnectionState {
    Off,        // grau: nicht eingerichtet oder abgeschaltet
    Error,      // rot: eingerichtet, aber nicht verbunden
    Connecting, // orange: Verbindungsversuch läuft
    Connected,  // grün: verbunden bzw. Ausgabe läuft (NDI)
};

// Name für Tooltip und Protokoll: «Twitch», «YouTube», «OBS», «NDI».
QString connectionName(Connection connection);

// Wert der Eigenschaft «state» des Knopfes, auf den das Stylesheet reagiert:
// «off», «error», «connecting», «connected».
QString connectionStateKey(ConnectionState state);

// Umkehrung von connectionStateKey; false bei unbekanntem Wert (Schnittstelle zum Kern, P07).
bool connectionStateFromKey(const QString &key, ConnectionState *state);

// Alle vier Verbindungen in der Reihenfolge des Kopfbereichs.
inline constexpr Connection kAllConnections[] = {Connection::Twitch, Connection::YouTube,
                                                 Connection::Obs, Connection::Ndi};

// Feldname in der Schnittstelle zum Kern: «twitch», «youtube», «obs», «ndi».
QString connectionKey(Connection connection);

// Text für Tooltip und Bildschirmleser, zum Beispiel «Twitch: verbunden»,
// «NDI: Ausgabe läuft».
QString connectionStateText(Connection connection, ConnectionState state);

// Zustände nur für das Bildschirmfoto mit --zustaende-zeigen (Auftrag P06): Twitch grün,
// YouTube orange, OBS rot, NDI grau.
ConnectionState shownState(Connection connection);

} // namespace dss
