#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>

#include <optional>

// Teile von HTTP/1.1 für den Overlay-Server (P10, Teil A), ohne Verbindung und testbar:
// Anfrage lesen, Range auswerten, Antwortkopf bauen. Nur, was eine Browserquelle braucht
// (GET, HEAD, POST mit Content-Length; keine Chunked-Anfragen, kein Pipelining über mehrere
// gleichzeitig offene Antworten).
// Quellen: RFC 9112 (Nachrichtenformat) https://www.rfc-editor.org/rfc/rfc9112,
// RFC 9110 (Semantik, Range 14.1.2, 206/416 15.3.7/15.5.17) https://www.rfc-editor.org/rfc/rfc9110

namespace dss {

inline constexpr qsizetype kMaxHttpHeaderBytes = 16 * 1024;
inline constexpr qsizetype kMaxHttpBodyBytes = 64 * 1024;

struct HttpRequest
{
    QByteArray method;
    QByteArray target;  // wie gesendet, mit Prozent-Kodierung, ohne Anfragezeichen und Rest
    QByteArray version; // "HTTP/1.1"
    QHash<QByteArray, QByteArray> headers; // Namen klein geschrieben
    QByteArray body;
    bool keepAlive() const;
};

enum class ParseResult { Incomplete, Complete, Invalid };

// Liest die erste vollständige Anfrage aus buffer und entfernt sie. Invalid: kaputt oder zu gross
// (die Verbindung wird dann mit 400 beendet).
ParseResult takeHttpRequest(QByteArray *buffer, HttpRequest *request);

// Range: bytes=a-b | a- | -n (nur ein Bereich). nullopt ohne Range-Kopf; ungültig oder
// ausserhalb der Datei: {-1, -1}.
struct ByteRange
{
    qint64 first = 0;
    qint64 last = 0; // einschliesslich
    bool operator==(const ByteRange &) const = default;
};
std::optional<ByteRange> parseRange(const QByteArray &header, qint64 size);

// "200 OK" usw.
QByteArray statusLine(int status);
// Kopf einer Antwort mit Content-Length (ohne Inhalt).
QByteArray responseHead(int status, const QByteArray &contentType, qint64 length,
                        const QList<std::pair<QByteArray, QByteArray>> &extra = {});

// Inhaltstyp nach Dateiendung (klein geschrieben, ohne Punkt).
QByteArray contentTypeForSuffix(const QString &suffix);

// Vergleich mit gleichbleibender Laufzeit (unabhängig davon, wo sich die Texte unterscheiden),
// wie sodium_memcmp: https://libsodium.gitbook.io/doc/helpers#constant-time-test-for-equality
// Die Länge ist nicht geheim (Token immer 43 Zeichen).
bool constantTimeEquals(QByteArrayView a, QByteArrayView b);

} // namespace dss
