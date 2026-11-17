#pragma once

#include "actionengine.h"
#include "emotes.h"
#include "windowdata.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>

#include <optional>

// P14 e2-5: Ansichtslogik des Unterreiters «Live» im Reiter «Chat-Bot» (Plan 3.5), ohne Fenster
// (Regel 9). windowdata.h bleibt für P13.

namespace dss {

// --- Bereich «Läufe der Aktionen» ---

// Eine Zeile: Zeit (lokal, hh:mm:ss, Eintreffen des Laufs), Aktion, Auslöser (Bezeichnung der Art),
// Ergebnis. state ist der Zustand für die Farbe: "ok", "warning", "error" oder leer (wartet, läuft).
struct RunRow
{
    QUuid runId;
    QUuid actionId;
    QString time;
    QString action;
    QString trigger;
    QString result;
    QString state;
    bool cancellable = false; // «Lauf abbrechen»: nur wartend und laufend
    bool isError = false;     // state == "error", für «Nur Fehler»

    bool operator==(const RunRow &) const = default;
};
RunRow runRow(const actions::RunRecord &record);
// Ergebnistext, z. B. «läuft, Schritt 2 von 5», «Probelauf · fertig (1.5 s)»
QString runResultText(const actions::RunRecord &record);
// Bezeichnung des Auslösers («Befehl», «Test», «Aktion ausführen», …); unbekannte Art unverändert
QString runTriggerText(const QString &triggerType);

// Liste der Läufe, älteste zuerst, höchstens kMaxRows. Ein Lauf hat genau eine Zeile; Meldungen
// zum selben Lauf (actions.run_started, run_changed, run_finished) ersetzen sie.
class RunRows
{
public:
    static constexpr int kMaxRows = 200;

    // Was das Fenster an der sichtbaren Liste (visibleRows) ändern muss, in dieser Reihenfolge:
    // erst removedFront Zeilen vorne entfernen, dann bei appended eine Zeile an index einfügen,
    // sonst die Zeile an index ersetzen. index -1: sichtbar ändert sich nichts. Eingefügt wird am
    // Ende, ausser bei «Nur Fehler», wenn ein bestehender Lauf zum Fehler wird (dann an seiner Stelle).
    struct Change
    {
        int index = -1;
        bool appended = false;
        int removedFront = 0;

        bool operator==(const Change &) const = default;
    };

    // Ersetzt die Liste (action_runs); von einem längeren Verlauf bleiben die neuesten.
    void reset(const QList<actions::RunRecord> &records);
    Change apply(const actions::RunRecord &record);
    // «Nur Fehler»: sichtbar sind nur Zeilen mit isError. Danach baut das Fenster die Liste neu.
    void setOnlyErrors(bool onlyErrors);
    bool onlyErrors() const { return m_onlyErrors; }
    const QList<RunRow> &rows() const { return m_rows; }
    QList<RunRow> visibleRows() const;
    // Zeile zum Lauf, nullptr wenn nicht (mehr) in der Liste
    const RunRow *row(const QUuid &runId) const;

private:
    bool visible(const RunRow &row) const { return !m_onlyErrors || row.isError; }
    int visibleIndex(int index) const; // Stelle in visibleRows für m_rows[index]

    QList<RunRow> m_rows;
    bool m_onlyErrors = false;
};

// --- Bereich «Chat» (P14 e2-6) ---

// Eine Zeile: Zeit (lokal, hh:mm:ss), Abzeichen («Bot», «Kanal», «Mod»; Farben aus dem Theme),
// Name, Chatfarbe des Absenders (#RRGGBB, leer = Standard), Text. isEvent: graue Zeile für ein
// Ereignis, eine ausgelöste Aktion oder eine nicht gesendete Nachricht, dann Name und badges leer.
// messageId: Kennung der Nachricht bei Twitch (nicht angezeigt), gegen doppelte Zeilen.
// P14 Emotes em-3: platform für das Symbol vor der Zeile (aus dem Typ, twitch.* → Twitch; None
// für Zeilen ohne Plattform wie actions.*). fragments und segments nur bei Nachrichten (nicht
// isEvent): segments ist, was der Delegat zeichnet (Text und Emotes); GIFs (em-9) zeichnet er in
// einer eigenen Zeile aus chatGifs(fragments), sie fehlen in segments. awaitingEcho: Zeile aus
// twitch.chat_sent, deren Echo noch nicht da ist.
enum class ChatPlatform { None, Twitch, YouTube };
struct ChatRow
{
    QString time;
    QStringList badges;
    QString name;
    QString color;
    QString text;
    bool isEvent = false;
    QString messageId;
    ChatPlatform platform = ChatPlatform::None;
    QList<ChatFragment> fragments;
    QList<ChatSegment> segments;
    bool awaitingEcho = false;

