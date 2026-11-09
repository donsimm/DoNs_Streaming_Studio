// ton-messen: Prototyp P12a (Ton von Videos), Plan docs/archiv/prompts/P12-ton.md, Abschnitt 2.
// Läuft mit QCoreApplication wie der Kern (ohne QGuiApplication); M1 prüft, ob Qt Multimedia so
// und als Benutzerdienst (systemd-run --user) Geräte findet und spielt.
//
//   ton-messen --liste
//   ton-messen --stille [--ausgang X] [--puffer-ms 20] [--dauer-s 5]       (Nullen, Lautstärke 0)
//   ton-messen --dekodieren <Datei>                    (Ton laden, Pieps finden, Lage prüfen)
//   ton-messen --klicks --ausgang X --eingang Y [--puffer-ms 20] [--dauer-s 120] [--abstand-ms 500]
//              --aus <Präfix>                                                         (M1)
//   ton-messen --server [--lauschen 127.0.0.1] [--port 24990] [--daten tests/daten/ton]
//              [--dauer-s 0 = bis SIGTERM] [--aus <Präfix>]
//              [--folgen <Datei> --ausgang X [--puffer-ms 40] [--versatz-ms 0] [--geraet-ms 0]
//               [--lautstaerke 0.25]]                                                 (M2, M3, M4)
//   ton-messen --selbsttest [--daten tests/daten/ton]   (Server ohne Browser und ohne Ton prüfen)
//   pw-dump | ton-messen --pw-zustand                   (für nachtlauf.sh)
#include "auswertung.h"
#include "folgen.h"
#include "klickmessung.h"
#include "meldeserver.h"
#include "tondatei.h"
#include "zeit.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <map>

