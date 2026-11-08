// P11, K-08: Bildvergleich der zwei Zeichner mit fester Grenze. Der Test legt die Overlays «Bild»,
// «Text», «Videofarbe», «WebM» und «Gesamt» zur Laufzeit an (OverlayStore, Medien aus
// tests/daten/bildvergleich/, ../exif/, Schriften), startet einen eigenen Kern und vergleicht je
// Overlay das Bild der Seite in Qt WebEngine (Chromium) mit dem Zeichner des Editors (dss_render).
// Masszahlen je Bereich und Quellenart nach tests/bildvergleich.h. Seite–Editor muss in jedem
// Bereich unter der Grenze bleiben (kKindLimits je Quellenart, kKnown je Fall, siehe dort); sonst
// scheitert der Test mit Bereich und Zahlen, Seite, Editor und Differenzbild liegen in der Ausgabe.
// absichtlicheFehler: Jeder absichtliche Fehler (Plan P11, Abschnitt 4) muss mindestens das Doppelte
// der Grenze erreichen; «falsches Schriftgewicht» erreicht es nicht und steht als QEXPECT_FAIL da
// ([OFFEN], Punkt 101). Der Test scheitert ausserdem, wenn ein Bild fehlt, die Grösse nicht stimmt,
// ein Video mit «Am Ende anhalten» nicht das letzte Bild zeigt (K-05) oder die Ausrichtung eines
// WebM nicht stimmt (Punkt 80).
//
// Chromium läuft in einem echten X11-Fenster ausserhalb des Bildschirms, ohne Fenstermanager und
// ohne Fokus (wie P02, M2 «ausserhalb»; Regel 6). Abgriff wie tst_browserquelle.
//
// Ausgabe unter build/tests/bildvergleich/ (DSS_BILDVERGLEICH_AUSGABE), damit jeder Lauf seine
// Werte behält:
//   werte.tsv            jede Masszahl jedes Laufs (angehängt, erste Spalte = Lauf)
//   zusammenfassung.tsv  kleinster und grösster Wert je Masszahl über die letzten 30 Läufe
//   felder.tsv           Farbe jedes Farbfelds je Video im letzten Lauf (Soll, Editor, Seite, OBS)
//   <Overlay>-seite.png, -editor.png, -differenz.png (bei OBS dazu -obs.png, -differenz-obs.png)
//
// OBS (echtes OBS nur mit DSS_BILDVERGLEICH_OBS):
//   DSS_BILDVERGLEICH_OBS="<Adresse>[:<Port>];<Szene>;<Zugangsdatei>;<Name der Zugangsdatei>"
// Der Test liest das Passwort mit systemd-creds (ObsSecretStore mit diesem Namen), legt in der Szene
// die Browserquelle «DSS Bildvergleich» an, stellt sie nacheinander auf jedes Overlay, holt
// GetSourceScreenshot und vergleicht mit Editor und Qt WebEngine. Am Ende löscht er die eigene Quelle
// wieder (RemoveInput). Das Passwort kommt nie in die Ausgabe (Regel 12). Die Farben im Bild von
// GetSourceScreenshot sind schon vormultipliziert und werden so gelesen (bv::fromObsScreenshot).
// Ohne die Variable läuft derselbe Weg gegen den Testserver (obstestserver.h) mit Szene «test 2» und
// eigener Zugangsdatei mit Zufallspasswort; «OBS» liefert dort das Bild der Seite (Werte als
// «Testserver–…»). So fällt ein Fehler, der nur mit OBS auftritt, schon in ctest auf.
// Overlay «Weitere Formate» (nur notieren): HEVC in MP4, zur Laufzeit erzeugt. ProRes gibt es nur in
// MOV und wird vom Studio nicht angenommen (nur webm, mp4); der Test prüft das und schreibt es ins
// Protokoll.
//
// Quellen:
//   obs-websocket: https://raw.githubusercontent.com/obsproject/obs-websocket/master/docs/generated/protocol.md
//     (GetVersion, GetSceneList, CreateInput, SetInputSettings, GetSourceScreenshot, RemoveInput;
//     gelesen 2026-11-08). Die Form von imageData nennt die Doku nicht (nur «Base64-encoded»);
//     der Test nimmt Base64 mit und ohne Vorsatz «data:…;base64,».
//   QWebEngineView https://doc.qt.io/qt-6.8/qwebengineview.html,
//   QQuickWidget::grabFramebuffer https://doc.qt.io/qt-6.8/qquickwidget.html#grabFramebuffer,
//   Qt::X11BypassWindowManagerHint, Qt::WA_ShowWithoutActivating
//     https://doc.qt.io/qt-6.8/qt.html#WindowType-enum, https://doc.qt.io/qt-6.8/qt.html#WidgetAttribute-enum
//   QWebSocketHandshakeOptions https://doc.qt.io/qt-6.8/qwebsockethandshakeoptions.html

#include "bildvergleich.h"
#include "coreclient.h"
#include "coresettings.h"
#include "mediainfo.h"
#include "obsprotocol.h"
#include "obssecretstore.h"
#include "obstestserver.h"
#include "overlayimage.h"
#include "overlaystore.h"
#include "prozesshilfe.h"
#include "sourcerenderer.h"
#include "version.h"

#include <QApplication>
#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QNetworkProxy>
#include <QProcess>
#include <QQuickWidget>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QWebSocket>
#include <QWebSocketHandshakeOptions>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <functional>

using namespace dss;
using namespace testhelp;
namespace bv = bildvergleich;

