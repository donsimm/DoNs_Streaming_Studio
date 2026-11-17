// Prüft die Ansichtslogik des Reiters «Kameras» ohne Fenster (P15 e1, Plan P15 Abschnitt 8, Regel 9).
// Bildwerte im Format von GET controls des Dienstes; Bereiche der C922 wie in der Attrappe
// (kamera-dienst/dss_camera/fake.py, Werte aus dem Nachtrag am Gerät), Gruppe «OBSBOT» wie
// obsbot.describe. Kein Abzug der echten Kameras (nicht unter tests/daten, siehe Bericht).

#include "cameraview.h"

#include <QJsonDocument>
#include <QTest>

using namespace dss;

namespace {

QJsonObject control(const char *key, const char *type, int min, int max, int value, bool inactive = false,
                    bool remembered = false, const QJsonArray &menu = {}, bool readonly = false)
{
    QJsonObject o{{QStringLiteral("key"), QLatin1String(key)},
                  {QStringLiteral("label"), QLatin1String(key)},
                  {QStringLiteral("group"), QStringLiteral("Bild")},
                  {QStringLiteral("type"), QLatin1String(type)},
                  {QStringLiteral("min"), min},
                  {QStringLiteral("max"), max},
                  {QStringLiteral("step"), 1},
                  {QStringLiteral("default"), min},
                  {QStringLiteral("inactive"), inactive},
                  {QStringLiteral("remembered"), remembered},
                  {QStringLiteral("readonly"), readonly}};
    if (QLatin1String(type) == QLatin1String("bool"))
        o.insert(QStringLiteral("value"), value != 0);
    else
        o.insert(QStringLiteral("value"), value);
    o.insert(QStringLiteral("menu"), menu.isEmpty() ? QJsonValue() : QJsonValue(menu));
    return o;
}

QJsonArray menu(std::initializer_list<std::pair<int, const char *>> entries)
{
    QJsonArray a;
    for (const auto &[value, label] : entries)
        a.append(QJsonObject{{QStringLiteral("value"), value}, {QStringLiteral("label"), QString::fromUtf8(label)}});
    return a;
}

// C922: automatischer Weissabgleich aus und Temperatur gemerkt, Belichtung «Manuell»
QJsonArray c922Controls()
{
    return {control("brightness", "int", 0, 255, 128),
            control("white_balance_automatic", "bool", 0, 1, 0, false, true),
            control("power_line_frequency", "menu", 0, 2, 1, false, true, menu({{0, "Aus"}, {1, "50 Hz"}, {2, "60 Hz"}})),
            control("white_balance_temperature", "int", 2000, 6500, 4600, false, true),
            control("auto_exposure", "menu", 0, 3, 3, false, false, menu({{1, "Manuell"}, {3, "Blende fest"}})),
            control("exposure_time_absolute", "int", 3, 2047, 250, true),
            control("pan_absolute", "int", -36000, 36000, 0),
            control("focus_absolute", "int", 0, 250, 0, true),
            control("focus_automatic_continuous", "bool", 0, 1, 1),
            control("zoom_absolute", "int", 100, 500, 240)};
}

QJsonArray tiny2Controls()
{
    return {control("brightness", "int", 0, 100, 50),
            control("zoom_absolute", "int", 0, 100, 35),
            control("obsbot_hdr", "bool", 0, 1, 0),
            control("obsbot_fov", "menu", 0, 2, 0, false, true, menu({{0, "Weit"}, {1, "Mittel"}, {2, "Eng"}})),
            control("obsbot_ai", "menu", 0, 1, 1, false, false, menu({{0, "Aus"}, {1, "Oberkörper"}})),
            control("obsbot_state", "menu", 0, 255, 1, false, false, {}, true),
            control("obsbot_auto_sleep", "int", 0, 65535, 0, false, false, {}, true)};
}

CameraEntry entry(const char *model, const char *state, bool present = true)
{
    CameraEntry e;
    e.id = QStringLiteral("3564:fef8:A1");
    e.name = QLatin1String(model);
    e.model = QLatin1String(model);
    e.state = QLatin1String(state);
    e.present = present;
    e.ptz = true;
    return e;
}

CameraServiceView connected()
{
    CameraServiceInfo info;
    info.state = QString::fromLatin1(kCameraStateConnected);
    return cameraServiceView(info);
}

CameraControlRow row(const QList<CameraControlRow> &rows, const QString &key)
{
    for (const CameraControlRow &r : rows) {
        if (r.spec.key == key)
            return r;
    }
    qFatal("Zeile fehlt");
}

} // namespace

class TestKameraAnsicht : public QObject
{
    Q_OBJECT

private slots:
    void kelvinMired_data()
    {
        QTest::addColumn<int>("mired");
        QTest::addColumn<int>("kelvin");
        // Grenzen der Lampe: 143 Mired = 6993 K → 7000 K, 344 Mired = 2907 K → 2900 K
        QTest::newRow("kalt") << 143 << 7000;
        QTest::newRow("warm") << 344 << 2900;
        QTest::newRow("Mitte") << 200 << 5000;
        QTest::newRow("4500") << 222 << 4500;
        QTest::newRow("unter der Grenze") << 100 << 7000;
        QTest::newRow("über der Grenze") << 400 << 2900;
    }
    void kelvinMired()
    {
        QFETCH(int, mired);
        QFETCH(int, kelvin);
        QCOMPARE(lightKelvinFromMired(mired), kelvin);
    }

