// dss-cli: Werkzeug ohne Fenster für den Handtest des Overlay-Modells (P04).
// Enthält keine Logik ausser dem Lesen der Befehlszeile und der Ausgabe; alles andere
// steckt in dss_common (OverlayStore). Gibt nie einen Token aus (CLAUDE.md Regel 12).
// Seit P07 (Entscheid 2): Solange der Kern läuft, ändert nur er die Overlay-Dateien. Anzeige-
// Befehle lesen dann nur (nichts wird geschrieben); Änderungs-Befehle nehmen die Sperre im
// Datenordner und brechen ab, wenn sie belegt ist.

#include "browsersource.h"
#include "configpaths.h"
#include "coresession.h"
#include "coresettings.h"
#include "datalock.h"
#include "overlaylive.h"
#include "overlaystore.h"
#if DSS_MEDIA_SIZE
#include "mediainfo.h"
#endif
#include "version.h"

#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include <functional>

namespace {

constexpr int kExitOk = 0;
constexpr int kExitError = 1;
constexpr int kExitUsage = 2;
constexpr int kExitLocked = 3;
constexpr int kExitCoreNotRunning = 4;

// Befehle, die nur anzeigen; alle anderen ändern den Datenordner.
const QStringList kReadOnlyCommands = {QStringLiteral("overlay-liste"), QStringLiteral("quelle-liste"),
                                       QStringLiteral("medien-liste")};
// Befehle für die Einstellungen des Kerns (P10-vor): ohne Datenordner und ohne Sperre.
const QStringList kSettingsCommands = {QStringLiteral("einstellungen"), QStringLiteral("einstellung-port"),
                                       QStringLiteral("einstellung-adresse")};
// Befehle für die Medienliste (P10b, Teil A 6): Nur der Kern schreibt medien.json; dss-cli
// schickt die Änderung an den laufenden Kern. Name → Anzahl Argumente.
const QList<std::pair<QString, int>> kMediaCommands = {
    {QStringLiteral("medium-umbenennen"), 2},       {QStringLiteral("medium-verschieben"), 2},
    {QStringLiteral("medium-loeschen"), 1},         {QStringLiteral("medien-ordner-anlegen"), 1},
    {QStringLiteral("medien-ordner-umbenennen"), 2}, {QStringLiteral("medien-ordner-verschieben"), 2},
    {QStringLiteral("medien-ordner-loeschen"), 1}};

// Laufzustand der Overlays (P14 d): gehen an den laufenden Kern, er hält ihn nur im Speicher.
const QStringList kLiveCommands = {QStringLiteral("live"), QStringLiteral("live-zeigen"),
                                   QStringLiteral("live-ausblenden")};

bool isMediaCommand(const QString &command)
{
    return std::any_of(kMediaCommands.begin(), kMediaCommands.end(),
                       [&command](const auto &c) { return c.first == command; });
}

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

QTextStream &err()
{
    static QTextStream stream(stderr);
    return stream;
}

const char *const kUsageTemplate =
    "Aufruf: dss-cli [--datenordner PFAD] [--verbindung PFAD] BEFEHL [ARGUMENTE]\n"
    "\n"
    "Befehle:\n"
    "  overlay-liste\n"
    "  overlay-neu NAME BREITE HOEHE\n"
    "  overlay-umbenennen ADRESSE NEUER_NAME [--adresse-mitaendern]\n"
    "  overlay-kopieren ADRESSE\n"
    "  overlay-loeschen ADRESSE\n"
    "  neuer-token ADRESSE [--ja]\n"
    "  quelle-liste ADRESSE\n"
    "  quelle-bild ADRESSE DATEI\n"
    "  quelle-video ADRESSE DATEI [--wiederholen] [--am-ende-anhalten]\n"
    "  quelle-text ADRESSE TEXT SCHRIFTDATEI\n"
    "  quelle-geometrie ADRESSE NR X Y BREITE HOEHE DREHUNG [--waagrecht-gespiegelt]\n"
    "                   [--senkrecht-gespiegelt]\n"
    "  medien-liste\n"
    "  medium-umbenennen DATEI NEUER_NAME\n"
    "  medium-verschieben DATEI ZIELORDNER\n"
    "  medium-loeschen DATEI\n"
    "  medien-ordner-anlegen ORDNER\n"
    "  medien-ordner-umbenennen ORDNER NEUER_NAME\n"
    "  medien-ordner-verschieben ORDNER ZIELORDNER\n"
    "  medien-ordner-loeschen ORDNER\n"
    "  einstellungen\n"
    "  einstellung-port PORT\n"
    "  einstellung-adresse ADRESSE\n"
    "  live ADRESSE\n"
    "  live-zeigen ADRESSE NR[,NR…] [--verstecken|--umschalten] [--dauer MS] [--text TEXT]\n"
    "              [--bild BILD] [NAME=WERT …]\n"
    "  live-ausblenden ADRESSE [NR …]\n"
    "\n"
    "--wiederholen: Video am Ende wieder von vorne (ohne: nicht wiederholen).\n"
    "--am-ende-anhalten: letztes Bild am Ende stehen lassen (ohne beides: Quelle wird leer).\n"
    "quelle-bild und quelle-video: Grösse der Quelle = Grösse der Datei (Video: erstes Bild).\n"
    "NR: Nummer der Quelle aus quelle-liste (1 = ganz hinten).\n"
    "DREHUNG: Grad mit höchstens einer Nachkommastelle, 0 bis 359.9 (zum Beispiel 12.5).\n"
    "--waagrecht-gespiegelt, --senkrecht-gespiegelt: Inhalt im Kasten spiegeln (ohne: nicht\n"
    "gespiegelt). Breite und Höhe bleiben positiv.\n"
    "neuer-token: neuer Token für die Browserquelle (P10). Die alte Adresse funktioniert danach\n"
    "nicht mehr; dss-cli fragt vorher nach (--ja: ohne Rückfrage). Der Token wird nicht ausgegeben.\n"
    "Ohne --datenordner: Datenordner des Programms (~/.local/share/%1).\n"
    "overlay-liste und quelle-liste gehen immer. Alle anderen Befehle ändern Overlays und gehen\n"
    "nur, wenn der Kern angehalten ist (systemctl --user stop %2).\n"
    "einstellungen: Port und Adresse im Heimnetz des Kerns (kern.ini). Läuft der Kern, fragt dss-cli\n"
    "ihn; sonst liest es die Datei ~/.config/%1/kern.ini (nur lesen).\n"
    "einstellung-port, einstellung-adresse: ändern über den laufenden Kern (nur er schreibt die\n"
    "Datei). PORT 1024 bis 32767; ADRESSE IPv4 wie 192.168.1.9, \"\" = leer (der Kern trägt\n"
    "innerhalb von 2 s die Adresse der Standardroute ein, sobald es eine gibt); nur Adressen\n"
    "dieses PCs. Beides wirkt sofort (der Overlay-Server startet neu; in OBS dann die neue\n"
    "Adresse der Browserquelle eintragen).\n"
    "medien-liste: alle Mediendateien mit Name, Ordner, Typ, Grösse und «verwendet in» (geht immer).\n"
    "Die übrigen medien- und medium-Befehle ändern die Medienliste über den laufenden Kern (nur er\n"
    "schreibt medien.json). DATEI: Nr aus medien-liste, Verweis media/… oder Name (eindeutig).\n"
    "ORDNER, ZIELORDNER: Pfad wie in medien-liste, zum Beispiel «Logos/Alt»; \"/\" = oberste Ebene.\n"
    "medien-ordner-anlegen legt den letzten Teil des Pfads an. Löschen eines Ordners: Inhalt kommt\n"
    "eine Ebene höher. medium-loeschen löscht nur Dateien, die keine Quelle verwendet.\n"
    "live, live-zeigen, live-ausblenden: Laufzustand eines Overlays über den laufenden Kern (wie\n"
    "ein Schritt des Chat-Bots, nur «jetzt», nie in der Overlay-Datei, nach einem Neustart weg).\n"
    "live zeigt ihn je Element. live-zeigen zeigt die Elemente NR (aus quelle-liste), mit\n"
    "--verstecken versteckt, mit --umschalten schaltet es um; --dauer: danach fällt der Laufzustand\n"
    "weg (ohne: bis live-ausblenden); --text ersetzt den Text der Textelemente, --bild das Bild der\n"
    "Bildelemente (media/… oder Adresse eines Profilbilds); NAME=WERT sind Argumente für die\n"
    "Platzhalter %NAME% im Text. live-ausblenden setzt die Elemente NR zurück (ohne NR: alle).\n"
    "Ohne --verbindung: Verbindung des Kerns dieser Version ($XDG_RUNTIME_DIR/%1/kern.sock).\n"
    "Exit-Code: 0 = erfolgreich, 1 = Fehler, 2 = falscher Aufruf, 3 = Kern läuft (Datenordner\n"
    "gesperrt), 4 = Kern läuft nicht (einstellung-port, einstellung-adresse, Medien ändern, live).\n";

// Hilfe mit Datenordner und Einheit dieser Version (P10a, Teil B)
QString usageText()
{
    return QString::fromUtf8(kUsageTemplate).arg(dss::appDirName(), dss::coreUnitName());
}

struct Arguments
{
    QString dataDir;
    QString connection; // P10-vor: Verbindung zum Kern (nur Einstellungen)
    bool changeAddress = false;
    bool loop = false;
    bool holdAtEnd = false;
    bool flipHorizontal = false; // P09b
    bool flipVertical = false;   // P09b
    bool yes = false;            // P10: neuer-token ohne Rückfrage
    // P14 d: live-zeigen
    bool liveHide = false;
    bool liveToggle = false;
    std::optional<int> liveDurationMs;
    std::optional<QString> liveText;
    std::optional<QString> liveImage;
    QStringList positional; // Befehl und seine Argumente
};

// Eigene Auswertung statt QCommandLineParser, damit negative Zahlen («-100») als
// Argument und nicht als Option gelten.
bool parseArguments(const QStringList &args, Arguments *result, QString *error)
{
    for (qsizetype i = 1; i < args.size(); ++i) {
        const QString &a = args.at(i);
        if (a == QLatin1String("--datenordner")) {
            if (i + 1 >= args.size()) {
                *error = QStringLiteral("--datenordner braucht einen Pfad.");
                return false;
            }
            result->dataDir = args.at(++i);
        } else if (a == QLatin1String("--verbindung")) {
            if (i + 1 >= args.size()) {
                *error = QStringLiteral("--verbindung braucht einen Pfad.");
                return false;
            }
            result->connection = args.at(++i);
        } else if (a == QLatin1String("--adresse-mitaendern")) {
            result->changeAddress = true;
        } else if (a == QLatin1String("--wiederholen")) {
            result->loop = true;
        } else if (a == QLatin1String("--am-ende-anhalten")) {
            result->holdAtEnd = true;
        } else if (a == QLatin1String("--waagrecht-gespiegelt")) {
            result->flipHorizontal = true;
        } else if (a == QLatin1String("--senkrecht-gespiegelt")) {
            result->flipVertical = true;
        } else if (a == QLatin1String("--ja")) {
            result->yes = true;
        } else if (a == QLatin1String("--verstecken")) {
            result->liveHide = true;
        } else if (a == QLatin1String("--umschalten")) {
            result->liveToggle = true;
        } else if (a == QLatin1String("--dauer") || a == QLatin1String("--text") || a == QLatin1String("--bild")) {
            if (i + 1 >= args.size()) {
                *error = QStringLiteral("%1 braucht einen Wert.").arg(a);
                return false;
            }
            const QString value = args.at(++i);
            if (a == QLatin1String("--text")) {
                result->liveText = value;
            } else if (a == QLatin1String("--bild")) {
                result->liveImage = value;
            } else {
                bool ok = false;
                result->liveDurationMs = value.toInt(&ok);
                if (!ok) {
                    *error = QStringLiteral("--dauer «%1» ist keine ganze Zahl.").arg(value);
                    return false;
                }
            }
        } else if (a.startsWith(QLatin1String("--"))) {
            *error = QStringLiteral("Unbekannte Option %1.").arg(a);
            return false;
        } else {
            result->positional.append(a);
        }
    }
    return true;
}

bool parseInt(const QString &text, const QString &what, int *value, QString *error)
{
    bool ok = false;
    *value = text.toInt(&ok);
    if (!ok)
        *error = QStringLiteral("%1 «%2» ist keine ganze Zahl.").arg(what, text);
    return ok;
}

// "12.5" → 125 Zehntelgrad. Ohne Umweg über Gleitkommazahlen.
bool parseRotation(const QString &text, int *tenths, QString *error)
{
    static const QRegularExpression pattern(QStringLiteral("^(\\d{1,3})(?:\\.(\\d))?$"));
    const QRegularExpressionMatch m = pattern.match(text);
    if (m.hasMatch()) {
        *tenths = m.captured(1).toInt() * 10 + (m.captured(2).isEmpty() ? 0 : m.captured(2).toInt());
        if (*tenths >= dss::kRotationMin && *tenths <= dss::kRotationMax)
            return true;
    }
    *error = QStringLiteral("Drehung «%1» ungültig, erlaubt sind 0 bis 359.9 Grad mit höchstens "
                            "einer Nachkommastelle.").arg(text);
    return false;
}

QString yesNo(bool value)
{
    return value ? QStringLiteral("ja") : QStringLiteral("nein");
}

// Spalte «Gespiegelt» (P09b): nein, waagrecht, senkrecht oder beide.
QString flipText(const dss::Source &s)
{
    if (s.flipHorizontal && s.flipVertical)
        return QStringLiteral("beide");
    if (s.flipHorizontal)
        return QStringLiteral("waagrecht");
    if (s.flipVertical)
        return QStringLiteral("senkrecht");
    return QStringLiteral("nein");
}

// Gibt eine Tabelle mit linksbündigen Spalten aus.
void printTable(const QList<QStringList> &rows)
{
    QList<qsizetype> widths;
    for (const QStringList &row : rows) {
        for (qsizetype c = 0; c < row.size(); ++c) {
            if (widths.size() <= c)
                widths.append(0);
            widths[c] = std::max(widths[c], row.at(c).size());
        }
    }
    for (const QStringList &row : rows) {
        QString line;
        for (qsizetype c = 0; c < row.size(); ++c) {
            line += c + 1 < row.size() ? row.at(c).leftJustified(widths.at(c) + 2) : row.at(c);
        }
        out() << line << '\n';
    }
}

class Cli
{
public:
    Cli(dss::OverlayStore *store, const Arguments &args)
        : m_store(store), m_args(args)
    {
    }