namespace {

const QString kData = QStringLiteral(DSS_TEST_BILDVERGLEICH_DIR);
const QString kExif = QStringLiteral(DSS_TEST_EXIF_DIR);
const QString kOut = QStringLiteral(DSS_BILDVERGLEICH_AUSGABE);
const QString kUrbanist = QStringLiteral(DSS_TEST_VARIABLE_FONT);
const QString kVietnam = QStringLiteral(DSS_TEST_BUILTIN_FONT_DIR);
constexpr int kSummaryRuns = 30;
constexpr int kHoldingState = 2; // dss::VideoPlayer::State::Holding
const QString kObsInputName = QStringLiteral("DSS Bildvergleich");

// Farben der Felder wie tests/daten/bildvergleich/erzeugen.sh
const char *const kFieldColors[4][8] = {
    {"#FF0000", "#00FF00", "#0000FF", "#00FFFF", "#FF00FF", "#FFFF00", "#FFFFFF", "#000000"},
    {"#BFBFBF", "#BFBF00", "#00BFBF", "#00BF00", "#BF00BF", "#BF0000", "#0000BF", "#202020"},
    {"#000000", "#242424", "#494949", "#6D6D6D", "#929292", "#B6B6B6", "#DBDBDB", "#FFFFFF"},
    {"#FFDBAC", "#F1C27D", "#E0AC69", "#C68642", "#8D5524", "#5C3836", "#3F7F3F", "#3F3F7F"}};

struct Region
{
    QString name;
    QString kind; // Bild, Text, Video, WebM
    QRect rect;
};

// Feste Grenze Seite–Editor (K-08). Gemessen am 2026-11-08 mit 30 Läufen (Bericht P11):
// Grenze = grösster Wert + Reserve, aufgerundet. Reserve: Mittel × 1.25 (mindestens + 0.1, auf 0.1
// aufgerundet), grösste Abweichung ausserhalb der Kanten × 1.25 (mindestens + 2, ganze Zahl), Anteil
// über 32 an den Kanten × 1.25 (mindestens + 1, ganze Prozent). Eine neue Grenze nur mit Commit und
// Begründung im Bericht (Plan P11, Abschnitt 4).
struct Limit
{
    double mean = 0;
    int maxOutsideEdges = 0;
    double edgeOver32 = 0;
};

// Je Quellenart, für jeden Bereich dieser Art ohne eigenen Eintrag in kKnown
const QHash<QString, Limit> kKindLimits = {
    {QStringLiteral("Bild"), {1.5, 4, 13}},  // gemessen 1.124 (verkleinert), 2, 10.00 % (gedreht 12.5°)
    {QStringLiteral("Text"), {2.0, 4, 21}},  // 1.534 (Gesamt, Text über dem Video), 2, 16.73 % (Be Vietnam Pro gedreht)
    {QStringLiteral("Video"), {1.6, 6, 7}},  // 1.250 (bildwechsel.mp4), 4, 5.37 % (Gesamt, Video BT.709 1280x720)
};

// Bekannte Werte je Fall («Overlay|Bereich»), ebenfalls aus den 30 Läufen mit derselben Reserve:
// - Videos, bei denen Qt WebEngine (Chromium 122) anders rechnet als die Browserquelle in OBS
//   (Chromium 146), die massgebend ist (Entscheid des Nutzers 2026-11-08, Frage 2: A); der Editor
//   folgt OBS (Prüfung V): H.264 unter 720 Pixeln oder mit SMPTE170M, VP9 ohne Farbangabe (auch die
//   WebM mit Drehangabe und mit Alpha).
// - Urbanist 800 und Be Vietnam Pro klein: Der Editor weicht von beiden Browsern ab (Prüfung V:
//   OBS–Editor bis 6.5, OBS–Seite 0.6 bis 2.4), Ursache [OFFEN] (Punkt 96).
// - «ganzes Overlay» je Overlay (enthält die Fälle oben).
// Kommentar: gemessen Mittel, grösste ausserhalb Kanten, über 32 an Kanten (in allen 30 Läufen gleich).
const QHash<QString, Limit> kKnown = {
    {QStringLiteral("Videofarbe|h264-640x360-ohne.mp4"), {6.0, 88, 32}},     // 4.746, 70, 24.95 %
    {QStringLiteral("Videofarbe|h264-1280x718-ohne.mp4"), {5.7, 88, 27}},    // 4.538, 70, 21.24 %
    {QStringLiteral("Videofarbe|h264-1920x1080-bt601.mp4"), {5.7, 88, 32}},  // 4.541, 70, 24.99 %
    {QStringLiteral("Videofarbe|vp9-640x360-ohne.webm"), {4.7, 50, 15}},     // 3.732, 40, 11.86 %
    {QStringLiteral("Videofarbe|vp9-1920x1080-ohne.webm"), {4.0, 40, 1}},    // 3.154, 32, 0.00 %
    {QStringLiteral("WebM|drehung-90.webm"), {8.7, 49, 46}},                 // 6.936, 39, 36.30 %
    {QStringLiteral("WebM|drehung-180.webm"), {8.8, 49, 49}},                // 7.009, 39, 38.71 %
    {QStringLiteral("WebM|drehung-270.webm"), {8.7, 49, 48}},                // 6.933, 39, 37.62 %
    {QStringLiteral("WebM|alpha.webm 1:1"), {5.4, 48, 29}},                  // 4.271, 38, 22.76 %
    {QStringLiteral("WebM|alpha.webm vergrössert"), {5.4, 49, 27}},          // 4.246, 39, 21.24 %
    {QStringLiteral("Gesamt|WebM mit Alpha"), {5.4, 49, 27}},                // 4.246, 39, 21.24 %
    {QStringLiteral("Text|Urbanist 800 Mitte"), {7.8, 2, 35}},               // 6.193, 0, 27.86 %
    {QStringLiteral("Text|Be Vietnam Pro klein"), {7.6, 2, 35}},             // 6.009, 0, 27.41 %
    {QStringLiteral("Bild|ganzes Overlay"), {0.3, 4, 3}},                    // 0.108, 2, 1.34 %
    {QStringLiteral("Text|ganzes Overlay"), {1.8, 2, 20}},                   // 1.422, 0, 15.93 %
    {QStringLiteral("Videofarbe|ganzes Overlay"), {2.1, 88, 10}},            // 1.607, 70, 7.34 %
    {QStringLiteral("WebM|ganzes Overlay"), {1.7, 49, 29}},                  // 1.295, 39, 22.60 %
    {QStringLiteral("Gesamt|ganzes Overlay"), {0.8, 49, 6}},                 // 0.618, 39, 4.73 %
};

// Nur notieren (Entscheid des Nutzers 2026-11-08): HEVC spielt kein Browser ab, ohne Grenze.
const QString kNotedOnly = QStringLiteral("Weitere Formate");
// Video mit Farbfeldern: Kasten im Overlay und Grösse der Datei
struct FieldVideo
{
    QString name;
    QRect box;
    QSize size;
};

struct TestOverlay
{
    QString name;
    QUuid id;
    QSize size;
    int videos = 0;
    int texts = 0;
    QList<Region> regions;
    QList<FieldVideo> fieldVideos;
    QList<Region> oriented; // Ausrichtung ablesen: Viertel rot, grün, blau, gelb
    QRect lastFrame;        // K-05: bildwechsel.mp4, letztes Bild Grau 128
};

// Absichtlicher Fehler «BT.601 statt BT.709» (deckende Farbe): nach Y'CbCr mit BT.709 und mit BT.601
// zurück, wie der Editor vor Punkt 79 ein BT.709-Video zeigte. Der Bereich hebt sich auf.
// Kr/Kb: ITU-R BT.709-6 (0.2126/0.0722), BT.601-7 (0.299/0.114)
QRgb bt709AsBt601(QRgb c)
{
    const double r = qRed(c) / 255.0, g = qGreen(c) / 255.0, b = qBlue(c) / 255.0;
    const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    const double pb = (b - y) / (2 * (1 - 0.0722));
    const double pr = (r - y) / (2 * (1 - 0.2126));
    const double R = y + 2 * (1 - 0.299) * pr;
    const double B = y + 2 * (1 - 0.114) * pb;
    const double G = (y - 0.299 * R - 0.114 * B) / (1 - 0.299 - 0.114);
    const auto to8 = [](double v) { return std::clamp(int(std::lround(255 * v)), 0, 255); };
    return qRgba(to8(R), to8(G), to8(B), qAlpha(c));
}

QString rgbText(QRgb p)
{
    return QStringLiteral("%1/%2/%3").arg(qRed(p)).arg(qGreen(p)).arg(qBlue(p));
}

// Mittel über 5 x 5 Pixel um p (vormultipliziert; die Felder sind deckend)
QRgb sample(const QImage &image, QPoint p)
{
    int r = 0, g = 0, b = 0, a = 0, n = 0;
    for (int y = p.y() - 2; y <= p.y() + 2; ++y) {
        for (int x = p.x() - 2; x <= p.x() + 2; ++x) {
            if (!image.rect().contains(x, y))
                continue;
            const QRgb c = image.pixel(x, y);
            r += qRed(c);
            g += qGreen(c);
            b += qBlue(c);
            a += qAlpha(c);
            ++n;
        }
    }
    return n ? qRgba(r / n, g / n, b / n, a / n) : 0;
}

// Ausrichtung als Text: nächste Farbe der Viertel (links oben, rechts oben, links unten, rechts unten)
QString orientation(const QImage &image, const QRect &box)
{
    static const std::pair<QRgb, const char *> colors[] = {
        {qRgb(255, 0, 0), "rot"}, {qRgb(0, 255, 0), "grün"}, {qRgb(0, 0, 255), "blau"}, {qRgb(255, 255, 0), "gelb"}};
    QStringList parts;
    for (const QPointF at : {QPointF(0.25, 0.25), QPointF(0.75, 0.25), QPointF(0.25, 0.75), QPointF(0.75, 0.75)}) {
        const QRgb c = sample(image, QPoint(box.x() + int(box.width() * at.x()), box.y() + int(box.height() * at.y())));
        if (qAlpha(c) < 128) {
            parts << QStringLiteral("leer");
            continue;
        }
        int best = INT_MAX;
        QString name;
        for (const auto &[color, text] : colors) {
            const int d = std::abs(qRed(c) - qRed(color)) + std::abs(qGreen(c) - qGreen(color))
                          + std::abs(qBlue(c) - qBlue(color));
            if (d < best) {
                best = d;
                name = QString::fromUtf8(text);
            }
        }
        parts << name;
    }
    return parts.join(QLatin1Char(' '));
}

// Masszahlen: Ausgabe, angehängte Datei je Lauf, Zusammenfassung über die letzten Läufe
class Recorder
{
public:
    Recorder()
        : m_run(QDateTime::currentDateTime().toString(Qt::ISODateWithMs) + QLatin1Char('-')
                + QString::number(QCoreApplication::applicationPid()))
    {
        QDir().mkpath(kOut);
    }

    QString run() const { return m_run; }

    void add(const QString &comparison, const QString &overlay, const QString &region, const QString &kind,
             const bv::Measure &m)
    {
        qInfo("%s | %s | %-30s | %-6s | mittel %7.3f | grösste ausserhalb Kanten %3d | über 32 an Kanten %6.2f %% (%lld von %lld Pixeln an Kanten)",
              qPrintable(comparison), qPrintable(overlay), qPrintable(region), qPrintable(kind), m.mean,
              m.maxOutsideEdges, m.edgeOver32, static_cast<long long>(m.edgePixels), static_cast<long long>(m.pixels));
        m_lines << QStringList{m_run, comparison, overlay, region, kind, QString::number(m.mean, 'f', 4),
                               QString::number(m.maxOutsideEdges), QString::number(m.edgeOver32, 'f', 3),
                               QString::number(m.edgePixels), QString::number(m.pixels)}
                       .join(QLatin1Char('\t'));
    }

    void field(const QString &line) { m_fields << line; }

    // Hängt die Werte an werte.tsv an (nach jedem Overlay, damit ein späterer Fehler nichts verliert).
    bool flush()
    {
        QFile f(kOut + QStringLiteral("/werte.tsv"));
        const bool fresh = !f.exists();
        if (!f.open(QIODevice::Append | QIODevice::Text))
            return false;
        if (fresh)
            f.write("Lauf\tVergleich\tOverlay\tBereich\tArt\tMittel\tGrösste ausserhalb Kanten\tÜber 32 an Kanten (%)\tKantenpixel\tPixel\n");
        for (const QString &l : std::as_const(m_lines))
            f.write((l + QLatin1Char('\n')).toUtf8());
        m_lines.clear();
        return true;
    }

