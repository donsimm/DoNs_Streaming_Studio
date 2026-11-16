#pragma once

#include "eventbus.h"

#include <QObject>
#include <QSet>
#include <QString>

class QTimer;

// «Erste Nachricht in diesem Stream» (P13, Plan 2.6; bei Streamer.bot «First Words»). Twitch liefert
// nur die erste Nachricht überhaupt (message_type user_intro), darum leitet der Kern sie ab: Die
// Sitzung hört auf dem Fluss twitch.chat_message, twitch.stream_online und twitch.stream_offline und
// veröffentlicht für die erste Nachricht jedes Zuschauers zusätzlich twitch.first_message (alle
// Felder der Nachricht und first_ever).
//
// - Die Liste der user_id gilt seit dem letzten twitch.stream_online; ohne laufenden Stream seit dem
//   Start des Kerns. Sie wird bei twitch.stream_offline und bei einem neuen Stream geleert.
// - Neustart während des Streams: Die Liste (nur user_id, keine Namen) steht verzögert in
//   twitch-sitzung.json im Datenordner, mit started_at des Streams. Beim Start wird sie gelesen und
//   übernommen, sobald twitch.stream_online denselben started_at meldet (at_start, Plan 2.5); sonst
//   verworfen. Ohne laufenden Stream wird nichts geschrieben. Bei twitch.stream_offline wird die Datei
//   gelöscht.
// - Nur für Zuschauer (Entscheid des Nutzers, 2026-11-16, em-7b): Nachrichten des eigenen Kanals und
//   des eigenen Bots lösen kein twitch.first_message aus. Erkennung über die user_id, nicht über
//   Namen; eine leere Kennung (zum Beispiel kein Bot angemeldet) zählt nicht.

namespace dss::twitch {

class ChatSession : public QObject
{
    Q_OBJECT

public:
    // stateFile: twitch-sitzung.json im Datenordner; leer = nichts speichern. saveDelayMs: Wartezeit
    // vor dem Schreiben (mehrere neue Zuschauer in einem Zug).
    ChatSession(events::EventBus *bus, const QString &stateFile, int saveDelayMs = 2000, QObject *parent = nullptr);
    ~ChatSession() override;

    // Kennungen von Kanal und Bot (leer = nicht angemeldet); deren Nachrichten zählen nicht.
    void setOwnUserIds(const QString &channelId, const QString &botId)
    {
        m_channelId = channelId;
        m_botId = botId;
    }

    // Für Tests und Anzeige
    bool isLive() const { return !m_startedAt.isEmpty(); }
    QString startedAt() const { return m_startedAt; }
    qsizetype chatterCount() const { return m_chatters.size(); }
    // Schreibt sofort, wenn ein Schreiben aussteht (Beenden des Kerns).
    void flush();

private:
    void onEvent(const events::Event &event);
    void onStreamOnline(const QString &startedAt);
    void onStreamOffline();
    void onChatMessage(const events::Event &event);
    void loadState();
    void save();

    events::EventBus *m_bus;
    events::Subscription m_subscription;
    QString m_stateFile;
    QTimer *m_saveTimer;
    QString m_startedAt;          // leer: kein laufender Stream bekannt
    QSet<QString> m_chatters;     // user_id, die in diesem Stream geschrieben haben
    QString m_storedStartedAt;    // aus der Datei, bis twitch.stream_online kommt
    QSet<QString> m_storedChatters;
    QString m_channelId;
    QString m_botId;
};

} // namespace dss::twitch
