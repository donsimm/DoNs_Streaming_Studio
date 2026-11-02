// Prüft Reihenfolge und Ordner der Overlay-Liste (P08, Teil B) ohne Qt Gui: Datei liste.json
// lesen und schreiben, fehlende und überzählige Einträge, kaputte Datei, Änderungen und das Ziel
// beim Ziehen mit der Maus. Datenordner mit echten Overlays wie im Handtest (CLAUDE.md Regel 7),
// angelegt mit OverlayStore in einem temporären Ordner.

#include "overlaylist.h"
#include "overlaystore.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

using namespace dss;

namespace {

QUuid uuid(int n)
{
    return QUuid::fromString(QStringLiteral("00000000-0000-4000-8000-%1").arg(n, 12, 10, QLatin1Char('0')));
}

// Liste: A, Ordner F1 (B, C), D, Ordner F2 (leer)
OverlayLayout sample()
{
    OverlayLayout l = layoutFromIds({uuid(1)});
    ListEntry f1;
    f1.isFolder = true;
    f1.folder = {uuid(101), QStringLiteral("Szenen"), true, {uuid(2), uuid(3)}};
    l.entries.append(f1);
    ListEntry d;
    d.overlayId = uuid(4);
    l.entries.append(d);
    ListEntry f2;
    f2.isFolder = true;
    f2.folder = {uuid(102), QStringLiteral("Leer"), false, {}};
    l.entries.append(f2);
    return l;
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

QStringList names(const OverlayStore &store)
{
    QStringList list;
    for (const QUuid &id : store.layout().overlayIds())
        list << store.overlay(id)->name;
    return list;
}

} // namespace

class TestOverlayListe : public QObject
{
    Q_OBJECT

private slots:
    void jsonRoundTrip()
    {
        const OverlayLayout l = sample();
        OverlayLayout back;
        QString error;
        QVERIFY2(parseLayout(serializeLayout(l), &back, &error), qPrintable(error));
        QCOMPARE(back, l);
        QCOMPARE(back.overlayIds(), (QList<QUuid>{uuid(1), uuid(2), uuid(3), uuid(4)}));
        const QJsonObject o = layoutToJson(l);
        QCOMPARE(o.value(QStringLiteral("format_version")).toInt(), 1);
        const QJsonObject folder = o.value(QStringLiteral("entries")).toArray().at(1).toObject();
        QCOMPARE(folder.value(QStringLiteral("kind")).toString(), QStringLiteral("folder"));
        QCOMPARE(folder.value(QStringLiteral("name")).toString(), QStringLiteral("Szenen"));
        QCOMPARE(folder.value(QStringLiteral("overlays")).toArray().size(), 2);
    }