    int run()
    {
        const QString command = m_args.positional.value(0);
        const QStringList a = m_args.positional.mid(1);
        if (command != QLatin1String("overlay-umbenennen") && m_args.changeAddress)
            return usage(QStringLiteral("--adresse-mitaendern gilt nur für overlay-umbenennen."));
        if (command != QLatin1String("quelle-video") && m_args.loop)
            return usage(QStringLiteral("--wiederholen gilt nur für quelle-video."));
        if (command != QLatin1String("quelle-video") && m_args.holdAtEnd)
            return usage(QStringLiteral("--am-ende-anhalten gilt nur für quelle-video."));
        if (command != QLatin1String("neuer-token") && m_args.yes)
            return usage(QStringLiteral("--ja gilt nur für neuer-token."));
        if (command != QLatin1String("quelle-geometrie") && (m_args.flipHorizontal || m_args.flipVertical))
            return usage(QStringLiteral("--waagrecht-gespiegelt und --senkrecht-gespiegelt gelten nur für "
                                        "quelle-geometrie."));
        if (m_args.liveHide || m_args.liveToggle || m_args.liveDurationMs || m_args.liveText || m_args.liveImage)
            return usage(QStringLiteral("--verstecken, --umschalten, --dauer, --text und --bild gelten nur für "
                                        "live-zeigen."));

        struct Command
        {
            const char *name;
            int argumentCount;
            std::function<int(const QStringList &)> run;
        };
        const QList<Command> commands = {
            {"overlay-liste", 0, [this](const QStringList &) { return listOverlays(); }},
            {"overlay-neu", 3, [this](const QStringList &x) { return createOverlay(x); }},
            {"overlay-umbenennen", 2, [this](const QStringList &x) { return renameOverlay(x); }},
            {"overlay-kopieren", 1, [this](const QStringList &x) { return copyOverlay(x); }},
            {"overlay-loeschen", 1, [this](const QStringList &x) { return removeOverlay(x); }},
            {"neuer-token", 1, [this](const QStringList &x) { return newToken(x); }},
            {"quelle-liste", 1, [this](const QStringList &x) { return listSources(x); }},
            {"quelle-bild", 2, [this](const QStringList &x) { return addMedia(x, dss::SourceKind::Image); }},
            {"quelle-video", 2, [this](const QStringList &x) { return addMedia(x, dss::SourceKind::Video); }},
            {"quelle-text", 3, [this](const QStringList &x) { return addText(x); }},
            {"quelle-geometrie", 7, [this](const QStringList &x) { return setGeometry(x); }},
            {"medien-liste", 0, [this](const QStringList &) { return listMedia(); }},
        };
        for (const Command &c : commands) {
            if (command == QLatin1String(c.name)) {
                if (a.size() != c.argumentCount)
                    return usage(QStringLiteral("%1 erwartet %2 Argument(e), erhalten %3.")
                                     .arg(command).arg(c.argumentCount).arg(a.size()));
                return c.run(a);
            }
        }
        return usage(command.isEmpty() ? QStringLiteral("Befehl fehlt.")
                                       : QStringLiteral("Unbekannter Befehl %1.").arg(command));
    }

private:
    int usage(const QString &message)
    {
        err() << "Fehler: " << message << "\n\n" << usageText();
        return kExitUsage;
    }

