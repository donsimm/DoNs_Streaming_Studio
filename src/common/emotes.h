#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
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

} // namespace dss