    void invalidFiles_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::addColumn<QString>("expected");
        const QByteArray a = "00000000-0000-4000-8000-000000000001";
        const QByteArray f = "00000000-0000-4000-8000-000000000101";
        QTest::newRow("kein JSON") << QByteArray("{") << QStringLiteral("kein gültiges JSON");
        QTest::newRow("Liste statt Objekt") << QByteArray("[]") << QStringLiteral("kein JSON-Objekt");
        QTest::newRow("Version fehlt") << QByteArray("{\"entries\":[]}") << QStringLiteral("format_version");
        QTest::newRow("Version 2") << QByteArray("{\"format_version\":2,\"entries\":[]}")
                                   << QStringLiteral("format_version 2 ist unbekannt");
        QTest::newRow("unbekanntes Feld") << QByteArray("{\"format_version\":1,\"entries\":[],\"x\":1}")
                                          << QStringLiteral("«x»: unbekanntes Feld");
        QTest::newRow("Art unbekannt") << QByteArray("{\"format_version\":1,\"entries\":[{\"kind\":\"x\",\"id\":\"" + a + "\"}]}")
                                       << QStringLiteral("entries[0].kind");
        QTest::newRow("UUID gross") << QByteArray("{\"format_version\":1,\"entries\":[{\"kind\":\"overlay\",\"id\":\"" + a.toUpper() + "X\"}]}")
                                    << QStringLiteral("entries[0].id");
        QTest::newRow("Overlay doppelt") << QByteArray("{\"format_version\":1,\"entries\":[{\"kind\":\"overlay\",\"id\":\"" + a
                                                       + "\"},{\"kind\":\"folder\",\"id\":\"" + f
                                                       + "\",\"name\":\"F\",\"expanded\":true,\"overlays\":[\"" + a + "\"]}]}")
                                         << QStringLiteral("mehrfach");
        QTest::newRow("Ordner ohne Name") << QByteArray("{\"format_version\":1,\"entries\":[{\"kind\":\"folder\",\"id\":\"" + f
                                                        + "\",\"name\":\" \",\"expanded\":true,\"overlays\":[]}]}")
                                          << QStringLiteral("entries[0].name");
        QTest::newRow("Ordner im Ordner") << QByteArray("{\"format_version\":1,\"entries\":[{\"kind\":\"folder\",\"id\":\"" + f
                                                        + "\",\"name\":\"F\",\"expanded\":true,\"overlays\":[{\"kind\":\"folder\"}]}]}")
                                          << QStringLiteral("entries[0].overlays[0]");
        QTest::newRow("expanded fehlt") << QByteArray("{\"format_version\":1,\"entries\":[{\"kind\":\"folder\",\"id\":\"" + f
                                                      + "\",\"name\":\"F\",\"overlays\":[]}]}")
                                        << QStringLiteral("entries[0].expanded»: fehlt");
    }

    void invalidFiles()
    {
        QFETCH(QByteArray, data);
        QFETCH(QString, expected);
        OverlayLayout l;
        QString error;
        QVERIFY(!parseLayout(data, &l, &error));
        QVERIFY2(error.contains(expected), qPrintable(error));
    }

    void normalizeMissingAndSurplus()
    {
        OverlayLayout l = sample();
        // uuid(3) gibt es nicht mehr, uuid(5) und uuid(6) sind neu (zum Beispiel mit dss-cli angelegt).
        QVERIFY(normalizeLayout(&l, {uuid(6), uuid(1), uuid(2), uuid(4), uuid(5)}));
        QCOMPARE(l.overlayIds(), (QList<QUuid>{uuid(1), uuid(2), uuid(4), uuid(6), uuid(5)}));
        QCOMPARE(l.folder(uuid(101))->overlays, QList<QUuid>{uuid(2)});
        QVERIFY(l.folder(uuid(102))); // leerer Ordner bleibt
        QVERIFY(!normalizeLayout(&l, {uuid(6), uuid(1), uuid(2), uuid(4), uuid(5)}));
    }