    int fail(const QString &message)
    {
        err() << "Fehler: " << message << '\n';
        return kExitError;
    }

    const dss::Overlay *find(const QString &address, QString *error)
    {
        const dss::Overlay *overlay = m_store->overlayByAddress(address);
        if (!overlay)
            *error = QStringLiteral("Kein Overlay mit der Adresse «%1».").arg(address);
        return overlay;
    }

    int listOverlays()
    {
        const QList<dss::Overlay> overlays = m_store->overlays();
        if (overlays.isEmpty()) {
            out() << "Keine Overlays vorhanden.\n";
            return kExitOk;
        }
        // Reihenfolge und Ordner wie in der Liste des Fensters (liste.json, P08 Teil B).
        QList<QStringList> rows = {{QStringLiteral("Ordner"), QStringLiteral("Name"),
                                    QStringLiteral("Adresse"), QStringLiteral("Auflösung"),
                                    QStringLiteral("Quellen")}};
        const auto addRow = [&](const QString &folder, const QUuid &id) {
            const dss::Overlay *o = m_store->overlay(id);
            if (!o)
                return;
            rows.append({folder, o->name, o->address,
                         QStringLiteral("%1x%2").arg(o->width).arg(o->height),
                         QString::number(o->sources.size())});
        };
        for (const dss::ListEntry &e : m_store->layout().entries) {
            if (!e.isFolder) {
                addRow(QStringLiteral("–"), e.overlayId);
                continue;
            }
            for (const QUuid &id : e.folder.overlays)
                addRow(e.folder.name, id);
        }
        printTable(rows);
        return kExitOk;
    }

