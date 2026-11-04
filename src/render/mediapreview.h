#pragma once

#include "medialibrary.h"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QSize>
#include <QThreadPool>

// Vorschaubilder im Dock «Medien» (P10b, Teil B 1). Qt Gui ohne Qt Widgets (CLAUDE.md Regel 9);
// geladen wird in einem Thread des Pools, nie im Thread der Oberfläche.
//   Bild:    die Datei, mit EXIF-Ausrichtung wie auf der Leinwand (imageorientation.h)
//   Video:   das erste dekodierte Bild (VideoPlayer, mit Drehung aus den Metadaten). Gewählt,
//            weil es das Bild ist, mit dem eine neue Quelle auf der Leinwand beginnt, und weil es
//            ohne Suchen in der Datei kommt (schnell, auch bei Dateien ohne Index). Beginnt ein
//            Video schwarz oder durchsichtig, ist die Vorschau ebenso leer.
//   Schrift: «Aa» in der Schrift (QRawFont, ohne die Schrift im Programm anzumelden)
// Quellen: https://doc.qt.io/qt-6.8/qimagereader.html, https://doc.qt.io/qt-6.8/qrawfont.html,
// https://doc.qt.io/qt-6.8/qpainter.html#drawGlyphRun, https://doc.qt.io/qt-6.8/qthreadpool.html

namespace dss {

// Vorschau einer Datei, eingepasst in box (Seitenverhältnis bleibt, nie grösser als die Datei,
// durchsichtiger Rand). Leer mit Meldung, wenn die Datei nicht lesbar ist. Blockiert (Video:
// bis das erste Bild dekodiert ist, höchstens timeoutMs).
QImage mediaPreview(const QString &path, MediaKind kind, const QSize &box, const QColor &textColor,
                    QString *error, int timeoutMs = 3000);

class MediaPreviewLoader : public QObject
{
    Q_OBJECT

public:
    MediaPreviewLoader(const QSize &box, const QColor &textColor, QObject *parent = nullptr);
    ~MediaPreviewLoader() override; // wartet auf den Pool

    // Datenordner des Kerns (hello.data_dir); leert den Zwischenspeicher.
    void setDataDir(const QString &dir);
    // Vorschau eines Verweises "media/…" aus dem Zwischenspeicher. Fehlt sie, beginnt das Laden
    // und das Ergebnis ist leer; danach kommt previewReady. Die Dateien in media/ ändern sich nie
    // (Name = SHA-256 des Inhalts), der Zwischenspeicher gilt also, bis der Datenordner wechselt.
    QImage preview(const QString &reference);

signals:
    void previewReady(const QString &reference);

private:
    QSize m_box;
    QColor m_textColor;
    QString m_dataDir;
    quint64 m_generation = 0; // zählt mit setDataDir; ältere Ergebnisse verwerfen
    QHash<QString, QImage> m_cache;
    QSet<QString> m_loading;
    QThreadPool m_pool;
};

} // namespace dss
