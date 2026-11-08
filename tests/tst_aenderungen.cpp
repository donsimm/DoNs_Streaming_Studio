// Änderungen ohne Fenster und ohne Kern-Prozess (P08, Tests): Nachrichten aller Änderungsarten
// hin und zurück, Abweisungen, Antwort change_result, Schriftliste font_list, Änderungen einzelner
// Eigenschaften im Modell und das Zusammenfassen beim Speichern. Nur Qt Core und Qt Test
// (CLAUDE.md Regel 9). Daten wie im Handtest: Bild aus prototypes/media, DejaVu Sans.

#include "fontcatalog.h"
#include "overlay.h"
#include "overlaystore.h"
#include "protocol.h"
#include "savescheduler.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace dss;

namespace {

const QString kImage = QStringLiteral(DSS_TEST_MEDIA_DIR "/bild1.png");
const QString kFont = QStringLiteral(DSS_TEST_FONT);

QJsonObject roundTrip(const QJsonObject &o)
{
    // Wie über die Verbindung: eine Zeile JSON.
    QJsonObject back;
    QString error;
    QByteArray line = encodeMessage(o);
    line.chop(1);
    if (!decodeMessage(line, &back, &error))
        qWarning("%s", qPrintable(error));
    return back;
}

ChangeRequest fullRequest(const QString &type)
{
    ChangeRequest r;
    r.type = type;
    r.requestId = 4711;
    r.overlayId = QUuid::fromString(QStringLiteral("11111111-1111-4111-8111-111111111111"));
    r.sourceId = QUuid::fromString(QStringLiteral("22222222-2222-4222-8222-222222222222"));
    r.folderId = QUuid::fromString(QStringLiteral("33333333-3333-4333-8333-333333333333"));
    r.name = QStringLiteral("Überblick «neu»");
    r.file = QStringLiteral("/home/nutzer/Bilder/logo.png");
    r.width = 1280;
    r.height = 720;
    r.index = 2;
    r.changeAddress = true;
    r.expanded = true;
    r.weight = 700;
    r.italic = true;
    r.changes.x = -5;
    return r;
}

Source textSource()
{
    Source s;
    s.id = QUuid::createUuid();
    s.kind = SourceKind::Text;
    s.name = QStringLiteral("Text");
    s.width = 400;
    s.height = 100;
    s.text.content = QStringLiteral("Hallo");
    s.text.fontFile = QStringLiteral("media/") + QString(64, QLatin1Char('a')) + QStringLiteral(".ttf");
    return s;
}

} // namespace

class TestAenderungen : public QObject
{
    Q_OBJECT

private slots:
    void allChangeTypesRoundTrip_data()
    {
        QTest::addColumn<QString>("type");
        for (const QString &t : changeTypes())
            QTest::newRow(qPrintable(t)) << t;
    }
    void allChangeTypesRoundTrip()
    {
        // Jede Art kommt so an, wie sie geschickt wurde; Felder, die die Art nicht hat, fehlen in
        // der Nachricht und bleiben beim Lesen auf dem Standardwert.
        QFETCH(QString, type);
        QVERIFY(isChangeType(type));
        ChangeRequest r;
        r.type = type;
        r.requestId = 4711;
        r.overlayId = QUuid::createUuid();
        r.sourceId = QUuid::createUuid();
        r.folderId = QUuid::createUuid();
        r.name = QStringLiteral("Überblick «neu»");
        r.file = QStringLiteral("/home/nutzer/Bilder/logo.png");
        r.width = 1280;
        r.height = 720;
        r.index = 2;
        r.changeAddress = true;
        r.expanded = true;
        r.output = false;
        r.weight = 700;
        r.italic = true;
        r.changes.x = -5;
        r.changes.content = QStringLiteral("Zeile 1\nZeile 2");
        r.parentId = QUuid::createUuid(); // P10b
        r.importId = 17;
        r.centerX = -40;
        r.centerY = 1080;
        r.insertIndex = 3;
        const QJsonObject message = roundTrip(changeRequestMessage(r));
        QCOMPARE(messageType(message), type);
        QCOMPARE(messageRequestId(message), 4711);
        ChangeRequest back;
        QString error;
        QVERIFY2(parseChangeRequest(message, &back, &error), qPrintable(error));
        // Was die Nachricht enthält, muss gleich sein; der Rest ist Standard.
        const ChangeRequest standard;
        const auto has = [&](const char *key) { return message.contains(QLatin1String(key)); };
        QCOMPARE(back.type, type);
        QCOMPARE(back.requestId, 4711);
        QCOMPARE(back.overlayId, has("overlay_id") ? r.overlayId : standard.overlayId);
        QCOMPARE(back.sourceId, has("source_id") ? r.sourceId : standard.sourceId);
        QCOMPARE(back.folderId, has("folder_id") ? r.folderId : standard.folderId);
        QCOMPARE(back.name, has("name") ? r.name : standard.name);
        QCOMPARE(back.file, has("file") ? r.file : standard.file);
        QCOMPARE(back.width, has("width") ? r.width : standard.width);
        QCOMPARE(back.index, has("index") ? r.index : standard.index);
        QCOMPARE(back.changeAddress, has("change_address") ? r.changeAddress : standard.changeAddress);
        QCOMPARE(back.expanded, has("expanded") ? r.expanded : standard.expanded);
        QCOMPARE(back.output, has("output") ? r.output : standard.output);
        QCOMPARE(back.weight, has("weight") ? r.weight : standard.weight);
        QCOMPARE(back.italic, has("italic") ? r.italic : standard.italic);
        QCOMPARE(back.changes, has("changes") ? r.changes : standard.changes);
        QCOMPARE(back.parentId, has("parent_id") ? r.parentId : standard.parentId);
        QCOMPARE(back.importId, has("import_id") ? r.importId : standard.importId);
        QCOMPARE(back.centerX, has("center_x") ? r.centerX : standard.centerX);
        QCOMPARE(back.centerY, has("center_y") ? r.centerY : standard.centerY);
        QCOMPARE(back.insertIndex, has("insert_index") ? r.insertIndex : standard.insertIndex);
        // Kein Zugangsdatum in einer Änderung (Teil A): kein Feld heisst token (new_token, P10,
        // hat nur overlay_id).
        for (const QString &key : message.keys())
            QVERIFY2(!key.contains(QStringLiteral("token")), qPrintable(key));
    }

