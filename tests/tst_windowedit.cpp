// Rechnungen für das Bearbeiten im Fenster (P08, Teile C bis E) ohne Qt Gui: Zeilen und
// Reihenfolge der Quellen, Adresse beim Umbenennen, Drehung, Farbe, Auswahl der Schrift.
// Schriften wie auf dem Studio-PC: die feste Aufnahme von fc-list (tests/daten/schriften) und
// die mitgelieferte Be Vietnam Pro.
// P10b: Dock «Medien» (Sortieren, Ziehen zwischen Ordnern, Löschen, Hinweise) und neue Quelle an
// einer Stelle der Liste «Quellen».

#include "medialibrary.h"
#include "windowdata.h"
#include "windowedit.h"

#include <QFile>
#include <QTest>

using namespace dss;

namespace {

QUuid uuid(int n)
{
    return QUuid(QStringLiteral("00000000-0000-4000-8000-%1").arg(n, 12, 10, QLatin1Char('0')));
}

// Overlay mit den Quellen A (hinten), B, C (vorne); im Dock: C, B, A.
Overlay threeSources()
{
    Overlay o;
    o.id = uuid(100);
    for (int i = 0; i < 3; ++i) {
        Source s;
        s.id = uuid(i + 1);
        s.name = QString(QLatin1Char('A' + i));
        o.sources.append(s);
    }
    return o;
}

QList<FontFace> studioFonts()
{
    QFile f(QStringLiteral(DSS_TEST_FONT_DATA "/fc-list.txt"));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QList<FontFace> faces = parseFontconfigOutput(f.readAll(), FontOrigin::System);
    QString error;
    faces.append(queryFontFiles(fontFilesIn(builtinFontDir()), FontOrigin::Builtin, &error));
    sortFontFaces(&faces);
    return faces;
}

} // namespace

class TestWindowEdit : public QObject
{
    Q_OBJECT

private slots:
    void rowsAndIndices()
    {
        const Overlay o = threeSources();
        QCOMPARE(sourceRows(o).first().id, uuid(3)); // vorderste oben
        QCOMPARE(sourceIndexForRow(o, 0), 2);
        QCOMPARE(sourceIndexForRow(o, 2), 0);
        QCOMPARE(sourceIndexForRow(o, 3), -1);
        QCOMPARE(sourceIndexForRow(o, -1), -1);
    }

    void dropIndex_data()
    {
        QTest::addColumn<int>("source");    // 1 = A, 2 = B, 3 = C
        QTest::addColumn<int>("insertRow"); // im Dock C, B, A; 0 = über C
        QTest::addColumn<int>("expected");  // -1 = nichts ändern
        QTest::newRow("A ganz nach oben") << 1 << 0 << 2;
        QTest::newRow("A zwischen C und B") << 1 << 1 << 1;
        QTest::newRow("A an seiner Stelle (über A)") << 1 << 2 << -1;
        QTest::newRow("A an seiner Stelle (unter A)") << 1 << 3 << -1;
        QTest::newRow("C ganz nach unten") << 3 << 3 << 0;
        QTest::newRow("C zwischen B und A") << 3 << 2 << 1;
        QTest::newRow("C über C") << 3 << 0 << -1;
        QTest::newRow("C unter C") << 3 << 1 << -1;
        QTest::newRow("B nach oben") << 2 << 0 << 2;
        QTest::newRow("B nach unten") << 2 << 3 << 0;
        QTest::newRow("ausserhalb") << 2 << 4 << -1;
        QTest::newRow("negativ") << 2 << -1 << -1;
    }
    void dropIndex()
    {
        QFETCH(int, source);
        QFETCH(int, insertRow);
        QFETCH(int, expected);
        const Overlay o = threeSources();
        const std::optional<int> index = sourceDropIndex(o, uuid(source), insertRow);
        QCOMPARE(index.value_or(-1), expected);
        if (index) {
            // So wie es der Kern mit move_source macht: Die Quelle steht danach an der Zeile,
            // an der sie losgelassen wurde.
            Overlay moved = o;
            QCOMPARE(moveSource(&moved, uuid(source), *index), QString());
            const int fromRow = 2 - sourceIndex(o, uuid(source));
            const int row = insertRow > fromRow ? insertRow - 1 : insertRow;
            QCOMPARE(sourceRows(moved).at(row).id, uuid(source));
        }
        QVERIFY(!sourceDropIndex(o, uuid(9), 0));
    }

