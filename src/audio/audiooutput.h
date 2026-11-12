#pragma once

#include <QList>
#include <QString>

#include <functional>

// Ausgang eines Mischers (P12b b4): gemeinsame Schnittstelle des Ausgangs über PipeWire
// (pwoutput.h, b3) und des Ersatzausgangs ohne Gerät (nulloutput.h, für tst_kernton). Der Kern
// (CoreService, VideoAudio) kennt nur diese Schnittstelle.
//
// Threads wie in pwoutput.h: Die Funktionen dürfen aus jedem Thread ausser dem des Ausgangs
// aufgerufen werden. onChange und onLog laufen im Thread des Ausgangs: nur weiterreichen.

namespace dss {

struct AudioDevice
{
    QString name;        // node.name (target.object, kern.ini)
    QString description; // node.description, sonst node.name
    bool isDefault = false;

    bool operator==(const AudioDevice &) const = default;
};

class AudioOutput
{
public:
    enum class State {
        NoPipeWire, // keine Verbindung zu PipeWire
        NoTarget,   // kein Ausgang gewählt
        Missing,    // gewählter Ausgang fehlt (kein Ton, kein Ausweichen)
        Connecting, // Strom angelegt, Verbindung zum Ausgang noch nicht bestätigt (Stille)
        Playing,    // Strom läuft und hängt nur am gewählten Ausgang
        Error,      // Fehler des Stroms oder fremde Verbindung (Stille, Strom entfernt)
    };
    static const char *stateName(State state); // "no_pipewire", "no_target", … (Protokoll, Schnittstelle)

    virtual ~AudioOutput() = default;

    // Startet den Ausgang (Thread). false: kein Ton möglich, Grund in error.
    virtual bool start(QString *error) = 0;
    // Gewählter Ausgang (node.name); leer = kein Ausgang.
    virtual void setTarget(const QString &nodeName) = 0;
    virtual QString target() const = 0;
    virtual QList<AudioDevice> devices() const = 0;
    virtual State state() const = 0;
    // Kurzer Grund zum Zustand; sonst leer.
    virtual QString stateDetail() const = 0;

    // Liste der Ausgänge oder Zustand haben sich geändert (Thread des Ausgangs).
    std::function<void()> onChange;
    // Zeile fürs Protokoll (Thread des Ausgangs).
    std::function<void(const QString &)> onLog;
};

} // namespace dss
