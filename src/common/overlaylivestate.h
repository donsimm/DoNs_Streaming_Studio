#pragma once

#include "overlaylive.h"

#include <QElapsedTimer>
#include <QHash>
#include <QMap>
#include <QObject>
#include <QStringList>
#include <QUuid>

#include <functional>

class QNetworkAccessManager;
class QTimer;

namespace dss {

class OverlayStore;

// Laufzustand im Kern (P14 d, Plan 4.4): hält je Overlay die Elemente, die eine Aktion gerade
// zeigt, versteckt oder ändert, nur im Speicher. Nach der Dauer fällt der Laufzustand der Elemente
// weg. Profilbilder (Adresse aus einem Argument) lädt der Kern selbst, nur über HTTPS vom
// Bildserver von Twitch, höchstens 2 MiB, nur PNG und JPEG, und legt sie unter
// <Datenordner>/live-bilder/<SHA-256>.<endung> ab (höchstens 200, älteste zuerst gelöscht, beim
// Start geleert). Beide Zeichner zeigen dieselbe Datei (K-07): der Editor aus dem Datenordner, die
// Seite über …/live/<SHA-256>.<endung> (Overlay-Server). Qt Core und Qt Network, kein Qt Gui.
// https://doc.qt.io/qt-6.8/qnetworkaccessmanager.html, https://doc.qt.io/qt-6.8/qnetworkrequest.html
class OverlayLiveState : public QObject
{
    Q_OBJECT

public:
    struct Options
    {
        QString imageDir; // <Datenordner>/live-bilder
        // Bildserver der Profilbilder von Twitch: Beispiel «profile_image_url» in der Antwort von
        // «Get Users», https://dev.twitch.tv/docs/api/reference/#get-users (am 2026-11-15 nicht im
        // Web nachgelesen, Bericht P14 d [OFFEN]).
        QStringList imageHosts = {QStringLiteral("static-cdn.jtvnw.net")};
        bool allowHttp = false; // nur Tests: http:// und eigener Port erlaubt
        qint64 maxImageBytes = 2 * 1024 * 1024;
        int maxImages = 200;
        int timeoutMs = 10000;
        // Platzhalter einsetzen; Abschnitt c2 kann hier den Ablauf der Aktionen (Abschnitt a) einsetzen.
        std::function<QString(const QString &, const QHash<QString, QString> &)> substitute = substitutePlaceholders;
    };

    OverlayLiveState(const OverlayStore *store, const Options &options, QObject *parent = nullptr);
    ~OverlayLiveState() override;

    // Leert den Ordner der Profilbilder (beim Start des Kerns). Rückgabe: gelöschte Dateien.
    int clearImages();

    struct Result
    {
        bool ok = false;
        QString code;    // bei !ok: "unknown_overlay" oder "rejected" (err in protocol.h)
        QString message; // bei !ok
        QString warning; // bei ok: Hinweis (Bild nicht geladen, Text gekürzt); leer = keiner
    };
    using Done = std::function<void(const Result &)>;
    // Zeigt, versteckt oder schaltet die Elemente um. done kommt, sobald der Laufzustand gilt (mit
    // Profilbildern erst nach dem Laden, bei einem Fehler ohne das Bild, mit warning).
    void set(const LiveSetRequest &request, const Done &done);
    // Laufzustand zurücksetzen (sources leer = alle Elemente). Leer = ok, sonst Meldung.
    QString reset(const LiveResetRequest &request);
    // Nach Änderungen an den Overlays: Laufzustand gelöschter Overlays und Quellen fällt weg.
    void overlaysChanged();

    // Laufzustand eines Overlays mit Restzeit (remainingMs); leer = keiner.
    LiveElements elements(const QUuid &overlayId) const;
    // Alle Overlays mit Laufzustand (overlay_live)
    QList<OverlayLiveInfo> all() const;
    // Benutzt der Laufzustand des Overlays diese Datei ("media/…" oder "live-bilder/…")?
    bool usesImage(const QUuid &overlayId, const QString &reference) const;

signals:
    void changed(const QUuid &overlayId);

private:
    struct Entry
    {
        LiveElement element;
        qint64 deadlineMs = -1; // m_clock; -1 = ohne Ende
    };
    using Loaded = std::function<void(const QString &reference, const QString &error)>;

    void apply(const LiveSetRequest &request, const QHash<QUuid, QString> &images, QStringList warnings,
               const Done &done);
    void loadImage(const QString &url, const Loaded &loaded);
    QString checkImageUrl(const QString &url) const;
    QString storeImage(const QByteArray &data, QString *error);
    void pruneImages();
    void scheduleExpiry();
    void expire();

    const OverlayStore *m_store;
    Options m_options;
    QHash<QUuid, QMap<QUuid, Entry>> m_state;
    QElapsedTimer m_clock;
    QTimer *m_expiry = nullptr;
    QNetworkAccessManager *m_network = nullptr; // erst beim ersten Profilbild
    QHash<QString, QString> m_urlCache;         // Adresse → "live-bilder/…"
    QStringList m_imageOrder;                   // geladene Bilder, älteste zuerst
};

} // namespace dss