    bool writeFields()
    {
        QFile f(kOut + QStringLiteral("/felder.tsv"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        f.write(QStringLiteral("# Lauf %1\nVideo\tZeile\tSpalte\tSoll\tEditor\tSeite\tOBS\n").arg(m_run).toUtf8());
        for (const QString &l : std::as_const(m_fields))
            f.write((l + QLatin1Char('\n')).toUtf8());
        return true;
    }

    // Kleinster und grösster Wert je Masszahl über die letzten kSummaryRuns Läufe in werte.tsv
    bool writeSummary()
    {
        QFile in(kOut + QStringLiteral("/werte.tsv"));
        if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
            return false;
        QList<QStringList> rows;
        QStringList runs;
        in.readLine(); // Kopf
        while (!in.atEnd()) {
            const QStringList c = QString::fromUtf8(in.readLine()).trimmed().split(QLatin1Char('\t'));
            if (c.size() < 10)
                continue;
            if (!runs.contains(c.at(0)))
                runs << c.at(0);
            rows << c;
        }
        const QStringList last = runs.mid(std::max<qsizetype>(0, runs.size() - kSummaryRuns));
        struct Range
        {
            QSet<QString> runs;
            double mean[2] = {1e9, -1e9};
            int max[2] = {INT_MAX, INT_MIN};
            double over[2] = {1e9, -1e9};
        };
        QList<QString> order;
        QHash<QString, Range> ranges;
        for (const QStringList &c : std::as_const(rows)) {
            if (!last.contains(c.at(0)))
                continue;
            const QString key = c.mid(1, 4).join(QLatin1Char('\t'));
            if (!ranges.contains(key))
                order << key;
            Range &r = ranges[key];
            r.runs.insert(c.at(0));
            const double mean = c.at(5).toDouble();
            const int max = c.at(6).toInt();
            const double over = c.at(7).toDouble();
            r.mean[0] = std::min(r.mean[0], mean);
            r.mean[1] = std::max(r.mean[1], mean);
            r.max[0] = std::min(r.max[0], max);
            r.max[1] = std::max(r.max[1], max);
            r.over[0] = std::min(r.over[0], over);
            r.over[1] = std::max(r.over[1], over);
        }
        QFile out(kOut + QStringLiteral("/zusammenfassung.tsv"));
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        out.write(QStringLiteral("# Letzte %1 Läufe in werte.tsv: %2 bis %3\n")
                      .arg(last.size()).arg(last.value(0), last.value(last.size() - 1)).toUtf8());
        out.write("Vergleich\tOverlay\tBereich\tArt\tLäufe\tMittel min\tMittel max\tGrösste ausserhalb Kanten min\t"
                  "Grösste ausserhalb Kanten max\tÜber 32 an Kanten min (%)\tÜber 32 an Kanten max (%)\n");
        for (const QString &key : std::as_const(order)) {
            const Range &r = ranges.value(key);
            out.write(QStringLiteral("%1\t%2\t%3\t%4\t%5\t%6\t%7\t%8\n")
                          .arg(key).arg(r.runs.size())
                          .arg(r.mean[0], 0, 'f', 3).arg(r.mean[1], 0, 'f', 3)
                          .arg(r.max[0]).arg(r.max[1])
                          .arg(r.over[0], 0, 'f', 2).arg(r.over[1], 0, 'f', 2)
                          .toUtf8());
        }
        return true;
    }

private:
    QString m_run;
    QStringList m_lines;
    QStringList m_fields;
};

// Verbindung zu OBS nur für diesen Test: obs-websocket direkt über QWebSocket, mit den Bausteinen
// aus obsprotocol.h. ObsConnection und ObsSources bleiben unberührt (sie löschen nie etwas, F8).
class ObsSession : public QObject
{
public:
    struct Settings
    {
        QString host;
        int port = dss::obs::kDefaultPort;
        QString scene;
        QString credentialFile;
        QString credentialName;
    };

    static std::optional<Settings> parse(const QString &text, QString *error)
    {
        const QStringList p = text.split(QLatin1Char(';'));
        if (p.size() != 4 || p.contains(QString())) {
            *error = QStringLiteral("DSS_BILDVERGLEICH_OBS: erwartet «Adresse[:Port];Szene;Zugangsdatei;Name»");
            return std::nullopt;
        }
        Settings s;
        s.host = p.at(0).section(QLatin1Char(':'), 0, 0);
        if (p.at(0).contains(QLatin1Char(':'))) {
            bool ok = false;
            s.port = p.at(0).section(QLatin1Char(':'), 1).toInt(&ok);
            if (!ok || s.port <= 0 || s.port > 65535) {
                *error = QStringLiteral("DSS_BILDVERGLEICH_OBS: Port ungültig");
                return std::nullopt;
            }
        }
        s.scene = p.at(1);
        s.credentialFile = p.at(2);
        s.credentialName = p.at(3);
        return s;
    }

    ObsSession()
    {
        m_socket.setProxy(QNetworkProxy::NoProxy);
        connect(&m_socket, &QWebSocket::textMessageReceived, this, [this](const QString &text) {
            dss::obs::OpCode op = dss::obs::OpCode::Hello;
            QJsonObject data;
            QString error;
            if (!dss::obs::parseFrame(text, &op, &data, &error))
                return;
            if (op == dss::obs::OpCode::Hello) {
                dss::obs::Hello hello;
                if (dss::obs::parseHello(data, &hello, &error))
                    m_hello = hello;
            } else if (op == dss::obs::OpCode::Identified) {
                m_identified = true;
            } else if (op == dss::obs::OpCode::RequestResponse) {
                dss::obs::Response r;
                if (dss::obs::parseResponse(data, &r, &error))
                    m_responses.insert(r.requestId, r);
            }
        });
    }
    ~ObsSession() override { finish(); }

    QString info() const { return m_info; }
    QString inputUuid() const { return m_inputUuid; }

    // Passwort lesen, anmelden, Szene suchen, Browserquelle anlegen
    bool start(const Settings &s, const QString &url, QSize size, QString *error)
    {
        dss::obs::ObsSecretStore store(s.credentialFile);
        store.setCredentialName(s.credentialName);
        if (!store.isSet()) {
            *error = QStringLiteral("Zugangsdatei %1 fehlt").arg(s.credentialFile);
            return false;
        }
        std::optional<std::optional<QString>> read;
        store.read([&read](const std::optional<QString> &v) { read = v; });
        if (!QTest::qWaitFor([&] { return read.has_value(); }, 15000) || !read->has_value()) {
            *error = QStringLiteral("Zugangsdatei %1 nicht lesbar (systemd-creds, Name «%2»)").arg(s.credentialFile, s.credentialName);
            return false;
        }
        const QString password = **read;
        QUrl wsUrl;
        wsUrl.setScheme(QStringLiteral("ws"));
        wsUrl.setHost(s.host);
        wsUrl.setPort(s.port);
        QWebSocketHandshakeOptions options;
        options.setSubprotocols({QString::fromLatin1(dss::obs::kSubprotocol)});
        m_socket.open(wsUrl, options);
        if (!QTest::qWaitFor([&] { return m_hello.has_value() || m_socket.state() == QAbstractSocket::UnconnectedState; }, 10000)
            || !m_hello) {
            *error = QStringLiteral("keine Verbindung zu %1:%2 (%3)").arg(s.host).arg(s.port).arg(m_socket.errorString());
            return false;
        }
        m_socket.sendTextMessage(dss::obs::encodeFrame(dss::obs::OpCode::Identify, dss::obs::identifyData(*m_hello, password)));
        if (!QTest::qWaitFor([&] { return m_identified || m_socket.state() == QAbstractSocket::UnconnectedState; }, 10000)
            || !m_identified) {
            *error = QStringLiteral("Anmeldung bei OBS abgewiesen (Code %1)").arg(int(m_socket.closeCode()));
            return false;
        }
        const auto version = request(QStringLiteral("GetVersion"), {}, error);
        if (!version)
            return false;
        m_info = QStringLiteral("OBS %1, obs-websocket %2, %3 (%4)")
                     .arg(version->data.value(QStringLiteral("obsVersion")).toString(),
                          version->data.value(QStringLiteral("obsWebSocketVersion")).toString(),
                          version->data.value(QStringLiteral("platform")).toString(),
                          version->data.value(QStringLiteral("platformDescription")).toString());
        const auto scenes = request(QStringLiteral("GetSceneList"), {}, error);
        if (!scenes)
            return false;
        QString sceneUuid;
        for (const dss::obs::Scene &scene : dss::obs::parseScenes(scenes->data))
            if (scene.name == s.scene)
                sceneUuid = scene.uuid;
        if (sceneUuid.isEmpty()) {
            *error = QStringLiteral("Szene «%1» gibt es in OBS nicht").arg(s.scene);
            return false;
        }
        const dss::obs::Scene current = dss::obs::parseCurrentScene(scenes->data);
        m_info += QStringLiteral("; aktuelle Szene «%1»").arg(current.name);
        if (current.uuid != sceneUuid)
            qWarning("OBS: aktuelle Szene ist «%s», nicht «%s»; bleibt das Bild leer, die Szene wechseln",
                     qPrintable(current.name), qPrintable(s.scene));
        const auto created = request(QStringLiteral("CreateInput"),
                                     {{QStringLiteral("sceneUuid"), sceneUuid},
                                      {QStringLiteral("inputName"), kObsInputName},
                                      {QStringLiteral("inputKind"), QString::fromLatin1(dss::obs::kBrowserKind)},
                                      {QStringLiteral("inputSettings"), settings(url, size)},
                                      {QStringLiteral("sceneItemEnabled"), true}},
                                     error);
        if (!created)
            return false;
        m_inputUuid = created->data.value(QStringLiteral("inputUuid")).toString();
        return !m_inputUuid.isEmpty();
    }

    bool show(const QString &url, QSize size, QString *error)
    {
        return request(QStringLiteral("SetInputSettings"),
                       {{QStringLiteral("inputUuid"), m_inputUuid}, {QStringLiteral("inputSettings"), settings(url, size)}},
                       error)
            .has_value();
    }

    QImage screenshot(QString *error)
    {
        const auto r = request(QStringLiteral("GetSourceScreenshot"),
                               {{QStringLiteral("sourceUuid"), m_inputUuid}, {QStringLiteral("imageFormat"), QStringLiteral("png")}},
                               error);
        if (!r)
            return {};
        QString data = r->data.value(QStringLiteral("imageData")).toString();
        if (data.startsWith(QLatin1String("data:")))
            data = data.section(QLatin1Char(','), 1);
        const QImage image = QImage::fromData(QByteArray::fromBase64(data.toLatin1()), "PNG");
        if (image.isNull()) {
            *error = QStringLiteral("GetSourceScreenshot: Bild nicht lesbar");
            return {};
        }
        // Farben im PNG schon vormultipliziert (bv::fromObsScreenshot)
        return bv::fromObsScreenshot(image, &m_overAlpha);
    }
    // Pixel mit Farbe grösser als Alpha im letzten Bild (Gegenprobe zu bv::fromObsScreenshot)
    qint64 overAlpha() const { return m_overAlpha; }

    // Wartet, bis zwei Bilder nacheinander gleich und nicht einfarbig sind (Videos halten am Ende).
    QImage stableScreenshot(int firstWaitMs, int timeoutMs, QString *error)
    {
        QTest::qWait(firstWaitMs);
        QElapsedTimer t;
        t.start();
        QImage last;
        while (t.elapsed() < timeoutMs) {
            const QImage now = screenshot(error);
            if (now.isNull())
                return {};
            if (!last.isNull() && now == last && !uniform(now))
                return now;
            last = now;
            QTest::qWait(500);
        }
        *error = uniform(last) ? QStringLiteral("Bild von OBS bleibt leer (einfarbig); Szene wechseln?")
                               : QStringLiteral("Bild von OBS wird nicht ruhig");
        m_lastImage = last;
        return {};
    }
    QImage lastImage() const { return m_lastImage; }

    // Eigene Quelle löschen (Recht von Lotse, 2026-11-08), Verbindung schliessen
    void finish()
    {
        if (!m_inputUuid.isEmpty() && m_identified && m_socket.state() == QAbstractSocket::ConnectedState) {
            QString error;
            if (request(QStringLiteral("RemoveInput"), {{QStringLiteral("inputUuid"), m_inputUuid}}, &error))
                qInfo("OBS: eigene Quelle «%s» gelöscht", qPrintable(kObsInputName));
            else
                qWarning("OBS: eigene Quelle «%s» nicht gelöscht: %s", qPrintable(kObsInputName), qPrintable(error));
        }
        m_inputUuid.clear();
        if (m_socket.state() != QAbstractSocket::UnconnectedState) {
            m_socket.close();
            static_cast<void>(QTest::qWaitFor([&] { return m_socket.state() == QAbstractSocket::UnconnectedState; }, 3000));
        }
    }

private:
    static QJsonObject settings(const QString &url, QSize size)
    {
        return {{QStringLiteral("url"), url}, {QStringLiteral("width"), size.width()}, {QStringLiteral("height"), size.height()}};
    }

    static bool uniform(const QImage &image)
    {
        if (image.isNull())
            return true;
        const QRgb first = image.pixel(0, 0);
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (image.pixel(x, y) != first)
                    return false;
        return true;
    }

    std::optional<dss::obs::Response> request(const QString &type, const QJsonObject &fields, QString *error)
    {
        const QString id = QStringLiteral("bv-%1").arg(m_next++);
        m_socket.sendTextMessage(dss::obs::encodeFrame(dss::obs::OpCode::Request, dss::obs::requestData(type, id, fields)));
        if (!QTest::qWaitFor([&] { return m_responses.contains(id) || m_socket.state() != QAbstractSocket::ConnectedState; }, 15000)
            || !m_responses.contains(id)) {
            *error = QStringLiteral("%1: keine Antwort von OBS").arg(type);
            return std::nullopt;
        }
        const dss::obs::Response r = m_responses.take(id);
        if (!r.ok) {
            *error = QStringLiteral("%1: OBS meldet Code %2 %3").arg(type).arg(r.code).arg(r.comment);
            return std::nullopt;
        }
        return r;
    }

    QWebSocket m_socket;
    std::optional<dss::obs::Hello> m_hello;
    bool m_identified = false;
    QHash<QString, dss::obs::Response> m_responses;
    quint64 m_next = 1;
    QString m_inputUuid;
    QString m_info;
    QImage m_lastImage;
    qint64 m_overAlpha = 0;
};

} // namespace

class TestBildvergleich : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_data;
    QTemporaryDir m_config;
    std::unique_ptr<QTemporaryDir> m_run;
    QString m_socket;
    QString m_home;
    int m_port = 0;
    std::unique_ptr<QProcess> m_core;
    QString m_coreLog;
    std::unique_ptr<CoreClient> m_client;
    std::unique_ptr<QWebEngineView> m_view;
    QList<TestOverlay> m_overlays;
    Recorder m_recorder;
    std::optional<ObsSession::Settings> m_obsSettings;
    std::unique_ptr<ObsSession> m_obs;
    // Ohne DSS_BILDVERGLEICH_OBS: derselbe Weg gegen den Testserver; «OBS» liefert das Bild der Seite.
    std::unique_ptr<ObsTestServer> m_fakeObs;
    QTemporaryDir m_credentialDir;
    QString m_obsLabel = QStringLiteral("OBS");
    // Je Overlay (Name): Seite, Editor und Overlay aus overlays(), für absichtlicheFehler()
    QHash<QString, QImage> m_pages;
    QHash<QString, QImage> m_editors;
    QHash<QString, Overlay> m_overlayData;

