#pragma once

#include <QObject>

class QTimer;

namespace dss {

// Zusammenfassen häufiger Änderungen beim Speichern (P08, Teil A). Nur Qt Core.
//
// Der Kern übernimmt jede Änderung sofort im Speicher und verteilt sie an alle Fenster; die
// Datei schreibt er erst, wenn idleMs lang keine weitere Änderung kam, spätestens aber maxDelayMs
// nach der ersten noch nicht gespeicherten Änderung. Hält der Nutzer eine Pfeiltaste gedrückt,
// entsteht so höchstens eine Datei je maxDelayMs statt einer je Schritt.
class SaveScheduler : public QObject
{
    Q_OBJECT

public:
    static constexpr int kDefaultIdleMs = 300;
    static constexpr int kDefaultMaxDelayMs = 1000;

    explicit SaveScheduler(QObject *parent = nullptr, int idleMs = kDefaultIdleMs,
                           int maxDelayMs = kDefaultMaxDelayMs);

    // Eine Änderung ist im Speicher, aber noch nicht in der Datei.
    void changed();
    bool isPending() const;
    // Sofort speichern (zum Beispiel vor dem Beenden): löst saveRequested() aus, wenn etwas offen ist.
    void saveNow();

signals:
    void saveRequested();

private:
    void fire();

    QTimer *m_idle;
    QTimer *m_max;
};

} // namespace dss
