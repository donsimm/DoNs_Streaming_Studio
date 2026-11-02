#pragma once

#include <QObject>

class QSocketNotifier;

namespace dss {

// Leitet SIGINT und SIGTERM in die Ereignisschleife von Qt weiter.
// Der Signal-Handler schreibt nur die Signalnummer in ein Socket-Paar; ausgewertet wird
// sie in der Ereignisschleife (Vorgehen nach https://doc.qt.io/qt-6.8/unix-signals.html).
// Nur ein Objekt pro Prozess.
class UnixSignalWatcher : public QObject
{
    Q_OBJECT

public:
    explicit UnixSignalWatcher(QObject *parent = nullptr);
    ~UnixSignalWatcher() override;

    // false, wenn Socket-Paar oder sigaction nicht eingerichtet werden konnten.
    bool isValid() const { return m_valid; }

signals:
    void terminationRequested(int signalNumber);

private:
    void readSignal();

    QSocketNotifier *m_notifier = nullptr;
    bool m_valid = false;
};

} // namespace dss
