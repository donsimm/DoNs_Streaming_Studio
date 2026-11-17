#pragma once

#include "emotes.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

// P14 Emotes em-4: lädt die Emote-Listen von BTTV, 7TV und FFZ und die Bilder aller vier Anbieter
// über den HTTP-Zwischenspeicher (QNetworkDiskCache), ohne Qt Gui (Regel 9). Das Fenster setzt
// mit indexChanged() die Erkennung (em-3) und dekodiert die Bild-Bytes selbst (em-5).
// Regeln: Plan P14 Emotes, «Wer lädt: das Fenster» und «Ohne Netz, ohne WebP».

namespace dss {

// Grenzen (Plan «Wer lädt»); die Grenze der Listen ist kEmoteListMaxBytes aus emotes.h.
inline constexpr qint64 kEmoteImageMaxBytes = 2 * 1024 * 1024;
inline constexpr qint64 kEmoteCacheMaxBytes = 100 * 1024 * 1024;
inline constexpr int kEmoteRequestTimeoutMs = 15000;
// P14 Emotes em-9: GIFs im Chat (Giphy) höchstens so gross; eigener Wert, nicht gemessen
inline constexpr qint64 kGifMaxBytes = 8 * 1024 * 1024;

// Ein erlaubter Server: host ist der Host der echten Adresse (emoteListUrl, emoteImageUrl), base
// der Ort, an den die Anfrage geht (Schema, Host, Port, Pfad davor). Normal https://<host>;
// Tests geben einen lokalen Stub an. host «*.<Domain>» gilt für jeden Host unter der Domain
// (GIFs, em-9); base ohne Host heisst dann: die echte Adresse unverändert, nur mit diesem Schema.
struct EmoteEndpoint
{
    QString host;
    QUrl base;
};

// Die einzige Liste erlaubter Server. Anfragen und Weiterleitungen gehen nur an Adressen unter
// einer base dieser Liste.
struct EmoteEndpoints
{
    QList<EmoteEndpoint> list;

    // api.betterttv.net, cdn.betterttv.net, 7tv.io, cdn.7tv.app, api.frankerfacez.com,
    // cdn.frankerfacez.com, static-cdn.jtvnw.net, je über https; *.giphy.com (GIFs, em-9) über
    // https mit der unveränderten Adresse
    static EmoteEndpoints standard();
    // Echte Adresse → Adresse der Anfrage; leer, wenn ihr Host nicht in der Liste steht
    QUrl map(const QUrl &real) const;
    // Liegt die Adresse unter einer base (gleiches Schema, Host, Port, Pfad darunter)?
    bool allows(const QUrl &url) const;
};

class EmoteLoader : public QObject
{
    Q_OBJECT

public:
    // cacheDir: Ordner des Zwischenspeichers (emoteCacheDir(), im Test ein QTemporaryDir)
    explicit EmoteLoader(const QString &cacheDir, EmoteEndpoints endpoints = EmoteEndpoints::standard(),
                         QObject *parent = nullptr);
    ~EmoteLoader() override;

    // Lädt die Listen neu: global und, mit gültiger Kanal-ID, die des Kanals; ohne WebP nur FFZ.
    // Läuft schon ein Laden mit derselben Kanal-ID, passiert nichts; mit einer anderen werden die
    // laufenden Anfragen der Listen abgebrochen und neu begonnen. Am Ende indexChanged() und
    // stateChanged().
    void load(const std::optional<QString> &channelId, bool webp);
    bool isLoading() const { return !m_listReplies.isEmpty(); }

    // Lädt das Bild (zuerst aus dem Zwischenspeicher), am Ende imageLoaded(key, …). Gibt den
    // Schlüssel zurück, leer, wenn es keine Adresse gibt (ohne WebP BTTV und 7TV, siehe
    // emoteImageUrl; WebP vom letzten load()). Dieselbe Adresse läuft nur einmal gleichzeitig.
    QString image(const Emote &emote, bool animate);

    // P14 Emotes em-9: lädt ein GIF aus dem Chat, am Ende gifLoaded(url, …). Die Adresse geht
    // unverändert hinaus (Twitch: «must not be modified»; Giphy: Query-Parameter nicht ändern),
    // nur an erlaubte Server (*.giphy.com), nur in den Arbeitsspeicher: eigener Netzzugang ohne
    // QNetworkDiskCache (Giphy: «Do not … cache or store copies»). Gibt url zurück, leer, wenn
    // die Adresse nicht erlaubt ist (dann bleibt der Text). Dieselbe Adresse läuft nur einmal.
    // Quellen: Recherche docs/archiv/recherchen/2026-11-16-twitch-gif.md, Abschnitte 1 und 5.
    QString gif(const QString &url);

signals:
    void indexChanged(const dss::EmoteIndex &index);
    void stateChanged(const dss::EmoteLoadState &state);
    // bytes leer: nicht geladen (Fehler ist im Protokoll), das Wort bleibt Text
    void imageLoaded(const QString &key, const QByteArray &bytes);
    // wie imageLoaded, für gif(); key ist die Adresse
    void gifLoaded(const QString &key, const QByteArray &bytes);

private:
    struct ListJob
    {
        EmoteProvider provider;
        bool channel;
        QUrl url;
        bool fromCacheTry = false; // zweiter Versuch nur aus dem Zwischenspeicher
    };
    struct ListResult
    {
        EmoteProvider provider;
        bool channel;
        QList<Emote> emotes;
        bool ok = false;         // gelesen (Netz oder Zwischenspeicher)
        bool netFailed = false;  // Server ohne Antwort
        bool fromCache = false;  // nach netFailed aus dem Zwischenspeicher
    };

    void abortLists();
    void startList(ListJob job);
    void listFinished(QNetworkReply *reply, const ListJob &job);
    void finishLoad();
    QNetworkReply *get(QNetworkAccessManager *network, const QUrl &real, int cacheControl, qint64 maxBytes);
    // Bild-Anfrage zu key starten; am Ende loaded(key, Bytes) über das Signal des Aufrufers
    bool startImage(QNetworkAccessManager *network, const QUrl &real, const QString &key, int cacheControl,
                    qint64 maxBytes, void (EmoteLoader::*loaded)(const QString &, const QByteArray &));

    EmoteEndpoints m_endpoints;
    QNetworkAccessManager *m_network = nullptr;
    QNetworkAccessManager *m_gifNetwork = nullptr; // ohne Zwischenspeicher (em-9)
    std::optional<QString> m_channelId;
    bool m_webp = true;
    bool m_channelValid = false;
    bool m_webpWarned = false; // «WebP fehlt» nur einmal ins Protokoll
    QList<QPointer<QNetworkReply>> m_listReplies;
    QList<ListResult> m_results;
    int m_pendingLists = 0;
    QHash<QString, QPointer<QNetworkReply>> m_imageReplies; // Schlüssel → laufende Anfrage
};

} // namespace dss