    int createOverlay(const QStringList &a)
    {
        QString error;
        int width = 0;
        int height = 0;
        if (!parseInt(a.at(1), QStringLiteral("BREITE"), &width, &error)
            || !parseInt(a.at(2), QStringLiteral("HOEHE"), &height, &error))
            return fail(error);
        const QUuid id = m_store->createOverlay(a.at(0), width, height, &error);
        if (id.isNull())
            return fail(error);
        const dss::Overlay *o = m_store->overlay(id);
        out() << QStringLiteral("Overlay «%1» angelegt: Adresse %2, %3x%4\n")
                     .arg(o->name, o->address).arg(o->width).arg(o->height);
        return kExitOk;
    }

    int renameOverlay(const QStringList &a)
    {
        QString error;
        const dss::Overlay *o = find(a.at(0), &error);
        if (!o)
            return fail(error);
        const QUuid id = o->id;
        const QString oldAddress = o->address;
        if (!m_store->renameOverlay(id, a.at(1), m_args.changeAddress, &error))
            return fail(error);
        o = m_store->overlay(id);
        if (o->address == oldAddress)
            out() << QStringLiteral("Overlay umbenannt in «%1», Adresse %2 (unverändert)\n")
                         .arg(o->name, o->address);
        else
            out() << QStringLiteral("Overlay umbenannt in «%1», Adresse %2 (vorher %3)\n")
                         .arg(o->name, o->address, oldAddress);
        return kExitOk;
    }

    int copyOverlay(const QStringList &a)
    {
        QString error;
        const dss::Overlay *o = find(a.at(0), &error);
        if (!o)
            return fail(error);
        const QUuid id = m_store->copyOverlay(o->id, &error);
        if (id.isNull())
            return fail(error);
        const dss::Overlay *copy = m_store->overlay(id);
        out() << QStringLiteral("Kopie «%1» angelegt: Adresse %2, %3 Quelle(n)\n")
                     .arg(copy->name, copy->address).arg(copy->sources.size());
        return kExitOk;
    }

    int removeOverlay(const QStringList &a)
    {
        QString error;
        const dss::Overlay *o = find(a.at(0), &error);
        if (!o)
            return fail(error);
        const QString name = o->name;
        const QString address = o->address;
        if (!m_store->removeOverlay(o->id, &error))
            return fail(error);
        out() << QStringLiteral("Overlay «%1» (Adresse %2) gelöscht\n").arg(name, address);
        return kExitOk;
    }

    // P10 (Teil A 8): dieselbe Rückfrage wie der Knopf «Neuer Token» im Fenster.
    int newToken(const QStringList &a)
    {
        QString error;
        const dss::Overlay *o = find(a.at(0), &error);
        if (!o)
            return fail(error);
        if (!m_args.yes) {
            out() << dss::newTokenQuestion() << " [j/N] ";
            out().flush();
            QTextStream in(stdin);
            const QString answer = in.readLine().trimmed().toLower();
            if (answer != QLatin1String("j") && answer != QLatin1String("ja")) {
                out() << "Abgebrochen, Token unverändert.\n";
                return kExitError;
            }
        }
        if (!m_store->newToken(o->id, &error))
            return fail(error);
        out() << QStringLiteral("Neuer Token für Overlay «%1» (Adresse %2). Die neue Adresse der "
                                "Browserquelle steht im Fenster, Dock «Steuerung».\n")
                     .arg(o->name, o->address);
        return kExitOk;
    }

    int listSources(const QStringList &a)
    {
        QString error;
        const dss::Overlay *o = find(a.at(0), &error);
        if (!o)
            return fail(error);
        if (o->sources.isEmpty()) {
            out() << QStringLiteral("Overlay «%1» hat keine Quellen.\n").arg(o->name);
            return kExitOk;
        }
        QList<QStringList> rows = {{QStringLiteral("Nr"), QStringLiteral("Art"),
                                    QStringLiteral("Sichtbar"), QStringLiteral("X"),
                                    QStringLiteral("Y"), QStringLiteral("Breite"),
                                    QStringLiteral("Höhe"), QStringLiteral("Drehung"),
                                    QStringLiteral("Gespiegelt"), QStringLiteral("Wiederholen"), QStringLiteral("Am Ende anhalten"),
                                    QStringLiteral("Lautstärke"), QStringLiteral("Stumm"), QStringLiteral("Name")}};
        for (qsizetype i = 0; i < o->sources.size(); ++i) {
            const dss::Source &s = o->sources.at(i);
            const bool video = s.kind == dss::SourceKind::Video;
            // P12b b6: Lautstärke und Stumm (Format 5), nur bei Video
            rows.append({QString::number(i + 1), dss::sourceKindName(s.kind), yesNo(s.visible),
                         QString::number(s.x), QString::number(s.y), QString::number(s.width),
                         QString::number(s.height), dss::rotationDisplay(s.rotation),
                         flipText(s), video ? yesNo(s.loop) : QStringLiteral("–"),
                         video ? yesNo(s.holdAtEnd) : QStringLiteral("–"),
                         video ? QStringLiteral("%1 %").arg(s.volume) : QStringLiteral("–"),
                         video ? yesNo(s.muted) : QStringLiteral("–"), s.name});
        }
        printTable(rows);
        return kExitOk;
    }