    void fieldsPerType()
    {
        // Die Felder je Art wie in docs/08 (Abschnitt Änderungen).
        const auto keys = [](const char *type) {
            ChangeRequest r = fullRequest(QString::fromLatin1(type));
            QStringList k = changeRequestMessage(r).keys();
            k.removeAll(QStringLiteral("type"));
            k.removeAll(QStringLiteral("request_id"));
            k.sort();
            return k.join(QLatin1Char(','));
        };
        QCOMPARE(keys(msg::kCreateOverlay), QStringLiteral("height,name,width"));
        QCOMPARE(keys(msg::kRenameOverlay), QStringLiteral("change_address,name,overlay_id"));
        QCOMPARE(keys(msg::kCopyOverlay), QStringLiteral("overlay_id"));
        QCOMPARE(keys(msg::kRemoveOverlay), QStringLiteral("overlay_id"));
        QCOMPARE(keys(msg::kMoveOverlay), QStringLiteral("folder_id,index,overlay_id"));
        QCOMPARE(keys(msg::kCreateFolder), QStringLiteral("name"));
        QCOMPARE(keys(msg::kRenameFolder), QStringLiteral("folder_id,name"));
        QCOMPARE(keys(msg::kRemoveFolder), QStringLiteral("folder_id"));
        QCOMPARE(keys(msg::kMoveFolder), QStringLiteral("folder_id,index"));
        QCOMPARE(keys(msg::kSetFolderExpanded), QStringLiteral("expanded,folder_id"));
        QCOMPARE(keys(msg::kAddImageSource),
                 QStringLiteral("center_x,center_y,file,insert_index,overlay_id"));
        QCOMPARE(keys(msg::kAddVideoSource),
                 QStringLiteral("center_x,center_y,file,insert_index,overlay_id"));
        QCOMPARE(keys(msg::kAddTextSource), QStringLiteral("overlay_id"));
        QCOMPARE(keys(msg::kRemoveSource), QStringLiteral("overlay_id,source_id"));
        QCOMPARE(keys(msg::kMoveSource), QStringLiteral("index,overlay_id,source_id"));
        QCOMPARE(keys(msg::kUpdateSource), QStringLiteral("changes,overlay_id,source_id"));
        QCOMPARE(keys(msg::kSetTextFont), QStringLiteral("file,italic,overlay_id,source_id,weight"));
        QCOMPARE(keys(msg::kReplaceImageFile), QStringLiteral("file,overlay_id,source_id"));
        QCOMPARE(keys(msg::kSetOverlaySize), QStringLiteral("height,overlay_id,width"));
        QCOMPARE(keys(msg::kSetOverlayOutput), QStringLiteral("output,overlay_id"));
        QCOMPARE(keys(msg::kNewToken), QStringLiteral("overlay_id"));
        // P10b: Medien
        QCOMPARE(keys(msg::kAddMedia), QStringLiteral("file,folder_id"));
        QCOMPARE(keys(msg::kRenameMedia), QStringLiteral("file,name"));
        QCOMPARE(keys(msg::kMoveMedia), QStringLiteral("file,folder_id"));
        QCOMPARE(keys(msg::kRemoveMedia), QStringLiteral("file"));
        QCOMPARE(keys(msg::kCreateMediaFolder), QStringLiteral("name,parent_id"));
        QCOMPARE(keys(msg::kRenameMediaFolder), QStringLiteral("folder_id,name"));
        QCOMPARE(keys(msg::kRemoveMediaFolder), QStringLiteral("folder_id"));
        QCOMPARE(keys(msg::kMoveMediaFolder), QStringLiteral("folder_id,parent_id"));
        QCOMPARE(keys(msg::kSetMediaFolderExpanded), QStringLiteral("expanded,folder_id"));
        QCOMPARE(keys(msg::kCancelImport), QStringLiteral("import_id"));
        QCOMPARE(changeTypes().size(), 31);
        QVERIFY(!isChangeType(QStringLiteral("get_overlay")));
        QVERIFY(!isChangeType(QStringLiteral("set_token")));
    }

