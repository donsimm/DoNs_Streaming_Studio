#include "eventbus.h"

#include "eventtypes.h"

#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>

namespace dss::events {

namespace detail {

// Gemeinsamer Zustand von Abo, Fluss und den Zustellungen unterwegs. Die Sperre schützt active und
// filters: Das Kontextobjekt kann in einem anderen Thread zerstört werden, während der Fluss
// zustellt. Wird es zerstört, setzt destroyed (direkt im zerstörenden Thread) active auf false,
// bevor ~QObject die schon geschickten Zustellungen verwirft; publish stellt nur unter der Sperre
// und nur bei active zu.
struct SubscriberState
{
    QMutex mutex;
    bool active = true;
    QObject *context = nullptr;
    QStringList filters;
    EventBus::Handler handler; // nach dem Anlegen unverändert
    QMetaObject::Connection destroyedConnection;

    void deactivate()
    {
        QMetaObject::Connection connection;
        {
            QMutexLocker locker(&mutex);
            active = false;
            connection = destroyedConnection;
        }
        QObject::disconnect(connection);
    }
};

} // namespace detail

Subscription::Subscription(std::shared_ptr<detail::SubscriberState> state)
    : m_state(std::move(state))
{
}

Subscription &Subscription::operator=(Subscription &&other) noexcept
{
    if (this != &other) {
        unsubscribe();
        m_state = std::move(other.m_state);
    }
    return *this;
}

Subscription::~Subscription()
{
    unsubscribe();
}

bool Subscription::isActive() const
{
    if (!m_state)
        return false;
    QMutexLocker locker(&m_state->mutex);
    return m_state->active;
}

void Subscription::setFilters(const QStringList &filters)
{
    if (!m_state)
        return;
    QMutexLocker locker(&m_state->mutex);
    m_state->filters = filters;
}

void Subscription::unsubscribe()
{
    if (!m_state)
        return;
    m_state->deactivate();
    m_state.reset(); // der Fluss entfernt den Zustand beim nächsten publish
}

EventBus::EventBus(QObject *parent)
    : QObject(parent)
{
}

EventBus::~EventBus()
{
    for (const auto &state : std::as_const(m_subscribers))
        state->deactivate();
}

QString EventBus::validate(const Event &event)
{
    const EventType *spec = findEventType(event.type);
    if (!spec)
        return QStringLiteral("unbekannter Typ");
    if (!event.origin.isEmpty() && event.origin != QLatin1String(spec->origin))
        return QStringLiteral("Herkunft «%1» statt «%2»").arg(event.origin, QLatin1String(spec->origin));
    QSet<QString> expected;
    for (const EventField &field : spec->fields) {
        const QString name = QString::fromLatin1(field.name);
        if (!event.data.contains(name))
            return QStringLiteral("Feld «%1» fehlt").arg(name);
        expected.insert(name);
    }
    for (auto it = event.data.begin(); it != event.data.end(); ++it) {
        if (!expected.contains(it.key()))
            return QStringLiteral("Feld «%1» gehört nicht zum Typ").arg(it.key());
    }
    return {};
}

bool EventBus::publish(Event event)
{
    const QString problem = validate(event);
    if (!problem.isEmpty()) {
        ++m_rejected;
        // Nur Typ und Grund, nie der Inhalt (Chat-Texte, Plan «Annahmen»).
        qWarning("Ereignisfluss: Ereignis «%s» abgewiesen: %s", qPrintable(event.type), qPrintable(problem));
        return false;
    }
    if (event.id.isNull())
        event.id = QUuid::createUuid();
    if (event.origin.isEmpty())
        event.origin = QString::fromLatin1(findEventType(event.type)->origin);
    if (!event.time.isValid())
        event.time = QDateTime::currentDateTimeUtc();
    else
        event.time = event.time.toUTC();

    m_history.append(event);
    if (m_history.size() > kHistorySize)
        m_history.remove(0, m_history.size() - kHistorySize);

    for (qsizetype i = 0; i < m_subscribers.size();) {
        const std::shared_ptr<detail::SubscriberState> state = m_subscribers.at(i);
        QMutexLocker locker(&state->mutex);
        if (!state->active) {
            locker.unlock();
            m_subscribers.removeAt(i);
            continue;
        }
        if (matchesFilters(event.type, state->filters)) {
            QMetaObject::invokeMethod(
                state->context,
                [state, event] {
                    {
                        QMutexLocker inner(&state->mutex);
                        if (!state->active)
                            return; // abgemeldet, während das Ereignis unterwegs war
                    }
                    state->handler(event);
                },
                Qt::QueuedConnection);
        }
        ++i;
    }
    return true;
}

Subscription EventBus::subscribe(QObject *context, const QStringList &filters, Handler handler)
{
    Q_ASSERT(context);
    auto state = std::make_shared<detail::SubscriberState>();
    state->context = context;
    state->filters = filters;
    state->handler = std::move(handler);
    const std::weak_ptr<detail::SubscriberState> weak = state;
    // Ohne Empfänger: direkt im Thread, der das Kontextobjekt zerstört (siehe SubscriberState).
    state->destroyedConnection = connect(context, &QObject::destroyed, [weak] {
        if (const auto locked = weak.lock()) {
            QMutexLocker locker(&locked->mutex);
            locked->active = false;
        }
    });
    m_subscribers.append(state);
    return Subscription(state);
}

QList<Event> EventBus::history(const QStringList &filters) const
{
    QList<Event> result;
    for (const Event &event : m_history) {
        if (matchesFilters(event.type, filters))
            result.append(event);
    }
    return result;
}

} // namespace dss::events