    void stepIndex()
    {
        const Overlay o = threeSources();
        QCOMPARE(sourceStepIndex(o, uuid(1), +1).value_or(-1), 1);
        QCOMPARE(sourceStepIndex(o, uuid(1), -1).value_or(-1), -1); // schon ganz hinten
        QCOMPARE(sourceStepIndex(o, uuid(3), +1).value_or(-1), -1); // schon ganz vorne
        QCOMPARE(sourceStepIndex(o, uuid(3), -1).value_or(-1), 1);
        QCOMPARE(sourceStepIndex(o, uuid(9), +1).value_or(-1), -1);
        QCOMPARE(sourceStepIndex(o, uuid(2), 0).value_or(-1), -1);
    }

    // P10b, Teil B 6: neue Quelle an der Einfügestelle; sie steht danach an dieser Zeile
    void newSourceAtRow_data()
    {
        QTest::addColumn<int>("insertRow");
        QTest::addColumn<int>("expected");
        QTest::newRow("über der obersten") << 0 << 3;
        QTest::newRow("zwischen C und B") << 1 << 2;
        QTest::newRow("zwischen B und A") << 2 << 1;
        QTest::newRow("unter der untersten") << 3 << 0;
        QTest::newRow("zu weit unten") << 9 << 0;
        QTest::newRow("zu weit oben") << -1 << 3;
    }

    void newSourceAtRow()
    {
        QFETCH(int, insertRow);
        QFETCH(int, expected);
        Overlay o = threeSources();
        const int index = newSourceIndex(o, insertRow);
        QCOMPARE(index, expected);
        // Wie der Kern: zuerst zuoberst anhängen, dann an index verschieben
        Source added;
        added.id = uuid(4);
        o.sources.append(added);
        QCOMPARE(moveSource(&o, added.id, index), QString());
        QCOMPARE(sourceRows(o).at(std::clamp(insertRow, 0, 3)).id, added.id);
        QCOMPARE(newSourceIndex(Overlay(), 0), 0);
    }

    // P10b, Teil B 1: Ordner zuerst in beiden Richtungen, Namen ohne Gross- und Kleinschreibung
    void mediaSorting()
    {
        const MediaSortKey folderB{true, QStringLiteral("b"), QStringLiteral("Ordner"), 0, QStringLiteral("1")};
        const MediaSortKey folderZ{true, QStringLiteral("Z"), QStringLiteral("Ordner"), 0, QStringLiteral("2")};
        const MediaSortKey bigImage{false, QStringLiteral("a.png"), QStringLiteral("Bild"), 5000, QStringLiteral("m1")};
        const MediaSortKey smallVideo{false, QStringLiteral("B.webm"), QStringLiteral("Video"), 10, QStringLiteral("m2")};
        const MediaSortKey font{false, QStringLiteral("c.ttf"), QStringLiteral("Schrift"), 10, QStringLiteral("m3")};
        for (MediaColumn column : {MediaColumn::Name, MediaColumn::Kind, MediaColumn::Size}) {
            for (bool ascending : {true, false}) {
                QVERIFY(mediaRowBefore(folderZ, bigImage, column, ascending));
                QVERIFY(!mediaRowBefore(bigImage, folderZ, column, ascending));
            }
        }
        QVERIFY(mediaRowBefore(folderB, folderZ, MediaColumn::Name, true));
        QVERIFY(mediaRowBefore(folderZ, folderB, MediaColumn::Name, false));
        QVERIFY(mediaRowBefore(bigImage, smallVideo, MediaColumn::Name, true));
        QVERIFY(mediaRowBefore(smallVideo, bigImage, MediaColumn::Name, false));
        QVERIFY(mediaRowBefore(smallVideo, bigImage, MediaColumn::Size, true));
        QVERIFY(mediaRowBefore(bigImage, smallVideo, MediaColumn::Size, false));
        // gleiche Grösse: nach Name
        QVERIFY(mediaRowBefore(smallVideo, font, MediaColumn::Size, true));
        QVERIFY(mediaRowBefore(bigImage, font, MediaColumn::Kind, true)); // Bild vor Schrift
        QVERIFY(mediaRowBefore(font, smallVideo, MediaColumn::Kind, true)); // Schrift vor Video
        QVERIFY(mediaRowBefore(smallVideo, font, MediaColumn::Kind, false));
        // ganz gleich: nach Verweis, in beiden Richtungen gleich
        MediaSortKey same = bigImage;
        same.tieBreak = QStringLiteral("m0");
        QVERIFY(mediaRowBefore(same, bigImage, MediaColumn::Name, true));
        QVERIFY(mediaRowBefore(same, bigImage, MediaColumn::Name, false));
        QVERIFY(!mediaRowBefore(bigImage, bigImage, MediaColumn::Name, true));
    }

