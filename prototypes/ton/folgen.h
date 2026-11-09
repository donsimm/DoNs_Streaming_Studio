// Ton nach den Meldungen der Seite nachführen (Grundlage für P12b, Variante S1 und Q):
// Die Seite meldet je Bild mediaTime und expectedDisplayTime. Der Prototyp bildet die Uhr der
// Seite auf steady_clock ab (kleinster Abstand Eintreffen − performance.now() der letzten 10 s)
// und spielt die Probe, die zu dem Zeitpunkt gehört, an dem sie zu hören ist (Schreibzeit +
// geschätzter Pufferinhalt + Gerätezeit). Abweichung < 20 ms: weich über die Abspielrate
// (± 0,5 %), darüber Sprung. Stopp bei «stop», «aktiv: false» oder 3 s ohne Meldung.
#pragma once

#include "tondatei.h"

#include <QAudioDevice>
#include <QJsonObject>
#include <QString>
#include <QThread>

#include <memory>
#include <mutex>

struct FolgeOptionen
{
    QString ausgang;
    int pufferMs = 40;
    double versatzMs = 0; // > 0: Ton später
    double geraetMs = 0; // Verzögerung nach dem Puffer (aus M1)
    double lautstaerke = 0.25;
};

class Folger
{
public:
    Folger(TonDatei ton, FolgeOptionen o);
    ~Folger();
    bool starten(QString *fehler);
    void meldung(const QJsonObject &m, qint64 ankunftNs); // aus dem Hauptthread
    void beenden();
    QString zusammenfassung() const;
    bool protokollSchreiben(const QString &pfad) const;

    struct Eintrag
    {
        int64_t ns;
        double sollS, istS, abweichungMs, rate;
        bool sprung;
    };
    struct Ereignis
    {
        int64_t ns;
        QString text;
    };

private:
    friend class FolgeGenerator;
    struct Zustand
    {
        bool laeuft = false;
        bool hatAnker = false;
        double ankerMedien = 0; // s
        int64_t ankerLokalNs = 0;
        int64_t letzteMeldungNs = 0;
    };

    TonDatei m_ton;
    FolgeOptionen m_o;
    QThread m_thread;
    std::unique_ptr<QObject> m_ctx;
    class QAudioSink *m_sink = nullptr;
    class FolgeGenerator *m_gen = nullptr;
    qint64 m_pufferIst = 0;
    int64_t m_startNs = 0;

    mutable std::mutex m_mutex;
    Zustand m_z;
    std::vector<std::pair<int64_t, int64_t>> m_abstaende; // Eintreffen, Eintreffen − Seitenzeit
    std::vector<Eintrag> m_protokoll; // nur im Audio-Thread geschrieben, nach beenden() gelesen
    std::vector<Ereignis> m_ereignisse;
};
