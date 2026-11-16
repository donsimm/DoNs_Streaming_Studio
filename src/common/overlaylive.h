#pragma once

#include "overlay.h"
#include "placeholders.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QUuid>

#include <optional>

// Laufzustand der Overlays (P14 d, Plan docs/archiv/prompts/P14-chatbot.md, Abschnitt 4.4): Eine
// Aktion des Chat-Bots ändert ein Element (eine Quelle) eines Overlays nur «jetzt»: Sichtbarkeit,
// Text, Bild. Der Kern hält das im Speicher, schreibt es nie in die Overlay-Datei und vergisst es
// beim Neustart. Hier stehen das Modell, das Einsetzen der Platzhalter, das Anwenden auf ein
// Overlay zum Zeichnen und die Nachrichten (overlay_live, overlay_live_set, overlay_live_reset,
// docs/08-kern-schnittstelle.md). Nur Qt Core (CLAUDE.md Regel 9).

namespace dss {

// Grenzen eines Aufrufs (Interna-Entscheid P14 d, Bericht)
inline constexpr int kLiveMaxDurationMs = 60 * 60 * 1000; // 1 h wie die Pause (Plan 2.4)
inline constexpr int kLiveMaxElements = 32;
inline constexpr qsizetype kLiveMaxTextChars = 2000;
inline constexpr qsizetype kLiveMaxArguments = 200;
// Ordner der geladenen Profilbilder im Datenordner (Plan 4.4), Verweise «live-bilder/<sha256>.<endung>»
inline constexpr char kLiveImageDir[] = "live-bilder";

namespace msg {
inline constexpr char kOverlayLive[] = "overlay_live";            // Kern → Fenster, an alle
inline constexpr char kOverlayLiveSet[] = "overlay_live_set";     // Fenster/dss-cli → Kern
inline constexpr char kOverlayLiveReset[] = "overlay_live_reset"; // Fenster/dss-cli → Kern
} // namespace msg

// Was eine Aktion mit den Elementen macht (Feld «mode» des Schritts, Plan 2.4)
enum class LiveMode { Show, Hide, Toggle };
QString liveModeName(LiveMode mode); // "show", "hide", "toggle"
bool liveModeFromName(const QString &name, LiveMode *mode);

// Laufzustand eines Elements. Nur gesetzte Werte gelten «jetzt», die übrigen kommen aus dem Overlay.
struct LiveElement
{
    std::optional<bool> visible;
    std::optional<QString> text; // fertiger Text, Platzhalter schon eingesetzt (nur Text)
    QString image;               // "media/…" oder "live-bilder/<sha256>.<endung>" (nur Bild); leer = Bild des Elements
    // Restzeit in ms beim Erstellen der Nachricht; -1 = ohne Ende (bis «Ausblenden» oder Neustart)
    qint64 remainingMs = -1;

    bool operator==(const LiveElement &) const = default;
};
using LiveElements = QMap<QUuid, LiveElement>; // nach Kennung der Quelle

struct OverlayLiveInfo
{
    QUuid overlayId;
    LiveElements elements;

    bool operator==(const OverlayLiveInfo &) const = default;
};

// Sichtbarkeit, Text und Bild aus dem Laufzustand auf eine Kopie des Overlays (für beide Zeichner).
// Elemente ohne Quelle im Overlay werden übergangen.
Overlay applyLiveState(const Overlay &overlay, const LiveElements &elements);

// Platzhalter wie Streamer.bot (Plan 2.9), dieselbe Ersetzung wie im Ablauf der Aktionen
// (dss::actions::replacePlaceholders, P14 c5): «%name%» setzt das Argument ein, Gross- und
// Kleinschreibung des Namens zählt nicht; «~name~» setzt die dauerhafte globale Variable aus
// globals ein. Unbekanntes bleibt wörtlich stehen, eingesetzte Werte werden nicht nochmals ersetzt.
QString substitutePlaceholders(const QString &text, const QHash<QString, QString> &arguments,
                               const dss::actions::GlobalLookup &globals = {});

// Adresse aus dem Netz (http:// oder https://) statt einer Datei der Mediathek
bool isLiveImageUrl(const QString &image);

// Ein Element in overlay_live_set
struct LiveElementRequest
{
    QUuid source;
    std::optional<QString> text; // ersetzt den ganzen Text (mit Platzhaltern); leer = Text des Elements
    QString image;               // Adresse oder "media/…" (mit Platzhaltern); leer = Bild des Elements

    bool operator==(const LiveElementRequest &) const = default;
};

// overlay_live_set: dieselben Werte wie der Schritt «Overlay-Element zeigen» (Plan 2.4, ohne
// Übergänge und ohne «warten»), für dss-cli und Prüfungen; der Schritt (Abschnitt c2) ruft
// denselben Weg im Kern direkt auf.
struct LiveSetRequest
{
    qint64 requestId = 0;
    QUuid overlayId;
    LiveMode mode = LiveMode::Show;
    QList<LiveElementRequest> elements;
    int durationMs = 0; // 0 = ohne Ende
    QHash<QString, QString> arguments; // Argumente des Laufs für die Platzhalter
    // Text und Bild der Elemente sind schon eingesetzt (Schritt einer Aktion, der Lauf ersetzt sie):
    // nicht nochmals ersetzen, sonst würde ein Wert mit «%…%» erneut ersetzt (Plan 2.9). Der eigene
    // Text der Elemente wird weiter ersetzt. Nur im Kern, nicht in der Nachricht.
    bool resolved = false;

    bool operator==(const LiveSetRequest &) const = default;
};
QJsonObject liveSetMessage(const LiveSetRequest &request);
bool parseLiveSet(const QJsonObject &message, LiveSetRequest *request, QString *error);
// Prüft Overlay, Elemente, Dauer und Art; leer = gültig.
QString validateLiveSet(const LiveSetRequest &request, const Overlay &overlay);

// overlay_live_reset: Laufzustand zurücksetzen; sources leer = alle Elemente des Overlays.
struct LiveResetRequest
{
    qint64 requestId = 0;
    QUuid overlayId;
    QList<QUuid> sources;

    bool operator==(const LiveResetRequest &) const = default;
};
QJsonObject liveResetMessage(const LiveResetRequest &request);
bool parseLiveReset(const QJsonObject &message, LiveResetRequest *request, QString *error);

// overlay_live: Laufzustand aller Overlays mit Laufzustand (nach hello und nach jeder Änderung an
// alle). Overlays ohne Laufzustand fehlen.
QJsonObject overlayLiveMessage(const QList<OverlayLiveInfo> &overlays);
bool parseOverlayLive(const QJsonObject &message, QList<OverlayLiveInfo> *overlays, QString *error);

// Anzeige im Fenster (P14 d, Bereich «Chat-Bot» im Dock «Eigenschaften», Plan 4.4a): «live sichtbar,
// noch 6 s», «live versteckt», «live: Text», «–» ohne Laufzustand.
QString liveStateText(const std::optional<LiveElement> &element, qint64 remainingMs);

} // namespace dss