    void placementOptional()
    {
        // P10b: center_x, center_y, insert_index dürfen fehlen oder null sein (Lage 0/0, zuoberst
        // wie bis Version 9); center_x und center_y nur zusammen.
        ChangeRequest r = fullRequest(QString::fromLatin1(msg::kAddImageSource));
        QJsonObject o = changeRequestMessage(r);
        QVERIFY(o.value(QStringLiteral("center_x")).isNull());
        o.remove(QStringLiteral("center_y"));
        o.remove(QStringLiteral("insert_index"));
        ChangeRequest back;
        QString error;
        QVERIFY2(parseChangeRequest(o, &back, &error), qPrintable(error));
        QVERIFY(!back.centerX && !back.centerY && !back.insertIndex);
        o.insert(QStringLiteral("center_x"), 10);
        QVERIFY(!parseChangeRequest(o, &back, &error));
        QVERIFY2(error.contains(QStringLiteral("zusammen")), qPrintable(error));
        o.insert(QStringLiteral("center_y"), 20);
        o.insert(QStringLiteral("insert_index"), -1);
        QVERIFY(!parseChangeRequest(o, &back, &error));
        o.insert(QStringLiteral("insert_index"), 1);
        QVERIFY2(parseChangeRequest(o, &back, &error), qPrintable(error));
        QCOMPARE(back.centerX, 10);
        QCOMPARE(back.centerY, 20);
        QCOMPARE(back.insertIndex, 1);
        // parent_id null = oberste Ebene, import_id ab 1
        r = fullRequest(QString::fromLatin1(msg::kCreateMediaFolder));
        o = changeRequestMessage(r);
        QVERIFY(o.value(QStringLiteral("parent_id")).isNull());
        QVERIFY2(parseChangeRequest(o, &back, &error), qPrintable(error));
        QVERIFY(back.parentId.isNull());
        r = fullRequest(QString::fromLatin1(msg::kCancelImport));
        o = changeRequestMessage(r);
        QVERIFY(!parseChangeRequest(o, &back, &error)); // import_id 0
        o.insert(QStringLiteral("import_id"), 3);
        QVERIFY2(parseChangeRequest(o, &back, &error), qPrintable(error));
        QCOMPARE(back.importId, 3);
    }

    void nullValues()
    {
        // move_overlay: folder_id null = oberste Ebene; set_text_font: weight/italic null = aus der Datei.
        ChangeRequest r = fullRequest(QString::fromLatin1(msg::kMoveOverlay));
        r.folderId = QUuid();
        QJsonObject o = changeRequestMessage(r);
        QVERIFY(o.value(QStringLiteral("folder_id")).isNull());
        ChangeRequest back;
        QString error;
        QVERIFY2(parseChangeRequest(o, &back, &error), qPrintable(error));
        QVERIFY(back.folderId.isNull());
        r = fullRequest(QString::fromLatin1(msg::kSetTextFont));
        r.weight.reset();
        r.italic.reset();
        o = changeRequestMessage(r);
        QVERIFY(o.value(QStringLiteral("weight")).isNull());
        QVERIFY(o.value(QStringLiteral("italic")).isNull());
        QVERIFY2(parseChangeRequest(o, &back, &error), qPrintable(error));
        QVERIFY(!back.weight);
        QVERIFY(!back.italic);
    }

