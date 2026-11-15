#pragma once

#include "event.h"

#include <QList>
#include <QObject>
#include <QStringList>

#include <functional>
#include <memory>

// Ereignisfluss des Kerns (P13, Plan 1.1, Regel 14, Punkt 92). Jedes Modul veröffentlicht und
// abonniert über diesen Fluss; es gibt keine Plugins. Nur Qt Core, ohne protocol.h (Regel 9).
//
// - publish prüft gegen die Typenliste (eventtypes.h) und weist ab, was nicht passt.
// - Zustellung immer verzögert über das Kontextobjekt des Abos (Qt::QueuedConnection), nie im
//   Aufruf von publish: keine Rekursion, wenn ein Abonnent selbst veröffentlicht; Reihenfolge je
//   Abonnent fest; ein Abonnent in einem anderen Thread bekommt die Ereignisse in seinem Thread.
// - Verlauf: die letzten 200 Ereignisse, nur im Speicher.
//
// publish, subscribe und history nur aus dem Thread des Flusses (Hauptthread des Kerns). Das
// Kontextobjekt darf in einem anderen Thread leben und dort zerstört werden.

namespace dss::events {

class EventBus;

namespace detail {
struct SubscriberState;
}

// Abo; meldet sich beim Zerstören ab. Wird das Kontextobjekt zerstört, endet das Abo ebenfalls.
// Nach dem Abmelden wird kein Ereignis mehr zugestellt, auch keines, das schon unterwegs ist.
class Subscription
{
public:
    Subscription() = default;
    Subscription(Subscription &&other) noexcept = default;
    Subscription &operator=(Subscription &&other) noexcept;
    Subscription(const Subscription &) = delete;
    Subscription &operator=(const Subscription &) = delete;
    ~Subscription();

    bool isActive() const;
    // Filter ändern (leer = nichts mehr); gilt für Ereignisse, die danach veröffentlicht werden.
    void setFilters(const QStringList &filters);
    void unsubscribe();

private:
    friend class EventBus;
    explicit Subscription(std::shared_ptr<detail::SubscriberState> state);

    std::shared_ptr<detail::SubscriberState> m_state;
};

class EventBus : public QObject
{
    Q_OBJECT

public:
    using Handler = std::function<void(const Event &)>;

    static constexpr int kHistorySize = 200;

    explicit EventBus(QObject *parent = nullptr);
    ~EventBus() override;

    // Prüft das Ereignis gegen die Typenliste, vergibt id (wenn leer), origin (wenn leer) und time
    // (wenn ungültig: jetzt), legt es in den Verlauf und stellt es zu. false mit Warnung im
    // Protokoll: unbekannter Typ, falsche Herkunft, fehlendes oder zusätzliches Feld.
    bool publish(Event event);

    // filters: genaue Typen oder Vorsilben mit Punkt («twitch.»); leer = nichts. Der Handler läuft
    // im Thread von context.
    [[nodiscard]] Subscription subscribe(QObject *context, const QStringList &filters, Handler handler);

    // Letzte Ereignisse (höchstens kHistorySize), älteste zuerst, nur die zu den Filtern passenden.
    QList<Event> history(const QStringList &filters) const;

    // Zählt abgewiesene Ereignisse (Tests, Protokoll).
    int rejectedCount() const { return m_rejected; }

    // Grund, warum ein Ereignis abgewiesen würde; leer, wenn es zur Typenliste passt.
    static QString validate(const Event &event);

private:
    QList<std::shared_ptr<detail::SubscriberState>> m_subscribers;
    QList<Event> m_history;
    int m_rejected = 0;
};

} // namespace dss::events
