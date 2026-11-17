#include "cameraview.h"

#include <QJsonValue>

#include <algorithm>
#include <cmath>

namespace dss {

namespace {

const QString kTiny2Model = QStringLiteral("OBSBOT Tiny 2");
const QString kNotThere = QStringLiteral("Diese Kamera hat das nicht.");
const QString kNoValue = QStringLiteral("–");

QString str(const QJsonObject &o, const char *key)
{
    return o.value(QLatin1String(key)).toString();
}

// Ganze Grad mit echtem Minuszeichen wie im Entwurf («−14°»)
QString degreeText(const QJsonValue &value)
{
    if (!value.isDouble())
        return kNoValue;
    const int degrees = qRound(value.toDouble());
    return (degrees < 0 ? QStringLiteral("−%1°").arg(-degrees) : QStringLiteral("%1°").arg(degrees));
}

QJsonObject controlByKey(const QJsonArray &controls, const QString &key)
{
    for (const QJsonValue &v : controls) {
        const QJsonObject o = v.toObject();
        if (str(o, "key") == key)
            return o;
    }
    return {};
}

// Zoom in Prozent des Bereichs aus zoom_absolute; ohne Bereich nullopt
std::optional<int> zoomPercent(const QJsonArray &controls, const QJsonValue &value)
{
    const QJsonObject zoom = controlByKey(controls, QStringLiteral("zoom_absolute"));
    if (zoom.isEmpty() || !value.isDouble())
        return std::nullopt;
    const double minimum = zoom.value(QLatin1String("min")).toDouble();
    const double maximum = zoom.value(QLatin1String("max")).toDouble();
    if (maximum <= minimum)
        return std::nullopt;
    return qRound((value.toDouble() - minimum) * 100.0 / (maximum - minimum));
}

QString cameraPath(const QString &cameraId, const char *rest)
{
    return QStringLiteral("cameras/%1/%2").arg(cameraId, QLatin1String(rest));
}

CameraCall call(const char *method, const QString &path, std::optional<QJsonObject> body = std::nullopt)
{
    CameraCall c;
    c.method = QString::fromLatin1(method);
    c.path = path;
    c.body = std::move(body);
    return c;
}

QJsonObject axes(int pan, int tilt, int zoom)
{
    return {{QStringLiteral("pan"), pan}, {QStringLiteral("tilt"), tilt}, {QStringLiteral("zoom"), zoom}};
}

QString lightCell(const QJsonValue &value)
{
    if (!value.isObject())
        return kNoValue;
    const QJsonObject o = value.toObject();
    const QJsonValue on = o.value(QLatin1String("on"));
    if (on.isBool() && !on.toBool())
        return QStringLiteral("aus");
    QStringList parts;
    if (on.isBool())
        parts << QStringLiteral("ein");
    if (o.value(QLatin1String("brightness")).isDouble())
        parts << lightBrightnessText(o.value(QLatin1String("brightness")).toInt());
    if (o.value(QLatin1String("temperature")).isDouble())
        parts << lightKelvinText(lightKelvinFromMired(o.value(QLatin1String("temperature")).toInt()));
    return parts.isEmpty() ? kNoValue : parts.join(QStringLiteral(" · "));
}

} // namespace

int lightKelvinFromMired(int mired)
{
    const double kelvin = 1000000.0 / std::clamp(mired, kLightMiredMin, kLightMiredMax);
    const int rounded = qRound(kelvin / kLightKelvinStep) * kLightKelvinStep;
    return std::clamp(rounded, kLightKelvinMin, kLightKelvinMax);
}

int lightMiredFromKelvin(int kelvin)
{
    return std::clamp(qRound(1000000.0 / std::max(kelvin, 1)), kLightMiredMin, kLightMiredMax);
}

int lightBrightnessClamped(int brightness)
{
    return std::clamp(brightness, kLightBrightnessMin, kLightBrightnessMax);
}

QString lightKelvinText(int kelvin)
{
    return QStringLiteral("%1 K").arg(kelvin);
}

QString lightBrightnessText(int brightness)
{
    return QStringLiteral("%1 %").arg(brightness);
}

CameraServiceView cameraServiceView(const std::optional<CameraServiceInfo> &info)
{
    CameraServiceView v;
    if (!info) {
        v.text = QStringLiteral("Kamera-Dienst: –");
        return v;
    }
    const QString &state = info->state;
    if (state == QLatin1String(kCameraStateConnected)) {
        v.text = QStringLiteral("Kamera-Dienst: verbunden");
        v.state = QStringLiteral("ok");
        v.usable = true;
    } else if (state == QLatin1String(kCameraStateConnecting)) {
        v.text = QStringLiteral("Kamera-Dienst: Verbindungsversuch läuft");
        v.state = QStringLiteral("warning");
    } else if (state == QLatin1String(kCameraStateError)) {
        v.text = QStringLiteral("Kamera-Dienst nicht erreichbar:");
        v.detail = info->message;
        v.state = QStringLiteral("error");
        v.blockedHint = QStringLiteral("Steuerung, Bild und Licht sind gesperrt, bis der Dienst wieder antwortet.");
    } else if (state == QLatin1String(kCameraStateUnavailable)) {
        v.text = QStringLiteral("Kamera-Dienst: in dieser Version nicht eingebaut");
        v.detail = info->message;
    } else {
        v.text = QStringLiteral("Kamera-Dienst: aus");
        v.detail = QStringLiteral("Einschalten im Dialog «Einstellungen»");
    }
    return v;
}

QList<CameraEntry> cameraEntries(const QJsonArray &cameras)
{
    QList<CameraEntry> list;
    for (const QJsonValue &v : cameras) {
        const QJsonObject o = v.toObject();
        CameraEntry e;
        e.id = str(o, "id");
        if (e.id.isEmpty())
            continue;
        e.name = str(o, "name");
        e.model = str(o, "model");
        e.state = str(o, "state");
        e.present = o.value(QLatin1String("present")).toBool();
        e.ptz = o.value(QLatin1String("ptz")).toBool();
        if (e.name.isEmpty())
            e.name = e.model.isEmpty() ? e.id : e.model;
        list.append(e);
    }
    return list;
}

bool cameraIsTiny2(const CameraEntry &camera)
{
    return camera.model == kTiny2Model;
}

QString cameraStateText(const QString &state)
{
    if (state == QLatin1String("awake"))
        return QStringLiteral("wach");
    if (state == QLatin1String("asleep"))
        return QStringLiteral("schläft");
    if (state == QLatin1String("privacy"))
        return QStringLiteral("Privatsphäre");
    if (state == QLatin1String("absent"))
        return QStringLiteral("fehlt");
    return QStringLiteral("unbekannt");
}

QString cameraChoiceText(const CameraEntry &camera)
{
    return camera.name + QStringLiteral(" · ") + cameraStateText(camera.state);
}

QString cameraChoiceAfterUpdate(const QList<CameraEntry> &cameras, const QString &currentId)
{
    for (const CameraEntry &c : cameras) {
        if (c.id == currentId)
            return currentId;
    }
    return cameras.isEmpty() ? QString() : cameras.first().id;
}

CameraControlView cameraControlView(const CameraServiceView &service, const CameraEntry *camera,
                                    bool presetSelected, const QJsonArray &controls)
{
    CameraControlView v;
    v.chooserEnabled = service.usable && camera;
    if (!service.usable)
        return v;
    if (!camera) {
        v.moveHint = QStringLiteral("Der Dienst meldet keine Kamera.");
        return v;
    }
    const bool tiny2 = cameraIsTiny2(*camera);
    const QString &state = camera->state;
    const bool awake = camera->present && state == QLatin1String("awake");
    const bool asleep = camera->present && state == QLatin1String("asleep");
    v.setupEnabled = true;

    // Wecken/Schlafen nur bei der Tiny 2 (Entwurf Seite 4 C: bei der C922 ausgegraut)
    if (tiny2) {
        v.wakeText = awake ? QStringLiteral("Schlafen") : QStringLiteral("Wecken");
        v.wakeEnabled = camera->present && state != QLatin1String("unknown");
    } else {
        v.wakeText = QStringLiteral("Schlafen");
        v.wakeToolTip = kNotThere;
    }

    v.moveEnabled = awake;
    if (!camera->present) {
        v.moveHint = QStringLiteral("Kamera nicht gefunden. Sie erscheint wieder, sobald sie eingesteckt ist.");
    } else if (asleep) {
        v.moveHint = QStringLiteral("Kamera schläft");
        v.moveHintState = QStringLiteral("warning");
        v.moveHintWake = true;
    } else if (state == QLatin1String("privacy")) {
        v.moveHint = QStringLiteral("Kamera im Modus «Privatsphäre»");
        v.moveHintState = QStringLiteral("warning");
    } else if (!awake) {
        v.moveHint = QStringLiteral("Zustand der Kamera unbekannt");
    }

    // KI-Verfolgung: Wert «obsbot_ai» des Dienstes (obsbot.py, P15 e1b), nur bei wacher Tiny 2
    if (!tiny2) {
        v.aiHint = kNotThere;
    } else {
        const QJsonObject ai = controlByKey(controls, QStringLiteral("obsbot_ai"));
        v.aiValue = ai.value(QLatin1String("value")).toInt();
        for (const QJsonValue &m : ai.value(QLatin1String("menu")).toArray()) {
            const QJsonObject entry = m.toObject();
            v.aiMenu.append({entry.value(QLatin1String("value")).toInt(), str(entry, "label")});
        }
        v.aiEnabled = awake && !ai.isEmpty();
        if (!awake)
            v.aiHint = QStringLiteral("Nur bei wacher Kamera");
    }

    v.presetsEnabled = camera->present;
    v.editEnabled = v.presetsEnabled && presetSelected;
    // Abrufen weckt die Tiny 2 (recall mit wake, Standard); Speichern im Schlaf nähme die
    // Schlafstellung (docs/09-kamera-dienst.md, «Kamera-Presets»)
    v.recallEnabled = presetSelected && (awake || asleep);
    if (asleep)
        v.recallHint = QStringLiteral("weckt die Kamera zuerst");
    v.saveEnabled = awake;
    v.imageEnabled = camera->present;
    return v;
}

QString cameraImageTitle(const CameraEntry *camera)
{
    return camera ? QStringLiteral("Bildwerte · ") + camera->name : QStringLiteral("Bildwerte");
}

QStringList cameraControlSections()
{
    return {QStringLiteral("Bild"),          QStringLiteral("Belichtung"), QStringLiteral("Fokus"),
            QStringLiteral("Weissabgleich"), QStringLiteral("Weitere"),    QStringLiteral("OBSBOT")};
}

const QList<CameraControlSpec> &cameraControlSpecs()
{
    // Schlüssel: Namen der V4L2-Werte im Kernel (drivers/media/v4l2-core/v4l2-ctrls-defs.c) nach der
    // Regel des Dienstes (controls.control_key); Gruppe «OBSBOT» aus obsbot.py. Namen wie im Dienst
    // (controls.LABELS), im Bereich ohne Wiederholung («Temperatur» statt «Farbtemperatur»).
    using K = CameraControlKind;
    static const QList<CameraControlSpec> specs = {
        {QStringLiteral("brightness"), QStringLiteral("Helligkeit"), QStringLiteral("Bild"), K::Slider},
        {QStringLiteral("contrast"), QStringLiteral("Kontrast"), QStringLiteral("Bild"), K::Slider},
        {QStringLiteral("saturation"), QStringLiteral("Sättigung"), QStringLiteral("Bild"), K::Slider},
        {QStringLiteral("sharpness"), QStringLiteral("Schärfe"), QStringLiteral("Bild"), K::Slider},
        {QStringLiteral("gain"), QStringLiteral("Verstärkung"), QStringLiteral("Bild"), K::Slider},
        {QStringLiteral("gamma"), QStringLiteral("Gamma"), QStringLiteral("Bild"), K::Slider},
        {QStringLiteral("hue"), QStringLiteral("Farbton"), QStringLiteral("Bild"), K::Slider},
        {QStringLiteral("backlight_compensation"), QStringLiteral("Gegenlicht"), QStringLiteral("Bild"), K::Slider},
        {QStringLiteral("auto_exposure"), QStringLiteral("Modus"), QStringLiteral("Belichtung"), K::Choice},
        {QStringLiteral("exposure_time_absolute"), QStringLiteral("Belichtungszeit"), QStringLiteral("Belichtung"),
         K::Slider},
        {QStringLiteral("exposure_dynamic_framerate"), QStringLiteral("Dynamische Bildrate"),
         QStringLiteral("Belichtung"), K::Check},
        {QStringLiteral("focus_automatic_continuous"), QStringLiteral("Autofokus"), QStringLiteral("Fokus"), K::Check},
        {QStringLiteral("focus_absolute"), QStringLiteral("Fokus"), QStringLiteral("Fokus"), K::Slider},
        {QStringLiteral("white_balance_automatic"), QStringLiteral("Automatisch"), QStringLiteral("Weissabgleich"),
         K::Check},
        {QStringLiteral("white_balance_temperature"), QStringLiteral("Temperatur"), QStringLiteral("Weissabgleich"),
         K::Slider},
        {QStringLiteral("power_line_frequency"), QStringLiteral("Netzfrequenz"), QStringLiteral("Weitere"), K::Choice},
        {QStringLiteral("obsbot_hdr"), QStringLiteral("HDR"), QStringLiteral("OBSBOT"), K::Check},
        {QStringLiteral("obsbot_fov"), QStringLiteral("Bildwinkel"), QStringLiteral("OBSBOT"), K::Choice},
        {QStringLiteral("obsbot_auto_sleep"), QStringLiteral("Auto-Schlaf"), QStringLiteral("OBSBOT"), K::Text},
    };
    return specs;
}

QList<CameraControlRow> cameraControlRows(const QJsonArray &controls, const CameraEntry *camera,
                                          const CameraServiceView &service)
{
    const bool cameraThere = service.usable && camera && camera->present;
    const bool awake = cameraThere && camera->state == QLatin1String("awake");
    QList<CameraControlRow> rows;
    for (const CameraControlSpec &spec : cameraControlSpecs()) {
        CameraControlRow row;
        row.spec = spec;
        const QJsonObject o = controlByKey(controls, spec.key);
        if (o.isEmpty() || !cameraThere) {
            rows.append(row);
            continue;
        }
        row.available = true;
        row.minimum = o.value(QLatin1String("min")).toInt();
        row.maximum = o.value(QLatin1String("max")).toInt();
        row.step = std::max(1, o.value(QLatin1String("step")).toInt(1));
        const QJsonValue value = o.value(QLatin1String("value"));
        row.value = value.isBool() ? int(value.toBool()) : value.toInt();
        row.remembered = o.value(QLatin1String("remembered")).toBool();
        for (const QJsonValue &m : o.value(QLatin1String("menu")).toArray()) {
            const QJsonObject entry = m.toObject();
            row.menu.append({entry.value(QLatin1String("value")).toInt(), str(entry, "label")});
        }
        const bool inactive = o.value(QLatin1String("inactive")).toBool();
        const bool readOnly = o.value(QLatin1String("readonly")).toBool();
        const bool obsbot = spec.section == QLatin1String("OBSBOT");
        row.enabled = !inactive && !readOnly && (!obsbot || awake);
        if (inactive)
            row.toolTip = QStringLiteral("Bei eingeschalteter Automatik nicht einstellbar");
        else if (obsbot && !readOnly && !awake)
            row.toolTip = QStringLiteral("Nur bei wacher Kamera");

        switch (spec.kind) {
        case CameraControlKind::Check:
            row.valueText = row.value ? QStringLiteral("ein") : QStringLiteral("aus");
            break;
        case CameraControlKind::Choice: {
            row.valueText = QString::number(row.value);
            for (const auto &[menuValue, label] : row.menu) {
                if (menuValue == row.value)
                    row.valueText = label;
            }
            break;
        }
        case CameraControlKind::Text:
            // Auto-Schlaf in Sekunden, 0 = nie
            row.valueText = row.value == 0 ? QStringLiteral("nie") : QStringLiteral("%1 s").arg(row.value);
            break;
        case CameraControlKind::Slider:
            row.valueText = cameraSliderText(spec.key, row.value);
            break;
        }
        rows.append(row);
    }
    return rows;
}

QString cameraSectionHint(const QList<CameraControlRow> &rows, const QString &section)
{
    bool inSection = false;
    for (const CameraControlRow &row : rows) {
        if (row.spec.section != section)
            continue;
        inSection = true;
        if (row.available)
            return {};
    }
    return inSection ? kNotThere : QString();
}

bool cameraSectionResettable(const QList<CameraControlRow> &rows, const QString &section)
{
    return std::any_of(rows.cbegin(), rows.cend(), [&section](const CameraControlRow &row) {
        return row.spec.section == section && row.enabled && row.remembered;
    });
}

QString cameraZoomText(const QJsonArray &controls)
{
    const QJsonObject zoom = controlByKey(controls, QStringLiteral("zoom_absolute"));
    const std::optional<int> percent = zoomPercent(controls, zoom.value(QLatin1String("value")));
    return percent ? QStringLiteral("%1 %").arg(*percent) : kNoValue;
}

QList<CameraPresetRow> cameraPresetRows(const QJsonArray &presets, const QString &cameraId,
                                        const QJsonArray &controls)
{
    QList<CameraPresetRow> rows;
    for (const QJsonValue &v : presets) {
        const QJsonObject o = v.toObject();
        const QJsonValue camera = o.value(QLatin1String("camera"));
        const bool full = camera.isString();
        if (full && camera.toString() != cameraId)
            continue;
        CameraPresetRow row;
        row.id = str(o, "id");
        row.name = str(o, "name");
        if (full) {
            const std::optional<int> zoom = zoomPercent(controls, o.value(QLatin1String("zoom")));
            row.position = degreeText(o.value(QLatin1String("pan"))) + QStringLiteral(" · ")
                           + degreeText(o.value(QLatin1String("tilt"))) + QStringLiteral(" · Zoom ")
                           + (zoom ? QStringLiteral("%1 %").arg(*zoom) : kNoValue);
            row.withControls = o.value(QLatin1String("controls")).isObject() ? QStringLiteral("✔") : kNoValue;
        } else {
            row.position = kNoValue;
            row.withControls = kNoValue;
        }
        rows.append(row);
    }
    return rows;
}

QList<LightRow> lightRows(const QJsonArray &lights, const CameraServiceView &service)
{
    QList<LightRow> rows;
    for (const QJsonValue &v : lights) {
        const QJsonObject o = v.toObject();
        LightRow row;
        row.id = str(o, "id");
        row.name = str(o, "name");
        row.reachable = o.value(QLatin1String("reachable")).toBool();
        row.enabled = service.usable && row.reachable;
        row.stateText = row.reachable ? QStringLiteral("erreichbar") : QStringLiteral("nicht erreichbar");
        row.stateKey = row.reachable ? QStringLiteral("ok") : QStringLiteral("error");
        if (row.reachable) {
            row.on = o.value(QLatin1String("on")).toBool();
            row.brightness = lightBrightnessClamped(o.value(QLatin1String("brightness")).toInt());
            row.kelvin = lightKelvinFromMired(o.value(QLatin1String("temperature")).toInt());
            row.brightnessText = lightBrightnessText(row.brightness);
            row.kelvinText = lightKelvinText(row.kelvin);
        }
        rows.append(row);
    }
    return rows;
}

QStringList lightLayoutKey(const QJsonArray &lights)
{
    QStringList key;
    for (const QJsonValue &v : lights) {
        const QJsonObject o = v.toObject();
        key << str(o, "id") << str(o, "name");
    }
    return key;
}

QList<LightPresetRow> lightPresetRows(const QJsonArray &presets, const QJsonArray &lights)
{
    QList<LightPresetRow> rows;
    for (const QJsonValue &v : presets) {
        const QJsonObject o = v.toObject();
        LightPresetRow row;
        row.id = str(o, "id");
        row.name = str(o, "name");
        const QJsonObject values = o.value(QLatin1String("lights")).toObject();
        for (const QJsonValue &light : lights)
            row.cells << lightCell(values.value(str(light.toObject(), "id")));
        rows.append(row);
    }
    return rows;
}

CameraCall cameraMoveCall(const QString &cameraId, int pan, int tilt, int zoom)
{
    // Ohne «speed»: Der Dienst nimmt das Tempo aus den Einstellungen der Kamera.
    return call("POST", cameraPath(cameraId, "move"), axes(pan, tilt, zoom));
}

CameraCall cameraStopCall(const QString &cameraId)
{
    return call("POST", cameraPath(cameraId, "stop"));
}

CameraCall cameraStepCall(const QString &cameraId, int pan, int tilt, int zoom)
{
    return call("POST", cameraPath(cameraId, "step"), axes(pan, tilt, zoom));
}

CameraCall cameraHomeCall(const QString &cameraId)
{
    return call("POST", cameraPath(cameraId, "home"));
}

CameraCall cameraWakeCall(const QString &cameraId, bool wake)
{
    return call("POST", cameraPath(cameraId, wake ? "wake" : "sleep"));
}

CameraCall cameraControlsGetCall(const QString &cameraId)
{
    return call("GET", cameraPath(cameraId, "controls"));
}

CameraCall cameraConfigGetCall(const QString &cameraId)
{
    return call("GET", cameraPath(cameraId, "config"));
}

CameraCall cameraControlsPutCall(const QString &cameraId, const QJsonObject &values)
{
    return call("PUT", cameraPath(cameraId, "controls"), values);
}

CameraCall cameraAiCall(const QString &cameraId, int mode)
{
    return cameraControlsPutCall(cameraId, {{QStringLiteral("obsbot_ai"), mode}});
}

CameraCall cameraSectionResetCall(const QString &cameraId, const QList<CameraControlRow> &rows,
                                  const QString &section)
{
    QJsonObject values;
    for (const CameraControlRow &row : rows) {
        if (row.spec.section == section && row.enabled)
            values.insert(row.spec.key, QJsonValue::Null);
    }
    return cameraControlsPutCall(cameraId, values);
}

CameraCall cameraPresetRecallCall(const QString &presetId)
{
    return call("POST", QStringLiteral("camera-presets/%1/recall").arg(presetId), QJsonObject());
}

CameraCall cameraPresetCreateCall(const QString &name, const QString &cameraId, bool withControls)
{
    return call("POST", QStringLiteral("camera-presets"),
                QJsonObject{{QStringLiteral("name"), name},
                            {QStringLiteral("camera"), cameraId},
                            {QStringLiteral("with_controls"), withControls}});
}

CameraCall cameraPresetOverwriteCall(const QString &presetId, bool withControls)
{
    return call("POST", QStringLiteral("camera-presets/%1/save-current").arg(presetId),
                QJsonObject{{QStringLiteral("with_controls"), withControls}});
}

CameraCall cameraPresetGetCall(const QString &presetId)
{
    return call("GET", QStringLiteral("camera-presets/%1").arg(presetId));
}

CameraCall cameraPresetRenameCall(const QString &presetId, const QString &name)
{
    return call("PUT", QStringLiteral("camera-presets/%1").arg(presetId),
                QJsonObject{{QStringLiteral("name"), name}});
}

CameraCall cameraPresetDeleteCall(const QString &presetId)
{
    return call("DELETE", QStringLiteral("camera-presets/%1").arg(presetId));
}

CameraCall cameraConfigPutCall(const QString &cameraId, const QJsonObject &values)
{
    return call("PUT", cameraPath(cameraId, "config"), values);
}

QString cameraResultError(const CameraResult &result)
{
    if (result.status >= 200 && result.status < 300)
        return {};
    if (result.status == 0)
        return result.error.isEmpty() ? QStringLiteral("keine Antwort") : result.error;
    const QString message = str(result.body, "message");
    return message.isEmpty() ? QStringLiteral("Fehler %1").arg(result.status) : message;
}

QString cameraSpeedSuffix(const CameraEntry *camera)
{
    return camera && cameraIsTiny2(*camera) ? QStringLiteral(" °/s") : QStringLiteral(" %");
}

std::pair<double, double> cameraConfigRange(const QJsonObject &config, const QString &key, double fallbackMin,
                                            double fallbackMax)
{
    const QJsonArray range = config.value(QLatin1String("ranges")).toObject().value(key).toArray();
    if (range.size() != 2 || !range.at(0).isDouble() || !range.at(1).isDouble())
        return {fallbackMin, fallbackMax};
    return {range.at(0).toDouble(), range.at(1).toDouble()};
}

QJsonObject cameraConfigChanges(const QJsonObject &before, const QJsonObject &after)
{
    QJsonObject changes;
    for (auto it = after.constBegin(); it != after.constEnd(); ++it) {
        if (before.value(it.key()) != it.value())
            changes.insert(it.key(), it.value());
    }
    return changes;
}

QList<CameraRememberedRow> cameraRememberedRows(const QJsonArray &controls)
{
    // Anzeige wie in der mittleren Spalte (cameraControlRows), auch für den Standard des Geräts
    const auto text = [](const CameraControlSpec *spec, const QJsonObject &o, const QJsonValue &v) {
        if (v.isBool())
            return v.toBool() ? QStringLiteral("ein") : QStringLiteral("aus");
        if (!v.isDouble())
            return kNoValue;
        const int value = v.toInt();
        for (const QJsonValue &m : o.value(QLatin1String("menu")).toArray()) {
            const QJsonObject entry = m.toObject();
            if (entry.value(QLatin1String("value")).toInt() == value)
                return str(entry, "label");
        }
        if (!spec)
            return QString::number(value);
        switch (spec->kind) {
        case CameraControlKind::Check:
            return value ? QStringLiteral("ein") : QStringLiteral("aus");
        case CameraControlKind::Text:
            return value == 0 ? QStringLiteral("nie") : QStringLiteral("%1 s").arg(value);
        case CameraControlKind::Slider:
            return cameraSliderText(spec->key, value);
        case CameraControlKind::Choice:
            break;
        }
        return QString::number(value);
    };
    QList<CameraRememberedRow> rows;
    for (const QJsonValue &c : controls) {
        const QJsonObject o = c.toObject();
        if (!o.value(QLatin1String("remembered")).toBool())
            continue;
        const QString key = str(o, "key");
        const CameraControlSpec *spec = nullptr;
        for (const CameraControlSpec &s : cameraControlSpecs()) {
            if (s.key == key)
                spec = &s;
        }
        CameraRememberedRow row;
        row.key = key;
        row.label = str(o, "label").isEmpty() ? (spec ? spec->label : key) : str(o, "label");
        row.value = text(spec, o, o.value(QLatin1String("value")));
        row.defaultValue = text(spec, o, o.value(QLatin1String("default")));
        rows.append(row);
    }
    return rows;
}

CameraCall lightPutCall(const QString &lightId, const QJsonObject &values)
{
    return call("PUT", QStringLiteral("lights/%1").arg(lightId), values);
}

CameraCall lightsAllCall(bool on)
{
    return lightPutCall(QStringLiteral("all"), QJsonObject{{QStringLiteral("on"), on}});
}

CameraCall lightPresetRecallCall(const QString &presetId)
{
    return call("POST", QStringLiteral("light-presets/%1/recall").arg(presetId), QJsonObject());
}

CameraCall lightPresetGetCall(const QString &presetId)
{
    return call("GET", QStringLiteral("light-presets/%1").arg(presetId));
}

CameraCall lightPresetCreateCall(const QString &name)
{
    return call("POST", QStringLiteral("light-presets"), QJsonObject{{QStringLiteral("name"), name}});
}

CameraCall lightPresetOverwriteCall(const QString &presetId)
{
    return call("POST", QStringLiteral("light-presets/%1/save-current").arg(presetId), QJsonObject());
}

CameraCall lightPresetRenameCall(const QString &presetId, const QString &name)
{
    return call("PUT", QStringLiteral("light-presets/%1").arg(presetId), QJsonObject{{QStringLiteral("name"), name}});
}

CameraCall lightPresetDeleteCall(const QString &presetId)
{
    return call("DELETE", QStringLiteral("light-presets/%1").arg(presetId));
}

CameraCall lightCreateCall(const QString &name, const QString &host)
{
    return call("POST", QStringLiteral("lights"),
                QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("host"), host}});
}

