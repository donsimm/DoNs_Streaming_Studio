#pragma once

#include "placeholders.h"

#include <QList>
#include <QString>
#include <QUuid>

#include <functional>
#include <optional>

// Schnittstellen der Schritte nach aussen (P14 b1, Plan 3.1): Chat, Twitch, OBS, Ton und
// Laufzustand der Overlays. Der Kern verbindet sie (b3, c), die Tests ersetzen sie. Nur Qt Core,
// kein Netz (CLAUDE.md Regel 9 und 14).
//
// Jeder Aufruf bekommt einen Rückruf done, den der Dienst genau einmal aufruft, sobald der Schritt
// fertig ist: beim Ton mit wait erst am Ende, sonst sobald er begonnen hat. done darf auch im
// Aufruf selbst kommen. Ist der Lauf inzwischen beendet (abgebrochen, Grenze), wird done ignoriert.

namespace dss::actions {

struct StepResult
{
    bool ok = true;
    QString message;     // Meldung im Lauf; bei einem Fehler der Grund
    Arguments arguments; // neue Argumente des Laufs (Benutzerinfo: targetUser …)

    static StepResult failure(const QString &message) { return {false, message, {}}; }
};
using StepDone = std::function<void(const StepResult &)>;

// Schritt «Chat senden» (Plan 4.1)
struct ChatRequest
{
    QString text;    // Platzhalter schon eingesetzt
    bool useBot = true;
    bool fallback = true;
    QString replyTo; // msgId der Nachricht, auf die geantwortet wird; leer = keine Antwort
};

// Schritt «Ansage» (Plan 4.9)
struct AnnounceRequest
{
    QString text;
    QString color;
    bool useBot = true;
};

// Schritt «Overlay-Element zeigen/verstecken» (Plan 4.4). text und image sind schon eingesetzt;
// arguments braucht der Kern für die Platzhalter im eigenen Text der Elemente.
struct OverlayRequest
{
    struct Element
    {
        QUuid source;
        std::optional<QString> text;
        std::optional<QString> image;
    };
    QUuid overlay;
    QString mode; // show, hide, toggle
    QList<Element> elements;
    qint64 durationMs = 0;
    QString transitionIn;
    QString transitionOut;
    qint64 transitionMs = 0;
    Arguments arguments;
};

// Schritt «Ton abspielen» (Plan 4.3)
struct SoundRequest
{
    QString file; // Verweis der Mediathek
    int volume = 100;
    bool wait = true;
};

// Schritt «OBS-Quelle ein/aus» (Plan 4.5, P14-2)
struct ObsVisibilityRequest
{
    QString sceneName;
    QString sourceName;
    QString state; // on, off, toggle
};

class ChatSender
{
public:
    virtual ~ChatSender() = default;
    virtual void sendChat(const ChatRequest &request, StepDone done) = 0;
};

class TwitchApi
{
public:
    virtual ~TwitchApi() = default;
    // login ohne «@». Unbekannter Name: ok mit targetUserFound = false (Plan 4.8)
    virtual void userInfo(const QString &login, StepDone done) = 0;
    virtual void shoutout(const QString &login, StepDone done) = 0;
    virtual void announce(const AnnounceRequest &request, StepDone done) = 0;
};

class ObsControl
{
public:
    virtual ~ObsControl() = default;
    virtual void setSourceVisible(const ObsVisibilityRequest &request, StepDone done) = 0;
};

class SoundPlayer
{
public:
    virtual ~SoundPlayer() = default;
    virtual void play(const SoundRequest &request, StepDone done) = 0;
};

class OverlayLive
{
public:
    virtual ~OverlayLive() = default;
    virtual void apply(const OverlayRequest &request, StepDone done) = 0;
};

// Was fehlt (nullptr), meldet der Schritt im Lauf als Fehler «nicht verfügbar».
struct ActionServices
{
    ChatSender *chat = nullptr;
    TwitchApi *twitch = nullptr;
    ObsControl *obs = nullptr;
    SoundPlayer *sound = nullptr;
    OverlayLive *overlay = nullptr;
};

} // namespace dss::actions