namespace {

std::atomic<bool> g_ende{false};

void signalEnde(int)
{
    g_ende = true;
}

QString repoOrdner()
{
    return QDir::cleanPath(QStringLiteral(TON_QUELLORDNER "/../.."));
}

int dekodieren(const QString &pfad)
{
    TonDatei ton;
    QString fehler;
    if (!tonLaden(pfad, &ton, &fehler)) {
        std::fprintf(stderr, "%s: %s\n", qPrintable(pfad), qPrintable(fehler));
        return 1;
    }
    const auto e = einsaetzeFinden(ton.proben.data(), ton.frames(), kKanaele, 0.01f, kRate / 4);
    std::printf("%s: %s, %d Hz, %d Kanäle → 48 kHz Stereo, %lld Proben = %.4f s, erster Block bei %.6f s\n",
                qPrintable(QFileInfo(pfad).fileName()), qPrintable(ton.codec), ton.quellRate, ton.quellKanaele,
                qlonglong(ton.frames()), double(ton.frames()) / kRate, ton.erstePts);
    QString lagen;
    double maxAbw = 0;
    for (size_t i = 0; i < e.size(); ++i) {
        const double s = double(e[i]) / kRate;
        maxAbw = std::max(maxAbw, std::fabs(s - std::round(s / 2.0) * 2.0));
        if (i < 3 || i + 1 == e.size())
            lagen += QStringLiteral(" %1").arg(s, 0, 'f', 6);
        else if (i == 3)
            lagen += QStringLiteral(" …");
    }
    std::printf("  Pieps: %zu, Einsätze (s):%s; grösste Abweichung von geraden Sekunden %.3f ms\n", e.size(),
                qPrintable(lagen), maxAbw * 1e3);
    return 0;
}

// Zustand von PipeWire aus `pw-dump` (JSON auf stdin) für nachtlauf.sh, eine Angabe je Zeile:
//   standard_senke <node.name>      Metadaten «default», default.audio.sink
//   force_quantum <Zahl>            Metadaten «settings», clock.force-quantum
//   knoten <id> <node.name>         jeder Knoten, dessen Name mit dss_p12a beginnt
//   gefahr <Knoten> -> <Knoten>     Verbindung zwischen dss_p12a… und einem echten Gerät (alsa_, bluez_)
// Quelle: https://docs.pipewire.org/page_man_pw-dump_1.html
int pwZustand()
{
    QFile ein;
    if (!ein.open(stdin, QIODevice::ReadOnly))
        return 1;
    const QJsonDocument doc = QJsonDocument::fromJson(ein.readAll());
    if (!doc.isArray()) {
        std::fprintf(stderr, "pw-dump: kein JSON-Feld\n");
        return 1;
    }
    std::map<qint64, QString> namen;
    const QJsonArray objekte = doc.array();
    for (const QJsonValue &v : objekte) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("type")).toString() == QLatin1String("PipeWire:Interface:Node"))
            namen[o.value(QStringLiteral("id")).toInteger()] =
                o.value(QStringLiteral("info")).toObject().value(QStringLiteral("props")).toObject()
                    .value(QStringLiteral("node.name")).toString();
    }
    for (const QJsonValue &v : objekte) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("type")).toString() != QLatin1String("PipeWire:Interface:Metadata"))
            continue;
        const QString meta = o.value(QStringLiteral("props")).toObject().value(QStringLiteral("metadata.name")).toString();
        for (const QJsonValue &e : o.value(QStringLiteral("metadata")).toArray()) {
            const QJsonObject eo = e.toObject();
            const QString key = eo.value(QStringLiteral("key")).toString();
            const QJsonValue wert = eo.value(QStringLiteral("value"));
            if (meta == QLatin1String("default") && key == QLatin1String("default.audio.sink"))
                std::printf("standard_senke %s\n", qPrintable(wert.isObject()
                    ? wert.toObject().value(QStringLiteral("name")).toString() : wert.toString()));
            if (meta == QLatin1String("settings") && key == QLatin1String("clock.force-quantum"))
                std::printf("force_quantum %lld\n", qlonglong(wert.toInteger()));
        }
    }
    for (const auto &[id, name] : namen)
        if (name.startsWith(QLatin1String("dss_p12a")))
            std::printf("knoten %lld %s\n", qlonglong(id), qPrintable(name));
    auto echt = [](const QString &n) {
        return n.startsWith(QLatin1String("alsa_")) || n.startsWith(QLatin1String("bluez_"));
    };
    for (const QJsonValue &v : objekte) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("type")).toString() != QLatin1String("PipeWire:Interface:Link"))
            continue;
        const QJsonObject info = o.value(QStringLiteral("info")).toObject();
        const QString a = namen[info.value(QStringLiteral("output-node-id")).toInteger()];
        const QString b = namen[info.value(QStringLiteral("input-node-id")).toInteger()];
        if ((a.startsWith(QLatin1String("dss_p12a")) && echt(b)) || (b.startsWith(QLatin1String("dss_p12a")) && echt(a)))
            std::printf("gefahr %s -> %s\n", qPrintable(a), qPrintable(b));
    }
    return 0;
}

struct ServerLauf
{
    QFile csv;
    int64_t startNs = 0;
    std::map<QString, std::vector<BildMeldung>> bilder; // je Seite
    std::unique_ptr<Folger> folger;
    qint64 anzahl = 0;
};

QString csvFeld(const QString &s)
{
    QString t = s;
    t.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + t + QLatin1Char('"');
}