    // P10b, Teil A 6: Medienliste aus dem Datenordner (nur gelesen, abgeglichen mit media/ wie im
    // Kern; Ersatznamen für Dateien ohne Eintrag).
    int listMedia()
    {
        const dss::MediaLibrary &library = m_store->mediaLibrary();
        if (!m_store->mediaLibraryError().isEmpty())
            err() << "Fehler: " << m_store->mediaLibraryError() << " (Ersatznamen)\n";
        const QList<dss::MediaEntry> files = dss::mediaFilesInOrder(library);
        if (files.isEmpty() && library.folders.isEmpty()) {
            out() << "Keine Mediendateien vorhanden.\n";
            return kExitOk;
        }
        const dss::MediaUsages usages = m_store->mediaUsages();
        QList<QStringList> rows = {{QStringLiteral("Nr"), QStringLiteral("Name"),
                                    QStringLiteral("Ordner"), QStringLiteral("Typ"),
                                    QStringLiteral("Grösse"), QStringLiteral("Format"),
                                    QStringLiteral("Datei"), QStringLiteral("Verwendet in")}};
        QStringList problems;
        for (qsizetype i = 0; i < files.size(); ++i) {
            const dss::MediaEntry &e = files.at(i);
            const QString folder = dss::mediaFolderPath(library, e.folder);
            const QString codec = m_store->videoCodec(e.file);
            const QString used = dss::usageText(usages.value(e.file));
            rows.append({QString::number(i + 1), e.name, folder.isEmpty() ? QStringLiteral("/") : folder,
                         dss::mediaKindName(dss::mediaKindOf(e.file).value_or(dss::MediaKind::Image)),
                         dss::mediaSizeText(m_store->mediaBytes(e.file)),
                         codec.isEmpty() ? QStringLiteral("–") : codec, e.file,
                         used.isEmpty() ? QStringLiteral("ungenutzt") : used.mid(13)});
            if (const QString p = m_store->browserProblem(e.file); !p.isEmpty())
                problems.append(QStringLiteral("Nr %1: %2").arg(i + 1).arg(p));
        }
        printTable(rows);
        // Ordner ohne Dateien erscheinen sonst nirgends.
        QStringList empty;
        for (const dss::MediaFolder &f : library.folders) {
            const bool hasFiles = std::any_of(library.files.begin(), library.files.end(),
                                              [&f](const dss::MediaEntry &e) { return e.folder == f.id; });
            if (!hasFiles)
                empty.append(dss::mediaFolderPath(library, f.id));
        }
        empty.sort(Qt::CaseInsensitive);
        if (!empty.isEmpty())
            out() << "Ordner ohne Dateien: " << empty.join(QStringLiteral(", ")) << '\n';
        for (const QString &p : std::as_const(problems))
            out() << "Hinweis: " << p << '\n';
        return kExitOk;
    }

    int printAdded(const QUuid &overlayId, const QUuid &sourceId)
    {
        const dss::Overlay *o = m_store->overlay(overlayId);
        const int index = dss::sourceIndex(*o, sourceId);
        const dss::Source &s = o->sources.at(index);
        const QString file = s.kind == dss::SourceKind::Text ? s.text.fontFile : s.file;
        out() << QStringLiteral("Quelle %1 «%2» (%3) hinzugefügt, Datei %4")
                     .arg(index + 1).arg(s.name, dss::sourceKindName(s.kind), file);
        if (s.kind == dss::SourceKind::Video)
            out() << QStringLiteral(", Wiederholen %1, Am Ende anhalten %2")
                         .arg(yesNo(s.loop), yesNo(s.holdAtEnd));
        out() << QStringLiteral(", Grösse %1x%2").arg(s.width).arg(s.height);
        out() << '\n';
        return kExitOk;
    }

    int addMedia(const QStringList &a, dss::SourceKind kind)
    {
        QString error;
        const dss::Overlay *o = find(a.at(0), &error);
        if (!o)
            return fail(error);
        const QUuid overlayId = o->id;
        QString warning;
        const QUuid sourceId =
            kind == dss::SourceKind::Image
                ? m_store->addImageSource(overlayId, a.at(1), &error, &warning)
                : m_store->addVideoSource(overlayId, a.at(1), m_args.loop, &error, &warning,
                                          m_args.holdAtEnd);
        if (sourceId.isNull())
            return fail(error);
        if (!warning.isEmpty())
            err() << "Warnung: " << warning << '\n';
        return printAdded(overlayId, sourceId);
    }

    int addText(const QStringList &a)
    {
        QString error;
        const dss::Overlay *o = find(a.at(0), &error);
        if (!o)
            return fail(error);
        const QUuid overlayId = o->id;
        const QUuid sourceId = m_store->addTextSource(overlayId, a.at(1), a.at(2), &error);
        if (sourceId.isNull())
            return fail(error);
        return printAdded(overlayId, sourceId);
    }

    int setGeometry(const QStringList &a)
    {
        QString error;
        const dss::Overlay *o = find(a.at(0), &error);
        if (!o)
            return fail(error);
        int number = 0, x = 0, y = 0, width = 0, height = 0, rotation = 0;
        if (!parseInt(a.at(1), QStringLiteral("NR"), &number, &error)
            || !parseInt(a.at(2), QStringLiteral("X"), &x, &error)
            || !parseInt(a.at(3), QStringLiteral("Y"), &y, &error)
            || !parseInt(a.at(4), QStringLiteral("BREITE"), &width, &error)
            || !parseInt(a.at(5), QStringLiteral("HOEHE"), &height, &error)
            || !parseRotation(a.at(6), &rotation, &error))
            return fail(error);
        if (number < 1 || number > o->sources.size())
            return fail(QStringLiteral("Quelle Nr. %1 gibt es nicht, das Overlay hat %2 Quelle(n).")
                            .arg(number).arg(o->sources.size()));
        const QUuid overlayId = o->id;
        const QUuid sourceId = o->sources.at(number - 1).id;
        // Lage, Grösse, Drehung und Spiegeln zusammen (P09b): alles oder nichts.
        dss::SourceChanges c;
        c.x = x;
        c.y = y;
        c.width = width;
        c.height = height;
        c.rotation = rotation;
        c.flipHorizontal = m_args.flipHorizontal;
        c.flipVertical = m_args.flipVertical;
        if (!m_store->updateSource(overlayId, sourceId, c, &error))
            return fail(error);
        const dss::Overlay *changed = m_store->overlay(overlayId);
        out() << QStringLiteral("Quelle %1: X %2, Y %3, Breite %4, Höhe %5, Drehung %6, gespiegelt %7\n")
                     .arg(number).arg(x).arg(y).arg(width).arg(height)
                     .arg(dss::rotationDisplay(rotation), flipText(changed->sources.at(number - 1)));
        return kExitOk;
    }