    void invalidRequests_data()
    {
        QTest::addColumn<QString>("json");
        QTest::addColumn<QString>("expected");
        const QString o = QStringLiteral("\"overlay_id\":\"11111111-1111-4111-8111-111111111111\"");
        const QString s = QStringLiteral("\"source_id\":\"22222222-2222-4222-8222-222222222222\"");
        const auto row = [](const char *name, const QString &json, const char *expected) {
            QTest::newRow(name) << json << QString::fromUtf8(expected);
        };
        row("keine Änderung", QStringLiteral("{\"type\":\"get_overlay\",\"request_id\":1}"), "keine Änderung");
        row("request_id fehlt", QStringLiteral("{\"type\":\"copy_overlay\",%1}").arg(o), "request_id");
        row("request_id negativ", QStringLiteral("{\"type\":\"copy_overlay\",\"request_id\":-1,%1}").arg(o),
            "request_id");
        row("request_id Bruch", QStringLiteral("{\"type\":\"copy_overlay\",\"request_id\":1.5,%1}").arg(o),
            "request_id");
        row("overlay_id fehlt", QStringLiteral("{\"type\":\"copy_overlay\",\"request_id\":1}"), "overlay_id");
        row("overlay_id kaputt",
            QStringLiteral("{\"type\":\"copy_overlay\",\"request_id\":1,\"overlay_id\":\"abc\"}"), "overlay_id");
        row("width Text",
            QStringLiteral("{\"type\":\"create_overlay\",\"request_id\":1,\"name\":\"a\",\"width\":\"1\",\"height\":1}"),
            "width");
        row("height Bruch",
            QStringLiteral("{\"type\":\"create_overlay\",\"request_id\":1,\"name\":\"a\",\"width\":1,\"height\":1.5}"),
            "height");
        row("index negativ",
            QStringLiteral("{\"type\":\"move_source\",\"request_id\":1,%1,%2,\"index\":-1}").arg(o, s), "index");
        row("folder_id fehlt (Pflicht)",
            QStringLiteral("{\"type\":\"remove_folder\",\"request_id\":1}"), "folder_id");
        row("folder_id fehlt (Ziel)",
            QStringLiteral("{\"type\":\"move_overlay\",\"request_id\":1,%1,\"index\":0}").arg(o), "folder_id");
        row("change_address fehlt",
            QStringLiteral("{\"type\":\"rename_overlay\",\"request_id\":1,%1,\"name\":\"a\"}").arg(o),
            "change_address");
        row("expanded Zahl",
            QStringLiteral("{\"type\":\"set_folder_expanded\",\"request_id\":1,"
                           "\"folder_id\":\"33333333-3333-4333-8333-333333333333\",\"expanded\":1}"),
            "expanded");
        row("weight fehlt",
            QStringLiteral("{\"type\":\"set_text_font\",\"request_id\":1,%1,%2,\"file\":\"a.ttf\",\"italic\":null}")
                .arg(o, s),
            "weight");
        row("weight 950",
            QStringLiteral("{\"type\":\"set_text_font\",\"request_id\":1,%1,%2,\"file\":\"a.ttf\","
                           "\"weight\":950,\"italic\":null}").arg(o, s),
            "weight");
        row("italic Text",
            QStringLiteral("{\"type\":\"set_text_font\",\"request_id\":1,%1,%2,\"file\":\"a.ttf\","
                           "\"weight\":null,\"italic\":\"ja\"}").arg(o, s),
            "italic");
        row("changes fehlt", QStringLiteral("{\"type\":\"update_source\",\"request_id\":1,%1,%2}").arg(o, s),
            "changes");
        row("changes unbekannt",
            QStringLiteral("{\"type\":\"update_source\",\"request_id\":1,%1,%2,\"changes\":{\"breite\":5}}").arg(o, s),
            "changes.breite");
        row("changes.text unbekannt",
            QStringLiteral("{\"type\":\"update_source\",\"request_id\":1,%1,%2,\"changes\":{\"text\":{\"font\":\"a\"}}}")
                .arg(o, s),
            "changes.text.font");
        row("changes.video kein Objekt",
            QStringLiteral("{\"type\":\"update_source\",\"request_id\":1,%1,%2,\"changes\":{\"video\":true}}").arg(o, s),
            "changes.video");
        row("changes.text.align",
            QStringLiteral("{\"type\":\"update_source\",\"request_id\":1,%1,%2,"
                           "\"changes\":{\"text\":{\"align\":\"mitte\"}}}").arg(o, s),
            "align");
        row("changes.visible Zahl",
            QStringLiteral("{\"type\":\"update_source\",\"request_id\":1,%1,%2,\"changes\":{\"visible\":1}}").arg(o, s),
            "visible");
        row("changes.x Bruch",
            QStringLiteral("{\"type\":\"update_source\",\"request_id\":1,%1,%2,\"changes\":{\"x\":1.5}}").arg(o, s),
            "x");
    }
    void invalidRequests()
    {
        QFETCH(QString, json);
        QFETCH(QString, expected);
        QJsonObject message;
        QString error;
        QVERIFY2(decodeMessage(json.toUtf8(), &message, &error), qPrintable(error));
        ChangeRequest r;
        QVERIFY(!parseChangeRequest(message, &r, &error));
        QVERIFY2(error.contains(expected), qPrintable(error));
    }