    void changes()
    {
        OverlayLayout l = sample();
        // Overlay in einen Ordner und an eine Stelle
        QVERIFY(moveOverlayInLayout(&l, uuid(4), uuid(101), 1).isEmpty());
        QCOMPARE(l.folder(uuid(101))->overlays, (QList<QUuid>{uuid(2), uuid(4), uuid(3)}));
        // aus dem Ordner heraus ganz nach oben
        QVERIFY(moveOverlayInLayout(&l, uuid(3), QUuid(), 0).isEmpty());
        QCOMPARE(l.overlayIds(), (QList<QUuid>{uuid(3), uuid(1), uuid(2), uuid(4)}));
        // ungültige Stelle und unbekannter Ordner ändern nichts
        const OverlayLayout before = l;
        QVERIFY(!moveOverlayInLayout(&l, uuid(1), QUuid(), 9).isEmpty());
        QVERIFY(!moveOverlayInLayout(&l, uuid(1), uuid(999), 0).isEmpty());
        QVERIFY(!moveOverlayInLayout(&l, uuid(999), QUuid(), 0).isEmpty());
        QCOMPARE(l, before);
        // Ordner verschieben
        QVERIFY(moveFolderInLayout(&l, uuid(102), 0).isEmpty());
        QVERIFY(l.entries.at(0).isFolder && l.entries.at(0).folder.id == uuid(102));
        QVERIFY(!moveFolderInLayout(&l, uuid(102), int(l.entries.size())).isEmpty());
        // Ordner anlegen, umbenennen, aufklappen
        QVERIFY(createFolderInLayout(&l, uuid(103), QStringLiteral("Neu")).isEmpty());
        QVERIFY(!createFolderInLayout(&l, uuid(103), QStringLiteral("Doppelt")).isEmpty());
        QVERIFY(!createFolderInLayout(&l, uuid(104), QStringLiteral("  ")).isEmpty());
        QCOMPARE(l.entries.last().folder.name, QStringLiteral("Neu"));
        QVERIFY(renameFolderInLayout(&l, uuid(103), QStringLiteral("Umbenannt")).isEmpty());
        QVERIFY(!renameFolderInLayout(&l, uuid(103), QString()).isEmpty());
        QCOMPARE(l.folder(uuid(103))->name, QStringLiteral("Umbenannt"));
        QVERIFY(setFolderExpandedInLayout(&l, uuid(101), false).isEmpty());
        QCOMPARE(l.folder(uuid(101))->expanded, false);
        // Ordner löschen: Overlays kommen an seine Stelle auf die oberste Ebene
        const int at = l.folderIndex(uuid(101));
        QVERIFY(removeFolderFromLayout(&l, uuid(101)).isEmpty());
        QVERIFY(!l.folder(uuid(101)));
        QCOMPARE(l.entries.at(at).overlayId, uuid(2));
        QCOMPARE(l.entries.at(at + 1).overlayId, uuid(4));
        QCOMPARE(l.overlayIds().size(), 4);
        // Overlay hinzufügen nach einem anderen, auch im Ordner
        OverlayLayout s = sample();
        QVERIFY(addOverlayToLayout(&s, uuid(7), uuid(2)).isEmpty());
        QCOMPARE(s.folder(uuid(101))->overlays, (QList<QUuid>{uuid(2), uuid(7), uuid(3)}));
        QVERIFY(addOverlayToLayout(&s, uuid(8)).isEmpty());
        QCOMPARE(s.entries.last().overlayId, uuid(8));
        QVERIFY(!addOverlayToLayout(&s, uuid(8)).isEmpty());
        QVERIFY(removeOverlayFromLayout(&s, uuid(7)).isEmpty());
        QCOMPARE(s.folder(uuid(101))->overlays, (QList<QUuid>{uuid(2), uuid(3)}));
    }