    QVariant js(const QString &code)
    {
        QVariant result;
        bool done = false;
        m_view->page()->runJavaScript(code, [&](const QVariant &v) {
            result = v;
            done = true;
        });
        static_cast<void>(QTest::qWaitFor([&] { return done; }, 5000));
        return result;
    }

    QImage grab()
    {
        auto *quick = m_view->findChild<QQuickWidget *>();
        if (!quick)
            return {};
        return quick->grabFramebuffer().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }

    QString urlOf(const QUuid &id)
    {
        m_client->requestBrowserSource(id);
        QString url;
        static_cast<void>(QTest::qWaitFor([&] {
            const auto b = m_client->browserSource();
            if (b && b->overlayId == id)
                url = b->url;
            return !url.isEmpty();
        }, 5000));
        return url;
    }

    static void save(const QImage &image, const QString &name)
    {
        image.save(kOut + QLatin1Char('/') + name);
    }

    // Overlay im Datenordner anlegen (vor dem Start des Kerns)
    void buildOverlays()
    {
        OverlayStore store(m_data.path());
        store.setMediaSizeReader(readMediaSize);
        QString error;
        QVERIFY2(store.load(&error), qPrintable(error));
        TestOverlay *o = nullptr;
        const auto create = [&](const QString &name, QSize size) {
            m_overlays.append(TestOverlay{});
            o = &m_overlays.last();
            o->name = name;
            o->size = size;
            o->id = store.createOverlay(name, size.width(), size.height(), &error);
            QVERIFY2(!o->id.isNull(), qPrintable(error));
        };
        const auto place = [&](const QUuid &s, const QRect &r, int rotation = 0, bool flipH = false, bool flipV = false) {
            QVERIFY2(!s.isNull(), qPrintable(error));
            SourceChanges c;
            c.x = r.x();
            c.y = r.y();
            c.width = r.width();
            c.height = r.height();
            c.rotation = rotation;
            c.flipHorizontal = flipH;
            c.flipVertical = flipV;
            QVERIFY2(store.updateSource(o->id, s, c, &error), qPrintable(error));
        };
        const auto image = [&](const QString &file, const QRect &r, const QString &region, int rotation = 0,
                               bool flipH = false, bool flipV = false, QRect area = QRect()) {
            place(store.addImageSource(o->id, file, &error), r, rotation, flipH, flipV);
            if (!region.isEmpty())
                o->regions.append({region, QStringLiteral("Bild"), area.isNull() ? r : area});
        };
        const auto video = [&](const QString &file, const QRect &r, const QString &region, const QString &kind) {
            place(store.addVideoSource(o->id, file, false, &error, nullptr, true), r);
            ++o->videos;
            if (!region.isEmpty())
                o->regions.append({region, kind, r});
        };
        const auto text = [&](const QString &content, const QString &font, int weight, const QRect &r, int size,
                              TextAlign align, const QString &color, const QString &region, int rotation = 0,
                              QRect area = QRect()) {
            const QUuid s = store.addTextSource(o->id, content, font, &error);
            place(s, r, rotation);
            if (weight != 400)
                QVERIFY2(store.setTextFont(o->id, s, font, weight, false, &error), qPrintable(error));
            SourceChanges c;
            c.fontSize = size;
            c.align = align;
            c.color = color;
            QVERIFY2(store.updateSource(o->id, s, c, &error), qPrintable(error));
            ++o->texts;
            o->regions.append({region, QStringLiteral("Text"), area.isNull() ? r : area});
        };
        const QString dejavu = QStringLiteral(DSS_TEST_FONT);

        // Bild: EXIF 1–8 (JPEG, PNG), Alpha, vergrössert, verkleinert, gespiegelt, gedreht, überlagert
        create(QStringLiteral("Bild"), QSize(960, 540));
        for (int row = 0; row < 2; ++row)
            for (int n = 1; n <= 8; ++n)
                image(kExif + QStringLiteral("/orientierung-%1.%2").arg(n).arg(row ? "png" : "jpg"),
                      QRect(10 + (n - 1) * 116, 10 + row * 62, 104, 52), QString());
        o->regions.append({QStringLiteral("JPEG mit EXIF 1–8"), QStringLiteral("Bild"), QRect(10, 10, 916, 52)});
        o->regions.append({QStringLiteral("PNG mit eXIf 1–8"), QStringLiteral("Bild"), QRect(10, 72, 916, 52)});
        image(kData + QStringLiteral("/alpha.png"), QRect(10, 140, 128, 64), QStringLiteral("PNG mit Alpha 1:1"));
        image(kData + QStringLiteral("/alpha.png"), QRect(10, 240, 256, 128), QStringLiteral("PNG mit Alpha vergrössert"));
        image(kData + QStringLiteral("/einzelheiten.png"), QRect(150, 140, 120, 90), QStringLiteral("verkleinert"));
        image(kData + QStringLiteral("/einzelheiten.png"), QRect(600, 140, 352, 264), QStringLiteral("vergrössert"));
        image(kExif + QStringLiteral("/orientierung-1.png"), QRect(290, 140, 140, 70), QStringLiteral("gespiegelt waagrecht"), 0, true);
        image(kExif + QStringLiteral("/orientierung-1.png"), QRect(440, 140, 140, 70), QStringLiteral("gespiegelt senkrecht"), 0, false, true);
        image(kData + QStringLiteral("/einzelheiten.png"), QRect(300, 250, 160, 120), QStringLiteral("gedreht 12.5°"), 125,
              false, false, QRect(285, 230, 190, 160));
        image(kData + QStringLiteral("/einzelheiten.png"), QRect(20, 390, 240, 135), QStringLiteral("überlagert"), 0, false,
              false, QRect(20, 390, 240, 135));
        image(kData + QStringLiteral("/alpha.png"), QRect(60, 420, 160, 80), QString());

        // Text: DejaVu, Be Vietnam Pro, Urbanist in zwei Gewichten, alle Ausrichtungen, Alpha, gedreht
        create(QStringLiteral("Text"), QSize(960, 540));
        text(QStringLiteral("Hallo Welt\nZweite Zeile ÄÖÜ"), dejavu, 400, QRect(10, 10, 460, 110), 36, TextAlign::Left,
             QStringLiteral("#FFE040FF"), QStringLiteral("DejaVu links"));
        text(QStringLiteral("Be Vietnam Pro\nrechts"), kVietnam + QStringLiteral("/BeVietnamPro-Bold.ttf"), 700,
             QRect(490, 10, 460, 110), 30, TextAlign::Right, QStringLiteral("#FFFFFFFF"), QStringLiteral("Be Vietnam Pro fett rechts"));
        text(QStringLiteral("Urbanist 300\nMitte"), kUrbanist, 300, QRect(10, 130, 460, 110), 40, TextAlign::Center,
             QStringLiteral("#80FFC0FF"), QStringLiteral("Urbanist 300 Mitte"));
        text(QStringLiteral("Urbanist 800\nMitte"), kUrbanist, 800, QRect(490, 130, 460, 110), 40, TextAlign::Center,
             QStringLiteral("#80FFC0FF"), QStringLiteral("Urbanist 800 Mitte"));
        text(QStringLiteral("Farbe mit Alpha 50 %"), dejavu, 400, QRect(10, 260, 460, 80), 32, TextAlign::Left,
             QStringLiteral("#40A0FF80"), QStringLiteral("DejaVu Alpha"));
        text(QStringLiteral("gedreht 350°"), kVietnam + QStringLiteral("/BeVietnamPro-Regular.ttf"), 400,
             QRect(520, 270, 400, 90), 36, TextAlign::Left, QStringLiteral("#FFFFFFFF"), QStringLiteral("Be Vietnam Pro gedreht"),
             3500, QRect(505, 225, 430, 180));
        text(QStringLiteral("Klein 14 px: Lorem ipsum dolor sit amet,\nconsectetur adipiscing elit 0123456789"),
             kVietnam + QStringLiteral("/BeVietnamPro-Regular.ttf"), 400, QRect(10, 400, 460, 60), 14, TextAlign::Left,
             QStringLiteral("#FFFFFFFF"), QStringLiteral("Be Vietnam Pro klein"));

        // Videofarbe: Farbfelder in H.264 und VP9 mit und ohne Angaben, Vorlage als PNG, Bildwechsel
        create(QStringLiteral("Videofarbe"), QSize(1920, 1080));
        const QList<std::pair<QString, QSize>> colorVideos = {
            {QStringLiteral("h264-640x360-ohne.mp4"), QSize(640, 360)},
            {QStringLiteral("h264-1280x718-ohne.mp4"), QSize(1280, 718)},
            {QStringLiteral("h264-1280x720-ohne.mp4"), QSize(1280, 720)},
            {QStringLiteral("h264-1920x1080-ohne.mp4"), QSize(1920, 1080)},
            {QStringLiteral("h264-1920x1080-bt709.mp4"), QSize(1920, 1080)},
            {QStringLiteral("h264-1920x1080-bt601.mp4"), QSize(1920, 1080)},
            {QStringLiteral("h264-1920x1080-voll.mp4"), QSize(1920, 1080)},
            {QStringLiteral("vp9-640x360-ohne.webm"), QSize(640, 360)},
            {QStringLiteral("vp9-1920x1080-ohne.webm"), QSize(1920, 1080)},
            {QStringLiteral("vp9-1920x1080-bt709.webm"), QSize(1920, 1080)}};
        for (qsizetype i = 0; i < colorVideos.size(); ++i) {
            const QRect box(int(i % 4) * 480, int(i / 4) * 270, 480, 270);
            video(kData + QLatin1Char('/') + colorVideos.at(i).first, box, colorVideos.at(i).first, QStringLiteral("Video"));
            o->fieldVideos.append({colorVideos.at(i).first, box, colorVideos.at(i).second});
        }
        image(kData + QStringLiteral("/farbfelder.png"), QRect(960, 540, 480, 270), QStringLiteral("Vorlage farbfelder.png"));
        o->fieldVideos.append({QStringLiteral("farbfelder.png"), QRect(960, 540, 480, 270), QSize(1920, 1080)});
        o->lastFrame = QRect(1440, 540, 480, 270);
        video(kData + QStringLiteral("/bildwechsel.mp4"), o->lastFrame, QStringLiteral("bildwechsel.mp4"), QStringLiteral("Video"));

        // WebM: Drehangabe 90/180/270 (Punkt 80), mit Alpha
        create(QStringLiteral("WebM"), QSize(960, 540));
        for (int i = 1; i <= 3; ++i) {
            const QString name = QStringLiteral("drehung-%1.webm").arg(i * 90);
            const QRect box(10 + (i - 1) * 200, 10, 176, 88);
            video(kData + QLatin1Char('/') + name, box, name, QStringLiteral("WebM"));
            o->oriented.append({name, QStringLiteral("WebM"), box});
        }
        video(kData + QStringLiteral("/alpha.webm"), QRect(10, 120, 128, 64), QStringLiteral("alpha.webm 1:1"), QStringLiteral("WebM"));
        video(kData + QStringLiteral("/alpha.webm"), QRect(160, 120, 384, 192), QStringLiteral("alpha.webm vergrössert"),
              QStringLiteral("WebM"));

        // Gesamt: gemischt wie im Einsatz
        create(QStringLiteral("Gesamt"), QSize(1920, 1080));
        video(kData + QStringLiteral("/h264-1920x1080-bt709.mp4"), QRect(0, 0, 1280, 720), QStringLiteral("Video BT.709 1280x720"),
              QStringLiteral("Video"));
        image(kData + QStringLiteral("/einzelheiten.png"), QRect(1400, 600, 400, 300), QStringLiteral("Bild 400x300"));
        image(kExif + QStringLiteral("/orientierung-6.jpg"), QRect(100, 820, 200, 100), QStringLiteral("JPEG mit EXIF 6"));
        image(kData + QStringLiteral("/alpha.png"), QRect(1400, 400, 256, 128), QStringLiteral("PNG mit Alpha"));
        video(kData + QStringLiteral("/alpha.webm"), QRect(1400, 100, 384, 192), QStringLiteral("WebM mit Alpha"), QStringLiteral("WebM"));
        text(QStringLiteral("Willkommen im Stream"), dejavu, 400, QRect(60, 600, 1100, 100), 72, TextAlign::Left,
             QStringLiteral("#FFFFFFFF"), QStringLiteral("Text über dem Video"));
        text(QStringLiteral("Be Vietnam Pro rechts"), kVietnam + QStringLiteral("/BeVietnamPro-Bold.ttf"), 700,
             QRect(1300, 950, 600, 100), 48, TextAlign::Right, QStringLiteral("#FFE040FF"), QStringLiteral("Text rechts"));

        // Weitere Formate (nur notieren): HEVC in MP4, zur Laufzeit erzeugt, dazu die Vorlage als PNG,
        // damit das Bild nie einfarbig ist, auch wenn kein Zeichner HEVC abspielt. ProRes gibt es nur in
        // MOV; das Studio nimmt es nicht an (nur webm, mp4), geprüft und im Protokoll festgehalten.
        {
            const QString dir = kOut + QStringLiteral("/formate");
            QVERIFY(QDir().mkpath(dir));
            const QString base = QStringLiteral("scale=640:360:flags=neighbor,scale=out_color_matrix=bt709:out_range=tv,");
            const QString tags = QStringLiteral(",setparams=colorspace=bt709:color_primaries=bt709:color_trc=bt709:range=tv");
            const auto make = [&](const QString &file, const QString &format, const QStringList &codec) {
                QProcess ff;
                ff.start(QStringLiteral("ffmpeg"), QStringList{QStringLiteral("-loglevel"), QStringLiteral("error"),
                                                               QStringLiteral("-y"), QStringLiteral("-nostdin"),
                                                               QStringLiteral("-loop"), QStringLiteral("1"), QStringLiteral("-i"),
                                                               kData + QStringLiteral("/farbfelder.png"), QStringLiteral("-vf"),
                                                               base + format + tags, QStringLiteral("-frames:v"), QStringLiteral("10"),
                                                               QStringLiteral("-r"), QStringLiteral("30")}
                                                       + codec + QStringList{dir + QLatin1Char('/') + file});
                return ff.waitForFinished(120000) && ff.exitCode() == 0;
            };
            QVERIFY(make(QStringLiteral("hevc.mp4"), QStringLiteral("format=yuv420p"),
                         {QStringLiteral("-c:v"), QStringLiteral("libx265"), QStringLiteral("-x265-params"),
                          QStringLiteral("log-level=error"), QStringLiteral("-tag:v"), QStringLiteral("hvc1")}));
            QVERIFY(make(QStringLiteral("prores.mov"), QStringLiteral("format=yuv422p10le"),
                         {QStringLiteral("-c:v"), QStringLiteral("prores_ks"), QStringLiteral("-profile:v"), QStringLiteral("2")}));
            create(QStringLiteral("Weitere Formate"), QSize(960, 540));
            video(dir + QStringLiteral("/hevc.mp4"), QRect(0, 0, 480, 270), QStringLiteral("HEVC 640x360"), QStringLiteral("Video"));
            o->fieldVideos.append({QStringLiteral("hevc.mp4"), QRect(0, 0, 480, 270), QSize(640, 360)});
            image(kData + QStringLiteral("/farbfelder.png"), QRect(480, 0, 480, 270), QStringLiteral("Vorlage farbfelder.png"));
            const QUuid prores = store.addVideoSource(o->id, dir + QStringLiteral("/prores.mov"), false, &error, nullptr, true);
            QVERIFY2(prores.isNull(), "ProRes (.mov) wird vom Studio angenommen; Overlay «Weitere Formate» nachführen");
            qInfo("ProRes: vom Studio nicht angenommen (nur webm, mp4): %s", qPrintable(error));
        }
    }

