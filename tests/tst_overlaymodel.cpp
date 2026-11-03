// Prüft das Overlay-Modell ohne Dateien: Adresse aus dem Namen, Prüfungen, Bearbeiten,
// Umwandlung nach JSON und zurück.

#include "address.h"
#include "overlay.h"
#include "overlayjson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

using namespace dss;

namespace {

const QString kMediaPng = QStringLiteral("media/") + QString(64, QLatin1Char('a')) + QStringLiteral(".png");
const QString kMediaWebm = QStringLiteral("media/") + QString(64, QLatin1Char('b')) + QStringLiteral(".webm");
const QString kMediaTtf = QStringLiteral("media/") + QString(64, QLatin1Char('c')) + QStringLiteral(".ttf");

Source makeImage()
{
    Source s;
    s.id = QUuid::createUuid();
    s.kind = SourceKind::Image;
    s.name = QStringLiteral("bild1.png");
    s.x = -20;
    s.y = 30;
    s.width = 256;
    s.height = 128;
    s.rotation = 125;
    s.file = kMediaPng;
    return s;
}

Source makeVideo()
{
    Source s = makeImage();
    s.id = QUuid::createUuid();
    s.kind = SourceKind::Video;
    s.name = QStringLiteral("alpha.webm");
    s.visible = false;
    s.file = kMediaWebm;
    s.loop = true;
    return s;
}

Source makeText()
{
    Source s;
    s.id = QUuid::createUuid();
    s.kind = SourceKind::Text;
    s.name = QStringLiteral("Text: Willkommen");
    s.width = 800;
    s.height = 200;
    s.text.content = QStringLiteral("Willkommen\nZweite Zeile");
    s.text.fontFile = kMediaTtf;
    s.text.weight = 700;
    s.text.italic = true;
    s.text.fontSize = 64;
    s.text.color = QStringLiteral("#FFCC00C0");
    s.text.align = TextAlign::Center;
    return s;
}

Overlay makeOverlay()
{
    Overlay o;
    o.id = QUuid::createUuid();
    o.name = QStringLiteral("Überblick Start");
    o.address = QStringLiteral("ueberblick-start");
    o.width = 1920;
    o.height = 1080;
    o.sources = {makeImage(), makeVideo(), makeText()};
    return o;
}

// Entfernt flip_horizontal und flip_vertical aus allen Quellen (Format vor Version 3, P09b).
void removeFlipFields(QJsonObject &o)
{
    QJsonArray sources = o.value(QStringLiteral("sources")).toArray();
    for (qsizetype i = 0; i < sources.size(); ++i) {
        QJsonObject s = sources.at(i).toObject();
        s.remove(QStringLiteral("flip_horizontal"));
        s.remove(QStringLiteral("flip_vertical"));
        sources[i] = s;
    }
    o.insert(QStringLiteral("sources"), sources);
}

// Entfernt hold_at_end aus allen Videoquellen (Format 1, vor P09).
void removeHoldAtEnd(QJsonObject &o)
{
    QJsonArray sources = o.value(QStringLiteral("sources")).toArray();
    for (qsizetype i = 0; i < sources.size(); ++i) {
        QJsonObject s = sources.at(i).toObject();
        if (s.contains(QStringLiteral("video"))) {
            QJsonObject video = s.value(QStringLiteral("video")).toObject();
            video.remove(QStringLiteral("hold_at_end"));
            s.insert(QStringLiteral("video"), video);
        }
        sources[i] = s;
    }
    o.insert(QStringLiteral("sources"), sources);
}

// Ändert in der JSON-Darstellung eines Overlays einen Wert und liest wieder ein.
QString parseError(const std::function<void(QJsonObject &)> &change)
{
    QJsonObject o = overlayToJson(makeOverlay());
    change(o);
    Overlay result;
    QString error;
    if (overlayFromJson(o, &result, &error))
        return QString();
    return error.isEmpty() ? QStringLiteral("(leere Meldung)") : error;
}

void setInSource(QJsonObject &o, int index, const QString &key, const QJsonValue &value)
{
    QJsonArray sources = o.value(QStringLiteral("sources")).toArray();
    QJsonObject s = sources.at(index).toObject();
    s.insert(key, value);
    sources[index] = s;
    o.insert(QStringLiteral("sources"), sources);
}

// Setzt einen Wert im Eigenschaftsobjekt einer Quelle ("text", "image", "video");
// Undefined entfernt das Feld.
void setInKind(QJsonObject &o, int index, const QString &kindKey, const QString &key,
               const QJsonValue &value)
{
    QJsonArray sources = o.value(QStringLiteral("sources")).toArray();
    QJsonObject s = sources.at(index).toObject();
    QJsonObject t = s.value(kindKey).toObject();
    if (value.isUndefined())
        t.remove(key);
    else
        t.insert(key, value);
    s.insert(kindKey, t);
    sources[index] = s;
    o.insert(QStringLiteral("sources"), sources);
}

void setInText(QJsonObject &o, const QString &key, const QJsonValue &value)
{
    setInKind(o, 2, QStringLiteral("text"), key, value);
}

} // namespace

