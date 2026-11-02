#pragma once

#include <QHash>
#include <QString>
#include <QUuid>

// Token pro Overlay für die Browserquelle (P04, Teil 4; docs/01-festlegungen.md, «Betrieb im Netz»).
// Die Token stehen nicht in den Overlay-Dateien, sondern in <Datenordner>/tokens.json mit
// Dateirechten 0600, damit sich Overlay-Dateien ohne Token weitergeben lassen.
// Ein Token erscheint nie im Protokoll und in keiner Meldung (CLAUDE.md Regel 12).

namespace dss {

inline constexpr int kTokenFormatVersion = 1;
inline constexpr int kTokenBytes = 32; // 256 Bit

class TokenStore
{
public:
    explicit TokenStore(const QString &filePath);

    QString filePath() const { return m_filePath; }

    // Fehlt die Datei, gibt es noch keine Token. Eine kaputte Datei ist ein Fehler und wird
    // nicht überschrieben. Andere Dateirechte als 0600 werden auf 0600 gesetzt und in
    // *warning gemeldet.
    bool load(QString *error, QString *warning);

    bool contains(const QUuid &overlayId) const { return m_tokens.contains(overlayId); }
    QString token(const QUuid &overlayId) const { return m_tokens.value(overlayId); }

    // Erzeugt einen neuen Token für das Overlay (ersetzt einen vorhandenen) und speichert.
    bool createToken(const QUuid &overlayId, QString *error);
    bool removeToken(const QUuid &overlayId, QString *error);

    // 32 Byte aus QRandomGenerator::system(), Base64url ohne «=» (43 Zeichen).
    static QString generateToken();

private:
    bool save(const QHash<QUuid, QString> &tokens, QString *error);

    QString m_filePath;
    QHash<QUuid, QString> m_tokens;
};

} // namespace dss
