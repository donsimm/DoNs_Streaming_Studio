#pragma once

#include "emoteloader.h"

#include <QByteArray>
#include <QCache>
#include <QImage>
#include <QList>
#include <QString>

// P14 Emotes em-5: dekodiert die Bild-Bytes aus EmoteLoader (PNG, GIF, WebP) zu fertig
// skalierten Bildern und wählt für animierte Bilder das Bild zu einem gemeinsamen Zeitpunkt
// (eine Uhr für alle, wie Chatterino; Plan P14 Emotes em-5, Recherche Abschnitt 6). Nur Qt Gui,
// kein QMovie, kein Fenster (Regel 9).

namespace dss {

// Höhe der Emotes im Chat (Frage 2 des Plans, Entscheid des Nutzers 2026-11-16)
inline constexpr int kEmoteHeight = 28;
// Speicher für dekodierte Bilder (Plan «Wer lädt»), Kosten in Bytes
inline constexpr qint64 kEmoteImageMemoryBytes = 64 * 1024 * 1024;
// Bilddauer bis und mit dieser Zeit gilt als kEmoteFrameDefaultMs (wie Chatterino)
inline constexpr int kEmoteFrameMinMs = 10;
inline constexpr int kEmoteFrameDefaultMs = 100;

// Grenzen je Bild. Der Standard gilt für Emotes; GIFs im Chat (em-9) geben eigene Werte an.
struct EmoteImageLimits
{
    int height = kEmoteHeight;     // Zielhöhe in Pixeln
    int maxWidthFactor = 4;        // Breite höchstens maxWidthFactor × height
    qint64 maxBytes = kEmoteImageMaxBytes;
    int maxSide = 512;             // Quellbild höchstens so breit und so hoch
    int maxFrames = 256;           // Bilder je Animation
};

// P14 Emotes em-9: GIFs im Chat in eigener Zeile, 112 px hoch (Plan em-9), Breite nach
// Seitenverhältnis. Grenzen eigene Werte, nicht gemessen: Giphy liefert das Original
// (giphy.gif, oft um 480 px breit und einige MiB), darum mehr Bytes und Pixel als ein Emote.
inline constexpr int kGifHeight = 112;
inline constexpr EmoteImageLimits kGifImageLimits{
    .height = kGifHeight, .maxWidthFactor = 4, .maxBytes = kGifMaxBytes, .maxSide = 1024, .maxFrames = 256};

class EmoteImages
{
public:
    explicit EmoteImages(qint64 memoryBytes = kEmoteImageMemoryBytes);

    // Kann Qt WebP lesen (Paket qt6-image-formats-plugins)? Einmal gemessen, danach gemerkt.
    static bool webpAvailable();

    // Dekodiert bytes und legt das Ergebnis unter key ab. false bei kaputten Bytes, über einer
    // Grenze oder wenn das Bild allein den Speicher übersteigt; dann eine Zeile ins Protokoll und
    // der Text bleibt. Ein vorhandener Eintrag mit key wird ersetzt.
    bool add(const QString &key, const QByteArray &bytes, const EmoteImageLimits &limits = {});
    bool contains(const QString &key) const;
    void clear();

    // Bild von key zum Zeitpunkt elapsedMs der gemeinsamen Uhr; animate false gibt das erste
    // Bild. nullptr, wenn key fehlt. Der Zeiger gilt bis zum nächsten add() oder clear().
    const QImage *frame(const QString &key, qint64 elapsedMs, bool animate = true) const;
    // Bilder von key (0, wenn key fehlt) und Dauer einer Runde in ms (0 bei einem Bild)
    int frameCount(const QString &key) const;
    qint64 duration(const QString &key) const;
    // Liegt ein Bild mit mehr als einem Bild im Speicher (braucht den Takt)?
    bool hasAnimated() const;

private:
    struct Entry
    {
        QList<QImage> frames;
        QList<qint64> ends;        // Ende jedes Bildes in ms ab Rundenbeginn
    };

    QCache<QString, Entry> cache_;
};

} // namespace dss
