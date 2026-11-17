#include "steptypes.h"

#include <QJsonArray>
#include <QJsonObject>

namespace dss::actions {

namespace {

FieldSpec boolField(const QString &name, bool defaultValue)
{
    return {name, FieldKind::Bool, defaultValue, {}};
}

FieldSpec intField(const QString &name, qint64 defaultValue, double min, double max)
{
    FieldSpec f{name, FieldKind::Int, double(defaultValue), {}};
    f.min = min;
    f.max = max;
    return f;
}

FieldSpec textField(const QString &name, const QString &defaultValue = QString())
{
    return {name, FieldKind::String, defaultValue, {}};
}

FieldSpec enumField(const QString &name, const QStringList &values, const QString &defaultValue)
{
    return {name, FieldKind::Enum, defaultValue, values};
}

FieldSpec kindField(const QString &name, FieldKind kind, const QJsonValue &defaultValue)
{
    return {name, kind, defaultValue, {}};
}

QList<StepTypeInfo> buildStepTypes()
{
    const QStringList scope{QStringLiteral("global"), QStringLiteral("user")};
    const QStringList user{QStringLiteral("trigger"), QStringLiteral("target")};
    const QStringList transition{QStringLiteral("none"), QStringLiteral("fade")};
    const double noMax = std::numeric_limits<double>::max();

    QList<StepTypeInfo> t;
    t.append({QString::fromLatin1(kStepComment), QStringLiteral("Kommentar"), 1009, StepPart::P14_1,
              false, true, {textField(QStringLiteral("text")), textField(QStringLiteral("color"))}});
    t.append({QString::fromLatin1(kStepGroup), QStringLiteral("Gruppe"), 99900, StepPart::P14_1, true,
              true, {textField(QStringLiteral("name")), boolField(QStringLiteral("random"), false)}});
    t.append({QString::fromLatin1(kStepIf), QStringLiteral("Wenn"), 120, StepPart::P14_1, true, true,
              {textField(QStringLiteral("input")),
               enumField(QStringLiteral("operation"), ifOperations(), QStringLiteral("equals")),
               textField(QStringLiteral("value")), boolField(QStringLiteral("auto_type"), true)}});
    t.append({QString::fromLatin1(kStepIfTrue), QStringLiteral("Dann"), 99901, StepPart::P14_1, true,
              false, {}});
    t.append({QString::fromLatin1(kStepIfFalse), QStringLiteral("Sonst"), 99902, StepPart::P14_1, true,
              false, {}});
    t.append({QString::fromLatin1(kStepBreak), QStringLiteral("Abbruch"), 124, StepPart::P14_1, false,
              true, {}});
    t.append({QString::fromLatin1(kStepDelay), QStringLiteral("Pause"), 1002, StepPart::P14_1, false,
              true,
              {intField(QStringLiteral("value_ms"), 1000, 0, kMaxDelayMs),
               intField(QStringLiteral("max_value_ms"), 0, 0, kMaxDelayMs),
               boolField(QStringLiteral("random"), false)}});
    // Streamer.bot schreibt das Feld «runImmedately» (Plan 2.4); hier immediately.
    t.append({QString::fromLatin1(kStepRunAction), QStringLiteral("Aktion ausführen"), 4,
              StepPart::P14_1, false, true,
              {kindField(QStringLiteral("action_id"), FieldKind::NullableUuid, QJsonValue::Null),
               boolField(QStringLiteral("immediately"), true)}});
    t.append({QString::fromLatin1(kStepSetArgument), QStringLiteral("Argument setzen"), 123,
              StepPart::P14_1, false, true,
              {textField(QStringLiteral("name")), textField(QStringLiteral("value")),
               boolField(QStringLiteral("auto_type"), false)}});
    t.append({QString::fromLatin1(kStepGlobalGet), QStringLiteral("Variable lesen"), 121,
              StepPart::P14_1, false, true,
              {textField(QStringLiteral("name")),
               enumField(QStringLiteral("scope"), scope, QStringLiteral("global")),
               enumField(QStringLiteral("user"), user, QStringLiteral("trigger")),
               boolField(QStringLiteral("persisted"), true), textField(QStringLiteral("destination")),
               textField(QStringLiteral("default"))}});
    t.append({QString::fromLatin1(kStepGlobalSet), QStringLiteral("Variable setzen"), 122,
              StepPart::P14_1, false, true,
              {textField(QStringLiteral("name")),
               enumField(QStringLiteral("scope"), scope, QStringLiteral("global")),
               enumField(QStringLiteral("user"), user, QStringLiteral("trigger")),
               boolField(QStringLiteral("persisted"), true),
               enumField(QStringLiteral("operation"),
                         {QStringLiteral("set"), QStringLiteral("increment"), QStringLiteral("decrement"),
                          QStringLiteral("delete")},
                         QStringLiteral("set")),
               textField(QStringLiteral("value"))}});
    t.append({QString::fromLatin1(kStepChatSend), QStringLiteral("Chat senden"), 10, StepPart::P14_1,
              false, true,
              {textField(QStringLiteral("text")), boolField(QStringLiteral("use_bot"), true),
               boolField(QStringLiteral("fallback"), true), boolField(QStringLiteral("reply"), false)}});
    t.append({QString::fromLatin1(kStepUserInfo), QStringLiteral("Benutzerinfo holen"), 50,
              StepPart::P14_1, false, true, {textField(QStringLiteral("login"), QStringLiteral("%userName%"))}});
    // Typnummer aus z0: Nummer von «Twitch Shoutout» bei Streamer.bot nicht dokumentiert (Schritt 0, C 20)
    t.append({QString::fromLatin1(kStepTwitchShoutout), QStringLiteral("Shoutout"), 0, StepPart::P14_1,
              false, true, {textField(QStringLiteral("login"))}});
    // color: Farbe der Ansage bei Twitch als Text. Laut Schritt 0 (B 17) blue, green, orange, purple,
    // primary; geprüft beim Senden im Kern (TwitchActions, c1b).
    t.append({QString::fromLatin1(kStepTwitchAnnounce), QStringLiteral("Ansage"), 23, StepPart::P14_1,
              false, true,
              {textField(QStringLiteral("text")), textField(QStringLiteral("color"), QStringLiteral("primary")),
               boolField(QStringLiteral("use_bot"), true)}});
    // file: Verweis «media/…» der Mediathek, leer = keine Datei gewählt
    t.append({QString::fromLatin1(kStepPlaySound), QStringLiteral("Ton abspielen"), 1, StepPart::P14_1,
              false, true,
              {textField(QStringLiteral("file")), intField(QStringLiteral("volume"), 100, 0, 200),
               boolField(QStringLiteral("wait"), true)}});
    t.append({QString::fromLatin1(kStepOverlaySource), QStringLiteral("Overlay-Element zeigen/verstecken"),
              0, StepPart::P14_1, false, true,
              {kindField(QStringLiteral("overlay"), FieldKind::NullableUuid, QJsonValue::Null),
               enumField(QStringLiteral("mode"),
                         {QStringLiteral("show"), QStringLiteral("hide"), QStringLiteral("toggle")},
                         QStringLiteral("show")),
               kindField(QStringLiteral("elements"), FieldKind::ElementList, QJsonArray()),
               intField(QStringLiteral("duration_ms"), 5000, 0, noMax),
               boolField(QStringLiteral("wait"), true),
               enumField(QStringLiteral("transition_in"), transition, QStringLiteral("none")),
               enumField(QStringLiteral("transition_out"), transition, QStringLiteral("none")),
               intField(QStringLiteral("transition_ms"), 0, 0, noMax)}});
    t.append({QString::fromLatin1(kStepObsSourceVisible), QStringLiteral("OBS-Quelle ein/aus"), 30,
              StepPart::P14_2, false, true,
              {textField(QStringLiteral("scene_name")), textField(QStringLiteral("source_name")),
               enumField(QStringLiteral("state"),
                         {QStringLiteral("on"), QStringLiteral("off"), QStringLiteral("toggle")},
                         QStringLiteral("on"))}});
    // P15 f1 (Plan P15 9.): Position bzw. Licht-Preset des Kamera-Dienstes abrufen; preset ist die
    // Kennung im Dienst, leer = keins gewählt. Kein Feld wake: Abrufen weckt die Tiny 2 immer
    // (Entscheid 6). Kein Feld wait: Der Dienst antwortet, sobald das Ziel gesetzt ist, und meldet
    // das Ende der Bewegung nicht (kamera-dienst/dss_camera/motion.py, _goto).
    t.append({QString::fromLatin1(kStepCameraPreset), QStringLiteral("Kamera-Position"), 0, StepPart::P14_1,
              false, true, {textField(QStringLiteral("preset"))}});
    t.append({QString::fromLatin1(kStepLightPreset), QStringLiteral("Licht-Preset"), 0, StepPart::P14_1,
              false, true, {textField(QStringLiteral("preset"))}});
    t.append({QString::fromLatin1(kStepScript), QStringLiteral("Skript-Schritt nicht verfügbar"), 99999,
              StepPart::Never, false, false,
              {textField(QStringLiteral("language")), textField(QStringLiteral("code"))}});
    t.append({QString::fromLatin1(kStepUnsupported), QStringLiteral("nicht unterstützt"), 0,
              StepPart::Never, false, false,
              {intField(QStringLiteral("sb_type"), 0, std::numeric_limits<double>::lowest(), noMax),
               kindField(QStringLiteral("original"), FieldKind::Object, QJsonObject())}});
    return t;
}

} // namespace

QStringList ifOperations()
{
    return {QStringLiteral("equals"),           QStringLiteral("not_equals"),
            QStringLiteral("equals_ignore_case"), QStringLiteral("not_equals_ignore_case"),
            QStringLiteral("contains"),         QStringLiteral("less_than"),
            QStringLiteral("greater_than"),     QStringLiteral("is_empty"),
            QStringLiteral("not_exists"),       QStringLiteral("regex")};
}

const QList<StepTypeInfo> &stepTypes()
{
    static const QList<StepTypeInfo> types = buildStepTypes();
    return types;
}

const StepTypeInfo *stepType(const QString &key)
{
    for (const StepTypeInfo &t : stepTypes()) {
        if (t.key == key)
            return &t;
    }
    return nullptr;
}

QString stepTypeForSbType(int sbType)
{
    if (sbType != 0) {
        for (const StepTypeInfo &t : stepTypes()) {
            if (t.sbType == sbType && t.part != StepPart::Never)
                return t.key;
        }
        if (sbType == 99999)
            return QString::fromLatin1(kStepScript);
    }
    return QString::fromLatin1(kStepUnsupported);
}

} // namespace dss::actions