CameraCall lightConfigCall(const QString &lightId, const QJsonObject &values)
{
    return call("PUT", QStringLiteral("lights/%1/config").arg(lightId), values);
}

CameraCall lightDeleteCall(const QString &lightId)
{
    return call("DELETE", QStringLiteral("lights/%1").arg(lightId));
}

CameraCall cameraStatusGetCall()
{
    return call("GET", QStringLiteral("status"));
}

CameraCall cameraExportGetCall()
{
    return call("GET", QStringLiteral("export"));
}

QString cameraSliderText(const QString &key, int value)
{
    return key == QLatin1String("white_balance_temperature") ? lightKelvinText(value) : QString::number(value);
}

QJsonValue cameraCheckValue(const QJsonArray &controls, const QString &key, bool checked)
{
    // controls.py im Dienst: Typ «bool» nur mit true/false, «int» und «menu» nur mit ganzen Zahlen
    const QJsonObject o = controlByKey(controls, key);
    if (str(o, "type") == QLatin1String("bool") || o.value(QLatin1String("value")).isBool())
        return checked;
    return checked ? 1 : 0;
}

LightPanelView lightPanelView(const CameraServiceView &service, const QJsonArray &lights, bool presetSelected)
{
    LightPanelView v;
    if (!service.usable)
        return v;
    v.allEnabled = !lights.isEmpty();
    v.lampsEnabled = true;
    v.presetsEnabled = true;
    v.recallEnabled = presetSelected;
    v.saveEnabled = !lights.isEmpty();
    v.overwriteEnabled = v.saveEnabled && presetSelected;
    v.editEnabled = presetSelected;
    return v;
}