    bool operator==(const ChatRow &) const = default;
};
// Zeile zu einem Ereignis aus chatBotLiveFilters(); nullopt, wenn der Typ im Chat keine Zeile hat.
// twitch: Namen der Konten für Zeilen aus twitch.chat_sent (ohne Angabe «—»). emotes: Emotes für
// die Abschnitte; ohne Index ist die Nachricht ein Abschnitt Text wie vor em-3.
std::optional<ChatRow> chatRow(const events::Event &event,
                               const std::optional<TwitchStatusInfo> &twitch = std::nullopt,
                               const EmoteIndex *emotes = nullptr);

// Liste des Chats, älteste zuerst, höchstens kMaxRows; Zeilen kommen nur hinten dazu. Eine
// Nachricht steht nur einmal: Kommt nach twitch.chat_sent ein twitch.chat_message mit derselben
// message_id (Echo über EventSub, nicht belegt), ersetzt es Fragmente, Abschnitte und Chatfarbe
// dieser Zeile (RowsAppend::changed); umgekehrt bringt twitch.chat_sent keine zweite Zeile.
class ChatRows
{
public:
    static constexpr int kMaxRows = 200;

    // Ersetzt die Liste (event_history); von einem längeren Verlauf bleiben die neuesten.
    void reset(const QList<events::Event> &events, const std::optional<TwitchStatusInfo> &twitch = std::nullopt);
    RowsAppend add(const events::Event &event, const std::optional<TwitchStatusInfo> &twitch = std::nullopt);
    // Neue Emotes: rechnet die Abschnitte aller Zeilen neu; danach baut das Fenster die Liste neu.
    void setEmotes(const EmoteIndex &emotes);
    const QList<ChatRow> &rows() const { return m_rows; }

private:
    int indexOf(const QString &messageId) const;
    const EmoteIndex *emotes() const { return m_emotes ? &*m_emotes : nullptr; }

    QList<ChatRow> m_rows;
    std::optional<EmoteIndex> m_emotes;
};

// --- Senden (Plan 4.1, chat_send) ---

inline constexpr int kChatSendMaxLength = 500; // wie im Kern; UTF-16-Einheiten (Plan 4.1)

// Auswahl «als Bot» / «als Kanal». account: bot, broadcaster (chat_send). enabled false
// (ausgegraut), wenn actions_status das Konto nicht senden lässt; toolTip dann der Grund.
struct ChatSendAccount
{
    QString account;
    QString label;
    bool enabled = true;
    QString toolTip;

    bool operator==(const ChatSendAccount &) const = default;
};
// Eingabezeile: counter «12 / 500», tooLong (Zähler als Fehler), sendEnabled («Senden» und Enter),
// problem: Grund, warum nicht gesendet werden kann (leer bei leerem Text).
struct ChatSendView
{
    QString counter;
    bool tooLong = false;
    bool sendEnabled = false;
    QList<ChatSendAccount> accounts;
    QString problem;

    bool operator==(const ChatSendView &) const = default;
};
// status fehlt (noch kein actions_status): Konten wählbar, der Kern entscheidet.
ChatSendView chatSendView(const QString &text, const QString &account, const std::optional<ActionsStatus> &status);
// Prüfung wie im Kern (actionservice.cpp, handleChatSend) mit denselben Texten; leer = senden
QString chatSendProblem(const QString &text, const QString &account, const std::optional<ActionsStatus> &status);
// «Senden»: Anfrage chat_send, sonst problem. Der Text geht unverändert (der Kern kürzt nicht).
EditResult chatSendEdit(const QString &text, const QString &account, const std::optional<ActionsStatus> &status);

} // namespace dss