    void miredFromKelvin()
    {
        QCOMPARE(lightMiredFromKelvin(7000), 143);
        QCOMPARE(lightMiredFromKelvin(2900), 344);
        QCOMPARE(lightMiredFromKelvin(5000), 200);
        QCOMPARE(lightMiredFromKelvin(10000), kLightMiredMin);
        QCOMPARE(lightMiredFromKelvin(1000), kLightMiredMax);
        QCOMPARE(lightMiredFromKelvin(0), kLightMiredMax); // ohne Division durch 0, wärmster Wert
        // Hin und zurück bleibt jeder Schritt von 50 K im Bereich gleich
        for (int k = kLightKelvinMin; k <= kLightKelvinMax; k += kLightKelvinStep)
            QVERIFY2(qAbs(lightKelvinFromMired(lightMiredFromKelvin(k)) - k) <= 100, qPrintable(QString::number(k)));
        QCOMPARE(lightBrightnessClamped(1), 3);
        QCOMPARE(lightBrightnessClamped(140), 100);
        QCOMPARE(lightKelvinText(4500), QStringLiteral("4500 K"));
        QCOMPARE(lightBrightnessText(40), QStringLiteral("40 %"));
    }

    void serviceStates()
    {
        CameraServiceView v = cameraServiceView(std::nullopt);
        QCOMPARE(v.text, QStringLiteral("Kamera-Dienst: –"));
        QVERIFY(!v.usable);

        CameraServiceInfo info;
        info.state = QString::fromLatin1(kCameraStateOff);
        v = cameraServiceView(info);
        QCOMPARE(v.state, QStringLiteral("off"));
        QVERIFY(!v.usable);
        QVERIFY(v.blockedHint.isEmpty());

        info.state = QString::fromLatin1(kCameraStateConnecting);
        QCOMPARE(cameraServiceView(info).state, QStringLiteral("warning"));

        info.state = QString::fromLatin1(kCameraStateError);
        info.message = QStringLiteral("keine Antwort vom Streaming-PC");
        v = cameraServiceView(info);
        QCOMPARE(v.text, QStringLiteral("Kamera-Dienst nicht erreichbar:"));
        QCOMPARE(v.detail, info.message);
        QCOMPARE(v.state, QStringLiteral("error"));
        QVERIFY(!v.blockedHint.isEmpty());

        info.state = QString::fromLatin1(kCameraStateUnavailable);
        QVERIFY(!cameraServiceView(info).usable);

        info.state = QString::fromLatin1(kCameraStateConnected);
        info.message.clear();
        v = cameraServiceView(info);
        QCOMPARE(v.text, QStringLiteral("Kamera-Dienst: verbunden"));
        QCOMPARE(v.state, QStringLiteral("ok"));
        QVERIFY(v.usable);
        QVERIFY(v.detail.isEmpty());
    }