QList<LightEntry> lightEntriesFromExport(const QJsonObject &exportBody)
{
    // {"files": {"licht.json": {"lights": [{"id", "name", "host"}]}}} (export.py im Dienst)
    const QJsonValue file = exportBody.value(QLatin1String("files")).toObject().value(QLatin1String("licht.json"));
    QList<LightEntry> entries;
    for (const QJsonValue &v : file.toObject().value(QLatin1String("lights")).toArray()) {
        const QJsonObject o = v.toObject();
        entries.append({str(o, "id"), str(o, "name"), str(o, "host")});
    }
    return entries;
}

QList<CameraCall> lightsDialogCalls(const QList<LightEntry> &before, const QList<LightEntry> &after)
{
    QList<CameraCall> removed;
    QList<CameraCall> changed;
    QList<CameraCall> created;
    for (const LightEntry &old : before) {
        const auto it = std::find_if(after.cbegin(), after.cend(),
                                     [&old](const LightEntry &e) { return e.id == old.id; });
        if (it == after.cend()) {
            removed.append(lightDeleteCall(old.id));
            continue;
        }
        QJsonObject values;
        if (it->name.trimmed() != old.name)
            values.insert(QStringLiteral("name"), it->name.trimmed());
        if (it->host.trimmed() != old.host)
            values.insert(QStringLiteral("host"), it->host.trimmed());
        if (!values.isEmpty())
            changed.append(lightConfigCall(old.id, values));
    }
    for (const LightEntry &e : after) {
        if (e.id.isEmpty())
            created.append(lightCreateCall(e.name.trimmed(), e.host.trimmed()));
    }
    return removed + changed + created;
}

