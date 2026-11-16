#pragma once

#include "actionservices.h"
#include "chatoutbox.h"

#include <QElapsedTimer>
#include <QList>
#include <QObject>

class QTimer;

namespace dss::twitch {
class TwitchService;
}

// Chat senden über Twitch im Kern (P14 c1a, Plan 4.1, nur mit DSS_CORE_TWITCH): verbindet
// dss::actions::ChatOutbox mit TwitchService. Bot: Token aus botSendToken() (App-Token für das Abzeichen
// oder das Token des Bots, P13 Plan 2.4), sender_id = Bot. Kanal: Token und ID des Kanals.
// broadcaster_id ist immer der eigene Kanal; ohne angemeldeten Kanal sendet auch der Bot nicht.
class TwitchChatTransport : public dss::actions::ChatTransport
{
public:
    // twitch lebt länger als diese Instanz.
    explicit TwitchChatTransport(dss::twitch::TwitchService *twitch);

    // Twitch-ID des eigenen Kanals; leer, wenn nicht angemeldet.
    QString broadcasterId() const;

    QString unavailableReason(dss::actions::ChatAccount account) const override;
    QString userId(dss::actions::ChatAccount account) const override;
    void post(dss::actions::ChatAccount account, const QString &text, const QString &replyTo,
              std::function<void(const dss::actions::ChatPostResult &)> done) override;

private:
    dss::twitch::TwitchService *m_twitch;
};

// Benutzerinfo, Shoutout und Ansage im Kern (P14 c1b, Plan 4.8 und 4.9, nur mit DSS_CORE_TWITCH):
// verbindet dss::actions::TwitchApi mit TwitchService (Aufrufe in helixactions.h).
// - Benutzerinfo mit dem Token des Kanals; setzt targetUser (Anzeigename), targetUserName (Login),
//   targetUserId, targetUserProfileImageUrl, targetDescription, game und targetUserFound. Unbekannter
//   Name: targetUserFound = false, kein Fehler.
// - Shoutout mit dem Token des Kanals (Kanal ist from_broadcaster_id und moderator_id). Eine Ablehnung
//   von Twitch (nicht live, Grenze) ist ein Fehler des Schritts; der Lauf geht weiter.
// - Ansage als Bot, wenn gewünscht, der Bot angemeldet ist und das Recht hat; antwortet Twitch mit
//   403 (Bot moderiert nicht), sendet der Kanal. Sonst der Kanal. Höchstens eine Ansage je
//   announceIntervalMs (Twitch: eine alle 2 s), die weiteren warten der Reihe nach.
// - In den Meldungen keine Namen und keine Texte (Journal, Plan 3.5).
class TwitchActions : public QObject, public dss::actions::TwitchApi
{
    Q_OBJECT

public:
    static constexpr int kAnnounceIntervalMs = 2000;

    // twitch lebt länger als diese Instanz.
    explicit TwitchActions(dss::twitch::TwitchService *twitch, QObject *parent = nullptr);

    void userInfo(const QString &login, dss::actions::StepDone done) override;
    void shoutout(const QString &login, dss::actions::StepDone done) override;
    void announce(const dss::actions::AnnounceRequest &request, dss::actions::StepDone done) override;

private:
    struct PendingAnnounce
    {
        dss::actions::AnnounceRequest request;
        dss::actions::StepDone done;
    };

    // Grund, warum der Kanal nicht kann; leer = kann. scope leer = nur angemeldet.
    QString channelProblem(const QString &scope = {}) const;
    void nextAnnounce();
    void postAnnounce(const PendingAnnounce &pending, bool asBot, const QString &note);

    dss::twitch::TwitchService *m_twitch;
    QList<PendingAnnounce> m_announceQueue;
    bool m_announceBusy = false;
    QElapsedTimer m_lastAnnounce; // Start der letzten Ansage
    QTimer *m_announceTimer;
};