    // P10b, Teil B 3: Ordner A (darin A1, darin A2), Ordner B; Datei x in A, Datei y oben
    static MediaListData mediaTree()
    {
        MediaListData m;
        m.folders = {{uuid(10), QStringLiteral("A"), QUuid(), true},
                     {uuid(11), QStringLiteral("A1"), uuid(10), true},
                     {uuid(12), QStringLiteral("A2"), uuid(11), false},
                     {uuid(13), QStringLiteral("B"), QUuid(), true}};
        MediaFileData x;
        x.entry = {QStringLiteral("media/x.png"), QStringLiteral("x"), uuid(10)};
        MediaFileData y;
        y.entry = {QStringLiteral("media/y.png"), QStringLiteral("y"), QUuid()};
        m.files = {x, y};
        return m;
    }

    void mediaDrop()
    {
        const MediaListData m = mediaTree();
        const QString x = QStringLiteral("media/x.png");
        const QString y = QStringLiteral("media/y.png");
        const auto file = [&](const QString &f, bool targetIsFolder, const QUuid &tf, const QString &tfile,
                              DropPosition p) { return mediaDropFolder(m, QUuid(), f, targetIsFolder, tf, tfile, p); };
        const auto folder = [&](const QUuid &f, bool targetIsFolder, const QUuid &tf, const QString &tfile,
                                DropPosition p) { return mediaDropFolder(m, f, QString(), targetIsFolder, tf, tfile, p); };
        // Datei auf einen Ordner: hinein
        QCOMPARE(file(x, true, uuid(13), {}, DropPosition::OnItem), std::optional<QUuid>(uuid(13)));
        QCOMPARE(file(y, true, uuid(12), {}, DropPosition::OnItem), std::optional<QUuid>(uuid(12)));
        // in den eigenen Ordner: nichts
        QCOMPARE(file(x, true, uuid(10), {}, DropPosition::OnItem), std::nullopt);
        // über oder unter einem Ordner: in dessen Elternordner
        QCOMPARE(file(y, true, uuid(11), {}, DropPosition::Above), std::optional<QUuid>(uuid(10)));
        QCOMPARE(file(x, true, uuid(13), {}, DropPosition::Below), std::optional<QUuid>(QUuid()));
        // auf, über oder unter einer Datei: in deren Ordner
        QCOMPARE(file(y, false, {}, x, DropPosition::OnItem), std::optional<QUuid>(uuid(10)));
        QCOMPARE(file(x, false, {}, y, DropPosition::Below), std::optional<QUuid>(QUuid()));
        QCOMPARE(file(x, false, {}, x, DropPosition::Above), std::nullopt);
        // ans Ende: oberste Ebene
        QCOMPARE(file(x, false, {}, {}, DropPosition::EndOfList), std::optional<QUuid>(QUuid()));
        QCOMPARE(file(y, false, {}, {}, DropPosition::EndOfList), std::nullopt);
        // Ordner: nicht in sich selbst oder einen Unterordner
        QCOMPARE(folder(uuid(10), true, uuid(10), {}, DropPosition::OnItem), std::nullopt);
        QCOMPARE(folder(uuid(10), true, uuid(12), {}, DropPosition::OnItem), std::nullopt);
        QCOMPARE(folder(uuid(10), true, uuid(12), {}, DropPosition::Above), std::nullopt); // in A1
        QCOMPARE(folder(uuid(10), false, {}, x, DropPosition::OnItem), std::nullopt);    // in A
        QCOMPARE(folder(uuid(12), true, uuid(13), {}, DropPosition::OnItem), std::optional<QUuid>(uuid(13)));
        QCOMPARE(folder(uuid(12), false, {}, {}, DropPosition::EndOfList), std::optional<QUuid>(QUuid()));
        QCOMPARE(folder(uuid(11), true, uuid(10), {}, DropPosition::OnItem), std::nullopt); // schon dort
        QCOMPARE(folder(uuid(13), true, uuid(11), {}, DropPosition::OnItem), std::optional<QUuid>(uuid(11)));
        // Unbekanntes: nichts
        QCOMPARE(file(QStringLiteral("media/z.png"), true, uuid(13), {}, DropPosition::OnItem), std::nullopt);
        QCOMPARE(folder(uuid(99), true, uuid(13), {}, DropPosition::OnItem), std::nullopt);
        QCOMPARE(file(x, true, uuid(99), {}, DropPosition::OnItem), std::nullopt);
    }

