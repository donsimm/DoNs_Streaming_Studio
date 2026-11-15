#include "obsevents.h"

#include "eventbus.h"
#include "obsconnection.h"

ObsEvents::ObsEvents(dss::obs::ObsConnection *connection, dss::events::EventBus *bus, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
{
    connect(connection, &dss::obs::ObsConnection::currentSceneChanged, this, &ObsEvents::onSceneChanged);
}

void ObsEvents::onSceneChanged(const dss::obs::Scene &scene)
{
    // Leer: Szene unbekannt (getrennt, neue Einstellungen); das ist kein Wechsel. Der erste Stand
    // nach dem Verbinden ist dagegen ein Wechsel (ObsConnection meldet ihn so).
    if (scene.isNull())
        return;
    dss::events::Event event;
    event.type = QStringLiteral("obs.scene_changed");
    event.data = QJsonObject{{QStringLiteral("scene_uuid"), scene.uuid},
                             {QStringLiteral("scene_name"), scene.name}};
    m_bus->publish(event);
}
