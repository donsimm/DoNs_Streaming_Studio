#pragma once

#include <QString>

#include <functional>

// Benutzerinfo, Shoutout und Ansage über Helix (P14 c1b, Plan 4.8 und 4.9). Je Aufruf genau eine
// Anfrage (Benutzerinfo: zwei); welches Konto, wie oft und die Meldungen im Lauf macht der Aufrufer
// (TwitchActions im Kern). Nie im Protokoll: Token, Texte, Namen.
//
// - Benutzerinfo: GET /helix/users?login=…, danach GET /helix/channels?broadcaster_id=… für die
//   Kategorie (game_name). Leere Liste = unbekannter Name, kein Fehler.
// - Shoutout: POST /helix/chat/shoutouts mit from_broadcaster_id, to_broadcaster_id und moderator_id
//   als Query-Parameter; Recht moderator:manage:shoutouts. Nur live, alle 2 min einer, dasselbe Ziel
//   alle 60 min (sonst Fehler von Twitch).
// - Ansage: POST /helix/chat/announcements mit broadcaster_id und moderator_id (Query), message und
//   color (blue, green, orange, purple, primary) im Körper; Recht moderator:manage:announcements,
//   moderator_id moderiert oder ist der Kanal (sonst 403).
//
// Quellen: https://dev.twitch.tv/docs/api/reference/chat (Shoutout, Ansage),
// https://dev.twitch.tv/docs/api/reference/#get-users, https://dev.twitch.tv/docs/api/reference/#get-channel-information,
// docs/archiv/recherchen/2026-11-15-schritt0-twitch-streamerbot.md (Abschnitt 17). Die Feldnamen von
// Get Users (display_name, description, profile_image_url) und Get Channel Information (game_name)
// sind nicht gegen die aktuelle Doku geprüft (Plan, «Wichtigste Befunde»).

namespace dss::twitch {

class HelixClient;
class TokenSource;

struct HelixUser
{
    QString id;
    QString login;
    QString displayName;
    QString description;
    QString profileImageUrl;
};

struct UserInfoResult
{
    bool found = false;
    HelixUser user;
    QString game;      // letzte Kategorie des Kanals; leer, wenn keine oder nicht geholt
    QString gameError; // Kategorie nicht geholt: Grund (Benutzer gilt trotzdem als gefunden)
    QString error;     // Benutzer nicht geholt: Grund auf Deutsch
};

struct HelixCallResult
{
    bool ok = false;
    int status = 0;
    QString error; // Grund auf Deutsch, wenn nicht ok
};

// token muss leben, bis done aufgerufen ist (HelixClient).
void getUser(HelixClient *helix, TokenSource *token, const QString &login,
             std::function<void(const UserInfoResult &)> done);
void sendShoutout(HelixClient *helix, TokenSource *token, const QString &fromId, const QString &toId,
                  const QString &moderatorId, std::function<void(const HelixCallResult &)> done);
void sendAnnouncement(HelixClient *helix, TokenSource *token, const QString &broadcasterId,
                      const QString &moderatorId, const QString &text, const QString &color,
                      std::function<void(const HelixCallResult &)> done);

} // namespace dss::twitch