    void cameraList()
    {
        const QJsonArray cameras = QJsonDocument::fromJson(R"([
            {"id": "3564:fef8:A1", "name": "Kamera oben", "model": "OBSBOT Tiny 2", "present": true, "state": "asleep", "ptz": true},
            {"id": "046d:085c:B2", "name": "", "model": "Logitech C922", "present": false, "state": "absent", "ptz": true},
            {"name": "ohne Kennung"}])").array();
        const QList<CameraEntry> list = cameraEntries(cameras);
        QCOMPARE(list.size(), 2);
        QVERIFY(cameraIsTiny2(list.at(0)));
        QVERIFY(!cameraIsTiny2(list.at(1)));
        QCOMPARE(list.at(1).name, QStringLiteral("Logitech C922"));
        QCOMPARE(cameraChoiceText(list.at(0)), QStringLiteral("Kamera oben · schläft"));
        QCOMPARE(cameraChoiceText(list.at(1)), QStringLiteral("Logitech C922 · fehlt"));
        QCOMPARE(cameraStateText(QStringLiteral("awake")), QStringLiteral("wach"));
        QCOMPARE(cameraStateText(QStringLiteral("seltsam")), QStringLiteral("unbekannt"));
        QCOMPARE(cameraChoiceAfterUpdate(list, QStringLiteral("046d:085c:B2")), QStringLiteral("046d:085c:B2"));
        QCOMPARE(cameraChoiceAfterUpdate(list, QStringLiteral("weg")), QStringLiteral("3564:fef8:A1"));
        QCOMPARE(cameraChoiceAfterUpdate({}, QStringLiteral("weg")), QString());
    }

    // Entwurf Seite 4: A schläft, B fehlt, C C922, E Dienst weg; dazu Tiny 2 wach (1B)
    void controlStates()
    {
        const CameraServiceView service = connected();

        CameraEntry tiny = entry("OBSBOT Tiny 2", "awake");
        CameraControlView v = cameraControlView(service, &tiny, true, tiny2Controls());
        QVERIFY(v.chooserEnabled && v.setupEnabled && v.moveEnabled && v.imageEnabled);
        QCOMPARE(v.wakeText, QStringLiteral("Schlafen"));
        QVERIFY(v.wakeEnabled);
        QVERIFY(v.moveHint.isEmpty());
        QVERIFY(v.recallEnabled && v.saveEnabled && v.editEnabled);
        // KI-Verfolgung (P15 e1b): bedienbar bei wacher Tiny 2, Wert und Auswahl vom Dienst
        QVERIFY(v.aiEnabled);
        QVERIFY(v.aiHint.isEmpty());
        QCOMPARE(v.aiValue, 1);
        QCOMPARE(v.aiMenu, (QList<std::pair<int, QString>>{{0, QStringLiteral("Aus")}, {1, QStringLiteral("Oberkörper")}}));
        // Ohne Werte vom Dienst (noch nicht gelesen) nicht bedienbar, ohne Hinweis
        v = cameraControlView(service, &tiny, true);
        QVERIFY(!v.aiEnabled && v.aiHint.isEmpty() && v.aiMenu.isEmpty());

        tiny.state = QStringLiteral("asleep");
        v = cameraControlView(service, &tiny, true, tiny2Controls());
        QVERIFY(!v.aiEnabled);
        QCOMPARE(v.aiHint, QStringLiteral("Nur bei wacher Kamera"));
        QCOMPARE(v.aiValue, 1);
        QCOMPARE(v.wakeText, QStringLiteral("Wecken"));
        QVERIFY(v.wakeEnabled);
        QVERIFY(!v.moveEnabled);
        QCOMPARE(v.moveHint, QStringLiteral("Kamera schläft"));
        QCOMPARE(v.moveHintState, QStringLiteral("warning"));
        QVERIFY(v.moveHintWake);
        QVERIFY(v.recallEnabled);
        QCOMPARE(v.recallHint, QStringLiteral("weckt die Kamera zuerst"));
        QVERIFY(!v.saveEnabled);
        QVERIFY(v.imageEnabled);

        tiny.state = QStringLiteral("absent");
        tiny.present = false;
        v = cameraControlView(service, &tiny, true);
        QVERIFY(!v.wakeEnabled && !v.moveEnabled && !v.presetsEnabled && !v.recallEnabled && !v.editEnabled);
        QVERIFY(!v.imageEnabled);
        QVERIFY(v.moveHintState.isEmpty());
        QVERIFY(v.moveHint.startsWith(QStringLiteral("Kamera nicht gefunden.")));
        QVERIFY(v.setupEnabled); // Einstellungen gehen auch bei ausgesteckter Kamera

        CameraEntry c922 = entry("Logitech C922", "awake");
        v = cameraControlView(service, &c922, false, c922Controls());
        QVERIFY(v.moveEnabled);
        QVERIFY(!v.aiEnabled && v.aiMenu.isEmpty());
        QVERIFY(!v.wakeEnabled);
        QCOMPARE(v.wakeText, QStringLiteral("Schlafen"));
        QCOMPARE(v.wakeToolTip, QStringLiteral("Diese Kamera hat das nicht."));
        QCOMPARE(v.aiHint, QStringLiteral("Diese Kamera hat das nicht."));
        QVERIFY(!v.recallEnabled && !v.editEnabled); // keine Position gewählt
        QVERIFY(v.saveEnabled);

        CameraServiceInfo error;
        error.state = QString::fromLatin1(kCameraStateError);
        v = cameraControlView(cameraServiceView(error), &tiny, true);
        QCOMPARE(v, CameraControlView());

        v = cameraControlView(service, nullptr);
        QVERIFY(!v.chooserEnabled && !v.moveEnabled && !v.setupEnabled);
        QVERIFY(!v.moveHint.isEmpty());

        QCOMPARE(cameraImageTitle(&c922), QStringLiteral("Bildwerte · Logitech C922"));
        QCOMPARE(cameraImageTitle(nullptr), QStringLiteral("Bildwerte"));
    }

    // Regel 9: Zeilen und Bereiche fest, gleiche Anzahl und Reihenfolge in jedem Zustand
    void fixedRows()
    {
        const QStringList sections = cameraControlSections();
        QCOMPARE(sections.size(), 6);
        QStringList keys;
        for (const CameraControlSpec &spec : cameraControlSpecs()) {
            QVERIFY2(sections.contains(spec.section), qPrintable(spec.key));
            QVERIFY2(!keys.contains(spec.key), qPrintable(spec.key));
            QVERIFY(!spec.label.isEmpty());
            keys << spec.key;
        }
        // Pan, Tilt und Zoom nur über Bewegen
        QVERIFY(!keys.contains(QStringLiteral("pan_absolute")));
        QVERIFY(!keys.contains(QStringLiteral("zoom_absolute")));

        const CameraEntry c922 = entry("Logitech C922", "awake");
        const QList<CameraControlRow> withValues = cameraControlRows(c922Controls(), &c922, connected());
        const QList<CameraControlRow> empty = cameraControlRows({}, nullptr, cameraServiceView(std::nullopt));
        QCOMPARE(withValues.size(), cameraControlSpecs().size());
        QCOMPARE(empty.size(), withValues.size());
        for (int i = 0; i < empty.size(); ++i) {
            QCOMPARE(withValues.at(i).spec.key, empty.at(i).spec.key);
            QVERIFY(!empty.at(i).available && !empty.at(i).enabled);
            QCOMPARE(empty.at(i).valueText, QStringLiteral("–"));
        }
    }

    void controlRowsC922()
    {
        const CameraEntry c922 = entry("Logitech C922", "awake");
        const QList<CameraControlRow> rows = cameraControlRows(c922Controls(), &c922, connected());

        const CameraControlRow brightness = row(rows, QStringLiteral("brightness"));
        QVERIFY(brightness.available && brightness.enabled && !brightness.remembered);
        QCOMPARE(brightness.maximum, 255);
        QCOMPARE(brightness.valueText, QStringLiteral("128"));

        const CameraControlRow temperature = row(rows, QStringLiteral("white_balance_temperature"));
        QVERIFY(temperature.remembered);
        QCOMPARE(temperature.valueText, QStringLiteral("4600 K"));

        const CameraControlRow power = row(rows, QStringLiteral("power_line_frequency"));
        QCOMPARE(power.spec.kind, CameraControlKind::Choice);
        QCOMPARE(power.valueText, QStringLiteral("50 Hz"));
        QCOMPARE(power.menu.size(), 3);

        // Inaktiv bei Automatik: ausgegraut mit Tooltip
        const CameraControlRow exposure = row(rows, QStringLiteral("exposure_time_absolute"));
        QVERIFY(exposure.available && !exposure.enabled);
        QVERIFY(!exposure.toolTip.isEmpty());
        QCOMPARE(row(rows, QStringLiteral("focus_automatic_continuous")).valueText, QStringLiteral("ein"));

        // Was die C922 nicht hat: ausgegraut, Bereich «OBSBOT» mit Hinweis
        QVERIFY(!row(rows, QStringLiteral("contrast")).available);
        QCOMPARE(cameraSectionHint(rows, QStringLiteral("OBSBOT")), QStringLiteral("Diese Kamera hat das nicht."));
        QVERIFY(cameraSectionHint(rows, QStringLiteral("Bild")).isEmpty());
        QVERIFY(cameraSectionHint(rows, QStringLiteral("gibt es nicht")).isEmpty());

        QVERIFY(cameraSectionResettable(rows, QStringLiteral("Weissabgleich")));
        QVERIFY(!cameraSectionResettable(rows, QStringLiteral("Bild")));
        const CameraCall reset = cameraSectionResetCall(c922.id, rows, QStringLiteral("Weissabgleich"));
        QCOMPARE(reset.method, QStringLiteral("PUT"));
        QCOMPARE(reset.path, QStringLiteral("cameras/3564:fef8:A1/controls"));
        QCOMPARE(*reset.body, (QJsonObject{{QStringLiteral("white_balance_automatic"), QJsonValue::Null},
                                           {QStringLiteral("white_balance_temperature"), QJsonValue::Null}}));

        QCOMPARE(cameraZoomText(c922Controls()), QStringLiteral("35 %"));
        QCOMPARE(cameraZoomText({}), QStringLiteral("–"));

        // Kamera fehlt: alle Zeilen ohne Wert
        CameraEntry absent = c922;
        absent.present = false;
        for (const CameraControlRow &r : cameraControlRows(c922Controls(), &absent, connected()))
            QVERIFY(!r.available && !r.enabled);
    }

    void controlRowsTiny2()
    {
        CameraEntry tiny = entry("OBSBOT Tiny 2", "awake");
        QList<CameraControlRow> rows = cameraControlRows(tiny2Controls(), &tiny, connected());
        QVERIFY(row(rows, QStringLiteral("obsbot_hdr")).enabled);
        QCOMPARE(row(rows, QStringLiteral("obsbot_fov")).valueText, QStringLiteral("Weit"));
        const CameraControlRow sleep = row(rows, QStringLiteral("obsbot_auto_sleep"));
        QVERIFY(sleep.available && !sleep.enabled);
        QCOMPARE(sleep.valueText, QStringLiteral("nie"));
        QVERIFY(cameraSectionHint(rows, QStringLiteral("OBSBOT")).isEmpty());
        QCOMPARE(cameraZoomText(tiny2Controls()), QStringLiteral("35 %"));

        // Im Schlaf: HDR und Bildwinkel nur wach (409 camera_asleep beim Dienst), andere Werte gehen
        tiny.state = QStringLiteral("asleep");
        rows = cameraControlRows(tiny2Controls(), &tiny, connected());
        QVERIFY(!row(rows, QStringLiteral("obsbot_hdr")).enabled);
        QCOMPARE(row(rows, QStringLiteral("obsbot_hdr")).toolTip, QStringLiteral("Nur bei wacher Kamera"));
        QVERIFY(row(rows, QStringLiteral("brightness")).enabled);
        QVERIFY(!cameraSectionResettable(rows, QStringLiteral("OBSBOT")));
    }

    void presetRows()
    {
        const QJsonArray presets = QJsonDocument::fromJson(R"([
            {"id": "totale", "name": "Totale", "camera": "046d:085c:B2", "pan": 0.0, "tilt": -2.0, "zoom": 100},
            {"id": "schreibtisch", "name": "Schreibtisch", "camera": "046d:085c:B2", "pan": 18.4, "tilt": -14.0,
             "zoom": 240, "controls": {"brightness": 140}},
            {"id": "oben", "name": "Oben", "camera": "3564:fef8:A1", "pan": 0, "tilt": 10, "zoom": 0},
            {"id": "neu", "name": "Neu"}])").array();
        const QList<CameraPresetRow> rows = cameraPresetRows(presets, QStringLiteral("046d:085c:B2"), c922Controls());
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows.at(0), (CameraPresetRow{QStringLiteral("totale"), QStringLiteral("Totale"),
                                              QStringLiteral("0° · −2° · Zoom 0 %"), QStringLiteral("–")}));
        QCOMPARE(rows.at(1).position, QStringLiteral("18° · −14° · Zoom 35 %"));
        QCOMPARE(rows.at(1).withControls, QStringLiteral("✔"));
        QCOMPARE(rows.at(2).position, QStringLiteral("–"));

        // Achse fehlt an der Kamera (null), Zoom ohne Bereich
        const QJsonArray noAxis = QJsonDocument::fromJson(
            R"([{"id": "a", "name": "A", "camera": "x", "pan": null, "tilt": null, "zoom": 3}])").array();
        QCOMPARE(cameraPresetRows(noAxis, QStringLiteral("x"), {}).at(0).position, QStringLiteral("– · – · Zoom –"));
    }

    void lights()
    {
        const QJsonArray lights = QJsonDocument::fromJson(R"([
            {"id": "links", "name": "Key Light links", "reachable": true, "on": true, "brightness": 40, "temperature": 222},
            {"id": "rechts", "name": "Key Light rechts", "reachable": false, "on": false, "brightness": 0, "temperature": 0}])")
                                     .array();
        QList<LightRow> rows = lightRows(lights, connected());
        QCOMPARE(rows.size(), 2);
        QVERIFY(rows.at(0).enabled && rows.at(0).on);
        QCOMPARE(rows.at(0).brightnessText, QStringLiteral("40 %"));
        QCOMPARE(rows.at(0).kelvin, 4500);
        QCOMPARE(rows.at(0).kelvinText, QStringLiteral("4500 K"));
        QCOMPARE(rows.at(0).stateText, QStringLiteral("erreichbar"));
        QVERIFY(!rows.at(1).enabled);
        QCOMPARE(rows.at(1).stateText, QStringLiteral("nicht erreichbar"));
        QCOMPARE(rows.at(1).stateKey, QStringLiteral("error"));
        QCOMPARE(rows.at(1).brightnessText, QStringLiteral("–"));

        rows = lightRows(lights, cameraServiceView(std::nullopt));
        QVERIFY(!rows.at(0).enabled);

        // Nur Kennung und Name bestimmen die Anordnung, nicht die Werte
        QJsonArray changed = lights;
        QJsonObject first = changed.at(0).toObject();
        first.insert(QStringLiteral("brightness"), 80);
        changed.replace(0, first);
        QCOMPARE(lightLayoutKey(changed), lightLayoutKey(lights));
        first.insert(QStringLiteral("name"), QStringLiteral("Links"));
        changed.replace(0, first);
        QVERIFY(lightLayoutKey(changed) != lightLayoutKey(lights));

        const QJsonArray presets = QJsonDocument::fromJson(R"([
            {"id": "stream", "name": "Stream", "lights": {"links": {"on": true, "brightness": 40, "temperature": 222},
                                                          "rechts": {"on": false}}},
            {"id": "abend", "name": "Abend", "lights": {"links": {"brightness": 20}, "rechts": null}}])").array();
        const QList<LightPresetRow> presetRows = lightPresetRows(presets, lights);
        QCOMPARE(presetRows.at(0).cells, (QStringList{QStringLiteral("ein · 40 % · 4500 K"), QStringLiteral("aus")}));
        QCOMPARE(presetRows.at(1).cells, (QStringList{QStringLiteral("20 %"), QStringLiteral("–")}));
    }

    // Alle Anfragen stehen in der festen Liste des Kerns (checkCameraCall)
    void callsAllowed()
    {
        const QString cam = QStringLiteral("046d:085c:B2");
        const QList<CameraCall> calls = {
            cameraMoveCall(cam, -1, 0, 0),         cameraStopCall(cam),
            cameraStepCall(cam, 0, 1, 0),          cameraHomeCall(cam),
            cameraWakeCall(cam, true),             cameraWakeCall(cam, false),
            cameraControlsGetCall(cam),            cameraConfigGetCall(cam),
            cameraControlsPutCall(cam, {}),        cameraAiCall(cam, 1),
            cameraPresetRecallCall(QStringLiteral("totale")),
            cameraPresetCreateCall(QStringLiteral("Totale"), cam, false),
            cameraPresetOverwriteCall(QStringLiteral("totale"), true),
            lightPutCall(QStringLiteral("links"), {}), lightsAllCall(true),
            lightPresetRecallCall(QStringLiteral("stream"))};
        for (const CameraCall &c : calls)
            QVERIFY2(checkCameraCall(c.method, c.path).isEmpty(), qPrintable(c.method + QLatin1Char(' ') + c.path));

        QCOMPARE(*cameraMoveCall(cam, -1, 1, 0).body,
                 (QJsonObject{{QStringLiteral("pan"), -1}, {QStringLiteral("tilt"), 1}, {QStringLiteral("zoom"), 0}}));
        QCOMPARE(cameraWakeCall(cam, false).path, QStringLiteral("cameras/046d:085c:B2/sleep"));
        QCOMPARE(cameraAiCall(cam, 0).method, QStringLiteral("PUT"));
        QCOMPARE(cameraAiCall(cam, 0).path, QStringLiteral("cameras/046d:085c:B2/controls"));
        QCOMPARE(*cameraAiCall(cam, 0).body, (QJsonObject{{QStringLiteral("obsbot_ai"), 0}}));
        QCOMPARE(*cameraPresetCreateCall(QStringLiteral("Nah"), cam, false).body,
                 (QJsonObject{{QStringLiteral("name"), QStringLiteral("Nah")},
                              {QStringLiteral("camera"), cam},
                              {QStringLiteral("with_controls"), false}}));
        QCOMPARE(*lightsAllCall(false).body, (QJsonObject{{QStringLiteral("on"), false}}));
        QCOMPARE(lightsAllCall(false).path, QStringLiteral("lights/all"));
    }

    void holdClickIsStep()
    {
        CameraHold hold;
        QCOMPARE(hold.nextDueMs(0), -1);
        hold.press(QStringLiteral("c"), -1, 0, 0, 1000);
        QVERIFY(hold.pressed());
        QCOMPARE(hold.nextDueMs(1000), kCameraHoldDelayMs);
        QVERIFY(!hold.tick(1100));
        const std::optional<CameraCall> step = hold.release(1200);
        QVERIFY(step);
        QCOMPARE(step->path, QStringLiteral("cameras/c/step"));
        QCOMPARE(step->body->value(QStringLiteral("pan")).toInt(), -1);
        QVERIFY(!hold.pressed());
        QVERIFY(!hold.release(1300));
        QVERIFY(!hold.tick(2000));
    }

    void holdRepeatsAndStops()
    {
        CameraHold hold;
        hold.press(QStringLiteral("c"), 0, 0, 1, 0);
        QVERIFY(!hold.tick(kCameraHoldDelayMs - 1));
        std::optional<CameraCall> first = hold.tick(kCameraHoldDelayMs);
        QVERIFY(first);
        QCOMPARE(first->path, QStringLiteral("cameras/c/move"));
        QCOMPARE(first->body->value(QStringLiteral("zoom")).toInt(), 1);
        QVERIFY(hold.moving());
        QCOMPARE(hold.nextDueMs(kCameraHoldDelayMs), kCameraHoldRepeatMs);
        // Wiederholung alle 200 ms, nie öfter; ein später Takt verschiebt die nächste
        QVERIFY(!hold.tick(kCameraHoldDelayMs + kCameraHoldRepeatMs - 1));
        QVERIFY(hold.tick(kCameraHoldDelayMs + kCameraHoldRepeatMs));
        QVERIFY(hold.tick(kCameraHoldDelayMs + 2 * kCameraHoldRepeatMs + 50));
        QVERIFY(!hold.tick(kCameraHoldDelayMs + 3 * kCameraHoldRepeatMs));
        // Zwischen zwei move höchstens 200 ms plus Verspätung des Takts: weit unter dem Totmann (500 ms)
        QVERIFY(kCameraHoldRepeatMs * 2 < 500);
        const std::optional<CameraCall> stop = hold.release(2000);
        QVERIFY(stop);
        QCOMPARE(stop->path, QStringLiteral("cameras/c/stop"));
        QVERIFY(!stop->body);
        QVERIFY(!hold.tick(3000));
    }

    void holdLongWithoutTickStops()
    {
        CameraHold hold;
        hold.press(QStringLiteral("c"), 1, 1, 0, 0);
        const std::optional<CameraCall> c = hold.release(kCameraHoldDelayMs + 10);
        QVERIFY(c);
        QCOMPARE(c->path, QStringLiteral("cameras/c/stop"));

        hold.press(QStringLiteral("c"), 1, 0, 0, 0);
        QVERIFY(hold.tick(kCameraHoldDelayMs));
        hold.cancel();
        QVERIFY(!hold.pressed());
        QVERIFY(!hold.release(5000));
    }

    void bundler()
    {
        CameraBundler b;
        const QString path = QStringLiteral("cameras/c/controls");
        QCOMPARE(b.nextDueMs(0), -1);
        std::optional<CameraCall> c = b.put(path, {{QStringLiteral("brightness"), 10}}, 0);
        QVERIFY(c);
        QCOMPARE(c->method, QStringLiteral("PUT"));
        QCOMPARE(*c->body, (QJsonObject{{QStringLiteral("brightness"), 10}}));
        QCOMPARE(b.nextDueMs(0), -1);

        // Innerhalb von 100 ms: nur der letzte Wert je Feld, zusammen mit anderen Feldern
        QVERIFY(!b.put(path, {{QStringLiteral("brightness"), 11}}, 20));
        QVERIFY(!b.put(path, {{QStringLiteral("brightness"), 12}}, 40));
        QVERIFY(!b.put(path, {{QStringLiteral("contrast"), 5}}, 60));
        QCOMPARE(b.nextDueMs(60), kCameraBundleMs - 60);
        QVERIFY(b.tick(99).isEmpty());
        QList<CameraCall> due = b.tick(100);
        QCOMPARE(due.size(), 1);
        QCOMPARE(*due.at(0).body, (QJsonObject{{QStringLiteral("brightness"), 12}, {QStringLiteral("contrast"), 5}}));
        QVERIFY(b.tick(300).isEmpty());

        // Pfade unabhängig
        QVERIFY(!b.put(path, {{QStringLiteral("brightness"), 13}}, 150));
        QVERIFY(b.put(QStringLiteral("lights/links"), {{QStringLiteral("brightness"), 50}}, 150));
        // Lange danach geht ein Wert sofort
        due = b.flush(160);
        QCOMPARE(due.size(), 1);
        QCOMPARE(due.at(0).path, path);
        QVERIFY(b.flush(170).isEmpty());
        QVERIFY(b.put(path, {{QStringLiteral("brightness"), 14}}, 1000));
    }

    void dataFromServiceAndEvents()
    {
        CameraData data;
        QCOMPARE(data.setService(std::nullopt), 0);

        CameraServiceInfo info;
        info.state = QString::fromLatin1(kCameraStateConnected);
        info.status = QJsonDocument::fromJson(R"({"ok": true,
            "cameras": [{"id": "c", "name": "Logitech C922", "model": "Logitech C922", "present": true, "state": "awake", "ptz": true}],
            "lights": [{"id": "links", "name": "Links", "reachable": true, "on": true, "brightness": 40, "temperature": 200}],
            "camera_presets": [{"id": "totale", "name": "Totale"}],
            "light_presets": [{"id": "stream", "name": "Stream"}]})").object();
        QCOMPARE(data.setService(info), CameraData::Cameras | CameraData::Lights | CameraData::Presets);
        QCOMPARE(data.setService(info), 0);
        QVERIFY(data.camera(QStringLiteral("c")));
        QVERIFY(!data.camera(QStringLiteral("x")));

        // Ganze Presets aus dem Ereignis; danach nennt status nur Kennung und Name
        CameraEventInfo presets{QStringLiteral("presets"), QJsonDocument::fromJson(R"({
            "camera_presets": [{"id": "totale", "name": "Totale", "camera": "c", "pan": 0, "tilt": 0, "zoom": 100}],
            "light_presets": [{"id": "stream", "name": "Stream", "lights": {"links": null}}]})").object()};
        QCOMPARE(data.applyEvent(presets), int(CameraData::Presets));
        QJsonObject status = info.status;
        status.insert(QStringLiteral("camera_presets"),
                      QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("totale")},
                                             {QStringLiteral("name"), QStringLiteral("Totale breit")}}});
        info.status = status;
        QCOMPARE(data.setService(info), int(CameraData::Presets));
        const QJsonObject merged = data.cameraPresets().at(0).toObject();
        QCOMPARE(merged.value(QStringLiteral("name")).toString(), QStringLiteral("Totale breit"));
        QCOMPARE(merged.value(QStringLiteral("camera")).toString(), QStringLiteral("c"));
        QVERIFY(data.lightPresets().at(0).toObject().contains(QStringLiteral("lights")));

        CameraEventInfo controls{QStringLiteral("controls"),
                                 QJsonObject{{QStringLiteral("camera"), QStringLiteral("c")},
                                             {QStringLiteral("controls"), c922Controls()}}};
        QCOMPARE(data.applyEvent(controls), int(CameraData::Controls));
        QCOMPARE(data.controls(QStringLiteral("c")), c922Controls());

        CameraEventInfo config{QStringLiteral("config"),
                               QJsonObject{{QStringLiteral("camera"), QStringLiteral("c")},
                                           {QStringLiteral("config"), QJsonObject{{QStringLiteral("speed"), 50}}}}};
        QCOMPARE(data.applyEvent(config), int(CameraData::Config));
        QVERIFY(data.config(QStringLiteral("c")).contains(QStringLiteral("config")));
        QVERIFY(!data.config(QStringLiteral("c")).contains(QStringLiteral("camera")));

        CameraEventInfo lights{QStringLiteral("lights"), QJsonObject{{QStringLiteral("lights"), QJsonArray()}}};
        QCOMPARE(data.applyEvent(lights), int(CameraData::Lights));
        QCOMPARE(data.applyEvent(lights), 0);
        QCOMPARE(data.applyEvent({QStringLiteral("status"), QJsonObject{{QStringLiteral("cameras"), QJsonArray()}}}),
                 int(CameraData::Cameras));
        QVERIFY(data.cameras().isEmpty());
        QCOMPARE(data.applyEvent({QStringLiteral("controls"), QJsonObject()}), 0);
        QCOMPARE(data.applyEvent({QStringLiteral("unbekannt"), QJsonObject()}), 0);

        // Antworten auf GET …/controls und GET …/config
        CameraResult result;
        result.status = 200;
        result.body = QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("controls"), tiny2Controls()}};
        QCOMPARE(data.applyResult(cameraControlsGetCall(QStringLiteral("t")), result), int(CameraData::Controls));
        QCOMPARE(data.controls(QStringLiteral("t")), tiny2Controls());
        result.body = QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("limits"), QJsonObject()}};
        QCOMPARE(data.applyResult(cameraConfigGetCall(QStringLiteral("t")), result), int(CameraData::Config));
        QVERIFY(!data.config(QStringLiteral("t")).contains(QStringLiteral("ok")));
        result.status = 404;
        QCOMPARE(data.applyResult(cameraControlsGetCall(QStringLiteral("t")), result), 0);
        result.status = 200;
        QCOMPARE(data.applyResult(cameraStopCall(QStringLiteral("t")), result), 0);
    }

    // P15 e2: Ganze Presets nach dem Start einzeln (GET camera-presets/<id>), nur bekannte Kennungen
    void presetsFetchedSingly()
    {
        CameraData data;
        CameraServiceInfo info;
        info.state = QString::fromLatin1(kCameraStateConnected);
        info.status = QJsonDocument::fromJson(R"({"cameras": [],
            "camera_presets": [{"id": "totale", "name": "Totale"}, {"id": "nah", "name": "Nah"}]})").object();
        data.setService(info);
        QCOMPARE(data.cameraPresetsWithoutDetails(), (QStringList{QStringLiteral("totale"), QStringLiteral("nah")}));

        CameraResult result;
        result.status = 200;
        result.body = QJsonDocument::fromJson(R"({"ok": true, "preset":
            {"id": "nah", "name": "Nah", "camera": "c", "pan": 18, "tilt": -14, "zoom": 35}})").object();
        QCOMPARE(data.applyResult(cameraPresetGetCall(QStringLiteral("nah")), result), int(CameraData::Presets));
        QCOMPARE(data.applyResult(cameraPresetGetCall(QStringLiteral("nah")), result), 0);
        QCOMPARE(data.cameraPresetsWithoutDetails(), QStringList{QStringLiteral("totale")});
        const QList<CameraPresetRow> rows = cameraPresetRows(data.cameraPresets(), QStringLiteral("c"), {});
        QCOMPARE(rows.size(), 2); // «Totale» noch ohne Kamera, steht bei jeder
        QCOMPARE(rows.at(1).position, QStringLiteral("18° · −14° · Zoom –"));

        // Gelöscht, bevor die Antwort kam: kommt nicht zurück
        result.body = QJsonDocument::fromJson(R"({"ok": true, "preset":
            {"id": "weg", "name": "Weg", "camera": "c", "pan": 0, "tilt": 0, "zoom": 0}})").object();
        QCOMPARE(data.applyResult(cameraPresetGetCall(QStringLiteral("weg")), result), 0);
        QCOMPARE(data.cameraPresets().size(), 2);
        result.status = 404;
        QCOMPARE(data.applyResult(cameraPresetGetCall(QStringLiteral("totale")), result), 0);
    }

    // Entscheid Lotse: Nach einer Bewegung zeigt die KI-Verfolgung «Aus», wenn ai_off_before_move gilt
    void aiOffAfterMove()
    {
        const QString cam = QStringLiteral("t");
        CameraData data;
        CameraResult result;
        result.status = 200;
        result.body = QJsonObject{{QStringLiteral("controls"), tiny2Controls()}};
        data.applyResult(cameraControlsGetCall(cam), result);
        const auto aiValue = [&] {
            for (const QJsonValue &v : data.controls(cam)) {
                if (v.toObject().value(QStringLiteral("key")).toString() == QStringLiteral("obsbot_ai"))
                    return v.toObject().value(QStringLiteral("value")).toInt();
            }
            return -1;
        };
        QCOMPARE(aiValue(), 1);
        // Keine Bewegung: nichts
        QCOMPARE(data.noteCallSent(cameraControlsGetCall(cam)), 0);
        QCOMPARE(data.noteCallSent(cameraStopCall(cam)), 0);
        QCOMPARE(data.noteCallSent(cameraWakeCall(cam, true)), 0);
        QCOMPARE(aiValue(), 1);
        // Ohne gelesene Einstellungen gilt der Standard (an)
        QCOMPARE(data.noteCallSent(cameraStepCall(cam, 1, 0, 0)), int(CameraData::Controls));
        QCOMPARE(aiValue(), 0);
        QCOMPARE(data.noteCallSent(cameraMoveCall(cam, 1, 0, 0)), 0); // schon aus

        // ai_off_before_move aus: bleibt
        data.applyResult(cameraControlsGetCall(cam), result);
        CameraResult config;
        config.status = 200;
        config.body = QJsonObject{{QStringLiteral("config"), QJsonObject{{QStringLiteral("ai_off_before_move"), false}}}};
        data.applyResult(cameraConfigGetCall(cam), config);
        QCOMPARE(data.noteCallSent(cameraMoveCall(cam, 0, 1, 0)), 0);
        QCOMPARE(aiValue(), 1);
        config.body = QJsonObject{{QStringLiteral("config"), QJsonObject{{QStringLiteral("ai_off_before_move"), true}}}};
        data.applyResult(cameraConfigGetCall(cam), config);
        QCOMPARE(data.noteCallSent(cameraHomeCall(cam)), int(CameraData::Controls));
        QCOMPARE(aiValue(), 0);

        // Preset abrufen: Kamera des Presets
        data.applyResult(cameraControlsGetCall(cam), result);
        data.applyEvent({QStringLiteral("presets"), QJsonDocument::fromJson(R"({"camera_presets":
            [{"id": "nah", "name": "Nah", "camera": "t", "pan": 0, "tilt": 0, "zoom": 0}], "light_presets": []})").object()});
        QCOMPARE(data.noteCallSent(cameraPresetRecallCall(QStringLiteral("gibt-es-nicht"))), 0);
        QCOMPARE(data.noteCallSent(cameraPresetRecallCall(QStringLiteral("nah"))), int(CameraData::Controls));
        QCOMPARE(aiValue(), 0);
        // Andere Kamera ohne obsbot_ai: nichts
        QCOMPARE(data.noteCallSent(cameraStepCall(QStringLiteral("c"), 1, 0, 0)), 0);
    }

    void callsE2()
    {
        const QString cam = QStringLiteral("t");
        for (const CameraCall &c : {cameraPresetGetCall(QStringLiteral("nah")),
                                    cameraPresetRenameCall(QStringLiteral("nah"), QStringLiteral("Nah 2")),
                                    cameraPresetDeleteCall(QStringLiteral("nah")),
                                    cameraConfigPutCall(cam, {{QStringLiteral("speed"), 20}})})
            QVERIFY2(checkCameraCall(c.method, c.path).isEmpty(), qPrintable(c.method + QLatin1Char(' ') + c.path));
        QCOMPARE(cameraPresetRenameCall(QStringLiteral("nah"), QStringLiteral("Nah 2")).method, QStringLiteral("PUT"));
        QCOMPARE(*cameraPresetRenameCall(QStringLiteral("nah"), QStringLiteral("Nah 2")).body,
                 (QJsonObject{{QStringLiteral("name"), QStringLiteral("Nah 2")}}));
        QCOMPARE(cameraPresetDeleteCall(QStringLiteral("nah")).method, QStringLiteral("DELETE"));
        QCOMPARE(cameraConfigPutCall(cam, {}).path, QStringLiteral("cameras/t/config"));

        CameraResult r;
        r.status = 200;
        QVERIFY(cameraResultError(r).isEmpty());
        r.status = 409;
        r.body = QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("message"), QStringLiteral("Kamera schläft")}};
        QCOMPARE(cameraResultError(r), QStringLiteral("Kamera schläft"));
        r.body = {};
        QCOMPARE(cameraResultError(r), QStringLiteral("Fehler 409"));
        r.status = 0;
        r.error = QStringLiteral("Kamera-Dienst nicht verbunden");
        QCOMPARE(cameraResultError(r), QStringLiteral("Kamera-Dienst nicht verbunden"));

        CameraEntry tiny2 = entry("OBSBOT Tiny 2", "awake");
        CameraEntry c922 = entry("Logitech C922", "awake");
        QCOMPARE(cameraSpeedSuffix(&tiny2), QStringLiteral(" °/s"));
        QCOMPARE(cameraSpeedSuffix(&c922), QStringLiteral(" %"));
        QCOMPARE(cameraSpeedSuffix(nullptr), QStringLiteral(" %"));
    }

    void configHelpers()
    {
        const QJsonObject config = QJsonDocument::fromJson(R"({"config": {}, "ranges": {"speed": [1, 100],
            "tilt_min": [-75, 90], "kaputt": [1]}})").object();
        QCOMPARE(cameraConfigRange(config, QStringLiteral("tilt_min"), 0, 1), (std::pair<double, double>{-75, 90}));
        QCOMPARE(cameraConfigRange(config, QStringLiteral("pan_min"), -5, 5), (std::pair<double, double>{-5, 5}));
        QCOMPARE(cameraConfigRange(config, QStringLiteral("kaputt"), 2, 3), (std::pair<double, double>{2, 3}));

        const QJsonObject before{{QStringLiteral("name"), QStringLiteral("A")}, {QStringLiteral("tilt_min"), QJsonValue()},
                                 {QStringLiteral("speed"), 10}};
        QJsonObject after = before;
        QVERIFY(cameraConfigChanges(before, after).isEmpty());
        after.insert(QStringLiteral("tilt_min"), -50);
        after.insert(QStringLiteral("speed"), 10);
        QCOMPARE(cameraConfigChanges(before, after), (QJsonObject{{QStringLiteral("tilt_min"), -50}}));
        after.insert(QStringLiteral("tilt_min"), QJsonValue());
        after.insert(QStringLiteral("name"), QJsonValue());
        QCOMPARE(cameraConfigChanges(before, after), (QJsonObject{{QStringLiteral("name"), QJsonValue()}}));
    }

    // «Gemerkte Werte» im Dialog «Kamera einrichten»: Anzeige wie in der Mitte, auch der Standard
    void rememberedRows()
    {
        QJsonArray controls = c922Controls();
        QJsonObject temperature = controls.at(3).toObject();
        temperature.insert(QStringLiteral("label"), QStringLiteral("Farbtemperatur"));
        temperature.insert(QStringLiteral("default"), 5000);
        controls.replace(3, temperature);
        QJsonObject automatic = controls.at(1).toObject();
        automatic.insert(QStringLiteral("default"), true);
        controls.replace(1, automatic);
        const QList<CameraRememberedRow> rows = cameraRememberedRows(controls);
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows.at(0), (CameraRememberedRow{QStringLiteral("white_balance_automatic"),
                                                  QStringLiteral("white_balance_automatic"), QStringLiteral("aus"),
                                                  QStringLiteral("ein")}));
        QCOMPARE(rows.at(1), (CameraRememberedRow{QStringLiteral("power_line_frequency"),
                                                  QStringLiteral("power_line_frequency"), QStringLiteral("50 Hz"),
                                                  QStringLiteral("Aus")}));
        QCOMPARE(rows.at(2), (CameraRememberedRow{QStringLiteral("white_balance_temperature"),
                                                  QStringLiteral("Farbtemperatur"), QStringLiteral("4600 K"),
                                                  QStringLiteral("5000 K")}));
        QVERIFY(cameraRememberedRows(tiny2Controls()).size() == 1); // Bildwinkel; KI nie gemerkt
    }
};

QTEST_GUILESS_MAIN(TestKameraAnsicht)
#include "tst_kameraansicht.moc"