    void mediaSelection()
    {
        MediaListData m = mediaTree();
        m.files[0].usedIn = {{uuid(100), QStringLiteral("Szene"), uuid(1), QStringLiteral("Logo")}};
        m.files[1].kind = MediaKind::Video;
        m.files[1].bytes = 2048;
        m.files[1].codec = QStringLiteral("hevc");
        m.files[1].browserProblem = QStringLiteral("y (HEVC) wird in der Browserquelle nicht angezeigt. In H.264 oder VP9 umwandeln.");
        const QList<ImportProgressData> imports{{7, QStringLiteral("gross.webm"), QUuid(), 37}};

        MediaSelectionInfo i = mediaSelectionInfo(m, imports, QString(), QUuid(), 0);
        QVERIFY(!i.removable);
        QCOMPARE(i.line, QString());
        i = mediaSelectionInfo(m, imports, QStringLiteral("media/x.png"), QUuid(), 0);
        QVERIFY(!i.removable);
        QCOMPARE(i.line, QStringLiteral("Verwendet in Overlay «Szene» (Quelle «Logo»)"));
        QCOMPARE(i.removeToolTip, QStringLiteral("«x» lässt sich nicht löschen, verwendet in Overlay «Szene» (Quelle «Logo»)."));
        i = mediaSelectionInfo(m, imports, QStringLiteral("media/y.png"), QUuid(), 0);
        QVERIFY(i.removable);
        QCOMPARE(i.line, QStringLiteral("Ungenutzt · ") + m.files[1].browserProblem);
        i = mediaSelectionInfo(m, imports, QString(), uuid(10), 0);
        QVERIFY(i.removable);
        QCOMPARE(i.line, QStringLiteral("Ordner «A»: 1 Datei(en)"));
        i = mediaSelectionInfo(m, imports, QString(), uuid(12), 0);
        QCOMPARE(i.line, QStringLiteral("Ordner «A/A1/A2»: 0 Datei(en)"));
        m.error = QStringLiteral("medien.json ungültig");
        QVERIFY(!mediaSelectionInfo(m, imports, QString(), uuid(10), 0).removable);
        i = mediaSelectionInfo(m, imports, QString(), QUuid(), 7);
        QVERIFY(i.removable);
        QCOMPARE(i.removeToolTip, QStringLiteral("Übernehmen von «gross.webm» abbrechen"));
        QCOMPARE(i.line, QStringLiteral("«gross.webm» wird übernommen (37 %)."));
        QVERIFY(!mediaSelectionInfo(m, imports, QString(), QUuid(), 8).removable); // schon fertig

        QCOMPARE(mediaFileToolTip(m.files[1]),
                 QStringLiteral("y\nVideo · 2.0 KiB · Format hevc\nmedia/y.png\nUngenutzt\n") + m.files[1].browserProblem);
        QCOMPARE(mediaFileToolTip(m.files[0]),
                 QStringLiteral("x\nBild · 0 Byte\nmedia/x.png\nVerwendet in Overlay «Szene» (Quelle «Logo»)"));
        QCOMPARE(importProgressText(imports.first()), QStringLiteral("übernehme … 37 %"));
        QCOMPARE(mediaLibraryFileFilter(),
                 QStringLiteral("Mediendateien (*.png *.jpg *.jpeg *.webm *.mp4 *.ttf *.otf);;Bilder (*.png *.jpg *.jpeg);;"
                                "Videos (*.webm *.mp4);;Schriften (*.ttf *.otf)"));
    }