    dss::OverlayStore *m_store;
    Arguments m_args;
};

void printSettings(const dss::CoreSettings &s, const QString &file, const QString &origin)
{
    printTable({{QStringLiteral("Port"), QStringLiteral("%1 (erlaubt %2 bis %3)")
                                             .arg(s.port).arg(dss::kPortMin).arg(dss::kPortMax)},
                {QStringLiteral("Adresse im Heimnetz"),
                 s.homeAddress.isEmpty() ? QStringLiteral("leer") : s.homeAddress},
                {QStringLiteral("Datei"), file},
                {QStringLiteral("Quelle"), origin}});
}

// Einstellungen des Kerns anzeigen oder ändern (P10-vor, Teil A 3). Nur der Kern schreibt
// kern.ini; ohne laufenden Kern zeigt dss-cli die Datei nur an.
int runSettings(const Arguments &args)
{
    const QString command = args.positional.first();
    const QStringList a = args.positional.mid(1);
    const int expected = command == QLatin1String("einstellungen") ? 0 : 1;
    if (a.size() != expected) {
        err() << "Fehler: " << QStringLiteral("%1 erwartet %2 Argument(e), erhalten %3.")
                                   .arg(command).arg(expected).arg(a.size())
              << "\n\n" << usageText();
        return kExitUsage;
    }
    dss::SettingsUpdate update;
    if (command == QLatin1String("einstellung-port")) {
        int port = 0;
        QString error;
        if (!parseInt(a.at(0), QStringLiteral("PORT"), &port, &error)) {
            err() << "Fehler: " << error << '\n';
            return kExitError;
        }
        update.port = port;
    } else if (command == QLatin1String("einstellung-adresse")) {
        update.homeAddress = a.at(0);
    }

    const QString socketPath = args.connection.isEmpty() ? dss::defaultConnectionPath() : args.connection;
    dss::CoreSession session;
    QString error;
    bool notRunning = false;
    if (!session.open(socketPath, QStringLiteral("dss-cli"), &error, &notRunning)) {
        if (!notRunning) {
            err() << "Fehler: " << error << '\n';
            return kExitError;
        }
        if (expected == 1) {
            err() << "Fehler: " << error << ".\n"
                  << "Die Einstellungen ändert nur der Kern. Kern starten:  systemctl --user start "
                  << dss::coreUnitName() << '\n';
            return kExitCoreNotRunning;
        }
        // Kern läuft nicht: Datei nur lesen.
        const QString path = dss::coreSettingsPath();
        const dss::CoreSettings defaults = dss::defaultCoreSettings();
        QFile file(path);
        if (!file.exists()) {
            printSettings(defaults, path, QStringLiteral("Standardwerte, Kern läuft nicht, Datei fehlt "
                                                         "(legt der Kern beim Start an)"));
            return kExitOk;
        }
        dss::CoreSettings s;
        if (!file.open(QIODevice::ReadOnly)) {
            err() << "Fehler: " << path << " nicht lesbar (" << file.errorString() << ")\n";
            return kExitError;
        }
        if (!dss::parseCoreSettings(file.readAll(), defaults, &s, &error)) {
            err() << "Fehler: " << path << " ungültig: " << error << '\n';
            return kExitError;
        }
        printSettings(s, path, QStringLiteral("Datei, Kern läuft nicht"));
        return kExitOk;
    }

    if (expected == 0) {
        const std::optional<dss::SettingsInfo> info = session.getSettings(&error);
        if (!info) {
            err() << "Fehler: " << error << '\n';
            return kExitError;
        }
        printSettings(info->settings, info->file, QStringLiteral("Kern"));
        if (!info->error.isEmpty()) {
            err() << "Fehler in der Datei: " << info->error << '\n';
            return kExitError;
        }
        return kExitOk;
    }
    const std::optional<dss::ChangeResult> result = session.updateSettings(update, &error);
    if (!result) {
        err() << "Fehler: " << error << '\n';
        return kExitError;
    }
    if (!result->ok) {
        err() << "Fehler: " << result->message << '\n';
        return kExitError;
    }
    const std::optional<dss::SettingsInfo> info = session.getSettings(&error);
    if (!info) {
        err() << "Fehler: " << error << '\n';
        return kExitError;
    }
    if (update.port)
        out() << QStringLiteral("Port: %1\n").arg(info->settings.port);
    else
        out() << QStringLiteral("Adresse im Heimnetz: %1\n")
                     .arg(info->settings.homeAddress.isEmpty() ? QStringLiteral("leer")
                                                               : info->settings.homeAddress);
    out() << "Gespeichert in " << info->file << "; der Overlay-Server startet mit den neuen Werten neu.\n";
    return kExitOk;
}

// Änderung der Medienliste über den laufenden Kern (P10b, Teil A 6).
int runMedia(const Arguments &args)
{
    const QString command = args.positional.first();
    const QStringList a = args.positional.mid(1);
    const int expected = std::find_if(kMediaCommands.begin(), kMediaCommands.end(),
                                      [&command](const auto &c) { return c.first == command; })->second;
    if (a.size() != expected) {
        err() << "Fehler: " << QStringLiteral("%1 erwartet %2 Argument(e), erhalten %3.")
                                   .arg(command).arg(expected).arg(a.size())
              << "\n\n" << usageText();
        return kExitUsage;
    }
    const QString socketPath = args.connection.isEmpty() ? dss::defaultConnectionPath() : args.connection;
    dss::CoreSession session;
    QString error;
    bool notRunning = false;
    if (!session.open(socketPath, QStringLiteral("dss-cli"), &error, &notRunning)) {
        err() << "Fehler: " << error << ".\n";
        if (!notRunning)
            return kExitError;
        err() << "Die Medienliste ändert nur der Kern. Kern starten:  systemctl --user start "
              << dss::coreUnitName() << '\n';
        return kExitCoreNotRunning;
    }
    const std::optional<dss::MediaListData> list = session.getMediaList(&error);
    if (!list) {
        err() << "Fehler: " << error << '\n';
        return kExitError;
    }
    // Pfade und Namen auflösen wie in medien-liste (dieselben Funktionen)
    dss::MediaLibrary library;
    library.folders = list->folders;
    for (const dss::MediaFileData &f : list->files)
        library.files.append(f.entry);
    const auto fail = [](const QString &message) {
        err() << "Fehler: " << message << '\n';
        return kExitError;
    };
    dss::ChangeRequest r;
    QString done;
    const auto folderArg = [&](const QString &path, QUuid *id) {
        const std::optional<QUuid> found = dss::findMediaFolder(library, path, &error);
        if (found)
            *id = *found;
        return found.has_value();
    };
    if (command == QLatin1String("medium-umbenennen") || command == QLatin1String("medium-verschieben")
        || command == QLatin1String("medium-loeschen")) {
        r.file = dss::findMediaFile(library, a.at(0), &error);
        if (r.file.isEmpty())
            return fail(error);
        const QString oldName = library.entry(r.file)->name;
        if (command == QLatin1String("medium-umbenennen")) {
            r.type = QString::fromLatin1(dss::msg::kRenameMedia);
            r.name = a.at(1);
            done = QStringLiteral("«%1» heisst jetzt «%2».").arg(oldName, a.at(1));
        } else if (command == QLatin1String("medium-verschieben")) {
            r.type = QString::fromLatin1(dss::msg::kMoveMedia);
            if (!folderArg(a.at(1), &r.folderId))
                return fail(error);
            done = QStringLiteral("«%1» liegt jetzt in «%2».")
                       .arg(oldName, r.folderId.isNull() ? QStringLiteral("/") : a.at(1));
        } else {
            r.type = QString::fromLatin1(dss::msg::kRemoveMedia);
            done = QStringLiteral("«%1» (%2) gelöscht.").arg(oldName, r.file);
        }
    } else if (command == QLatin1String("medien-ordner-anlegen")) {
        r.type = QString::fromLatin1(dss::msg::kCreateMediaFolder);
        QString path = a.at(0).trimmed();
        while (path.endsWith(QLatin1Char('/')))
            path.chop(1);
        const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
        r.name = path.mid(slash + 1);
        if (!folderArg(slash < 0 ? QString() : path.left(slash), &r.parentId))
            return fail(error);
        done = QStringLiteral("Ordner «%1» angelegt.").arg(path);
    } else {
        if (!folderArg(a.at(0), &r.folderId))
            return fail(error);
        if (r.folderId.isNull())
            return fail(QStringLiteral("Die oberste Ebene ist kein Ordner."));
        if (command == QLatin1String("medien-ordner-umbenennen")) {
            r.type = QString::fromLatin1(dss::msg::kRenameMediaFolder);
            r.name = a.at(1);
            done = QStringLiteral("Ordner «%1» heisst jetzt «%2».").arg(a.at(0), a.at(1));
        } else if (command == QLatin1String("medien-ordner-verschieben")) {
            r.type = QString::fromLatin1(dss::msg::kMoveMediaFolder);
            if (!folderArg(a.at(1), &r.parentId))
                return fail(error);
            done = QStringLiteral("Ordner «%1» liegt jetzt in «%2».")
                       .arg(a.at(0), r.parentId.isNull() ? QStringLiteral("/") : a.at(1));
        } else {
            r.type = QString::fromLatin1(dss::msg::kRemoveMediaFolder);
            done = QStringLiteral("Ordner «%1» gelöscht, sein Inhalt liegt eine Ebene höher.").arg(a.at(0));
        }
    }
    const std::optional<dss::ChangeResult> result = session.change(r, &error);
    if (!result)
        return fail(error);
    if (!result->ok)
        return fail(result->message);
    out() << done << '\n';
    return kExitOk;
}

// P14 d: Laufzustand über den laufenden Kern (live, live-zeigen, live-ausblenden).
int runLive(const Arguments &args)
{
    const QString command = args.positional.first();
    const QStringList a = args.positional.mid(1);
    const auto usageError = [](const QString &message) {
        err() << "Fehler: " << message << "\n\n" << usageText();
        return kExitUsage;
    };
    const auto fail = [](const QString &message) {
        err() << "Fehler: " << message << '\n';
        return kExitError;
    };
    const bool show = command == QLatin1String("live-zeigen");
    if (!show && (args.liveHide || args.liveToggle || args.liveDurationMs || args.liveText || args.liveImage))
        return usageError(QStringLiteral("--verstecken, --umschalten, --dauer, --text und --bild gelten nur für "
                                         "live-zeigen."));
    if (args.liveHide && args.liveToggle)
        return usageError(QStringLiteral("--verstecken und --umschalten schliessen sich aus."));
    if (a.isEmpty() || (command == QLatin1String("live") && a.size() != 1) || (show && a.size() < 2))
        return usageError(QStringLiteral("%1: falsche Anzahl Argumente.").arg(command));
    const QString socketPath = args.connection.isEmpty() ? dss::defaultConnectionPath() : args.connection;
    dss::CoreSession session;
    QString error;
    bool notRunning = false;
    if (!session.open(socketPath, QStringLiteral("dss-cli"), &error, &notRunning)) {
        err() << "Fehler: " << error << ".\n";
        if (!notRunning)
            return kExitError;
        err() << "Den Laufzustand hält nur der Kern. Kern starten:  systemctl --user start "
              << dss::coreUnitName() << '\n';
        return kExitCoreNotRunning;
    }
    // Laufzustand kommt nach hello (vor den Antworten auf die Anfragen unten)
    const std::optional<QJsonObject> liveMessage = session.waitFor(QString::fromLatin1(dss::msg::kOverlayLive), &error);
    QList<dss::OverlayLiveInfo> live;
    if (!liveMessage || !dss::parseOverlayLive(*liveMessage, &live, &error))
        return fail(error);
    const std::optional<dss::OverlayListData> list = session.getOverlayList(&error);
    if (!list)
        return fail(error);
    const auto summary = std::find_if(list->overlays.cbegin(), list->overlays.cend(),
                                      [&a](const dss::OverlaySummary &s) { return s.address == a.at(0); });
    if (summary == list->overlays.cend())
        return fail(QStringLiteral("Kein Overlay mit der Adresse «%1» (overlay-liste zeigt die Adressen).").arg(a.at(0)));
    const std::optional<dss::Overlay> overlay = session.getOverlay(summary->id, &error);
    if (!overlay)
        return fail(error);
    // NR wie quelle-liste: 1 = ganz hinten
    const auto sourceOf = [&overlay](const QString &text, QUuid *id, QString *problem) {
        bool ok = false;
        const int nr = text.toInt(&ok);
        if (!ok || nr < 1 || nr > overlay->sources.size()) {
            *problem = QStringLiteral("NR «%1» ungültig, das Overlay hat %2 Quellen.").arg(text).arg(overlay->sources.size());
            return false;
        }
        *id = overlay->sources.at(nr - 1).id;
        return true;
    };

    if (command == QLatin1String("live")) {
        dss::LiveElements elements;
        for (const dss::OverlayLiveInfo &info : std::as_const(live)) {
            if (info.overlayId == overlay->id)
                elements = info.elements;
        }
        if (elements.isEmpty()) {
            out() << "Kein Laufzustand in «" << overlay->name << "».\n";
            return kExitOk;
        }
        QList<QStringList> rows = {{QStringLiteral("Nr"), QStringLiteral("Name"), QStringLiteral("Jetzt"),
                                    QStringLiteral("Text"), QStringLiteral("Bild")}};
        for (qsizetype i = 0; i < overlay->sources.size(); ++i) {
            const dss::Source &s = overlay->sources.at(i);
            const auto e = elements.constFind(s.id);
            if (e == elements.constEnd())
                continue;
            QString text = e->text ? *e->text : QStringLiteral("–");
            text.replace(QLatin1Char('\n'), QLatin1Char(' '));
            rows.append({QString::number(i + 1), s.name, dss::liveStateText(*e, e->remainingMs), text,
                         e->image.isEmpty() ? QStringLiteral("–") : e->image});
        }
        printTable(rows);
        return kExitOk;
    }

    std::optional<dss::ChangeResult> result;
    if (show) {
        dss::LiveSetRequest r;
        r.overlayId = overlay->id;
        r.mode = args.liveHide ? dss::LiveMode::Hide : args.liveToggle ? dss::LiveMode::Toggle : dss::LiveMode::Show;
        r.durationMs = args.liveDurationMs.value_or(0);
        for (const QString &nr : a.at(1).split(QLatin1Char(','))) {
            dss::LiveElementRequest e;
            if (!sourceOf(nr.trimmed(), &e.source, &error))
                return fail(error);
            const dss::Source &s = overlay->sources.at(dss::sourceIndex(*overlay, e.source));
            if (args.liveText && s.kind == dss::SourceKind::Text)
                e.text = *args.liveText;
            if (args.liveImage && s.kind == dss::SourceKind::Image)
                e.image = *args.liveImage;
            r.elements.append(e);
        }
        for (const QString &pair : a.mid(2)) {
            const qsizetype eq = pair.indexOf(QLatin1Char('='));
            if (eq <= 0)
                return usageError(QStringLiteral("Argument «%1» hat nicht die Form NAME=WERT.").arg(pair));
            r.arguments.insert(pair.left(eq), pair.mid(eq + 1));
        }
        result = session.request(dss::liveSetMessage(r), &error);
    } else {
        dss::LiveResetRequest r;
        r.overlayId = overlay->id;
        for (const QString &nr : a.mid(1)) {
            QUuid id;
            if (!sourceOf(nr, &id, &error))
                return fail(error);
            r.sources.append(id);
        }
        result = session.request(dss::liveResetMessage(r), &error);
    }
    if (!result)
        return fail(error);
    if (!result->ok)
        return fail(result->message);
    if (!result->warning.isEmpty())
        err() << "Hinweis: " << result->warning << '\n';
    out() << (show ? QStringLiteral("Laufzustand gesetzt in «%1».") : QStringLiteral("Laufzustand zurückgesetzt in «%1»."))
                         .arg(overlay->name)
          << '\n';
    return kExitOk;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("dss-cli"));