QString lightsDialogProblem(const QList<LightEntry> &entries)
{
    for (const LightEntry &e : entries) {
        if (e.name.trimmed().isEmpty())
            return QStringLiteral("Jede Lampe braucht einen Namen.");
        if (e.host.trimmed().isEmpty())
            return QStringLiteral("Lampe «%1»: Adresse fehlt.").arg(e.name.trimmed());
    }
    return {};
}

std::pair<QString, QString> lightCheckResult(const CameraResult &status, const QString &lightId)
{
    if (const QString error = cameraResultError(status); !error.isEmpty())
        return {error, QStringLiteral("error")};
    for (const QJsonValue &v : status.body.value(QLatin1String("lights")).toArray()) {
        const QJsonObject o = v.toObject();
        if (str(o, "id") != lightId)
            continue;
        if (o.value(QLatin1String("reachable")).toBool())
            return {QStringLiteral("✔ Lampe antwortet"), QStringLiteral("ok")};
        return {QStringLiteral("Lampe antwortet nicht"), QStringLiteral("error")};
    }
    return {QStringLiteral("Lampe nicht gefunden"), QStringLiteral("error")};
}

void CameraHold::press(const QString &cameraId, int pan, int tilt, int zoom, qint64 nowMs)
{
    m_pressed = true;
    m_moving = false;
    m_cameraId = cameraId;
    m_pan = pan;
    m_tilt = tilt;
    m_zoom = zoom;
    m_pressedAt = nowMs;
    m_lastSent = nowMs;
    ++m_serial;
}