    void requestIdOfInvalidMessage()
    {
        // Die Antwort auf eine ungültige Änderung braucht die request_id, falls es eine gibt.
        QJsonObject o{{QStringLiteral("type"), QStringLiteral("update_source")},
                      {QStringLiteral("request_id"), 12}};
        QCOMPARE(messageRequestId(o), 12);
        o.insert(QStringLiteral("request_id"), QStringLiteral("12"));
        QCOMPARE(messageRequestId(o), -1);
        o.remove(QStringLiteral("request_id"));
        QCOMPARE(messageRequestId(o), -1);
    }

    void sourceChangesJson()
    {
        SourceChanges c;
        c.name = QStringLiteral("Logo");
        c.x = -10;
        c.y = 20;
        c.width = 300;
        c.height = 200;
        c.rotation = 3599;
        c.flipHorizontal = true; // P09b
        c.flipVertical = false;  // P09b
        c.visible = false;
        c.loop = true;
        c.holdAtEnd = true; // P09
        c.content = QStringLiteral("a\nb");
        c.fontSize = 72;
        c.color = QStringLiteral("#11223344");
        c.align = TextAlign::Center;
        const QJsonObject o = sourceChangesToJson(c);
        QCOMPARE(o.value(QStringLiteral("video")).toObject().value(QStringLiteral("loop")).toBool(), true);
        QCOMPARE(o.value(QStringLiteral("video")).toObject().value(QStringLiteral("hold_at_end")).toBool(), true);
        QCOMPARE(o.value(QStringLiteral("flip_horizontal")), QJsonValue(true));
        QCOMPARE(o.value(QStringLiteral("flip_vertical")), QJsonValue(false));
        QCOMPARE(o.value(QStringLiteral("text")).toObject().value(QStringLiteral("font_size")).toInt(), 72);
        QCOMPARE(o.value(QStringLiteral("text")).toObject().value(QStringLiteral("align")).toString(),
                 QStringLiteral("center"));
        SourceChanges back;
        QString error;
        QVERIFY2(sourceChangesFromJson(o, &back, &error), qPrintable(error));
        QCOMPARE(back, c);
        // Nur gesetzte Felder stehen in der Nachricht.
        SourceChanges one;
        one.y = 5;
        QCOMPARE(sourceChangesToJson(one), (QJsonObject{{QStringLiteral("y"), 5}}));
        QVERIFY(SourceChanges().isEmpty());
        QVERIFY(!one.isEmpty());
        // Spiegeln muss true/false sein (P09b).
        QVERIFY(!sourceChangesFromJson(QJsonObject{{QStringLiteral("flip_vertical"), 1}}, &back, &error));
        QVERIFY2(error.contains(QStringLiteral("flip_vertical")), qPrintable(error));
    }

    void changeResult()
    {
        ChangeResult ok{.requestId = 7, .ok = true, .id = QUuid::createUuid(), .code = {}, .message = {}};
        QJsonObject o = roundTrip(changeResultMessage(ok));
        QCOMPARE(messageType(o), QStringLiteral("change_result"));
        QVERIFY(o.value(QStringLiteral("code")).isNull());
        QVERIFY(o.value(QStringLiteral("message")).isNull());
        ChangeResult back;
        QString error;
        QVERIFY2(parseChangeResult(o, &back, &error), qPrintable(error));
        QCOMPARE(back, ok);
        ChangeResult fail{.requestId = 8, .ok = false, .id = {}, .code = QStringLiteral("rejected"),
                          .message = QStringLiteral("Breite 0 ungültig")};
        o = roundTrip(changeResultMessage(fail));
        QVERIFY(o.value(QStringLiteral("id")).isNull());
        QVERIFY2(parseChangeResult(o, &back, &error), qPrintable(error));
        QCOMPARE(back, fail);
        // Ohne ok, oder !ok ohne Meldung: ungültig
        o.remove(QStringLiteral("message"));
        QVERIFY(!parseChangeResult(o, &back, &error));
        o.remove(QStringLiteral("ok"));
        QVERIFY(!parseChangeResult(o, &back, &error));

        // P09: Warnung bei gelungener Änderung (Grösse der Datei nicht lesbar)
        ChangeResult warned{.requestId = 9, .ok = true, .id = QUuid::createUuid(), .code = {},
                            .message = {}, .warning = QStringLiteral("Grösse nicht lesbar")};
        o = roundTrip(changeResultMessage(warned));
        QCOMPARE(o.value(QStringLiteral("warning")).toString(), QStringLiteral("Grösse nicht lesbar"));
        QVERIFY2(parseChangeResult(o, &back, &error), qPrintable(error));
        QCOMPARE(back, warned);
        o = roundTrip(changeResultMessage(ok));
        QVERIFY(o.value(QStringLiteral("warning")).isNull());
    }