    const QStringList rawArgs = QCoreApplication::arguments();
    if (rawArgs.contains(QLatin1String("--help")) || rawArgs.contains(QLatin1String("-h"))) {
        out() << usageText();
        return kExitOk;
    }
    if (rawArgs.contains(QLatin1String("--version"))) {
        out() << dss::versionLine(QStringLiteral("dss-cli")) << '\n';
        return kExitOk;
    }

    Arguments args;
    QString error;
    if (!parseArguments(rawArgs, &args, &error)) {
        err() << "Fehler: " << error << "\n\n" << usageText();
        return kExitUsage;
    }
    if (args.positional.isEmpty()) {
        err() << "Fehler: Befehl fehlt.\n\n" << usageText();
        return kExitUsage;
    }

    if (kSettingsCommands.contains(args.positional.first())) {
        const int rc = runSettings(args);
        out().flush();
        err().flush();
        return rc;
    }
    if (isMediaCommand(args.positional.first())) {
        if (!args.dataDir.isEmpty()) {
            err() << "Fehler: --datenordner gilt nicht für Änderungen der Medienliste (sie gehen an "
                     "den Kern; --verbindung wählt ihn).\n\n" << usageText();
            return kExitUsage;
        }
        const int rc = runMedia(args);
        out().flush();
        err().flush();
        return rc;
    }
    if (kLiveCommands.contains(args.positional.first())) {
        if (!args.dataDir.isEmpty()) {
            err() << "Fehler: --datenordner gilt nicht für live-Befehle (sie gehen an den Kern; "
                     "--verbindung wählt ihn).\n\n" << usageText();
            return kExitUsage;
        }
        const int rc = runLive(args);
        out().flush();
        err().flush();
        return rc;
    }
    if (!args.connection.isEmpty()) {
        err() << "Fehler: --verbindung gilt nur für einstellungen, einstellung-port, "
                 "einstellung-adresse, die Änderungen der Medienliste und live.\n\n" << usageText();
        return kExitUsage;
    }