bool CameraHold::reportFailure(int serial)
{
    // Auch eine späte Antwort auf ein früheres Halten bleibt still, wenn ein neueres schon gemeldet ist
    if (serial <= m_reportedSerial)
        return false;
    m_reportedSerial = serial;
    return true;
}

std::optional<CameraCall> CameraHold::tick(qint64 nowMs)
{
    if (!m_pressed)
        return std::nullopt;
    if (!m_moving) {
        if (nowMs - m_pressedAt < kCameraHoldDelayMs)
            return std::nullopt;
        m_moving = true;
    } else if (nowMs - m_lastSent < kCameraHoldRepeatMs) {
        return std::nullopt;
    }
    m_lastSent = nowMs;
    return cameraMoveCall(m_cameraId, m_pan, m_tilt, m_zoom);
}

std::optional<CameraCall> CameraHold::release(qint64 nowMs)
{
    if (!m_pressed)
        return std::nullopt;
    // Lange gedrückt, auch wenn der erste move wegen eines späten Takts nicht mehr ging: Der Nutzer
    // wollte fahren, kein Schritt; stop schadet nicht.
    const bool held = m_moving || nowMs - m_pressedAt >= kCameraHoldDelayMs;
    m_pressed = false;
    m_moving = false;
    if (!held)
        return cameraStepCall(m_cameraId, m_pan, m_tilt, m_zoom);
    return cameraStopCall(m_cameraId);
}