void meldungVerarbeiten(ServerLauf &l, const QJsonObject &m, qint64 ankunftNs)
{
    ++l.anzahl;
    const QString art = m.value(QStringLiteral("art")).toString();
    const QString seite = m.value(QStringLiteral("seite")).toString();
    auto d = [&](const char *k) { return m.value(QLatin1String(k)).toDouble(); };
    if (art == QLatin1String("bild")) {
        BildMeldung b;
        b.ankunftNs = ankunftNs;
        b.mediaTime = d("mediaTime");
        b.presentationTime = d("presentationTime");
        b.expectedDisplayTime = d("expectedDisplayTime");
        b.now = d("now");
        b.dauer = d("dauer");
        b.presentedFrames = qint64(d("presentedFrames"));
        b.schleife = m.value(QStringLiteral("schleife")).toInt();
        b.lauf = m.value(QStringLiteral("lauf")).toInt();
        l.bilder[seite].push_back(b);
    } else {
        std::printf("%.3f s %s: %s\n", (ankunftNs - l.startNs) / 1e9, qPrintable(art),
                    QJsonDocument(m).toJson(QJsonDocument::Compact).constData());
        std::fflush(stdout);
    }
    if (l.csv.isOpen()) {
        const QString rest = art == QLatin1String("bild")
            ? QString()
            : QString::fromUtf8(QJsonDocument(m).toJson(QJsonDocument::Compact));
        l.csv.write(QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11,%12,%13\n")
                        .arg((ankunftNs - l.startNs) / 1e6, 0, 'f', 3)
                        .arg(epochMs())
                        .arg(csvFeld(seite), art)
                        .arg(m.value(QStringLiteral("n")).toInt())
                        .arg(m.value(QStringLiteral("lauf")).toInt())
                        .arg(m.value(QStringLiteral("schleife")).toInt())
                        .arg(d("mediaTime"), 0, 'f', 6)
                        .arg(d("presentationTime"), 0, 'f', 3)
                        .arg(d("expectedDisplayTime"), 0, 'f', 3)
                        .arg(d("now"), 0, 'f', 3)
                        .arg(qint64(d("presentedFrames")))
                        .arg(rest.isEmpty() ? QString() : csvFeld(rest))
                        .toUtf8());
    }
    if (l.folger)
        l.folger->meldung(m, ankunftNs);
}