    void dropTargets()
    {
        const OverlayLayout l = sample(); // A, F1(B, C), D, F2()
        QUuid folder;
        int index = -1;
        // D über A: ganz nach oben
        QVERIFY(overlayDropTarget(l, uuid(4), uuid(1), false, DropPosition::Above, &folder, &index));
        QVERIFY(folder.isNull());
        QCOMPARE(index, 0);
        // A auf Ordner F2: ans Ende von F2
        QVERIFY(overlayDropTarget(l, uuid(1), uuid(102), true, DropPosition::OnItem, &folder, &index));
        QCOMPARE(folder, uuid(102));
        QCOMPARE(index, 0);
        // A unter C (im Ordner F1)
        QVERIFY(overlayDropTarget(l, uuid(1), uuid(3), false, DropPosition::Below, &folder, &index));
        QCOMPARE(folder, uuid(101));
        QCOMPARE(index, 2);
        // B über F1: aus dem Ordner heraus vor den Ordner (A, B, F1, …)
        QVERIFY(overlayDropTarget(l, uuid(2), uuid(101), true, DropPosition::Above, &folder, &index));
        QVERIFY(folder.isNull());
        QCOMPARE(index, 1);
        // A ans Ende der Liste (leere Fläche)
        QVERIFY(overlayDropTarget(l, uuid(1), QUuid(), false, DropPosition::EndOfList, &folder, &index));
        QCOMPARE(index, 3); // ohne A: F1, D, F2
        OverlayLayout moved = l;
        QVERIFY(moveOverlayInLayout(&moved, uuid(1), folder, index).isEmpty());
        QCOMPARE(moved.entries.last().overlayId, uuid(1));
        // keine Änderung: A über F1 (steht schon dort), A auf sich selbst
        QVERIFY(!overlayDropTarget(l, uuid(1), uuid(101), true, DropPosition::Above, &folder, &index));
        QVERIFY(!overlayDropTarget(l, uuid(1), uuid(1), false, DropPosition::Below, &folder, &index));

        // Ordner F2 über A: ganz nach oben
        QVERIFY(folderDropTarget(l, uuid(102), uuid(1), false, DropPosition::Above, &index));
        QCOMPARE(index, 0);
        // Ordner F1 unter D
        QVERIFY(folderDropTarget(l, uuid(101), uuid(4), false, DropPosition::Below, &index));
        moved = l;
        QVERIFY(moveFolderInLayout(&moved, uuid(101), index).isEmpty());
        QCOMPARE(moved.entries.at(2).folder.id, uuid(101));
        // Ordner auf ein Overlay im Ordner: Ordner in Ordner ist nicht möglich
        QVERIFY(!folderDropTarget(l, uuid(102), uuid(2), false, DropPosition::Below, &index));
        // Ordner auf einen Ordner: daneben, nie hinein
        QVERIFY(folderDropTarget(l, uuid(102), uuid(101), true, DropPosition::OnItem, &index));
        QCOMPARE(index, 2);
    }

