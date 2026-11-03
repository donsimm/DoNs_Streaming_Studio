#pragma once

#include <QJsonArray>
#include <QObject>
#include <QString>
#include <QUuid>

#include <map>

class MainWindow;

// Leistungsmessung der Leinwand (P09, wie T-02 aus P01/P02): schreibt Zeitstempel jedes Zeichnens
// der Leinwand und jedes neu gezeigten Videobildes, nach Ablauf der Dauer als JSON in die Datei
// (Felder wie prototypes/messung/t02.py sie liest: frames, video_frames, alpha_frames, info),
// und schliesst dann das Fenster. Auswertung: docs/berichte/P09-daten/leistung.py.
class CanvasMeasurement : public QObject
{
    Q_OBJECT

public:
    // Misst ab jetzt seconds Sekunden lang. Welches Video WebM-Alpha ist (alpha_frames), wird am
    // Decoder libvpx-vp9 erkannt.
    CanvasMeasurement(MainWindow *window, const QString &file, int seconds);

private:
    void onPainted();
    void finish();

    MainWindow *m_window;
    QString m_file;
    QJsonArray m_frames;
    std::map<QUuid, QJsonArray> m_videoFrames;
    std::map<QUuid, quint64> m_lastSerial;
    std::map<QUuid, QString> m_decoder;
    bool m_finished = false;
};