    void fontListMessages()
    {
        const QList<FontFace> faces = {
            {QStringLiteral("Be Vietnam Pro"), QStringLiteral("Regular"), 400, false,
             QStringLiteral("/x/BeVietnamPro-Regular.ttf"), FontOrigin::Builtin},
            {QStringLiteral("DejaVu Sans"), QStringLiteral("Bold Oblique"), 700, true,
             QStringLiteral("/usr/share/fonts/truetype/dejavu/DejaVuSans-BoldOblique.ttf"), FontOrigin::System},
            {QStringLiteral("Eigene"), QStringLiteral("Light"), 300, false,
             QStringLiteral("media/") + QString(64, QLatin1Char('b')) + QStringLiteral(".otf"), FontOrigin::Media},
        };
        QJsonObject o = roundTrip(fontListMessage(faces, QString()));
        QVERIFY(o.value(QStringLiteral("error")).isNull());
        QList<FontFace> back;
        QString fontError;
        QString error;
        QVERIFY2(parseFontList(o, &back, &fontError, &error), qPrintable(error));
        QCOMPARE(back, faces);
        QCOMPARE(fontError, QString());
        o = roundTrip(fontListMessage({}, QStringLiteral("fc-list nicht ausführbar")));
        QVERIFY2(parseFontList(o, &back, &fontError, &error), qPrintable(error));
        QVERIFY(back.isEmpty());
        QCOMPARE(fontError, QStringLiteral("fc-list nicht ausführbar"));
        // ungültige Herkunft, Gewicht ausserhalb
        QJsonObject face = fontListMessage(faces, {}).value(QStringLiteral("faces")).toArray().at(0).toObject();
        for (const auto &[key, value] : {std::pair{QStringLiteral("origin"), QJsonValue(QStringLiteral("web"))},
                                         std::pair{QStringLiteral("weight"), QJsonValue(1000)},
                                         std::pair{QStringLiteral("italic"), QJsonValue(1)}}) {
            QJsonObject bad = face;
            bad.insert(key, value);
            QJsonObject m = fontListMessage({}, {});
            m.insert(QStringLiteral("faces"), QJsonArray{bad});
            QVERIFY2(!parseFontList(m, &back, &fontError, &error), qPrintable(key));
        }
    }

    void applyChanges()
    {
        Source t = textSource();
        SourceChanges c;
        QCOMPARE(applySourceChanges(&t, c), QStringLiteral("Keine Änderung angegeben."));
        c.content = QStringLiteral("Neu");
        c.color = QStringLiteral("#00FF0080");
        c.align = TextAlign::Right;
        c.x = 10;
        QCOMPARE(applySourceChanges(&t, c), QString());
        QCOMPARE(t.text.content, QStringLiteral("Neu"));
        QCOMPARE(t.text.align, TextAlign::Right);
        QCOMPARE(t.x, 10);
        // Ungültiges ändert nichts, auch nicht die gültigen Teile derselben Änderung.
        const Source before = t;
        c = {};
        c.x = 99;
        c.rotation = 3600;
        QVERIFY(applySourceChanges(&t, c).startsWith(QStringLiteral("Drehung 3600 ungültig")));
        QCOMPARE(t, before);
        c = {};
        c.color = QStringLiteral("#00FF00");
        QVERIFY(!applySourceChanges(&t, c).isEmpty());
        c = {};
        c.fontSize = 0;
        QVERIFY(!applySourceChanges(&t, c).isEmpty());
        c = {};
        c.height = 0;
        QVERIFY(!applySourceChanges(&t, c).isEmpty());
        c = {};
        c.name = QString();
        QCOMPARE(applySourceChanges(&t, c), QStringLiteral("Der Name der Quelle darf nicht leer sein."));
        c = {};
        c.loop = true;
        QCOMPARE(applySourceChanges(&t, c), QStringLiteral("Wiederholen gibt es nur bei Quellen der Art Video."));
        QCOMPARE(t, before);
        // Bild: keine Text-Eigenschaften
        Source image = t;
        image.kind = SourceKind::Image;
        image.text = {};
        image.file = QStringLiteral("media/") + QString(64, QLatin1Char('c')) + QStringLiteral(".png");
        c = {};
        c.fontSize = 20;
        QVERIFY(applySourceChanges(&image, c).startsWith(QStringLiteral("Inhalt, Schriftgrösse")));
        // Lage darf ausserhalb liegen (ganze Pixel, auch negativ)
        c = {};
        c.x = -5000;
        c.y = 5000;
        QCOMPARE(applySourceChanges(&image, c), QString());
    }