class TestOverlayModel : public QObject
{
    Q_OBJECT

private slots:
    void addressFromName_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("expected");
        QTest::newRow("Beispiel") << QStringLiteral("Überblick Start") << QStringLiteral("ueberblick-start");
        QTest::newRow("Umlaute") << QStringLiteral("ÄÖÜ äöü") << QStringLiteral("aeoeue-aeoeue");
        QTest::newRow("Umlaut zerlegt") << QStringLiteral("Über") << QStringLiteral("ueber");
        QTest::newRow("Akzente") << QStringLiteral("Café Crème à l'Hôtel")
                                 << QStringLiteral("cafe-creme-a-l-hotel");
        QTest::newRow("Akzente gross") << QStringLiteral("ÉCOLE Ñandú") << QStringLiteral("ecole-nandu");
        QTest::newRow("Sonderzeichen") << QStringLiteral("Chat & Alerts #2!") << QStringLiteral("chat-alerts-2");
        QTest::newRow("Rand und Mehrfach") << QStringLiteral("  --Hallo!!   Welt--  ")
                                           << QStringLiteral("hallo-welt");
        QTest::newRow("nur Sonderzeichen") << QStringLiteral("!!! ???") << QStringLiteral("overlay");
        QTest::newRow("leer") << QString() << QStringLiteral("overlay");
        QTest::newRow("Ziffern") << QStringLiteral("1080p 60") << QStringLiteral("1080p-60");
        // ß/æ/ø/ł haben keine Unicode-Zerlegung, eigene Regel (entschieden nach P04)
        QTest::newRow("Eszett") << QStringLiteral("Straße") << QStringLiteral("strasse");
        QTest::newRow("Eszett gross") << QStringLiteral("STRAẞE") << QStringLiteral("strasse");
        QTest::newRow("ae") << QStringLiteral("Æble æble") << QStringLiteral("aeble-aeble");
        QTest::newRow("oe") << QStringLiteral("Øl øl") << QStringLiteral("oel-oel");
        QTest::newRow("l") << QStringLiteral("Łódź łąka") << QStringLiteral("lodz-laka");
        QTest::newRow("gemischt") << QStringLiteral("Grüße aus Køge") << QStringLiteral("gruesse-aus-koege");
        QTest::newRow("Emoji") << QStringLiteral("Live 🎮 Start") << QStringLiteral("live-start");
    }

    void addressFromName()
    {
        QFETCH(QString, name);
        QFETCH(QString, expected);
        QCOMPARE(dss::addressFromName(name), expected);
        QVERIFY(validateAddress(expected).isEmpty());
    }

    void uniqueAddress()
    {
        QCOMPARE(dss::uniqueAddress(QStringLiteral("start"), {}), QStringLiteral("start"));
        QCOMPARE(dss::uniqueAddress(QStringLiteral("start"), {QStringLiteral("start")}),
                 QStringLiteral("start-2"));
        QCOMPARE(dss::uniqueAddress(QStringLiteral("start"),
                                    {QStringLiteral("start"), QStringLiteral("start-2")}),
                 QStringLiteral("start-3"));
        QCOMPARE(dss::uniqueAddress(QStringLiteral("start"), {QStringLiteral("start-2")}),
                 QStringLiteral("start"));
    }

    void validOverlayAccepted()
    {
        QCOMPARE(validateOverlay(makeOverlay()), QString());
        Overlay o = makeOverlay();
        o.width = kOverlayMaxWidth;
        o.height = kOverlayMaxHeight;
        o.sources[0].rotation = kRotationMax;
        o.sources[0].width = 1;
        o.sources[0].height = 1;
        o.sources[0].x = -100000;
        o.sources[2].text.weight = kFontWeightMin;
        o.sources[2].text.content.clear();
        QCOMPARE(validateOverlay(o), QString());
        o.sources[2].text.weight = kFontWeightMax;
        o.sources[2].text.color = QStringLiteral("#ffcc00c0");
        QCOMPARE(validateOverlay(o), QString());
    }

    void invalidValuesRejected_data()
    {
        QTest::addColumn<int>("caseNumber");
        QTest::addColumn<QString>("expectedPart");
        QTest::newRow("Drehung 3600") << 1 << QStringLiteral("Drehung 3600");
        QTest::newRow("Drehung -1") << 2 << QStringLiteral("Drehung -1");
        QTest::newRow("Breite Quelle 0") << 3 << QStringLiteral("Breite 0");
        QTest::newRow("Höhe Quelle 0") << 4 << QStringLiteral("Höhe 0");
        QTest::newRow("Name leer") << 5 << QStringLiteral("Name des Overlays");
        QTest::newRow("Name nur Leerzeichen") << 6 << QStringLiteral("Name des Overlays");
        QTest::newRow("Overlay Breite 0") << 7 << QStringLiteral("Breite 0");
        QTest::newRow("Overlay Breite 7681") << 8 << QStringLiteral("Breite 7681");
        QTest::newRow("Overlay Höhe 4321") << 9 << QStringLiteral("Höhe 4321");
        QTest::newRow("Farbe ohne Alpha") << 10 << QStringLiteral("Farbe");
        QTest::newRow("Gewicht 99") << 11 << QStringLiteral("Gewicht 99");
        QTest::newRow("Gewicht 901") << 12 << QStringLiteral("Gewicht 901");
        QTest::newRow("Schriftgrösse 0") << 13 << QStringLiteral("Schriftgrösse 0");
        QTest::newRow("Dateiverweis") << 14 << QStringLiteral("Dateiverweis");
        QTest::newRow("Adresse") << 15 << QStringLiteral("Adresse");
        QTest::newRow("Quellname leer") << 16 << QStringLiteral("Name der Quelle");
        QTest::newRow("Kennung doppelt") << 17 << QStringLiteral("mehrfach");
        QTest::newRow("Schriftdatei fehlt") << 18 << QStringLiteral("Schriftdatei");
        QTest::newRow("Wiederholen bei Bild") << 19 << QStringLiteral("nur bei Quellen der Art Video");
        QTest::newRow("Wiederholen bei Text") << 20 << QStringLiteral("nur bei Quellen der Art Video");
        QTest::newRow("Am Ende anhalten bei Bild") << 21 << QStringLiteral("Am Ende anhalten gibt es nur");
        QTest::newRow("Am Ende anhalten bei Text") << 22 << QStringLiteral("Am Ende anhalten gibt es nur");
    }

    void invalidValuesRejected()
    {
        QFETCH(int, caseNumber);
        QFETCH(QString, expectedPart);
        Overlay o = makeOverlay();
        switch (caseNumber) {
        case 1: o.sources[0].rotation = 3600; break;
        case 2: o.sources[0].rotation = -1; break;
        case 3: o.sources[0].width = 0; break;
        case 4: o.sources[0].height = 0; break;
        case 5: o.name.clear(); break;
        case 6: o.name = QStringLiteral("   "); break;
        case 7: o.width = 0; break;
        case 8: o.width = 7681; break;
        case 9: o.height = 4321; break;
        case 10: o.sources[2].text.color = QStringLiteral("#FFCC00"); break;
        case 11: o.sources[2].text.weight = 99; break;
        case 12: o.sources[2].text.weight = 901; break;
        case 13: o.sources[2].text.fontSize = 0; break;
        case 14: o.sources[0].file = QStringLiteral("/home/dominik/bild.png"); break;
        case 15: o.address = QStringLiteral("Ueberblick Start"); break;
        case 16: o.sources[1].name.clear(); break;
        case 17: o.sources[1].id = o.sources[0].id; break;
        case 18: o.sources[2].text.fontFile.clear(); break;
        case 19: o.sources[0].loop = true; break;
        case 20: o.sources[2].loop = true; break;
        case 21: o.sources[0].holdAtEnd = true; break;
        case 22: o.sources[2].holdAtEnd = true; break;
        }
        const QString error = validateOverlay(o);
        QVERIFY2(error.contains(expectedPart), qPrintable(error));
    }

    void editSources()
    {
        Overlay o = makeOverlay();
        const QUuid image = o.sources[0].id;
        const QUuid video = o.sources[1].id;
        const QUuid text = o.sources[2].id;

        QCOMPARE(moveSourceForward(&o, image), QString());
        QCOMPARE(o.sources[0].id, video);
        QCOMPARE(o.sources[1].id, image);
        QCOMPARE(moveSourceBackward(&o, image), QString());
        QCOMPARE(o.sources[0].id, image);
        QVERIFY(!moveSourceBackward(&o, image).isEmpty()); // schon ganz hinten
        QVERIFY(!moveSourceForward(&o, text).isEmpty());   // schon ganz vorne

        QCOMPARE(setSourceVisible(&o, image, false), QString());
        QCOMPARE(o.sources[0].visible, false);

        QCOMPARE(setSourceGeometry(&o, text, -50, -60, 300, 100, 3599), QString());
        QCOMPARE(o.sources[2].x, -50);
        QCOMPARE(o.sources[2].y, -60);
        QCOMPARE(o.sources[2].width, 300);
        QCOMPARE(o.sources[2].height, 100);
        QCOMPARE(o.sources[2].rotation, 3599);

        // Ungültig: nichts wird geändert, auch nicht teilweise
        const Overlay before = o;
        QVERIFY(!setSourceGeometry(&o, text, 1, 2, 3, 4, 3600).isEmpty());
        QVERIFY(!setSourceGeometry(&o, text, 1, 2, 0, 4, 0).isEmpty());
        QVERIFY(!setSourceGeometry(&o, QUuid::createUuid(), 1, 2, 3, 4, 0).isEmpty());
        QCOMPARE(o, before);

        Source invalid = makeImage();
        invalid.rotation = 3600;
        QVERIFY(!addSource(&o, invalid).isEmpty());
        QVERIFY(!addSource(&o, o.sources[0]).isEmpty()); // Kennung schon vergeben
        QCOMPARE(o, before);

        Source added = makeImage();
        QCOMPARE(addSource(&o, added), QString());
        QCOMPARE(o.sources.last().id, added.id); // neue Quelle liegt zuoberst

        QCOMPARE(removeSource(&o, video), QString());
        QCOMPARE(sourceIndex(o, video), -1);
        QVERIFY(!removeSource(&o, video).isEmpty());
    }

    void rotationDisplay()
    {
        QCOMPARE(dss::rotationDisplay(0), QStringLiteral("0.0 °"));
        QCOMPARE(dss::rotationDisplay(125), QStringLiteral("12.5 °"));
        QCOMPARE(dss::rotationDisplay(3599), QStringLiteral("359.9 °"));
    }

    void jsonRoundTrip()
    {
        const Overlay original = makeOverlay();
        Overlay loaded;
        QString error;
        QVERIFY2(parseOverlay(serializeOverlay(original), &loaded, &error), qPrintable(error));
        QCOMPARE(loaded, original);
        QCOMPARE(loaded.sources[1].loop, true);
        QCOMPARE(overlayToJson(original).value(QStringLiteral("format_version")).toInt(), 4); // seit P10
    }

    // P10 (E-24): Ausgabe ein/aus im Overlay, Format 4.
    void outputField()
    {
        Overlay original = makeOverlay();
        QCOMPARE(original.output, true); // neue Overlays: Ausgabe ein
        original.output = false;
        const QJsonObject o = overlayToJson(original);
        QCOMPARE(o.value(QStringLiteral("format_version")).toInt(), 4);
        QCOMPARE(o.value(QStringLiteral("output")), QJsonValue(false));
        Overlay loaded;
        QString error;
        QVERIFY2(parseOverlay(QJsonDocument(o).toJson(), &loaded, &error), qPrintable(error));
        QCOMPARE(loaded, original);

        // Version 4: Pflicht und true/false
        QJsonObject missing = o;
        missing.remove(QStringLiteral("output"));
        QVERIFY(!parseOverlay(QJsonDocument(missing).toJson(), &loaded, &error));
        QVERIFY2(error.contains(QStringLiteral("output")), qPrintable(error));
        QJsonObject wrong = o;
        wrong.insert(QStringLiteral("output"), 0);
        QVERIFY(!parseOverlay(QJsonDocument(wrong).toJson(), &loaded, &error));
        QVERIFY2(error.contains(QStringLiteral("output")), qPrintable(error));

        // Version 1 bis 3: ohne das Feld, Ausgabe ein; mit ihm ungültig (unbekanntes Feld).
        for (int version : {1, 2, 3}) {
            QJsonObject old = overlayToJson(makeOverlay());
            old.insert(QStringLiteral("format_version"), version);
            old.remove(QStringLiteral("output"));
            if (version < 3)
                removeFlipFields(old);
            if (version < 2)
                removeHoldAtEnd(old);
            QVERIFY2(parseOverlay(QJsonDocument(old).toJson(), &loaded, &error), qPrintable(error));
            QCOMPARE(loaded.output, true);
            old.insert(QStringLiteral("output"), false);
            QVERIFY(!parseOverlay(QJsonDocument(old).toJson(), &loaded, &error));
            QVERIFY2(error.contains(QStringLiteral("output")), qPrintable(error));
        }
    }

    // P09, Entscheid 9: «Am Ende anhalten» in Modell und Datei (format_version 2).
    void holdAtEnd()
    {
        Overlay original = makeOverlay();
        original.sources[1].loop = false;
        original.sources[1].holdAtEnd = true;
        const QJsonObject json = overlayToJson(original);
        const QJsonObject video = json.value(QStringLiteral("sources")).toArray().at(1).toObject()
                                      .value(QStringLiteral("video")).toObject();
        QCOMPARE(video.value(QStringLiteral("hold_at_end")).toBool(), true);
        QCOMPARE(video.value(QStringLiteral("loop")).toBool(), false);
        // Bild und Text haben kein Feld hold_at_end.
        QVERIFY(!json.value(QStringLiteral("sources")).toArray().at(0).toObject()
                     .value(QStringLiteral("image")).toObject().contains(QStringLiteral("hold_at_end")));
        Overlay loaded;
        QString error;
        QVERIFY2(parseOverlay(serializeOverlay(original), &loaded, &error), qPrintable(error));
        QCOMPARE(loaded.sources[1].holdAtEnd, true);

        // Ändern über SourceChanges; bei Bild abgewiesen, ohne Änderung
        SourceChanges c;
        c.holdAtEnd = false;
        QCOMPARE(updateSource(&loaded, loaded.sources[1].id, c), QString());
        QCOMPARE(loaded.sources[1].holdAtEnd, false);
        const Overlay before = loaded;
        c.holdAtEnd = true;
        QVERIFY(updateSource(&loaded, loaded.sources[0].id, c).contains(QStringLiteral("Am Ende anhalten")));
        QCOMPARE(loaded, before);
    }

    // Alte Dateien (format_version 1, P04 bis P08) laden weiter: ohne hold_at_end, Wert false.
    void formatVersion1Loads()
    {
        const Overlay original = makeOverlay();
        QJsonObject o = overlayToJson(original);
        o.insert(QStringLiteral("format_version"), 1);
        o.remove(QStringLiteral("output")); // erst ab Format 4 (P10)
        removeFlipFields(o);
        QJsonArray sources = o.value(QStringLiteral("sources")).toArray();
        QJsonObject s = sources.at(1).toObject();
        QJsonObject video = s.value(QStringLiteral("video")).toObject();
        video.remove(QStringLiteral("hold_at_end"));
        s.insert(QStringLiteral("video"), video);
        sources[1] = s;
        o.insert(QStringLiteral("sources"), sources);
        Overlay loaded;
        QString error;
        QVERIFY2(parseOverlay(QJsonDocument(o).toJson(), &loaded, &error), qPrintable(error));
        QCOMPARE(loaded, original);
        QCOMPARE(loaded.sources[1].holdAtEnd, false);

        // In Version 1 gibt es hold_at_end nicht; in Version 2 muss es da sein.
        video.insert(QStringLiteral("hold_at_end"), true);
        s.insert(QStringLiteral("video"), video);
        sources[1] = s;
        o.insert(QStringLiteral("sources"), sources);
        QVERIFY(!parseOverlay(QJsonDocument(o).toJson(), &loaded, &error));
        QVERIFY2(error.contains(QStringLiteral("hold_at_end")), qPrintable(error));
        QJsonObject v2 = overlayToJson(original);
        v2.insert(QStringLiteral("format_version"), 2);
        v2.remove(QStringLiteral("output")); // erst ab Format 4 (P10)
        removeFlipFields(v2);
        QJsonArray sources2 = v2.value(QStringLiteral("sources")).toArray();
        QJsonObject s2 = sources2.at(1).toObject();
        QJsonObject video2 = s2.value(QStringLiteral("video")).toObject();
        video2.remove(QStringLiteral("hold_at_end"));
        s2.insert(QStringLiteral("video"), video2);
        sources2[1] = s2;
        v2.insert(QStringLiteral("sources"), sources2);
        QVERIFY(!parseOverlay(QJsonDocument(v2).toJson(), &loaded, &error));
        QVERIFY2(error.contains(QStringLiteral("hold_at_end")), qPrintable(error));
    }

    // P09b, Entscheid 4: waagrecht und senkrecht gespiegelt je Quelle, Format 3.
    void flipFields()
    {
        Overlay original = makeOverlay();
        original.sources[0].flipHorizontal = true;
        original.sources[2].flipVertical = true;
        original.sources[1].flipHorizontal = true;
        original.sources[1].flipVertical = true;
        const QJsonObject o = overlayToJson(original);
        QCOMPARE(o.value(QStringLiteral("format_version")).toInt(), 4);
        const QJsonObject first = o.value(QStringLiteral("sources")).toArray().at(0).toObject();
        QCOMPARE(first.value(QStringLiteral("flip_horizontal")), QJsonValue(true));
        QCOMPARE(first.value(QStringLiteral("flip_vertical")), QJsonValue(false));
        Overlay loaded;
        QString error;
        QVERIFY2(parseOverlay(QJsonDocument(o).toJson(), &loaded, &error), qPrintable(error));
        QCOMPARE(loaded, original);

        // Ab Version 3: beide Felder Pflicht und true/false
        for (const QString &key : {QStringLiteral("flip_horizontal"), QStringLiteral("flip_vertical")}) {
            QJsonObject missing = o;
            QJsonArray sources = missing.value(QStringLiteral("sources")).toArray();
            QJsonObject s = sources.at(2).toObject();
            s.remove(key);
            sources[2] = s;
            missing.insert(QStringLiteral("sources"), sources);
            QVERIFY(!parseOverlay(QJsonDocument(missing).toJson(), &loaded, &error));
            QVERIFY2(error.contains(QStringLiteral("sources[2].") + key), qPrintable(error));
            s.insert(key, 1);
            sources[2] = s;
            missing.insert(QStringLiteral("sources"), sources);
            QVERIFY(!parseOverlay(QJsonDocument(missing).toJson(), &loaded, &error));
            QVERIFY2(error.contains(key), qPrintable(error));
        }

        // Version 2 (P09, Datei auf dem Studio-PC): ohne die Felder, beide gelten als false;
        // mit ihnen ist die Datei ungültig (unbekanntes Feld).
        QJsonObject v2 = overlayToJson(makeOverlay());
        v2.insert(QStringLiteral("format_version"), 2);
        v2.remove(QStringLiteral("output"));
        removeFlipFields(v2);
        QVERIFY2(parseOverlay(QJsonDocument(v2).toJson(), &loaded, &error), qPrintable(error));
        for (const Source &source : loaded.sources) {
            QCOMPARE(source.flipHorizontal, false);
            QCOMPARE(source.flipVertical, false);
        }
        QJsonObject v2flip = o;
        v2flip.insert(QStringLiteral("format_version"), 2);
        v2flip.remove(QStringLiteral("output"));
        QVERIFY(!parseOverlay(QJsonDocument(v2flip).toJson(), &loaded, &error));
        QVERIFY2(error.contains(QStringLiteral("flip_")), qPrintable(error));
        // Gespeichert wird die neueste Version (4, P10) mit beiden Feldern.
        QCOMPARE(overlayToJson(loaded).value(QStringLiteral("format_version")).toInt(), 4);

        // Ändern über SourceChanges, auch bei Text; Breite und Höhe bleiben positiv.
        Overlay edited = makeOverlay();
        SourceChanges c;
        c.flipHorizontal = true;
        QVERIFY(updateSource(&edited, edited.sources[2].id, c).isEmpty());
        QCOMPARE(edited.sources[2].flipHorizontal, true);
        QCOMPARE(edited.sources[2].flipVertical, false);
        c = SourceChanges();
        c.flipVertical = true;
        c.flipHorizontal = false;
        QVERIFY(updateSource(&edited, edited.sources[2].id, c).isEmpty());
        QCOMPARE(edited.sources[2].flipHorizontal, false);
        QCOMPARE(edited.sources[2].flipVertical, true);
        QVERIFY(!c.isEmpty());
    }

    // P09, Entscheid 5: Auflösung ändern, die Quellen behalten ihre Pixelwerte.
    void overlaySize()
    {
        Overlay o = makeOverlay();
        const QList<Source> sources = o.sources;
        QCOMPARE(setOverlaySize(&o, 1280, 720), QString());
        QCOMPARE(o.width, 1280);
        QCOMPARE(o.height, 720);
        QCOMPARE(o.sources, sources);
        const Overlay before = o;
        QVERIFY(!setOverlaySize(&o, 0, 720).isEmpty());
        QVERIFY(!setOverlaySize(&o, 1280, 4321).isEmpty());
        QCOMPARE(o, before);
    }

    void jsonRejected_data()
    {
        QTest::addColumn<int>("caseNumber");
        QTest::addColumn<QString>("expectedPart");
        QTest::newRow("unbekannte Art") << 1 << QStringLiteral("unbekannte Art «audio»");
        QTest::newRow("Drehung 3600") << 2 << QStringLiteral("Drehung 3600");
        QTest::newRow("Breite 0") << 3 << QStringLiteral("Breite 0");
        QTest::newRow("Kommazahl") << 4 << QStringLiteral("sources[0].x");
        QTest::newRow("unbekanntes Feld") << 5 << QStringLiteral("unbekanntes Feld");
        QTest::newRow("fehlendes Feld") << 6 << QStringLiteral("«width»: fehlt");
        QTest::newRow("format_version 5") << 7 << QStringLiteral("neuer");
        QTest::newRow("format_version 0") << 8 << QStringLiteral("unbekannt");
        QTest::newRow("format_version fehlt") << 9 << QStringLiteral("format_version");
        QTest::newRow("Ausrichtung") << 10 << QStringLiteral("justify");
        QTest::newRow("Text-Eigenschaft fehlt") << 11 << QStringLiteral("sources[2].text.color");
        QTest::newRow("falsches Eigenschaftsobjekt") << 12 << QStringLiteral("«sources[0].video»: unbekanntes Feld");
        QTest::newRow("Name leer") << 13 << QStringLiteral("nicht leer");
        QTest::newRow("Text statt Zahl") << 14 << QStringLiteral("«height»");
        QTest::newRow("Kennung mit Klammern") << 15 << QStringLiteral("«id»");
        QTest::newRow("Zahl zu gross") << 16 << QStringLiteral("sources[0].y");
        QTest::newRow("Wiederholen fehlt") << 17 << QStringLiteral("«sources[1].video.loop»: fehlt");
        QTest::newRow("Wiederholen kein bool") << 18 << QStringLiteral("«sources[1].video.loop»: muss true oder false");
        QTest::newRow("Wiederholen bei Bild") << 19 << QStringLiteral("«sources[0].image.loop»: unbekanntes Feld");
    }

    void jsonRejected()
    {
        QFETCH(int, caseNumber);
        QFETCH(QString, expectedPart);
        const QString error = parseError([caseNumber](QJsonObject &o) {
            switch (caseNumber) {
            case 1: setInSource(o, 0, QStringLiteral("kind"), QStringLiteral("audio")); break;
            case 2: setInSource(o, 0, QStringLiteral("rotation"), 3600); break;
            case 3: o.insert(QStringLiteral("width"), 0); break;
            case 4: setInSource(o, 0, QStringLiteral("x"), 1.5); break;
            case 5: o.insert(QStringLiteral("token"), QStringLiteral("x")); break;
            case 6: o.remove(QStringLiteral("width")); break;
            case 7: o.insert(QStringLiteral("format_version"), 5); break;
            case 8: o.insert(QStringLiteral("format_version"), 0); break;
            case 9: o.remove(QStringLiteral("format_version")); break;
            case 10: setInText(o, QStringLiteral("align"), QStringLiteral("justify")); break;
            case 11: setInText(o, QStringLiteral("color"), QJsonValue::Undefined); break;
            case 12: {
                // Bild mit Eigenschaften unter "video" statt "image"
                QJsonArray sources = o.value(QStringLiteral("sources")).toArray();
                QJsonObject s = sources.at(0).toObject();
                s.insert(QStringLiteral("video"), s.take(QStringLiteral("image")));
                sources[0] = s;
                o.insert(QStringLiteral("sources"), sources);
                break;
            }
            case 13: o.insert(QStringLiteral("name"), QStringLiteral("")); break;
            case 14: o.insert(QStringLiteral("height"), QStringLiteral("1080")); break;
            case 15: o.insert(QStringLiteral("id"), QUuid::createUuid().toString()); break;
            case 16: setInSource(o, 0, QStringLiteral("y"), 3e9); break;
            case 17: setInKind(o, 1, QStringLiteral("video"), QStringLiteral("loop"), QJsonValue::Undefined); break;
            case 18: setInKind(o, 1, QStringLiteral("video"), QStringLiteral("loop"), QStringLiteral("ja")); break;
            case 19: setInKind(o, 0, QStringLiteral("image"), QStringLiteral("loop"), false); break;
            }
        });
        QVERIFY2(error.contains(expectedPart), qPrintable(error));
    }

    void jsonNotJson()
    {
        Overlay o;
        QString error;
        QVERIFY(!parseOverlay("{ kaputt", &o, &error));
        QVERIFY(error.contains(QStringLiteral("kein gültiges JSON")));
        QVERIFY(!parseOverlay("[1, 2]", &o, &error));
        QVERIFY(error.contains(QStringLiteral("kein JSON-Objekt")));
    }
};

QTEST_GUILESS_MAIN(TestOverlayModel)
#include "tst_overlaymodel.moc"
