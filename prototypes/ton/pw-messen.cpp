// pw-messen: Prototyp P12b, Abschnitt b1 (Plan docs/archiv/prompts/P12-ton.md). Ausgabe über
// pw_stream (libpipewire 0.3) in pw_thread_loop, mit QCoreApplication wie der Kern, ohne Qt Gui und
// ohne Qt Multimedia. Messung Z3 wie M1, nur die Ausgabe ist anders (pwklick.h).
//
//   pw-messen --liste                                   Ausgänge und Eingänge über die Registry
//   pw-messen --klicks --ausgang dss_p12a_ausgabe --eingang dss_p12a_aufnahme [--latenz-ms 40]
//             [--dauer-s 120] [--abstand-ms 500] --aus <Präfix> [--marken <Datei>]          (Z3)
//
// Klicks gehen nur auf Knoten «dss_p12a…» (virtuelle Senke von pw-lauf.sh), nie auf ein Gerät.
#include "pwklick.h"

#include <QCommandLineParser>
#include <QCoreApplication>

#include <pipewire/pipewire.h>

#include <atomic>
#include <csignal>
#include <cstdio>

namespace {

std::atomic<bool> g_ende{false};

void signalEnde(int)
{
    g_ende = true;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    pw_init(&argc, &argv);
    QCommandLineParser p;
    p.addHelpOption();
    p.addOptions({
        {QStringLiteral("liste"), QStringLiteral("Ausgänge und Eingänge über die Registry auflisten")},
        {QStringLiteral("klicks"), QStringLiteral("Z3: Klicks über pw_stream spielen und aufnehmen")},
        {QStringLiteral("ausgang"), QStringLiteral("node.name der Senke"), QStringLiteral("name")},
        {QStringLiteral("eingang"), QStringLiteral("node.name der Quelle"), QStringLiteral("name")},
        {QStringLiteral("latenz-ms"), QStringLiteral("node.latency der Ströme"), QStringLiteral("ms"), QStringLiteral("40")},
        {QStringLiteral("dauer-s"), QStringLiteral("Dauer ab Freigabe der Klicks"), QStringLiteral("s"), QStringLiteral("0")},
        {QStringLiteral("abstand-ms"), QStringLiteral("Abstand der Klicks"), QStringLiteral("ms"), QStringLiteral("500")},
        {QStringLiteral("aus"), QStringLiteral("Präfix der Ausgabedateien"), QStringLiteral("pfad")},
        {QStringLiteral("marken"), QStringLiteral("Datei mit Zeilen «<epoch_ms> <Text>»"), QStringLiteral("datei")},
    });
    p.process(app);

    int rc = 64;
    if (p.isSet(QStringLiteral("liste"))) {
        rc = pwListe();
    } else if (p.isSet(QStringLiteral("klicks"))) {
        PwKlickOptionen o;
        o.ausgang = p.value(QStringLiteral("ausgang"));
        o.eingang = p.value(QStringLiteral("eingang"));
        o.latenzMs = p.value(QStringLiteral("latenz-ms")).toInt();
        o.dauerS = p.value(QStringLiteral("dauer-s")).toInt();
        o.abstandMs = p.value(QStringLiteral("abstand-ms")).toInt();
        o.aus = p.value(QStringLiteral("aus"));
        o.marken = p.value(QStringLiteral("marken"));
        o.ende = &g_ende;
        if (o.ausgang.isEmpty() || o.eingang.isEmpty() || o.aus.isEmpty() || o.dauerS <= 0 || o.latenzMs <= 0
            || o.abstandMs < 300) {
            // Sicherung: nie stillschweigend der Standardausgang; Abstand ≥ 300 ms, damit die
            // Erkennung (250 ms Ruhe) jeden Klick einzeln findet
            std::fprintf(stderr, "--klicks braucht --ausgang, --eingang, --aus, --dauer-s > 0, --latenz-ms > 0 "
                                 "und --abstand-ms ≥ 300\n");
        } else {
            // SIGTERM (systemctl stop) und SIGINT beenden die Messung geordnet mit Auswertung
            std::signal(SIGTERM, signalEnde);
            std::signal(SIGINT, signalEnde);
            rc = pwKlickMessung(o);
        }
    } else {
        p.showHelp(64);
    }
    pw_deinit();
    return rc;
}