    void moveSourceInModel()
    {
        Overlay o;
        o.id = QUuid::createUuid();
        o.name = QStringLiteral("A");
        o.address = QStringLiteral("a");
        for (int i = 0; i < 3; ++i) {
            Source s = textSource();
            s.name = QString::number(i);
            QCOMPARE(addSource(&o, s), QString());
        }
        const QUuid first = o.sources.at(0).id;
        QCOMPARE(moveSource(&o, first, 2), QString());
        QCOMPARE(o.sources.at(2).id, first);
        QCOMPARE(moveSource(&o, first, 0), QString());
        QCOMPARE(o.sources.at(0).id, first);
        QVERIFY(!moveSource(&o, first, 3).isEmpty());
        QVERIFY(!moveSource(&o, first, -1).isEmpty());
        QVERIFY(!moveSource(&o, QUuid::createUuid(), 0).isEmpty());
        QCOMPARE(o.sources.at(0).id, first);
    }

    void schedulerIdle()
    {
        // Eine einzelne Änderung: gespeichert nach idleMs.
        SaveScheduler s(nullptr, 300, 1000);
        QSignalSpy spy(&s, &SaveScheduler::saveRequested);
        QElapsedTimer t;
        t.start();
        s.changed();
        QVERIFY(s.isPending());
        QVERIFY(spy.wait(2000));
        const qint64 ms = t.elapsed();
        qInfo("eine Änderung: gespeichert nach %lld ms", static_cast<long long>(ms));
        QVERIFY(ms >= 280 && ms < 600); // 300 ms, CoarseTimer ±5 %
        QVERIFY(!s.isPending());
        QTest::qWait(500);
        QCOMPARE(spy.count(), 1);
    }

    void schedulerMaxDelay()
    {
        // Änderung alle 50 ms während 2.5 s (Pfeiltaste gedrückt): idleMs läuft nie ab, gespeichert
        // wird spätestens alle 1000 ms, am Schluss noch einmal nach idleMs.
        SaveScheduler s(nullptr, 300, 1000);
        QList<qint64> times;
        QElapsedTimer t;
        t.start();
        connect(&s, &SaveScheduler::saveRequested, this, [&] { times.append(t.elapsed()); });
        qint64 last = 0; // Zeit der letzten Änderung
        while (t.elapsed() < 2500) {
            s.changed();
            last = t.elapsed();
            QTest::qWait(50);
        }
        QTRY_VERIFY_WITH_TIMEOUT(!s.isPending(), 2000);
        QString text;
        for (qint64 v : std::as_const(times))
            text += QString::number(v) + QLatin1Char(' ');
        qInfo("Speichern bei %s ms (letzte Änderung %lld ms)", qPrintable(text), static_cast<long long>(last));
        // Qt::CoarseTimer (Standard von QTimer) darf um 5 % abweichen, auch nach vorne
        // (https://doc.qt.io/qt-6.8/qt.html#TimerType-enum), bei 1000 ms also ab 950 ms. Dazu kommt
        // die Messung in ganzen Millisekunden (QElapsedTimer::elapsed), gemessen wurden 949 ms.
        // Reserve deshalb 5 ms je Ablauf von maxDelayMs, beim zweiten Speichern zwei Abläufe.
        QCOMPARE(times.size(), 3);
        QVERIFY(times.at(0) >= 945 && times.at(0) < 1200);
        QVERIFY(times.at(1) >= 1890 && times.at(1) < 2300);
        QVERIFY(times.at(2) >= last + 280 && times.at(2) < last + 500);
    }

    void schedulerSaveNow()
    {
        SaveScheduler s(nullptr, 300, 1000);
        QSignalSpy spy(&s, &SaveScheduler::saveRequested);
        s.saveNow(); // nichts offen: kein Speichern
        QCOMPARE(spy.count(), 0);
        s.changed();
        s.saveNow();
        QCOMPARE(spy.count(), 1);
        QVERIFY(!s.isPending());
        QTest::qWait(1200); // Zeitgeber sind gestoppt
        QCOMPARE(spy.count(), 1);
    }