    void renameAddress()
    {
        const QList<OverlaySummary> overlays = {
            {uuid(1), QStringLiteral("Überblick Start"), QStringLiteral("ueberblick-start"), 1920, 1080, 0},
            {uuid(2), QStringLiteral("Chat"), QStringLiteral("chat"), 800, 600, 0},
        };
        QString address;
        // Gleiche Adresse aus dem neuen Namen: keine Frage
        QVERIFY(!renameChangesAddress(overlays, uuid(1), QStringLiteral("Überblick  START"), &address));
        QCOMPARE(address, QStringLiteral("ueberblick-start"));
        QVERIFY(renameChangesAddress(overlays, uuid(1), QStringLiteral("Pause"), &address));
        QCOMPARE(address, QStringLiteral("pause"));
        // Adresse eines anderen Overlays ist besetzt → eindeutig gemacht
        QVERIFY(renameChangesAddress(overlays, uuid(1), QStringLiteral("Chat"), &address));
        QCOMPARE(address, QStringLiteral("chat-2"));
        // Die eigene Adresse zählt nicht als besetzt
        QVERIFY(!renameChangesAddress(overlays, uuid(2), QStringLiteral("CHAT"), &address));
        QCOMPARE(address, QStringLiteral("chat"));
    }

    void firstOverlayInList()
    {
        OverlayLayout l;
        QCOMPARE(firstOverlay(l), QUuid());
        l = layoutFromIds({uuid(1), uuid(2)});
        QVERIFY(createFolderInLayout(&l, uuid(10), QStringLiteral("Szenen")).isEmpty());
        QVERIFY(moveFolderInLayout(&l, uuid(10), 0).isEmpty());
        QCOMPARE(firstOverlay(l), uuid(1)); // leerer Ordner zuerst
        QVERIFY(moveOverlayInLayout(&l, uuid(2), uuid(10), 0).isEmpty());
        QCOMPARE(firstOverlay(l), uuid(2)); // im Ordner zuerst
    }

    void rotation()
    {
        QCOMPARE(rotationTenths(0.0), 0);
        QCOMPARE(rotationTenths(12.5), 125);
        QCOMPARE(rotationTenths(359.9), 3599);
        QCOMPARE(rotationTenths(360.0), 0);
        QCOMPARE(rotationTenths(0.04), 0);
        QCOMPARE(rotationTenths(0.06), 1);
        QCOMPARE(rotationTenths(-0.1), 3599);
        for (int t = 0; t < 3600; ++t)
            QCOMPARE(rotationTenths(rotationDegrees(t)), t);
    }

    void colors()
    {
        QCOMPARE(colorText(255, 128, 0, 128), QStringLiteral("#FF800080"));
        QCOMPARE(colorText(0, 0, 0, 0), QStringLiteral("#00000000"));
        QCOMPARE(colorText(300, -5, 15, 255), QStringLiteral("#FF000FFF"));
        int r = 0, g = 0, b = 0, a = 0;
        QVERIFY(colorParts(QStringLiteral("#11a2B3c4"), &r, &g, &b, &a));
        QCOMPARE(r, 0x11);
        QCOMPARE(g, 0xA2);
        QCOMPARE(b, 0xB3);
        QCOMPARE(a, 0xC4);
        QVERIFY(!colorParts(QStringLiteral("#112233"), &r, &g, &b, &a));
        QVERIFY(!colorParts(QStringLiteral("rot"), &r, &g, &b, &a));
        QCOMPARE(colorText(r, g, b, a), QStringLiteral("#11A2B3C4"));
    }

    void keysAndRequests()
    {
        QCOMPARE(changeKey(uuid(1), QStringLiteral("x")),
                 QStringLiteral("00000000-0000-4000-8000-000000000001/x"));
        SourceChanges c;
        c.y = 5;
        const ChangeRequest r = sourceUpdate(uuid(100), uuid(1), c);
        QCOMPARE(r.type, QStringLiteral("update_source"));
        QCOMPARE(r.overlayId, uuid(100));
        QCOMPARE(r.sourceId, uuid(1));
        QCOMPARE(r.changes, c);
        QCOMPARE(mediaFileFilter(MediaUse::Image), QStringLiteral("Bilder (*.png *.jpg *.jpeg)"));
        QCOMPARE(mediaFileFilter(MediaUse::Video), QStringLiteral("Videos (*.webm *.mp4)"));
        QCOMPARE(mediaFileFilter(MediaUse::Font), QStringLiteral("Schriften (*.ttf *.otf)"));
        QCOMPARE(newOverlayName(), QStringLiteral("Neues Overlay"));
        QCOMPARE(newFolderName(), QStringLiteral("Neuer Ordner"));
    }

