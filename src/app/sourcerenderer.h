#pragma once

#include "canvasview.h"
#include "overlay.h"
#include "sourcegeometry.h"

#include <QHash>
#include <QImage>
#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <QUuid>

#include <chrono>
#include <map>
#include <memory>

class QPainter;

namespace dss {
class VideoPlayer;
struct VideoFrame;
}

// Zeichner der Leinwand im Editor (P09, Teil B, Schalter DSS_APP_RENDERER). Zeichnet die
// sichtbaren Quellen des gewählten Overlays in Zeichenreihenfolge mit Lage, Grösse und Drehung
// aus sourcegeometry.h. Bilder werden in einem Thread des Pools geladen, Videos in je einem
// eigenen Thread dekodiert (dss::VideoPlayer); im Thread der Oberfläche wird nur gezeichnet.
// Während ein Video läuft, meldet er 30 Mal pro Sekunde changed(), sonst nur bei Änderungen.
// Der Takt läuft nach geplanten Zeitpunkten (Beginn + n * 1/30 s), und jedes Video liefert das
// Bild für den geplanten Zeitpunkt (VideoPlayer::frameAt): So kommt bei 30 Bildern/s in jedem
// Takt genau ein neues Videobild, auch wenn der Timer um einige Millisekunden schwankt.
class SourceRenderer : public QObject
{
    Q_OBJECT

public:
    explicit SourceRenderer(QObject *parent = nullptr);
    ~SourceRenderer() override;

    // Datenordner des Kerns (hello.data_dir): dort liegen die Dateien der Quellen (media/…).
    void setDataDir(const QString &dir);

    // Gewähltes Overlay, nullptr = keines. Videos laufen ab der Wahl des Overlays
    // (Entscheid 10); sichtbar gewordene, neue oder geänderte Videos (Datei, Wiederholen,
    // Am Ende anhalten) beginnen von vorne. Verborgene Videos werden nicht dekodiert.
    void setOverlay(const dss::Overlay *overlay);

    // Zeichnet overlay; der Maler steht mit (0,0) in der Ecke der Fläche (CanvasFit) und ist
    // auf die Leinwand beschnitten. overlay darf von setOverlay abweichen (Vorschau beim Ziehen).
    void paint(QPainter *painter, const dss::Overlay &overlay, const dss::CanvasFit &fit) const;

    bool videoRunning() const { return m_timer.isActive(); }

    // Für Prüfungen und die Leistungsmessung.
    struct VideoInfo
    {
        QUuid sourceId;
        QString decoderName;
        double firstAlphaMean = -1.0;
        quint64 decoded = 0;
        quint64 late = 0;
        quint64 loops = 0;
        quint64 dropped = 0;
        bool hasFrame = false;
        int state = 0; // dss::VideoPlayer::State als int
        quint64 serial = 0; // zählt bei jedem neuen Bild (VideoPlayer::frameSerial)
    };
    QList<VideoInfo> videoInfo() const;
    bool imagesLoaded() const; // alle Bilder des Overlays geladen (oder mit Fehler)

signals:
    void changed();

private:
    struct ImageEntry
    {
        QImage image;
        bool loading = true;
    };
    struct VideoEntry
    {
        QString file;
        bool loop = false;
        bool holdAtEnd = false;
        std::unique_ptr<dss::VideoPlayer> player;
        std::shared_ptr<const dss::VideoFrame> frame; // gezeigtes Bild, im Takt nachgeführt
    };
    struct FontEntry
    {
        QString family;
        QList<dss::FontAxis> axes;
        QByteArray head;      // Tabelle 'head' der Datei, zum Vergleich mit der gewählten Schrift
        bool checked = false; // Vergleich schon gemacht (einmal je Datei)
    };

    QString mediaPath(const QString &reference) const;
    void requestImage(const QString &reference);
    FontEntry *font(const QString &reference) const; // lädt beim ersten Mal
    void onTick();
    void drawImage(QPainter *painter, const QImage &image, const dss::Source &source) const;
    void drawText(QPainter *painter, const dss::Source &source) const;

    QString m_dataDir;
    QUuid m_overlayId;
    QHash<QString, ImageEntry> m_images; // nach Verweis media/…
    std::map<QUuid, VideoEntry> m_videos; // nach Kennung der Quelle
    mutable QHash<QString, FontEntry> m_fonts; // nach Verweis media/…
    void startTicks();
    void scheduleNextTick();

    QTimer m_timer; // einmalig, für jeden Takt neu gestellt
    std::chrono::steady_clock::time_point m_tickStart;
    qint64 m_tick = 0;
    QThreadPool m_loader; // Bilder laden; der Destruktor wartet auf ihn
};
