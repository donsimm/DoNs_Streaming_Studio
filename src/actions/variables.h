#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonValue>
#include <QMap>
#include <QString>
#include <QUuid>

#include <optional>

// Variablen und Zähler der Befehle (P14 a, Plan 2.9) und die Datei <Datenordner>/variablen.json.
// Nur Qt Core.
//
// - Beliebige Namen, Werte Text oder Zahl. Namen aus Streamer.bot bleiben beim Import unverändert
//   (watchtime, firsts, hugsReceived).
// - Für alle (global) oder je Zuschauer, zugeordnet über die Twitch-Benutzer-ID, nie den Namen.
// - Dauerhaft (in der Datei) oder flüchtig (nur im Speicher, beim Neustart leer).
// - Je dauerhaftem Wert Herkunft (studio, streamerbot) und Zeitpunkt der letzten Änderung, damit ein
//   erneuter Import erkennt, was im Studio seither geändert wurde (Plan 9.4).
// - Die Datei schreibt der Kern verzögert über SaveScheduler (b3); hier nur Modell und Format.

namespace dss::actions {

inline constexpr int kVariablesFormatVersion = 1;
inline constexpr char kVariablesFileName[] = "variablen.json";

enum class VariableOrigin { Studio, Streamerbot };
QString variableOriginKey(VariableOrigin origin); // "studio", "streamerbot"

struct VariableValue
{
    QJsonValue value;  // Text oder Zahl
    VariableOrigin origin = VariableOrigin::Studio;
    QDateTime changedAt; // UTC

    bool operator==(const VariableValue &) const = default;
};

using VariableMap = QMap<QString, VariableValue>; // Name → Wert

struct CommandCounter
{
    std::optional<VariableValue> counter;   // Zähler über alle (persist_counter)
    QMap<QString, VariableValue> userCounters; // Twitch-ID → Zähler (persist_user_counter)

    bool operator==(const CommandCounter &) const = default;
};

// Inhalt von variablen.json (nur dauerhafte Werte)
struct VariablesData
{
    VariableMap global;
    QMap<QString, VariableMap> users;        // Twitch-ID → Variablen
    QMap<QUuid, CommandCounter> commandCounters; // Kennung des Befehls → Zähler

    bool operator==(const VariablesData &) const = default;
};

// Liest streng: format_version 1, Werte nur Text oder Zahl, origin bekannt, changed_at als
// Zeitpunkt nach ISO 8601, Namen und Twitch-IDs nicht leer, Zähler ganze Zahlen ab 0. Unbekannte
// Felder machen die Datei ungültig (die Datei gehört nur dem Kern, wie liste.json).
QByteArray serializeVariables(const VariablesData &data);
bool parseVariables(const QByteArray &bytes, VariablesData *data, QString *error);

enum class VariableScope { Global, User };

struct VariableKey
{
    VariableScope scope = VariableScope::Global;
    QString userId;   // nur bei User
    QString name;
    bool persisted = true;
};

// Dauerhafte und flüchtige Variablen und Zähler im Speicher. Jede Änderung eines dauerhaften
// Werts setzt Herkunft «studio» und den Zeitpunkt now und merkt «geändert» (zum Speichern).
class VariableStore
{
public:
    const VariablesData &data() const { return m_data; }
    // Für Laden und Import: ersetzt die dauerhaften Werte, ohne «geändert» zu setzen
    void setData(const VariablesData &data) { m_data = data; }

    std::optional<QJsonValue> value(const VariableKey &key) const;
    // Volle Angabe mit Herkunft; nur für dauerhafte Werte
    std::optional<VariableValue> entry(const VariableKey &key) const;

    // Leerer Text = erledigt, sonst die Meldung (Name leer, Zuschauer fehlt, Wert weder Text noch Zahl)
    QString set(const VariableKey &key, const QJsonValue &value, const QDateTime &now);
    // Erhöhen oder (negatives delta) verringern. Fehlt die Variable, gilt 0. Ein Text, der ganz
    // eine Zahl ist, zählt als Zahl; anderer Text ist ein Fehler. *result: der neue Wert.
    QString add(const VariableKey &key, double delta, const QDateTime &now, QJsonValue *result = nullptr);
    // true, wenn es die Variable gab
    bool remove(const VariableKey &key);

    // Ein Befehl wurde ausgeführt (Plan 2.6): Zähler über alle und des Zuschauers +1, je nach
    // persist_counter und persist_user_counter dauerhaft oder flüchtig. Ohne userId nur der
    // Zähler über alle; userCounter ist dann 0.
    struct CommandCount
    {
        qint64 counter = 0;
        qint64 userCounter = 0;
    };
    CommandCount countCommand(const QUuid &commandId, const QString &userId, bool persistCounter,
                              bool persistUserCounter, const QDateTime &now);

    bool changed() const { return m_changed; }
    void clearChanged() { m_changed = false; }

private:
    QJsonValue *volatileSlot(const VariableKey &key, bool create);
    VariableValue *persistedSlot(const VariableKey &key, bool create);

    VariablesData m_data;
    QMap<QString, QJsonValue> m_volatileGlobal;
    QMap<QString, QMap<QString, QJsonValue>> m_volatileUsers;
    QHash<QUuid, qint64> m_volatileCounters;
    QHash<QUuid, QHash<QString, qint64>> m_volatileUserCounters;
    bool m_changed = false;
};

} // namespace dss::actions