    void fontChoices()
    {
        const QList<FontFace> faces = studioFonts();
        QVERIFY(faces.size() > 100);
        // Be Vietnam Pro: 9 Gewichte, aufrecht und kursiv
        const QList<WeightChoice> bvp = weightChoices(faces, QStringLiteral("Be Vietnam Pro"));
        QCOMPARE(bvp.size(), 9);
        QCOMPARE(bvp.first(), (WeightChoice{100, QStringLiteral("Thin (100)")}));
        QCOMPARE(bvp.at(3), (WeightChoice{400, QStringLiteral("Regular (400)")}));
        QCOMPARE(bvp.last(), (WeightChoice{900, QStringLiteral("Black (900)")}));
        QVERIFY(italicSelectable(faces, QStringLiteral("Be Vietnam Pro")));
        // DejaVu Sans (fonts-dejavu-core): nur Book 400 und Bold 700, keine schrägen Schnitte;
        // DejaVu Sans Mono hat dazu Oblique und Bold Oblique.
        const QList<WeightChoice> dejavu = weightChoices(faces, QStringLiteral("DejaVu Sans"));
        QCOMPARE(dejavu, (QList<WeightChoice>{{400, QStringLiteral("Book (400)")},
                                               {700, QStringLiteral("Bold (700)")}}));
        QVERIFY(!italicSelectable(faces, QStringLiteral("DejaVu Sans")));
        QVERIFY(italicSelectable(faces, QStringLiteral("DejaVu Sans Mono")));
        // Unbekannte Familie
        QVERIFY(weightChoices(faces, QStringLiteral("Gibt es nicht")).isEmpty());
        QVERIFY(!italicSelectable(faces, QStringLiteral("Gibt es nicht")));

        // Nur kursive Schnitte: Name ohne «Italic»
        const QList<FontFace> onlyItalic = {
            {QStringLiteral("X"), QStringLiteral("Bold Italic"), 700, true, QStringLiteral("/a.ttf"), FontOrigin::System},
            {QStringLiteral("X"), QStringLiteral("Italic"), 400, true, QStringLiteral("/b.ttf"), FontOrigin::System},
        };
        const QList<WeightChoice> x = weightChoices(onlyItalic, QStringLiteral("X"));
        QCOMPARE(x.size(), 2);
        QCOMPARE(x.at(0).label, QStringLiteral("Regular (400)"));
        QCOMPARE(x.at(1).label, QStringLiteral("Bold (700)"));
        QVERIFY(!italicSelectable(onlyItalic, QStringLiteral("X")));

        // Familie einer Datei und Änderung für eine Auswahl
        const QString regular = defaultTextFontFile();
        QCOMPARE(familyOfFont(faces, regular), QStringLiteral("Be Vietnam Pro"));
        QCOMPARE(familyOfFont(faces, QStringLiteral("media/unbekannt.ttf")), QString());
        std::optional<ChangeRequest> r =
            fontChange(faces, uuid(100), uuid(1), QStringLiteral("Be Vietnam Pro"), 600, true);
        QVERIFY(r);
        QCOMPARE(r->type, QStringLiteral("set_text_font"));
        QVERIFY(r->file.endsWith(QStringLiteral("/BeVietnamPro-SemiBoldItalic.ttf")));
        QCOMPARE(r->weight, 600);
        QCOMPARE(r->italic, true);
        // Nächstes Gewicht, das es gibt: 550 liegt gleich weit von 400 und 700, bei Gleichstand
        // gilt das schwerere.
        r = fontChange(faces, uuid(100), uuid(1), QStringLiteral("DejaVu Sans"), 550, false);
        QVERIFY(r);
        QCOMPARE(r->weight, 700);
        QCOMPARE(r->italic, false);
        QVERIFY(!fontChange(faces, uuid(100), uuid(1), QStringLiteral("Gibt es nicht"), 400, false));
    }
};

QTEST_GUILESS_MAIN(TestWindowEdit)
#include "tst_windowedit.moc"
