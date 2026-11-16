#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonValue>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <optional>

// P14 Emotes em-2: Emote-Listen von BTTV, 7TV und FFZ lesen und die Adressen der Listen und Bilder
// bilden, ohne Netz und ohne Qt Gui (Regel 9). Geladen wird im Fenster (em-4), erkannt in em-3.
// Felder und Adressen: docs/archiv/recherchen/2026-11-16-emotes.md, Abschnitte 1–4.

namespace dss {

enum class EmoteProvider { Twitch, Bttv, SevenTv, Ffz };

struct Emote
{
    EmoteProvider provider = EmoteProvider::Twitch;
    QString id;
    QString name;
    bool animated = false;
    bool zeroWidth = false; // wird über das vorherige Emote gelegt
    int width = 0;          // Grösse in der kleinsten Stufe, 0 = unbekannt
    int height = 0;

    bool operator==(const Emote &) const = default;
};

// Grenzen gegen zu grosse Antworten (Plan «Wer lädt»): grössere Antworten werden nicht gelesen,
// von längeren Listen gelten die ersten kEmoteListMaxEmotes.
inline constexpr qsizetype kEmoteListMaxBytes = 8 * 1024 * 1024;
inline constexpr int kEmoteListMaxEmotes = 2000;

struct EmoteListResult
{
    QList<Emote> emotes;
    QString error;   // Liste nicht lesbar; Text auf Deutsch, ohne Inhalt der Antwort
    QString warning; // Liste gelesen, aber gekürzt

    bool ok() const { return error.isEmpty(); }
};

// «Twitch», «BTTV», «7TV», «FFZ»
QString emoteProviderName(EmoteProvider provider);

// Adresse der globalen Liste (ohne channelId) oder der Liste des Kanals. Leer für Twitch und für
// eine Kanal-ID, die nicht nur aus Ziffern besteht.
QUrl emoteListUrl(EmoteProvider provider, std::optional<QString> channelId);

// Liest die Antwort auf emoteListUrl(provider, …). channel: Liste des Kanals statt der globalen.
// HTTP 404 bei der Liste des Kanals heisst «Anbieter kennt den Kanal nicht»: leere Liste ohne
// Fehler. Einträge mit fehlenden Feldern oder falschen Typen werden ausgelassen.
EmoteListResult parseEmoteList(EmoteProvider provider, bool channel, int httpStatus,
                               const QByteArray &body);

// Bild in der kleinsten Stufe. animate: animiertes Bild, wenn das Emote eines hat. webp: WebP
// lässt sich dekodieren; ohne WebP leer für BTTV und 7TV, FFZ nur statisch.
QUrl emoteImageUrl(const Emote &emote, bool animate, bool webp);

// --- P14 Emotes em-3: Erkennung, Abschnitte, Statustexte ---

// Emotes von BTTV, 7TV und FFZ nach Namen, aus den sechs Listen (global und Kanal je Anbieter).
// Vorrang beim selben Namen (Recherche 5): FFZ-Kanal, BTTV-Kanal, 7TV-Kanal, FFZ-global,
// BTTV-global, 7TV-global. Innerhalb einer Liste gilt das erste Emote mit dem Namen. Vergleich
// exakt, Gross- und Kleinschreibung zählt.
class EmoteIndex
{
public:
    // Ersetzt eine Liste; Twitch wird ignoriert (Twitch-Emotes kommen als Fragmente).
    void setList(EmoteProvider provider, bool channel, const QList<Emote> &emotes);
    // nullptr, wenn kein Emote so heisst
    const Emote *find(const QString &name) const;
    // Anzahl je Anbieter in beiden Listen zusammen (vor dem Vorrang)
    int count(EmoteProvider provider) const;
    bool isEmpty() const { return m_byName.isEmpty(); }

private:
    static constexpr int kLists = 6;
    static int listIndex(EmoteProvider provider, bool channel); // Stelle nach Vorrang, -1 für Twitch

    QList<Emote> m_lists[kLists];
    QHash<QString, Emote> m_byName;
};

// Ein Stück der Nachricht aus EventSub (data.fragments, docs/08). type: text, emote, cheermote,
// mention, gif. emoteId und formats nur bei emote, gifId und gifUrl nur bei gif.
struct ChatFragment
{
    QString type;
    QString text;
    QString emoteId;
    QStringList formats; // emote_format: static, animated
    QString gifId;
    QString gifUrl;

    bool operator==(const ChatFragment &) const = default;
};
// data.fragments → Fragmente; Einträge ohne Objekt fallen weg, fehlende Felder bleiben leer.
QList<ChatFragment> chatFragments(const QJsonValue &fragments);

// Ein Abschnitt der angezeigten Nachricht: Text oder ein Emote (text ist dann sein Name).
struct ChatSegment
{
    QString text;
    std::optional<Emote> emote;

    bool operator==(const ChatSegment &) const = default;
};
// Fragmente emote werden Twitch-Emotes (Stelle aus EventSub, keine Textsuche; animiert, wenn
// formats animated enthält). In Fragmenten text wird an ' ' getrennt und jedes Wort exakt im Index
// gesucht; Leerzeichen bleiben Text. cheermote, mention und gif (Anzeige ab em-9) bleiben Text.
// Ein Zero-Width-Emote bleibt das nur direkt hinter einem Emote (Leerzeichen dazwischen zählen
// nicht), sonst wird es ein normales Emote. Aufeinanderfolgende Texte werden ein Abschnitt.
QList<ChatSegment> chatSegments(const QList<ChatFragment> &fragments, const EmoteIndex &index);

// Stand des Ladens für die Statuszeile. counts: geladene Anbieter mit Anzahl (global und Kanal
// zusammen), unreachable: Anbieter ohne Antwort. Ohne WebP fehlen BTTV und 7TV in beiden.
struct EmoteLoadState
{
    enum class Network { Online, OfflineCached, OfflineEmpty };

    Network network = Network::Online;
    QList<QPair<EmoteProvider, int>> counts;
    QList<EmoteProvider> unreachable;
    bool webp = true;
    bool channel = true; // Kanal-ID bekannt; sonst nur die globalen Listen

    bool operator==(const EmoteLoadState &) const = default;
};
// Statuszeile, Teile mit « · » getrennt, «Emotes» nur einmal vorne, z. B.
// «Emotes geladen: BTTV 74, 7TV 312, FFZ 15 · kein Kanal angemeldet»
QString emoteStatusText(const EmoteLoadState &state);
// «<Name> (BTTV)»
QString emoteToolTip(const Emote &emote);

} // namespace dss