    // Seite geladen: Schriften, Bilder, Videos (mit Bild oder gescheitert)
    bool pageReady(const TestOverlay &o)
    {
        const QString code = QStringLiteral(
            "(() => { const f=[...document.fonts]; const v=[...document.querySelectorAll('video')];"
            " const i=[...document.querySelectorAll('img')];"
            " return f.every(x=>x.status==='loaded') && f.length>=%1 && v.length===%2"
            " && v.every(x=>x.dssFailed || x.readyState>=2) && i.every(x=>x.complete && x.naturalWidth>0); })()")
                                 .arg(o.texts > 0 ? 1 : 0)
                                 .arg(o.videos);
        return QTest::qWaitFor([&] { return js(code).toBool(); }, 15000);
    }

    // Field colors for one image: «r/g/b» je Feld
    static QStringList fieldColors(const QImage &image, const FieldVideo &v)
    {
        QStringList result;
        if (image.isNull())
            return QStringList(32, QStringLiteral("-"));
        const int fw = v.size.width() / 16 * 2;
        const int fh = v.size.height() / 8 * 2;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 8; ++col) {
                const double cx = (col * fw + fw / 2.0) * v.box.width() / v.size.width();
                const double cy = (row * fh + fh / 2.0) * v.box.height() / v.size.height();
                result << rgbText(sample(image, QPoint(v.box.x() + int(cx), v.box.y() + int(cy))));
            }
        }
        return result;
    }

    // Grenze eines Bereichs; nullptr, wenn keine festgelegt ist
    static const Limit *limitFor(const QString &overlay, const QString &region, const QString &kind)
    {
        const auto known = kKnown.constFind(overlay + QLatin1Char('|') + region);
        if (known != kKnown.constEnd())
            return &*known;
        const auto byKind = kKindLimits.constFind(kind);
        return byKind != kKindLimits.constEnd() ? &*byKind : nullptr;
    }

    // Grösstes Verhältnis Masszahl / Grenze der drei Masszahlen (über 1 = über der Grenze)
    static double ratio(const bv::Measure &m, const Limit &l)
    {
        return std::max({m.mean / l.mean, double(m.maxOutsideEdges) / l.maxOutsideEdges, m.edgeOver32 / l.edgeOver32});
    }

    static QString limitText(const Limit &l)
    {
        return QStringLiteral("Grenze mittel %1, grösste ausserhalb Kanten %2, über 32 an Kanten %3 %")
            .arg(l.mean, 0, 'f', 1).arg(l.maxOutsideEdges).arg(l.edgeOver32, 0, 'f', 0);
    }

    static QString measureText(const bv::Measure &m)
    {
        return QStringLiteral("mittel %1, grösste ausserhalb Kanten %2, über 32 an Kanten %3 %")
            .arg(m.mean, 0, 'f', 3).arg(m.maxOutsideEdges).arg(m.edgeOver32, 0, 'f', 2);
    }

    // exceeded: nur für Seite–Editor; sammelt Bereiche über der Grenze oder ohne Grenze
    void record(const QString &comparison, const TestOverlay &o, const QImage &a, const QImage &b,
                QStringList *exceeded = nullptr)
    {
        const auto check = [&](const QString &region, const QString &kind, const bv::Measure &m) {
            if (!exceeded)
                return;
            const Limit *l = limitFor(o.name, region, kind);
            if (!l)
                *exceeded << QStringLiteral("%1 | %2: keine Grenze festgelegt").arg(o.name, region);
            else if (ratio(m, *l) > 1.0)
                *exceeded << QStringLiteral("%1 | %2: %3 (%4)").arg(o.name, region, measureText(m), limitText(*l));
        };
        const QImage mask = bv::edgeMask(a, b);
        const bv::Measure whole = bv::measure(a, b, mask, QRect(QPoint(0, 0), o.size));
        m_recorder.add(comparison, o.name, QStringLiteral("ganzes Overlay"), QStringLiteral("alle"), whole);
        check(QStringLiteral("ganzes Overlay"), QString(), whole);
        QHash<QString, QList<bv::Measure>> byKind;
        QStringList kinds;
        for (const Region &r : o.regions) {
            const bv::Measure m = bv::measure(a, b, mask, r.rect);
            m_recorder.add(comparison, o.name, r.name, r.kind, m);
            check(r.name, r.kind, m);
            if (!kinds.contains(r.kind))
                kinds << r.kind;
            byKind[r.kind].append(m);
        }
        for (const QString &kind : std::as_const(kinds))
            m_recorder.add(comparison, o.name, QStringLiteral("alle (%1)").arg(kind), kind, bv::combine(byKind.value(kind)));
    }