void CameraHold::cancel()
{
    m_pressed = false;
    m_moving = false;
}

int CameraHold::nextDueMs(qint64 nowMs) const
{
    if (!m_pressed)
        return -1;
    const qint64 due = m_moving ? m_lastSent + kCameraHoldRepeatMs : m_pressedAt + kCameraHoldDelayMs;
    return int(std::max<qint64>(0, due - nowMs));
}

std::optional<CameraCall> CameraBundler::put(const QString &path, const QJsonObject &values, qint64 nowMs)
{
    QJsonObject &pending = m_pending[path];
    for (auto it = values.begin(); it != values.end(); ++it)
        pending.insert(it.key(), it.value());
    const auto last = m_lastSent.constFind(path);
    if (last != m_lastSent.cend() && nowMs - *last < kCameraBundleMs)
        return std::nullopt;
    m_lastSent.insert(path, nowMs);
    return call("PUT", path, m_pending.take(path));
}

QList<CameraCall> CameraBundler::tick(qint64 nowMs)
{
    QList<CameraCall> calls;
    for (const QString &path : m_pending.keys()) {
        if (nowMs - m_lastSent.value(path) < kCameraBundleMs)
            continue;
        m_lastSent.insert(path, nowMs);
        calls.append(call("PUT", path, m_pending.take(path)));
    }
    return calls;
}

