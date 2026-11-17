#include "cameraactions.h"

#include "cameraprotocol.h"
#include "cameraserviceclient.h"
#include "cameraview.h"

#include <QJsonArray>
#include <QJsonObject>

using dss::actions::PresetKind;
using dss::actions::StepDone;
using dss::actions::StepResult;

CameraActions::CameraActions(dss::camera::CameraServiceClient *client)
    : m_client(client)
{
}

void CameraActions::recallPreset(PresetKind kind, const QString &presetId, StepDone done)
{
    if (m_client->state() == dss::camera::CameraServiceClient::State::Off) {
        done(StepResult::failure(QStringLiteral("Kamera-Dienst ist aus.")));
        return;
    }
    const dss::CameraCall call =
        kind == PresetKind::Light ? dss::lightPresetRecallCall(presetId) : dss::cameraPresetRecallCall(presetId);
    // Feste Liste wie bei camera_call: eine ungültige Kennung geht nie an den Dienst
    if (const QString problem = dss::checkCameraCall(call.method, call.path); !problem.isEmpty()) {
        done(StepResult::failure(problem));
        return;
    }
    const QString label = call.method + QLatin1Char(' ') + call.path;
    m_client->request(call.method.toLatin1(), call.path, call.body ? QJsonValue(*call.body) : QJsonValue(),
                      [kind, label, done](const dss::camera::Reply &reply) {
                          const QString error = dss::cameraResultError({0, reply.status, reply.body, reply.error});
                          if (!error.isEmpty()) {
                              qInfo("Aktionen: Kamera-Dienst: %s: %d %s", qPrintable(label), reply.status,
                                    qPrintable(reply.body.value(QStringLiteral("code")).toString()));
                              done(StepResult::failure(error));
                              return;
                          }
                          QStringList problems;
                          if (kind == PresetKind::Light) {
                              for (const QJsonValue &e : reply.body.value(QStringLiteral("errors")).toArray()) {
                                  const QJsonObject o = e.toObject();
                                  const QString message = o.value(QStringLiteral("message")).toString();
                                  problems.append(message.isEmpty() ? o.value(QStringLiteral("code")).toString() : message);
                              }
                          }
                          if (!problems.isEmpty()) {
                              qInfo("Aktionen: Kamera-Dienst: %s: %lld Meldungen", qPrintable(label),
                                    static_cast<long long>(problems.size()));
                              done(StepResult::failure(problems.join(QLatin1Char(' '))));
                              return;
                          }
                          done({});
                      });
}

QString CameraActions::presetName(PresetKind kind, const QString &presetId) const
{
    const QString key = kind == PresetKind::Light ? QStringLiteral("light_presets") : QStringLiteral("camera_presets");
    for (const QJsonValue &v : m_client->status().value(key).toArray()) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("id")).toString() == presetId)
            return o.value(QStringLiteral("name")).toString();
    }
    return {};
}