    void storeKeepsOrderAndFolders()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QUuid start, chat, alerts, folder;
        {
            OverlayStore store(dir.path());
            QString error;
            QVERIFY(store.load(&error));
            start = store.createOverlay(QStringLiteral("Überblick Start"), 1920, 1080, &error);
            chat = store.createOverlay(QStringLiteral("Chat"), 800, 600, &error);
            alerts = store.createOverlay(QStringLiteral("Alerts"), 1920, 1080, &error);
            QVERIFY2(!alerts.isNull(), qPrintable(error));
            // Neue Overlays kommen ans Ende der Liste, nicht nach Name.
            QCOMPARE(names(store), (QStringList{QStringLiteral("Überblick Start"), QStringLiteral("Chat"),
                                                QStringLiteral("Alerts")}));
            folder = store.createFolder(QStringLiteral("Szenen"), &error);
            QVERIFY2(!folder.isNull(), qPrintable(error));
            QVERIFY(store.moveOverlayInList(chat, folder, 0, &error));
            QVERIFY(store.setFolderExpanded(folder, false, &error));
            // Kopie direkt nach dem Original, im selben Ordner
            const QUuid copy = store.copyOverlay(chat, &error);
            QVERIFY(!copy.isNull());
            QCOMPARE(store.layout().folder(folder)->overlays, (QList<QUuid>{chat, copy}));
            QVERIFY(store.removeOverlay(copy, &error));
            QCOMPARE(store.layout().folder(folder)->overlays, QList<QUuid>{chat});
            QVERIFY(!store.moveFolderInList(QUuid::createUuid(), 0, &error));
        }
        {
            // Nach dem neuen Laden ist alles wie vorher.
            OverlayStore store(dir.path());
            QString error;
            QVERIFY(store.load(&error, OverlayStore::LoadMode::ReadOnly));
            QVERIFY(store.loadErrors().isEmpty());
            // Ordner am Ende angelegt, «Chat» hineingezogen: Start, Alerts, Szenen (Chat)
            QCOMPARE(store.layout().overlayIds(), (QList<QUuid>{start, alerts, chat}));
            QCOMPARE(store.layout().folder(folder)->name, QStringLiteral("Szenen"));
            QCOMPARE(store.layout().folder(folder)->expanded, false);
            QCOMPARE(store.layout().entries.size(), 3);
        }
        // Overlay fehlt in liste.json (zum Beispiel mit dss-cli bei angehaltenem Kern angelegt) und
        // ein Eintrag gehört zu keinem Overlay mehr.
        const QString listFile = dir.filePath(QStringLiteral("liste.json"));
        QJsonObject list = QJsonDocument::fromJson(readFile(listFile)).object();
        QJsonArray entries = list.value(QStringLiteral("entries")).toArray();
        entries.removeAt(0); // «Überblick Start» fehlt
        entries.append(QJsonObject{{QStringLiteral("kind"), QStringLiteral("overlay")},
                                   {QStringLiteral("id"), QStringLiteral("00000000-0000-4000-8000-000000000099")}});
        list.insert(QStringLiteral("entries"), entries);
        const QByteArray edited = QJsonDocument(list).toJson();
        QVERIFY(writeFile(listFile, edited));
        {
            OverlayStore store(dir.path());
            QString error;
            QVERIFY(store.load(&error));
            QVERIFY(store.loadErrors().isEmpty());
            QCOMPARE(store.layout().overlayIds(), (QList<QUuid>{alerts, chat, start}));
            QCOMPARE(readFile(listFile), edited); // Laden schreibt nicht
            // Nächste Änderung schreibt die bereinigte Liste.
            QVERIFY(store.renameFolder(folder, QStringLiteral("Szenen 2"), &error));
            OverlayLayout saved;
            QVERIFY(parseLayout(readFile(listFile), &saved, &error));
            QCOMPARE(saved.overlayIds(), (QList<QUuid>{alerts, chat, start}));
        }
    }

    void brokenFileIsReportedAndKept()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QUuid b, a;
        {
            OverlayStore store(dir.path());
            QString error;
            QVERIFY(store.load(&error));
            b = store.createOverlay(QStringLiteral("Beta"), 640, 360, &error);
            a = store.createOverlay(QStringLiteral("alpha"), 640, 360, &error);
            QVERIFY(!a.isNull());
        }
        const QString listFile = dir.filePath(QStringLiteral("liste.json"));
        const QByteArray broken = "{\"format_version\":1,\"entries\":[";
        QVERIFY(writeFile(listFile, broken));
        OverlayStore store(dir.path());
        QString error;
        QVERIFY(store.load(&error));
        QCOMPARE(store.loadErrors().size(), 1);
        QVERIFY2(store.loadErrors().first().startsWith(QStringLiteral("liste.json: kein gültiges JSON")),
                 qPrintable(store.loadErrors().first()));
        QVERIFY(!store.layoutError().isEmpty());
        // Rückfall: nach Name (ohne Gross-/Kleinschreibung)
        QCOMPARE(store.layout().overlayIds(), (QList<QUuid>{a, b}));
        // Änderungen der Liste werden abgewiesen, die Datei bleibt.
        QVERIFY(store.createFolder(QStringLiteral("F"), &error).isNull());
        QVERIFY2(error.contains(QStringLiteral("nicht überschrieben")), qPrintable(error));
        QVERIFY(!store.moveOverlayInList(a, QUuid(), 1, &error));
        // Overlays anlegen und löschen geht weiter, ohne liste.json zu schreiben.
        const QUuid c = store.createOverlay(QStringLiteral("Gamma"), 640, 360, &error);
        QVERIFY2(!c.isNull(), qPrintable(error));
        QVERIFY(store.layout().overlayIds().contains(c));
        QVERIFY(store.removeOverlay(b, &error));
        QCOMPARE(readFile(listFile), broken);
    }
};

QTEST_GUILESS_MAIN(TestOverlayListe)
#include "tst_overlayliste.moc"