QList<CameraCall> CameraBundler::flush(qint64 nowMs)
{
    QList<CameraCall> calls;
    for (const QString &path : m_pending.keys()) {
        m_lastSent.insert(path, nowMs);
        calls.append(call("PUT", path, m_pending.take(path)));
    }
    return calls;
}

int CameraBundler::nextDueMs(qint64 nowMs) const
{
    int next = -1;
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it) {
        const int due = int(std::max<qint64>(0, m_lastSent.value(it.key()) + kCameraBundleMs - nowMs));
        next = next < 0 ? due : std::min(next, due);
    }
    return next;
}

int CameraData::setService(const std::optional<CameraServiceInfo> &info)
{
    const QJsonObject status = info ? info->status : QJsonObject();
    int changed = setCameras(status.value(QLatin1String("cameras")).toArray());
    const QJsonArray lights = status.value(QLatin1String("lights")).toArray();
    if (lights != m_lights) {
        m_lights = lights;
        changed |= Lights;
    }
    changed |= setPresets(status.value(QLatin1String("camera_presets")).toArray(),
                          status.value(QLatin1String("light_presets")).toArray(), false);
    return changed;
}

int CameraData::applyEvent(const CameraEventInfo &event)
{
    const QJsonObject &d = event.data;
    if (event.name == QLatin1String("status"))
        return setCameras(d.value(QLatin1String("cameras")).toArray());
    if (event.name == QLatin1String("lights")) {
        const QJsonArray lights = d.value(QLatin1String("lights")).toArray();
        if (lights == m_lights)
            return 0;
        m_lights = lights;
        return Lights;
    }
    if (event.name == QLatin1String("presets"))
        return setPresets(d.value(QLatin1String("camera_presets")).toArray(),
                          d.value(QLatin1String("light_presets")).toArray(), true);
    const QString camera = str(d, "camera");
    if (camera.isEmpty())
        return 0;
    if (event.name == QLatin1String("controls")) {
        m_controls.insert(camera, d.value(QLatin1String("controls")).toArray());
        return Controls;
    }
    if (event.name == QLatin1String("config")) {
        QJsonObject config = d;
        config.remove(QStringLiteral("camera"));
        m_config.insert(camera, config);
        return Config;
    }
    return 0;
}

int CameraData::applyResult(const CameraCall &call, const CameraResult &result)
{
    if (call.method != QLatin1String("GET") || result.status != 200)
        return 0;
    const QStringList parts = call.path.split(QLatin1Char('/'));
    if (parts.size() == 2 && parts.at(0) == QLatin1String("camera-presets"))
        return setCameraPreset(result.body.value(QLatin1String("preset")).toObject());
    if (parts.size() == 2 && parts.at(0) == QLatin1String("light-presets"))
        return setLightPreset(result.body.value(QLatin1String("preset")).toObject());
    if (parts.size() != 3 || parts.at(0) != QLatin1String("cameras"))
        return 0;
    const QString &camera = parts.at(1);
    if (parts.at(2) == QLatin1String("controls")) {
        m_controls.insert(camera, result.body.value(QLatin1String("controls")).toArray());
        return Controls;
    }
    if (parts.at(2) == QLatin1String("config")) {
        QJsonObject config = result.body;
        config.remove(QStringLiteral("ok"));
        m_config.insert(camera, config);
        return Config;
    }
    return 0;
}