    void deferredWritesInStore()
    {
        // Kern-Betrieb: 100 Änderungen an derselben Quelle schreiben die Datei nicht; erst
        // flushPendingWrites schreibt sie einmal. Lesen mit einem zweiten Objekt wie nach einem
        // Neustart.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        OverlayStore store(dir.path());
        QString error;
        QVERIFY2(store.load(&error), qPrintable(error));
        store.setDeferredWrites(true);
        const QUuid overlay = store.createOverlay(QStringLiteral("Test"), 1920, 1080, &error);
        QVERIFY2(!overlay.isNull(), qPrintable(error));
        const QUuid image = store.addImageSource(overlay, kImage, &error);
        QVERIFY2(!image.isNull(), qPrintable(error));
        QVERIFY(store.hasPendingWrites());
        QVERIFY2(store.flushPendingWrites(&error), qPrintable(error));
        const int writesBefore = store.overlayWriteCount();
        for (int x = 1; x <= 100; ++x) {
            SourceChanges c;
            c.x = x;
            QVERIFY2(store.updateSource(overlay, image, c, &error), qPrintable(error));
        }
        QCOMPARE(store.overlayWriteCount(), writesBefore);
        QVERIFY(store.hasPendingWrites());
        {
            OverlayStore other(dir.path());
            QVERIFY(other.load(&error, OverlayStore::LoadMode::ReadOnly));
            QCOMPARE(other.overlay(overlay)->sources.first().x, 0);
        }
        QVERIFY2(store.flushPendingWrites(&error), qPrintable(error));
        QCOMPARE(store.overlayWriteCount(), writesBefore + 1);
        QVERIFY(!store.hasPendingWrites());
        OverlayStore other(dir.path());
        QVERIFY(other.load(&error, OverlayStore::LoadMode::ReadOnly));
        QCOMPARE(other.overlay(overlay)->sources.first().x, 100);
        // Eine abgewiesene Änderung merkt nichts vor.
        SourceChanges bad;
        bad.width = 0;
        QVERIFY(!store.updateSource(overlay, image, bad, &error));
        QVERIFY(!store.hasPendingWrites());
        // Ein gelöschtes Overlay wird nicht mehr geschrieben.
        SourceChanges c;
        c.y = 1;
        QVERIFY(store.updateSource(overlay, image, c, &error));
        QVERIFY(store.removeOverlay(overlay, &error));
        QVERIFY(!store.hasPendingWrites());
        QVERIFY(store.flushPendingWrites(&error));
        QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("overlays/") + overlay.toString(QUuid::WithoutBraces)
                                            + QStringLiteral(".json"))));
    }

    void immediateWritesWithoutDeferred()
    {
        // dss-cli: ohne setDeferredWrites schreibt jede Änderung sofort.
        QTemporaryDir dir;
        OverlayStore store(dir.path());
        QString error;
        QVERIFY(store.load(&error));
        const QUuid overlay = store.createOverlay(QStringLiteral("Test"), 1920, 1080, &error);
        const QUuid text = store.addTextSource(overlay, QStringLiteral("Hallo"), kFont, &error);
        QVERIFY2(!text.isNull(), qPrintable(error));
        const int before = store.overlayWriteCount();
        SourceChanges c;
        c.fontSize = 30;
        QVERIFY(store.updateSource(overlay, text, c, &error));
        QCOMPARE(store.overlayWriteCount(), before + 1);
        QVERIFY(!store.hasPendingWrites());
    }

    void textFontInStore()
    {
        QTemporaryDir dir;
        OverlayStore store(dir.path());
        QString error;
        QVERIFY(store.load(&error));
        const QUuid overlay = store.createOverlay(QStringLiteral("Test"), 1920, 1080, &error);
        const QUuid text = store.addTextSource(overlay, QStringLiteral("Hallo"), defaultTextFontFile(), &error);
        QVERIFY2(!text.isNull(), qPrintable(error));
        const QUuid image = store.addImageSource(overlay, kImage, &error);
        QString reference;
        QVERIFY2(store.setTextFont(overlay, text, kFont, 300, true, &error, &reference), qPrintable(error));
        QVERIFY(reference.startsWith(QStringLiteral("media/")) && reference.endsWith(QStringLiteral(".ttf")));
        const Source s = store.overlay(overlay)->sources.at(0);
        QCOMPARE(s.text.fontFile, reference);
        QCOMPARE(s.text.weight, 300);
        QCOMPARE(s.text.italic, true);
        // Verweis auf die Kopie, gleiche Datei → keine neue Kopie
        QVERIFY2(store.setTextFont(overlay, text, reference, 400, false, &error), qPrintable(error));
        QVERIFY(store.setTextFont(overlay, text, kFont, 400, false, &error, &reference));
        QCOMPARE(store.overlay(overlay)->sources.at(0).text.fontFile, reference);
        // abgewiesen: Gewicht, Art, unbekannte Kopie, falsche Endung
        QVERIFY(!store.setTextFont(overlay, text, kFont, 50, false, &error));
        QVERIFY(!store.setTextFont(overlay, image, kFont, 400, false, &error));
        QVERIFY(!store.setTextFont(overlay, text, QStringLiteral("media/") + QString(64, QLatin1Char('d'))
                                                      + QStringLiteral(".ttf"), 400, false, &error));
        QVERIFY(!store.setTextFont(overlay, text, kImage, 400, false, &error));
        QCOMPARE(store.overlay(overlay)->sources.at(0).text.weight, 400);
        // Bild ersetzen nur bei Bildern
        QVERIFY2(store.replaceImageFile(overlay, image, QStringLiteral(DSS_TEST_MEDIA_DIR "/bild2.png"), &error),
                 qPrintable(error));
        QVERIFY(!store.replaceImageFile(overlay, text, kImage, &error));
    }
};

QTEST_GUILESS_MAIN(TestAenderungen)
#include "tst_aenderungen.moc"