private slots:
    void initTestCase()
    {
        const QString obs = qEnvironmentVariable("DSS_BILDVERGLEICH_OBS");
        if (!obs.isEmpty()) {
            QString error;
            m_obsSettings = ObsSession::parse(obs, &error);
            QVERIFY2(m_obsSettings, qPrintable(error));
        }
        qInfo("Lauf %s, Ausgabe %s", qPrintable(m_recorder.run()), qPrintable(kOut));
        m_home = detectHomeAddress();
        QVERIFY(!m_home.isEmpty());
        m_port = 24000 + int(QCoreApplication::applicationPid() % 2000) * 4 + 3;
        QVERIFY(writeCoreSettings(m_config.path(), m_port, m_home));
        m_run = runtimeDir();
        m_socket = m_run->filePath(QStringLiteral("k.sock"));
        buildOverlays();
        if (QTest::currentTestFailed())
            return;

        m_core = testhelp::startCore(m_data.path(), m_socket, m_config.path());
        QVERIFY2(waitForLog(m_core.get(), QStringLiteral("Overlay-Server lauscht auf %1:%2").arg(m_home).arg(m_port),
                            &m_coreLog, 8000),
                 qPrintable(m_coreLog));
        CoreClient::Options opt;
        opt.socketPath = m_socket;
        opt.program = QStringLiteral("tst_bildvergleich");
        opt.version = version();
        opt.reconnectIntervalMs = 200;
        m_client = std::make_unique<CoreClient>(opt);
        m_client->start();
        QTRY_VERIFY_WITH_TIMEOUT(m_client->isConnected(), 5000);

        // Ausserhalb des Bildschirms (1920x1080), am Fenstermanager vorbei, ohne Fokus
        m_view = std::make_unique<QWebEngineView>();
        m_view->setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::X11BypassWindowManagerHint);
        m_view->setAttribute(Qt::WA_ShowWithoutActivating);
        m_view->page()->setBackgroundColor(Qt::transparent);
        m_view->page()->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
        m_view->setGeometry(3840, 0, 960, 540);
        m_view->show();

        if (!m_obsSettings) {
            // Testserver auf 127.0.0.1 mit Szene «test 2», eigenes Zufallspasswort in einer eigenen
            // Zugangsdatei (systemd-creds, Name obs-websocket); keine Datei des Nutzers, kein OBS.
            m_fakeObs = std::make_unique<ObsTestServer>();
            m_fakeObs->password = QStringLiteral("test-") + QString::number(QRandomGenerator::system()->generate64(), 36);
            m_fakeObs->scenes.append({QStringLiteral("scene-test-2"), QStringLiteral("test 2")});
            m_fakeObs->current = m_fakeObs->scenes.last();
            QVERIFY(m_fakeObs->start());
            const QString cred = m_credentialDir.filePath(QStringLiteral("obs-websocket.cred"));
            dss::obs::ObsSecretStore store(cred);
            store.setCredentialName(QStringLiteral("obs-websocket"));
            std::optional<QString> written;
            store.write(m_fakeObs->password, [&written](const QString &e) { written = e; });
            QTRY_VERIFY_WITH_TIMEOUT(written.has_value(), 15000);
            QVERIFY2(written->isEmpty(), qPrintable(*written));
            QString error;
            m_obsSettings = ObsSession::parse(
                QStringLiteral("127.0.0.1:%1;test 2;%2;obs-websocket").arg(m_fakeObs->port()).arg(cred), &error);
            QVERIFY2(m_obsSettings, qPrintable(error));
            m_obsLabel = QStringLiteral("Testserver");
        }
        m_obs = std::make_unique<ObsSession>();
        const TestOverlay &first = m_overlays.first();
        QString error;
        QVERIFY2(m_obs->start(*m_obsSettings, urlOf(first.id), first.size, &error), qPrintable(error));
        qInfo("%s: %s; Quelle «%s» in der Szene «%s» angelegt", qPrintable(m_obsLabel), qPrintable(m_obs->info()),
              qPrintable(kObsInputName), qPrintable(m_obsSettings->scene));
    }

    void cleanupTestCase()
    {
        if (m_obs)
            m_obs->finish();
        m_obs.reset();
        QVERIFY(m_recorder.flush());
        QVERIFY(m_recorder.writeFields());
        QVERIFY(m_recorder.writeSummary());
        m_view.reset();
        m_client.reset();
        if (m_core && m_core->state() != QProcess::NotRunning) {
            m_core->terminate();
            m_core->waitForFinished(5000);
        }
    }

    // Der Weg zu OBS gegen den Testserver (ohne echtes OBS): Zugangsdatei mit eigenem Namen,
    // Anmeldung, Quelle anlegen, umstellen, Bild holen, eigene Quelle löschen.
    void obsSessionAgainstTestServer()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("systemd-creds")).isEmpty())
            QFAIL("systemd-creds fehlt");
        ObsTestServer server;
        server.password = QStringLiteral("test-") + QString::number(QRandomGenerator::system()->generate64(), 36);
        QVERIFY(server.start());
        QImage shot(64, 32, QImage::Format_ARGB32);
        shot.fill(qRgba(10, 200, 30, 255));
        shot.setPixel(5, 5, qRgb(255, 255, 255));
        QBuffer buffer(&server.screenshotPng);
        QVERIFY(buffer.open(QIODevice::WriteOnly) && shot.save(&buffer, "PNG"));

        QTemporaryDir dir;
        const QString cred = dir.filePath(QStringLiteral("obs-websocket.cred"));
        dss::obs::ObsSecretStore store(cred);
        store.setCredentialName(QStringLiteral("obs-websocket"));
        std::optional<QString> written;
        store.write(server.password, [&written](const QString &e) { written = e; });
        QTRY_VERIFY_WITH_TIMEOUT(written.has_value(), 15000);
        QVERIFY2(written->isEmpty(), qPrintable(*written));

        QString error;
        const auto settings = ObsSession::parse(QStringLiteral("127.0.0.1:%1;Live;%2;obs-websocket").arg(server.port()).arg(cred), &error);
        QVERIFY2(settings, qPrintable(error));
        QCOMPARE(settings->port, int(server.port()));
        QVERIFY(!ObsSession::parse(QStringLiteral("127.0.0.1;Live;%1").arg(cred), &error));
        QCOMPARE(ObsSession::parse(QStringLiteral("192.0.2.7;test 2;%1;obs-websocket").arg(cred), &error)->port, 4455);
        {
            ObsSession session;
            QVERIFY2(session.start(*settings, QStringLiteral("http://192.0.2.1:1/o/a/x"), QSize(64, 32), &error), qPrintable(error));
            QCOMPARE(server.inputs.size(), 1);
            QCOMPARE(server.inputs.first().name, kObsInputName);
            QCOMPARE(server.sceneItems.value(QStringLiteral("scene-live")), QStringList{session.inputUuid()});
            QVERIFY(session.show(QStringLiteral("http://192.0.2.1:1/o/b/y"), QSize(640, 360), &error));
            QCOMPARE(server.inputs.first().settings.value(QStringLiteral("url")).toString(), QStringLiteral("http://192.0.2.1:1/o/b/y"));
            QCOMPARE(server.inputs.first().settings.value(QStringLiteral("width")).toInt(), 640);
            const QImage got = session.stableScreenshot(0, 5000, &error);
            QVERIFY2(!got.isNull(), qPrintable(error));
            QCOMPARE(got, shot.convertToFormat(QImage::Format_ARGB32_Premultiplied));
            session.finish();
            QVERIFY(server.inputs.isEmpty());
            QCOMPARE(server.requestCount(QStringLiteral("RemoveInput")), 1);
        }
        // Einfarbiges Bild gilt als leer
        shot.fill(qRgba(0, 0, 0, 0));
        server.screenshotPng.clear();
        QBuffer empty(&server.screenshotPng);
        QVERIFY(empty.open(QIODevice::WriteOnly) && shot.save(&empty, "PNG"));
        {
            ObsSession session;
            QVERIFY2(session.start(*settings, QStringLiteral("http://192.0.2.1:1/o/a/x"), QSize(64, 32), &error), qPrintable(error));
            QVERIFY(session.stableScreenshot(0, 1500, &error).isNull());
            QVERIFY(error.contains(QStringLiteral("leer")));
        }
        QVERIFY(server.inputs.isEmpty()); // Destruktor löscht die eigene Quelle
        // Falsches Passwort, falscher Name, fehlende Szene
        server.password = QStringLiteral("anderes");
        {
            ObsSession session;
            QVERIFY(!session.start(*settings, QStringLiteral("http://x/"), QSize(64, 32), &error));
            QVERIFY2(error.contains(QStringLiteral("Anmeldung")), qPrintable(error));
        }
        {
            ObsSession::Settings wrongName = *settings;
            wrongName.credentialName = QStringLiteral("dss-obs-passwort");
            // systemd-creds lehnt den falschen Namen ab; die Warnung von ObsSecretStore ist hier erwartet
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^OBS-Passwort: Entschlüsseln fehlgeschlagen: ")));
            ObsSession session;
            QVERIFY(!session.start(wrongName, QStringLiteral("http://x/"), QSize(64, 32), &error));
            QVERIFY2(error.contains(QStringLiteral("nicht lesbar")), qPrintable(error));
        }
        server.password.clear();
        {
            ObsSession::Settings noScene = *settings;
            noScene.scene = QStringLiteral("gibt es nicht");
            ObsSession session;
            QVERIFY(!session.start(noScene, QStringLiteral("http://x/"), QSize(64, 32), &error));
            QVERIFY2(error.contains(QStringLiteral("Szene")), qPrintable(error));
        }
        QVERIFY(server.inputs.isEmpty());
    }

    void overlays_data()
    {
        QTest::addColumn<int>("index");
        for (int i = 0; i < m_overlays.size(); ++i)
            QTest::newRow(qPrintable(m_overlays.at(i).name)) << i;
    }

    // Seite (Qt WebEngine) gegen Editor, mit OBS auch OBS gegen beide
    void overlays()
    {
        QFETCH(int, index);
        const TestOverlay &o = m_overlays.at(index);
        const QString url = urlOf(o.id);
        QVERIFY(!url.isEmpty());

        // Seite
        m_view->setGeometry(3840, 0, o.size.width(), o.size.height());
        QSignalSpy loaded(m_view.get(), &QWebEngineView::loadFinished);
        m_view->load(QUrl(url));
        QVERIFY(loaded.wait(15000));
        QVERIFY(QTest::qWaitFor([&] { return js(QStringLiteral("window.dssState ? window.dssState.applied : -1")).toInt() >= 1; }, 15000));
        QVERIFY(pageReady(o));
        // Videos am Ende angehalten (oder nicht abspielbar)
        QVERIFY(QTest::qWaitFor([&] {
            return js(QStringLiteral("[...document.querySelectorAll('video')].every(v => v.ended || v.dssFailed)")).toBool();
        }, 15000));
        QTest::qWait(300);
        const QImage page = grab();
        QCOMPARE(page.size(), o.size);
        const QString failed = js(QStringLiteral("window.dssState.reports.join(' | ')")).toString();
        if (!failed.isEmpty())
            qInfo("%s: Meldungen der Seite: %s", qPrintable(o.name), qPrintable(failed));

        // Editor
        m_client->requestOverlay(o.id);
        QTRY_VERIFY_WITH_TIMEOUT(m_client->overlay(o.id).has_value(), 5000);
        const Overlay overlay = *m_client->overlay(o.id);
        SourceRenderer renderer;
        renderer.setDataDir(m_client->coreHello().dataDir);
        renderer.setOverlay(&overlay);
        QTRY_VERIFY_WITH_TIMEOUT(renderer.imagesLoaded(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            const QList<SourceRenderer::VideoInfo> infos = renderer.videoInfo();
            return infos.size() == o.videos && std::all_of(infos.begin(), infos.end(), [](const auto &v) {
                       return v.hasFrame && v.state == kHoldingState;
                   });
        }(), 10000);
        const QImage editor = renderOverlayImage(renderer, overlay);
        QCOMPARE(editor.size(), o.size);

        const QString file = o.name.toLower().replace(QLatin1Char(' '), QLatin1Char('-'));
        save(page, file + QStringLiteral("-seite.png"));
        save(editor, file + QStringLiteral("-editor.png"));
        save(bv::differenceImage(page, editor, bv::edgeMask(page, editor)), file + QStringLiteral("-differenz.png"));
        QStringList exceeded;
        record(QStringLiteral("Seite–Editor"), o, page, editor, o.name == kNotedOnly ? nullptr : &exceeded);
        m_pages.insert(o.name, page);
        m_editors.insert(o.name, editor);
        m_overlayData.insert(o.name, overlay);

        // OBS
        QImage obs;
        if (m_obs) {
            QString error;
            if (m_fakeObs) {
                // Testserver: liefert die Seite wie obs-websocket (vormultipliziert im PNG), abwechselnd
                // mit Alpha und deckend auf Schwarz (beide Wege)
                QBuffer buffer(&m_fakeObs->screenshotPng);
                m_fakeObs->screenshotPng.clear();
                QVERIFY(buffer.open(QIODevice::WriteOnly)
                        && bv::toObsScreenshot(index % 2 ? bv::onBlack(page) : page).save(&buffer, "PNG"));
            }
            QVERIFY2(m_obs->show(url, o.size, &error), qPrintable(error));
            if (m_fakeObs) {
                QCOMPARE(m_fakeObs->inputs.size(), 1);
                const QJsonObject s = m_fakeObs->inputs.first().settings;
                QCOMPARE(s.value(QStringLiteral("url")).toString(), url);
                QCOMPARE(QSize(s.value(QStringLiteral("width")).toInt(), s.value(QStringLiteral("height")).toInt()), o.size);
            }
            obs = m_obs->stableScreenshot(m_fakeObs ? 0 : 2000, 30000, &error);
            if (obs.isNull() && !m_obs->lastImage().isNull())
                save(m_obs->lastImage(), file + QStringLiteral("-obs-letztes.png"));
            QVERIFY2(!obs.isNull(), qPrintable(error));
            QCOMPARE(obs.size(), o.size);
            save(obs, file + QStringLiteral("-obs.png"));
            qInfo("%s: Bild von %s vormultipliziert gelesen, Pixel mit Farbe grösser als Alpha: %lld",
                  qPrintable(o.name), qPrintable(m_obsLabel), static_cast<long long>(m_obs->overAlpha()));
            if (m_fakeObs) {
                // Schon der Abgriff von Qt WebEngine hat solche Pixel (bei WebM mit Alpha); gezählt
                // werden muss genau, was im verschickten PNG steht.
                qint64 sent = -1;
                bv::fromObsScreenshot(QImage::fromData(m_fakeObs->screenshotPng, "PNG"), &sent);
                QCOMPARE(m_obs->overAlpha(), sent);
            }
            // Ohne Alpha (deckend): Seite und Editor auf Schwarz gelegt vergleichen
            const bool opaque = bv::fullyOpaque(obs);
            if (opaque)
                qInfo("%s: Bild von OBS ohne Alpha, verglichen auf Schwarz", qPrintable(o.name));
            const QImage pageCmp = opaque ? bv::onBlack(page) : page;
            const QImage editorCmp = opaque ? bv::onBlack(editor) : editor;
            save(bv::differenceImage(obs, editorCmp, bv::edgeMask(obs, editorCmp)), file + QStringLiteral("-differenz-obs.png"));
            record(m_obsLabel + QStringLiteral("–Editor"), o, obs, editorCmp);
            record(m_obsLabel + QStringLiteral("–Seite"), o, obs, pageCmp);
            if (m_fakeObs) { // genau das verschickte PNG zurück (der Weg über PNG rundet, deshalb nicht die Seite)
                QCOMPARE(opaque, index % 2 == 1);
                QCOMPARE(obs, bv::fromObsScreenshot(QImage::fromData(m_fakeObs->screenshotPng, "PNG")));
            }
        }
        QVERIFY(m_recorder.flush());

        // Farben der Felder je Video
        for (const FieldVideo &v : o.fieldVideos) {
            const QStringList e = fieldColors(editor, v);
            const QStringList p = fieldColors(page, v);
            const QStringList b = fieldColors(obs, v);
            for (int i = 0; i < 32; ++i) {
                const QColor soll = QColor::fromString(QLatin1String(kFieldColors[i / 8][i % 8]));
                m_recorder.field(QStringList{v.name, QString::number(i / 8 + 1), QString::number(i % 8 + 1),
                                             rgbText(soll.rgb()), e.at(i), p.at(i), b.at(i)}
                                     .join(QLatin1Char('\t')));
            }
        }
        // Ausrichtung (Punkt 80): Editor richtig, Seite gleich (der Server neutralisiert die Drehung
        // auch bei WebM); OBS nur im Protokoll
        for (const Region &r : o.oriented) {
            qInfo("%s | Ausrichtung %s: Editor %s | Seite %s%s", qPrintable(o.name), qPrintable(r.name),
                  qPrintable(orientation(editor, r.rect)), qPrintable(orientation(page, r.rect)),
                  obs.isNull() ? "" : qPrintable(QStringLiteral(" | OBS ") + orientation(obs, r.rect)));
            QCOMPARE(orientation(editor, r.rect), QStringLiteral("rot grün blau gelb"));
            QCOMPARE(orientation(page, r.rect), orientation(editor, r.rect));
        }
        // K-05: letztes Bild (Grau 128) in beiden Zeichnern, nicht 0 oder 255
        if (!o.lastFrame.isNull()) {
            const QPoint c = o.lastFrame.center();
            const QList<std::pair<const char *, QImage>> shown = {{"Editor", editor}, {"Seite", page}, {"OBS", obs}};
            for (const auto &[who, img] : shown) {
                if (img.isNull())
                    continue;
                const QRgb p = sample(img, c);
                qInfo("%s | bildwechsel.mp4 am Ende: %s %s", qPrintable(o.name), who, qPrintable(rgbText(p)));
                QVERIFY2(qGray(p) >= 96 && qGray(p) <= 160 && qAlpha(p) == 255,
                         qPrintable(QStringLiteral("%1 zeigt am Ende nicht das letzte Bild: %2").arg(QLatin1String(who), rgbText(p))));
            }
        }
        // Feste Grenze (K-08); Bilder liegen schon in der Ausgabe
        QVERIFY2(exceeded.isEmpty(), qPrintable(QStringLiteral("Seite–Editor über der Grenze (Bilder in %1):\n").arg(kOut)
                                                 + exceeded.join(QLatin1Char('\n'))));
    }

    // Absichtliche Fehler (Plan P11, Abschnitt 4): Der Editor wird absichtlich falsch gezeichnet
    // (Overlay geändert oder Bild nachbearbeitet) und gegen dieselbe Seite gemessen. Jeder Fehler muss
    // in seinem Bereich mindestens das Doppelte der Grenze erreichen (in einer der drei Masszahlen).
    void absichtlicheFehler()
    {
        for (const char *name : {"Bild", "Text", "Videofarbe"})
            QVERIFY2(m_pages.contains(QLatin1String(name)), "overlays() muss vorher gelaufen sein");
        const auto region = [&](const QString &overlay, const QString &name) {
            for (const TestOverlay &o : std::as_const(m_overlays))
                if (o.name == overlay)
                    for (const Region &r : o.regions)
                        if (r.name == name)
                            return r;
            return Region{};
        };
        // Editor mit geändertem Overlay neu zeichnen (nur Overlays ohne Videos)
        const auto rerender = [&](const QString &overlay, const QRect &sourceBox, const std::function<void(Source &)> &change) {
            Overlay copy = m_overlayData.value(overlay);
            bool found = false;
            for (Source &s : copy.sources) {
                if (QRect(s.x, s.y, s.width, s.height) == sourceBox) {
                    change(s);
                    found = true;
                }
            }
            if (!found)
                return QImage();
            SourceRenderer renderer;
            renderer.setDataDir(m_client->coreHello().dataDir);
            renderer.setOverlay(&copy);
            if (!QTest::qWaitFor([&] { return renderer.imagesLoaded(); }, 5000))
                return QImage();
            return renderOverlayImage(renderer, copy);
        };
        // Bild nachbearbeiten: Inhalt eines Kastens ersetzen
        const auto edited = [&](const QString &overlay, const QRect &box, const std::function<QImage(const QImage &)> &change) {
            QImage out = m_editors.value(overlay).convertToFormat(QImage::Format_ARGB32_Premultiplied);
            const QImage part = change(out.copy(box)).convertToFormat(QImage::Format_ARGB32_Premultiplied);
            for (int y = 0; y < box.height(); ++y)
                memcpy(out.scanLine(box.y() + y) + 4 * box.x(), part.constScanLine(y), 4 * size_t(box.width()));
            return out;
        };

        struct Fault
        {
            QString name;
            QString overlay;
            QString region;
            QImage editor;
            bool knownShort = false; // erreicht das Doppelte nicht, [OFFEN] (siehe unten)
        };
        QList<Fault> faults;
        const QRect enlarged(600, 140, 352, 264);
        faults.append({QStringLiteral("1 Pixel verschoben"), QStringLiteral("Bild"), QStringLiteral("vergrössert"),
                       rerender(QStringLiteral("Bild"), enlarged, [](Source &s) { s.x += 1; })});
        faults.append({QStringLiteral("1° gedreht"), QStringLiteral("Bild"), QStringLiteral("gedreht 12.5°"),
                       rerender(QStringLiteral("Bild"), QRect(300, 250, 160, 120), [](Source &s) { s.rotation += 10; })});
        faults.append({QStringLiteral("falsch gespiegelt"), QStringLiteral("Bild"), QStringLiteral("gespiegelt waagrecht"),
                       rerender(QStringLiteral("Bild"), QRect(290, 140, 140, 70), [](Source &s) { s.flipHorizontal = false; })});
        // EXIF 6 (90° im Uhrzeigersinn) als 8 gelesen: Inhalt um 180° falsch
        faults.append({QStringLiteral("falsche EXIF-Angabe"), QStringLiteral("Bild"), QStringLiteral("JPEG mit EXIF 1–8"),
                       edited(QStringLiteral("Bild"), QRect(10 + 5 * 116, 10, 104, 52),
                              [](const QImage &p) { return p.mirrored(true, true); })});
        faults.append({QStringLiteral("Alpha um 8 daneben"), QStringLiteral("Bild"), QStringLiteral("PNG mit Alpha 1:1"),
                       edited(QStringLiteral("Bild"), QRect(10, 140, 128, 64), [](const QImage &p) {
                           QImage q = p.convertToFormat(QImage::Format_ARGB32);
                           for (int y = 0; y < q.height(); ++y)
                               for (int x = 0; x < q.width(); ++x) {
                                   const QRgb c = q.pixel(x, y);
                                   q.setPixel(x, y, qRgba(qRed(c), qGreen(c), qBlue(c), std::max(0, qAlpha(c) - 8)));
                               }
                           return q.convertToFormat(QImage::Format_ARGB32_Premultiplied);
                       })});
        // BT.709-Video mit BT.601 zurückgerechnet (wie vor Punkt 79)
        const QRect bt709Box = region(QStringLiteral("Videofarbe"), QStringLiteral("h264-1920x1080-bt709.mp4")).rect;
        faults.append({QStringLiteral("BT.601 statt BT.709"), QStringLiteral("Videofarbe"),
                       QStringLiteral("h264-1920x1080-bt709.mp4"),
                       edited(QStringLiteral("Videofarbe"), bt709Box, [](const QImage &p) {
                           QImage q = p.copy();
                           for (int y = 0; y < q.height(); ++y) {
                               QRgb *l = reinterpret_cast<QRgb *>(q.scanLine(y));
                               for (int x = 0; x < q.width(); ++x)
                                   l[x] = bt709AsBt601(l[x]);
                           }
                           return q;
                       })});
        // Falsches Videobild: das vorletzte Bild von bildwechsel.mp4 (weiss) statt des letzten (grau)
        const QRect lastBox = region(QStringLiteral("Videofarbe"), QStringLiteral("bildwechsel.mp4")).rect;
        faults.append({QStringLiteral("falsches Videobild"), QStringLiteral("Videofarbe"), QStringLiteral("bildwechsel.mp4"),
                       edited(QStringLiteral("Videofarbe"), lastBox, [](const QImage &p) {
                           QImage q = p.copy();
                           q.fill(qRgb(255, 255, 255));
                           return q;
                       })});
        faults.append({QStringLiteral("falsches Schriftgewicht"), QStringLiteral("Text"), QStringLiteral("Urbanist 300 Mitte"),
                       rerender(QStringLiteral("Text"), QRect(10, 130, 460, 110), [](Source &s) { s.text.weight = 400; }),
                       true});
        faults.append({QStringLiteral("Grundlinie 1 Pixel daneben"), QStringLiteral("Text"), QStringLiteral("DejaVu links"),
                       rerender(QStringLiteral("Text"), QRect(10, 10, 460, 110), [](Source &s) { s.y += 1; })});

        QStringList tooSmall;
        for (const Fault &f : std::as_const(faults)) {
            QVERIFY2(!f.editor.isNull(), qPrintable(f.name));
            const Region r = region(f.overlay, f.region);
            QVERIFY2(!r.rect.isNull(), qPrintable(f.region));
            const Limit *l = limitFor(f.overlay, r.name, r.kind);
            QVERIFY(l);
            const QImage &page = m_pages.value(f.overlay);
            const bv::Measure m = bv::measure(page, f.editor, bv::edgeMask(page, f.editor), r.rect);
            const double times = ratio(m, *l);
            qInfo("Absichtlicher Fehler %-28s | %s | %s: %s (%s) → %.1f-fach", qPrintable(f.name), qPrintable(f.overlay),
                  qPrintable(f.region), qPrintable(measureText(m)), qPrintable(limitText(*l)), times);
            save(bv::differenceImage(page, f.editor, bv::edgeMask(page, f.editor)),
                 QStringLiteral("fehler-%1.png").arg(f.name.toLower().replace(QLatin1Char(' '), QLatin1Char('-'))));
            if (f.knownShort) {
                // [OFFEN] Punkt 101: Urbanist 300 statt 400 liegt über der Grenze Text (wird erkannt),
                // erreicht aber nur das 1.96-fache (Mittel 3.919 gegen 2.0, 2026-11-08). Die Grenze
                // wird nicht zurechtgebogen (Plan P11, Abschnitt 4.6). Erreicht der Fehler das Doppelte,
                // scheitert der Test (XPASS) und der Eintrag muss weg.
                QVERIFY2(times > 1.0, qPrintable(QStringLiteral("%1 liegt unter der Grenze").arg(f.name)));
                QEXPECT_FAIL("", "[OFFEN] Punkt 101: falsches Schriftgewicht erreicht das Doppelte der Grenze nicht", Continue);
                QVERIFY2(times >= 2.0, qPrintable(QStringLiteral("%1: %2-fach").arg(f.name).arg(times, 0, 'f', 2)));
            } else if (times < 2.0) {
                tooSmall << QStringLiteral("%1: %2-fach").arg(f.name).arg(times, 0, 'f', 1);
            }
        }
        QVERIFY2(tooSmall.isEmpty(), qPrintable(QStringLiteral("Fehler erreicht nicht das Doppelte der Grenze: ")
                                                 + tooSmall.join(QStringLiteral(", "))));
    }
};

int main(int argc, char *argv[])
{
    if (qgetenv("XDG_SESSION_TYPE") != "x11" || qEnvironmentVariableIsEmpty("DISPLAY")) {
        qInfo("übersprungen: keine X11-Sitzung (XDG_SESSION_TYPE/DISPLAY)");
        return 77;
    }
    if (detectHomeAddress().isEmpty()) {
        qInfo("übersprungen: keine Adresse im Heimnetz (Standardroute fehlt)");
        return 77;
    }
    QApplication app(argc, argv);
    TestBildvergleich test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_bildvergleich.moc"