int CameraData::setCameraPreset(const QJsonObject &preset)
{
    // Nur bekannte Kennungen: Ein inzwischen gelöschtes Preset kommt nicht zurück.
    const QJsonValue id = preset.value(QLatin1String("id"));
    for (qsizetype i = 0; i < m_cameraPresets.size(); ++i) {
        if (m_cameraPresets.at(i).toObject().value(QLatin1String("id")) != id)
            continue;
        if (m_cameraPresets.at(i).toObject() == preset)
            return 0;
        m_cameraPresets.replace(i, preset);
        return Presets;
    }
    return 0;
}

int CameraData::setLightPreset(const QJsonObject &preset)
{
    // Wie setCameraPreset: nur bekannte Kennungen
    const QJsonValue id = preset.value(QLatin1String("id"));
    for (qsizetype i = 0; i < m_lightPresets.size(); ++i) {
        if (m_lightPresets.at(i).toObject().value(QLatin1String("id")) != id)
            continue;
        if (m_lightPresets.at(i).toObject() == preset)
            return 0;
        m_lightPresets.replace(i, preset);
        return Presets;
    }
    return 0;
}

QStringList CameraData::lightPresetsWithoutDetails() const
{
    QStringList ids;
    for (const QJsonValue &v : m_lightPresets) {
        const QJsonObject o = v.toObject();
        if (!o.value(QLatin1String("lights")).isObject())
            ids.append(str(o, "id"));
    }
    return ids;
}

QStringList CameraData::cameraPresetsWithoutDetails() const
{
    QStringList ids;
    for (const QJsonValue &v : m_cameraPresets) {
        const QJsonObject o = v.toObject();
        if (!o.value(QLatin1String("camera")).isString())
            ids.append(str(o, "id"));
    }
    return ids;
}

int CameraData::noteCallSent(const CameraCall &call)
{
    if (call.method != QLatin1String("POST"))
        return 0;
    const QStringList parts = call.path.split(QLatin1Char('/'));
    if (parts.size() != 3)
        return 0;
    QString cameraId;
    if (parts.at(0) == QLatin1String("cameras")
        && (parts.at(2) == QLatin1String("move") || parts.at(2) == QLatin1String("step")
            || parts.at(2) == QLatin1String("home"))) {
        cameraId = parts.at(1);
    } else if (parts.at(0) == QLatin1String("camera-presets") && parts.at(2) == QLatin1String("recall")) {
        for (const QJsonValue &v : m_cameraPresets) {
            const QJsonObject o = v.toObject();
            if (str(o, "id") == parts.at(1))
                cameraId = str(o, "camera");
        }
    }
    if (cameraId.isEmpty())
        return 0;
    // ai_off_before_move (Standard an, docs/09-kamera-dienst.md): Der Dienst schaltet die KI vor
    // der Bewegung aus, ohne Ereignis controls. Ohne gelesene Einstellungen gilt der Standard.
    const QJsonObject config = m_config.value(cameraId).value(QLatin1String("config")).toObject();
    if (!config.value(QLatin1String("ai_off_before_move")).toBool(true))
        return 0;
    QJsonArray controls = m_controls.value(cameraId);
    for (qsizetype i = 0; i < controls.size(); ++i) {
        QJsonObject o = controls.at(i).toObject();
        if (str(o, "key") != QLatin1String("obsbot_ai"))
            continue;
        if (o.value(QLatin1String("value")).toInt() == 0)
            return 0;
        o.insert(QStringLiteral("value"), 0);
        controls.replace(i, o);
        m_controls.insert(cameraId, controls);
        return Controls;
    }
    return 0;
}

const CameraEntry *CameraData::camera(const QString &id) const
{
    for (const CameraEntry &c : m_cameras) {
        if (c.id == id)
            return &c;
    }
    return nullptr;
}

int CameraData::setCameras(const QJsonArray &cameras)
{
    const QList<CameraEntry> entries = cameraEntries(cameras);
    if (entries == m_cameras)
        return 0;
    m_cameras = entries;
    return Cameras;
}

int CameraData::setPresets(const QJsonArray &camera, const QJsonArray &light, bool full)
{
    // Aus GET status kommen nur id und name: bekannte ganze Presets bleiben, mit neuem Namen.
    const auto merge = [full](const QJsonArray &known, const QJsonArray &incoming) {
        if (full)
            return incoming;
        QJsonArray merged;
        for (const QJsonValue &v : incoming) {
            QJsonObject entry = v.toObject();
            for (const QJsonValue &k : known) {
                const QJsonObject old = k.toObject();
                if (old.value(QLatin1String("id")) == entry.value(QLatin1String("id"))) {
                    QJsonObject updated = old;
                    updated.insert(QStringLiteral("name"), entry.value(QLatin1String("name")));
                    entry = updated;
                    break;
                }
            }
            merged.append(entry);
        }
        return merged;
    };
    const QJsonArray cameraPresets = merge(m_cameraPresets, camera);
    const QJsonArray lightPresets = merge(m_lightPresets, light);
    if (cameraPresets == m_cameraPresets && lightPresets == m_lightPresets)
        return 0;
    m_cameraPresets = cameraPresets;
    m_lightPresets = lightPresets;
    return Presets;
}

} // namespace dss
