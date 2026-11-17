#pragma once

#include <QByteArray>
#include <QJsonValue>
#include <QList>
#include <QString>

// Reihenfolge der Anfragen an den Kamera-Dienst (P15 f2a, Punkt 129). Der Dienst bearbeitet jede
// Anfrage in einem eigenen Thread; zwei Anfragen an dieselbe Lampe oder Kamera können sich dort
// überholen. Darum geht je Ziel die nächste schreibende Anfrage erst los, wenn die vorige
// beantwortet ist (oder ihre Zeitgrenze abgelaufen ist). Nur Qt Core, ohne Netz (Regel 9); den
// Versand macht CameraServiceClient.
//
// Ziele (callTarget):
// - eine Lampe: PUT lights/<id>, PUT lights/<id>/config, DELETE lights/<id>
// - «Licht» als Ganzes: PUT lights/all, POST lights, alles unter light-presets/; wartet auf jede
//   Lampe und jede Lampe auf «Licht» (ein Licht-Preset schaltet mehrere Lampen)
// - eine Kamera: cameras/<id>/… ausser move und stop
// - «Kamera» als Ganzes: alles unter camera-presets/ (ein Preset fährt eine Kamera, welche, weiss
//   nur der Dienst)
// - frei, wartet nie: GET, POST lights/check (liest nur), move und stop. Bewegen beim Halten
//   schickt alle 200 ms ein move, dessen Antwort bis 15 s dauern kann (Wecken der Tiny 2); in einer
//   Reihe würden sich die move stauen und stop käme zu spät. Ohne neue move hält der Dienst nach
//   500 ms von selbst an (Totmann, docs/09-kamera-dienst.md, «Bewegen»).
//
// Zusammenfassen: Wartet schon eine PUT mit gleichem Pfad, deren Felder dieselben sind (auch in
// verschachtelten Objekten), und steht keine andere Anfrage an dasselbe Ziel dahinter, ersetzt die
// neue deren Inhalt. Beide bekommen dieselbe Antwort, die des neuen Werts; Fenster und
// Aktionsverlauf sehen so keinen falschen Fehler. Nie zusammengefasst: andere Felder (sonst ginge
// ein Wert verloren), "toggle" (zweimal umschalten ist nicht einmal umschalten), POST (Bewegungen,
// step, Abrufe von Presets) und DELETE.
//
// Quelle: docs/09-kamera-dienst.md («Anfragen», «Bewegen», «Lampen», «Licht-Presets»).

namespace dss::camera {

struct CallTarget
{
    enum class Area { None, Light, Camera };

    Area area = Area::None; // None: frei
    QString key;            // Kennung der Lampe oder Kamera; leer = ganzer Bereich

    bool operator==(const CallTarget &) const = default;
};

// Ziel einer Anfrage; path relativ zu /api/v1/ wie bei CameraServiceClient::request().
CallTarget callTarget(const QByteArray &method, const QString &path);
// Zwei Ziele dürfen nicht gleichzeitig laufen.
bool targetsConflict(const CallTarget &a, const CallTarget &b);

class CallQueue
{
public:
    struct Call
    {
        quint64 id = 0;
        QByteArray method;
        QString path;
        QJsonValue body;
    };

    // Neue Anfrage. Rückgabe: Kennung, unter der sie beantwortet wird; nach dem Zusammenfassen die
    // der wartenden Anfrage, die jetzt den neuen Inhalt trägt.
    quint64 add(const QByteArray &method, const QString &path, const QJsonValue &body);
    // Anfragen, die jetzt losgehen dürfen, in der Reihenfolge von add(); ab hier laufen sie.
    QList<Call> takeReady();
    // Die Antwort auf eine laufende Anfrage ist da (auch Fehler und Zeitgrenze).
    void finish(quint64 id);

    qsizetype waitingCount() const { return m_waiting.size(); }
    qsizetype runningCount() const { return m_running.size(); }

private:
    struct Entry
    {
        Call call;
        CallTarget target;
    };

    QList<Entry> m_waiting;
    QList<Entry> m_running;
    quint64 m_nextId = 1;
};

} // namespace dss::camera
