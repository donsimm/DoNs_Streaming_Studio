#pragma once

#include <QObject>

namespace dss::events {
class EventBus;
}
namespace dss::obs {
class ObsConnection;
struct Scene;
}

// Adapter OBS → Ereignisfluss (P13, Plan 1.2, Punkt 92): veröffentlicht obs.scene_changed aus
// ObsConnection::currentSceneChanged. dss_obs bleibt unverändert und kennt den Fluss nicht.
class ObsEvents : public QObject
{
    Q_OBJECT

public:
    ObsEvents(dss::obs::ObsConnection *connection, dss::events::EventBus *bus, QObject *parent = nullptr);

private:
    void onSceneChanged(const dss::obs::Scene &scene);

    dss::events::EventBus *m_bus;
};