    const QString dataDir = args.dataDir.isEmpty() ? dss::defaultDataDir() : args.dataDir;
    const bool readOnly = kReadOnlyCommands.contains(args.positional.first());
    // Änderungen nur mit der Sperre, die sonst der Kern hält (bis zum Ende des Befehls).
    dss::DataLock lock(dataDir);
    if (!readOnly) {
        qint64 pid = 0;
        if (!lock.tryLock(&error, &pid)) {
            err() << "Fehler: " << error << ".\n"
                  << "Solange der Kern läuft, ändert nur er die Overlays; dss-cli darf dann nur "
                     "anzeigen (overlay-liste, quelle-liste).\n"
                  << "Kern anhalten:  systemctl --user stop " << dss::coreUnitName() << '\n'
                  << "Danach den Befehl wiederholen und den Kern wieder starten:  "
                     "systemctl --user start "
                  << dss::coreUnitName() << '\n';
            if (pid > 0)
                err() << "(Läuft der Kern ohne Dienst, mit «kill " << pid << "» beenden.)\n";
            return kExitLocked;
        }
    }

    dss::OverlayStore store(dataDir);
#if DSS_MEDIA_SIZE
    // Grösse neuer Bild- und Videoquellen aus der Datei wie im Kern (P09, Entscheid 6)
    store.setMediaSizeReader(dss::readMediaSize);
    store.setVideoCodecReader(dss::readVideoCodec); // P10b: Format in medien-liste
#endif
    if (!store.load(&error, readOnly ? dss::OverlayStore::LoadMode::ReadOnly
                                     : dss::OverlayStore::LoadMode::ReadWrite)) {
        err() << "Fehler: Datenordner " << store.dataDir() << ": " << error << '\n';
        return kExitError;
    }
    // Kaputte Dateien und fehlende Mediendateien melden, die übrigen Overlays sind nutzbar.
    for (const QString &e : store.loadErrors())
        err() << "Fehler beim Laden: " << e << '\n';
    for (const QString &w : store.loadWarnings())
        err() << "Warnung: " << w << '\n';

    Cli cli(&store, args);
    const int rc = cli.run();
    out().flush();
    err().flush();
    return rc;
}