int server(const QCommandLineParser &p, const QString &daten)
{
    ServerLauf l;
    l.startNs = steadyNs();
    MeldeServer srv(daten, QStringLiteral(TON_QUELLORDNER "/messseite.html"));
    QString fehler;
    const QHostAddress adresse(p.value(QStringLiteral("lauschen")));
    if (!srv.lauschen(adresse, quint16(p.value(QStringLiteral("port")).toUInt()), &fehler)) {
        std::fprintf(stderr, "Lauschen auf %s:%s fehlgeschlagen: %s\n", qPrintable(adresse.toString()),
                     qPrintable(p.value(QStringLiteral("port"))), qPrintable(fehler));
        return 5;
    }
    const QString aus = p.value(QStringLiteral("aus"));
    if (!aus.isEmpty()) {
        l.csv.setFileName(aus + QStringLiteral("-meldungen.csv"));
        if (!l.csv.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            std::fprintf(stderr, "kann %s nicht schreiben\n", qPrintable(l.csv.fileName()));
            return 4;
        }
        l.csv.write("ankunft_ms,epoch_ms,seite,art,n,lauf,schleife,mediaTime,presentationTime,"
                    "expectedDisplayTime,now,presentedFrames,json\n");
    }
    if (p.isSet(QStringLiteral("folgen"))) {
        TonDatei ton;
        if (!tonLaden(p.value(QStringLiteral("folgen")), &ton, &fehler)) {
            std::fprintf(stderr, "Ton: %s\n", qPrintable(fehler));
            return 1;
        }
        FolgeOptionen fo;
        fo.ausgang = p.value(QStringLiteral("ausgang"));
        fo.pufferMs = p.isSet(QStringLiteral("puffer-ms")) ? p.value(QStringLiteral("puffer-ms")).toInt() : 40;
        fo.versatzMs = p.value(QStringLiteral("versatz-ms")).toDouble();
        fo.geraetMs = p.value(QStringLiteral("geraet-ms")).toDouble();
        fo.lautstaerke = p.value(QStringLiteral("lautstaerke")).toDouble();
        l.folger = std::make_unique<Folger>(std::move(ton), fo);
        if (!l.folger->starten(&fehler)) {
            std::fprintf(stderr, "%s\n%s", qPrintable(fehler), qPrintable(geraeteListe()));
            return 2;
        }
    }
    QObject::connect(&srv, &MeldeServer::meldung, &srv,
                     [&](const QJsonObject &m, qint64 ns) { meldungVerarbeiten(l, m, ns); });
    std::printf("Server: http://%s:%u/messseite.html (Daten %s)\n", qPrintable(adresse.toString()), srv.port(),
                qPrintable(daten));
    std::fflush(stdout);

    const int dauerS = p.value(QStringLiteral("dauer-s")).toInt();
    QTimer pruefen;
    qint64 letzteAnzeige = 0;
    QObject::connect(&pruefen, &QTimer::timeout, qApp, [&] {
        const int64_t ns = steadyNs();
        if (g_ende || (dauerS > 0 && ns - l.startNs >= int64_t(dauerS) * 1'000'000'000))
            qApp->quit();
        if (ns - letzteAnzeige >= 60'000'000'000) {
            letzteAnzeige = ns;
            size_t n = 0;
            for (const auto &[s, v] : l.bilder)
                n += v.size();
            std::printf("%.0f s: %lld Meldungen, davon «bild» %zu\n", (ns - l.startNs) / 1e9,
                        qlonglong(l.anzahl), n);
            std::fflush(stdout);
        }
    });
    pruefen.start(200);
    qApp->exec();

    QString t = QStringLiteral("Server-Lauf: %1 s, %2 Meldungen\n")
                    .arg((steadyNs() - l.startNs) / 1e9, 0, 'f', 1)
                    .arg(l.anzahl);
    for (const auto &[seite, v] : l.bilder)
        t += QStringLiteral("\n== Seite %1\n").arg(seite) + seitenAuswertung(v);
    if (l.folger) {
        l.folger->beenden();
        t += QStringLiteral("\n") + l.folger->zusammenfassung();
        if (!aus.isEmpty())
            l.folger->protokollSchreiben(aus + QStringLiteral("-folgen.csv"));
    }
    if (!aus.isEmpty()) {
        QFile z(aus + QStringLiteral("-zusammenfassung.txt"));
        if (z.open(QIODevice::WriteOnly | QIODevice::Truncate))
            z.write(t.toUtf8());
    }
    std::printf("%s", qPrintable(t));
    return 0;
}

int selbsttest(const QString &daten)
{
    MeldeServer srv(daten, QStringLiteral(TON_QUELLORDNER "/messseite.html"));
    QString fehler;
    if (!srv.lauschen(QHostAddress::LocalHost, 0, &fehler)) {
        std::fprintf(stderr, "Lauschen: %s\n", qPrintable(fehler));
        return 5;
    }
    std::vector<QJsonObject> empfangen;
    QObject::connect(&srv, &MeldeServer::meldung, &srv,
                     [&](const QJsonObject &m, qint64) { empfangen.push_back(m); });
    QNetworkAccessManager nam;
    const QString basis = QStringLiteral("http://127.0.0.1:%1").arg(srv.port());
    struct Antwort
    {
        int code = 0;
        QByteArray rumpf;
        QByteArray contentRange;
    };
    auto anfrage = [&](const QByteArray &methode, const QString &pfad, const QByteArray &rumpf = {},
                       const QByteArray &range = {}) {
        QNetworkRequest r(QUrl(basis + pfad));
        if (!range.isEmpty())
            r.setRawHeader("Range", range);
        if (!rumpf.isEmpty())
            r.setHeader(QNetworkRequest::ContentTypeHeader, QByteArray("text/plain"));
        QNetworkReply *a = methode == "POST" ? nam.post(r, rumpf) : nam.get(r);
        QEventLoop loop;
        QObject::connect(a, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
        Antwort x;
        x.code = a->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        x.rumpf = a->readAll();
        x.contentRange = a->rawHeader("Content-Range");
        a->deleteLater();
        return x;
    };
    int fehlerZahl = 0;
    auto pruefe = [&](bool ok, const QString &was) {
        std::printf("%s %s\n", ok ? "ok    " : "FEHLER", qPrintable(was));
        if (!ok)
            ++fehlerZahl;
    };
    Antwort a = anfrage("GET", QStringLiteral("/messseite.html"));
    pruefe(a.code == 200 && a.rumpf.contains("requestVideoFrameCallback"),
           QStringLiteral("GET /messseite.html → %1, %2 Bytes").arg(a.code).arg(a.rumpf.size()));
    QFile video(daten + QStringLiteral("/blitz-piep.webm"));
    const qint64 groesse = video.size();
    a = anfrage("GET", QStringLiteral("/medien/blitz-piep.webm"), {}, "bytes=0-99");
    pruefe(a.code == 206 && a.rumpf.size() == 100
               && a.contentRange == "bytes 0-99/" + QByteArray::number(groesse),
           QStringLiteral("GET Range bytes=0-99 → %1, %2 Bytes, %3").arg(a.code).arg(a.rumpf.size())
               .arg(QString::fromLatin1(a.contentRange)));
    a = anfrage("GET", QStringLiteral("/medien/blitz-piep.webm"), {}, "bytes=-10");
    pruefe(a.code == 206 && a.rumpf.size() == 10, QStringLiteral("GET Range bytes=-10 → %1, %2 Bytes")
                                                      .arg(a.code).arg(a.rumpf.size()));
    a = anfrage("GET", QStringLiteral("/medien/blitz-piep.mp4"));
    pruefe(a.code == 200 && a.rumpf.size() == QFile(daten + QStringLiteral("/blitz-piep.mp4")).size(),
           QStringLiteral("GET /medien/blitz-piep.mp4 ganz → %1, %2 Bytes").arg(a.code).arg(a.rumpf.size()));
    a = anfrage("GET", QStringLiteral("/medien/..%2Fmesseite.html"));
    pruefe(a.code == 404, QStringLiteral("GET /medien/..%2F… → %1").arg(a.code));
    a = anfrage("GET", QStringLiteral("/medien/fehlt.mp4"));
    pruefe(a.code == 404, QStringLiteral("GET /medien/fehlt.mp4 → %1").arg(a.code));
    const int64_t vorher = steadyNs();
    a = anfrage("POST", QStringLiteral("/melden"),
                R"({"art":"bild","seite":"t","n":1,"mediaTime":1.5,"now":1000,"obs":false})");
    pruefe(a.code == 204, QStringLiteral("POST /melden (Objekt) → %1 in %2 ms").arg(a.code)
                              .arg((steadyNs() - vorher) / 1e6, 0, 'f', 2));
    a = anfrage("POST", QStringLiteral("/melden"), R"([{"art":"start","seite":"t"},{"art":"stop","seite":"t"}])");
    pruefe(a.code == 204, QStringLiteral("POST /melden (Liste) → %1").arg(a.code));
    a = anfrage("POST", QStringLiteral("/melden"), "kein json");
    pruefe(a.code == 400, QStringLiteral("POST /melden (kein JSON) → %1").arg(a.code));
    pruefe(empfangen.size() == 3 && empfangen[0].value(QStringLiteral("mediaTime")).toDouble() == 1.5
               && empfangen[2].value(QStringLiteral("art")).toString() == QLatin1String("stop"),
           QStringLiteral("Meldungen empfangen: %1").arg(empfangen.size()));
    std::printf("%s\n", fehlerZahl == 0 ? "Selbsttest bestanden" : "Selbsttest NICHT bestanden");
    return fehlerZahl == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser p;
    p.addHelpOption();
    const QList<QCommandLineOption> optionen = {
        {QStringLiteral("liste"), QStringLiteral("Ausgänge und Eingänge auflisten")},
        {QStringLiteral("stille"), QStringLiteral("Kurzprobe: Nullen mit Lautstärke 0 spielen")},
        {QStringLiteral("dekodieren"), QStringLiteral("Ton einer Datei laden und Pieps finden"), QStringLiteral("datei")},
        {QStringLiteral("klicks"), QStringLiteral("M1: Klicks spielen und aufnehmen")},
        {QStringLiteral("server"), QStringLiteral("Messseite ausliefern und Meldungen aufzeichnen")},
        {QStringLiteral("selbsttest"), QStringLiteral("Server ohne Browser prüfen")},
        {QStringLiteral("pw-zustand"), QStringLiteral("pw-dump von stdin auswerten (für nachtlauf.sh)")},
        {QStringLiteral("ausgang"), QStringLiteral("Ausgang (Teil der Kennung)"), QStringLiteral("text")},
        {QStringLiteral("eingang"), QStringLiteral("Eingang (Teil der Kennung)"), QStringLiteral("text")},
        {QStringLiteral("puffer-ms"), QStringLiteral("Puffer von QAudioSink"), QStringLiteral("ms"), QStringLiteral("20")},
        {QStringLiteral("dauer-s"), QStringLiteral("Dauer"), QStringLiteral("s"), QStringLiteral("0")},
        {QStringLiteral("abstand-ms"), QStringLiteral("Abstand der Klicks"), QStringLiteral("ms"), QStringLiteral("500")},
        {QStringLiteral("aus"), QStringLiteral("Präfix der Ausgabedateien"), QStringLiteral("pfad")},
        {QStringLiteral("lauschen"), QStringLiteral("Adresse des Servers"), QStringLiteral("adresse"), QStringLiteral("127.0.0.1")},
        {QStringLiteral("port"), QStringLiteral("Port des Servers"), QStringLiteral("port"), QStringLiteral("24990")},
        {QStringLiteral("daten"), QStringLiteral("Ordner der Videos"), QStringLiteral("ordner")},
        {QStringLiteral("folgen"), QStringLiteral("Ton dieser Datei nach den Meldungen spielen"), QStringLiteral("datei")},
        {QStringLiteral("versatz-ms"), QStringLiteral("Ton später um"), QStringLiteral("ms"), QStringLiteral("0")},
        {QStringLiteral("geraet-ms"), QStringLiteral("Verzögerung nach dem Puffer"), QStringLiteral("ms"), QStringLiteral("0")},
        {QStringLiteral("lautstaerke"), QStringLiteral("Pegel beim Nachführen (0–1)"), QStringLiteral("wert"), QStringLiteral("0.25")},
    };
    p.addOptions(optionen);
    p.process(app);
    const QString daten = p.isSet(QStringLiteral("daten")) ? p.value(QStringLiteral("daten"))
                                                           : repoOrdner() + QStringLiteral("/tests/daten/ton");

    if (p.isSet(QStringLiteral("pw-zustand")))
        return pwZustand();
    if (p.isSet(QStringLiteral("liste"))) {
        std::printf("QCoreApplication (ohne QGuiApplication), Qt %s\n%s", qVersion(), qPrintable(geraeteListe()));
        return 0;
    }
    if (p.isSet(QStringLiteral("stille"))) {
        const int dauer = p.value(QStringLiteral("dauer-s")).toInt();
        return stilleProbe(p.value(QStringLiteral("ausgang")), p.value(QStringLiteral("puffer-ms")).toInt(),
                           dauer > 0 ? std::min(dauer, 30) : 5);
    }
    if (p.isSet(QStringLiteral("dekodieren")))
        return dekodieren(p.value(QStringLiteral("dekodieren")));
    if (p.isSet(QStringLiteral("klicks"))) {
        KlickOptionen o;
        o.ausgang = p.value(QStringLiteral("ausgang"));
        o.eingang = p.value(QStringLiteral("eingang"));
        o.pufferMs = p.value(QStringLiteral("puffer-ms")).toInt();
        o.dauerS = p.value(QStringLiteral("dauer-s")).toInt();
        o.abstandMs = p.value(QStringLiteral("abstand-ms")).toInt();
        o.aus = p.value(QStringLiteral("aus"));
        if (o.ausgang.isEmpty() || o.eingang.isEmpty() || o.aus.isEmpty() || o.dauerS <= 0) {
            // Sicherung: Klicks nie auf den Standardausgang (dort hängt der RØDECaster)
            std::fprintf(stderr, "--klicks braucht --ausgang, --eingang, --aus und --dauer-s > 0\n");
            return 64;
        }
        return klickMessung(o);
    }
    if (p.isSet(QStringLiteral("server"))) {
        if (p.isSet(QStringLiteral("folgen")) && p.value(QStringLiteral("ausgang")).isEmpty()) {
            std::fprintf(stderr, "--folgen braucht --ausgang (nie stillschweigend der Standardausgang)\n");
            return 64;
        }
        // Nur hier: SIGTERM beendet geordnet und schreibt die Auswertung (systemctl stop).
        // Sonst beendet SIGTERM sofort (Abbruch des Nachtlaufs).
        std::signal(SIGTERM, signalEnde);
        std::signal(SIGINT, signalEnde);
        return server(p, daten);
    }
    if (p.isSet(QStringLiteral("selbsttest")))
        return selbsttest(daten);
    p.showHelp(64);
}
