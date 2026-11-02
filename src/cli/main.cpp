// dss-cli: Werkzeug ohne Fenster für den Handtest des Overlay-Modells (P04).
// Enthält keine Logik ausser dem Lesen der Befehlszeile und der Ausgabe; alles andere
// steckt in dss_common (OverlayStore). Gibt nie einen Token aus (CLAUDE.md Regel 12).
// Seit P07 (Entscheid 2): Solange der Kern läuft, ändert nur er die Overlay-Dateien. Anzeige-
// Befehle lesen dann nur (nichts wird geschrieben); Änderungs-Befehle nehmen die Sperre im
// Datenordner und brechen ab, wenn sie belegt ist.

#include "configpaths.h"
#include "datalock.h"
#include "overlaystore.h"
#include "version.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QTextStream>

#include <functional>

namespace {

constexpr int kExitOk = 0;
constexpr int kExitError = 1;
constexpr int kExitUsage = 2;
constexpr int kExitLocked = 3;

// Befehle, die nur anzeigen; alle anderen ändern den Datenordner.
const QStringList kReadOnlyCommands = {QStringLiteral("overlay-liste"), QStringLiteral("quelle-liste")};

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

const char *const kUsage =
    "Aufruf: dss-cli [--datenordner PFAD] BEFEHL [ARGUMENTE]\n"
    "\n"
    "Befehle:\n"
    "  overlay-liste\n"
    "  overlay-neu NAME BREITE HOEHE\n"
    "  overlay-umbenennen ADRESSE NEUER_NAME [--adresse-mitaendern]\n"
    "  overlay-kopieren ADRESSE\n"
    "  overlay-loeschen ADRESSE\n"
    "  quelle-liste ADRESSE\n"
    "  quelle-bild ADRESSE DATEI\n"
    "  quelle-video ADRESSE DATEI [--wiederholen]\n"
    "  quelle-text ADRESSE TEXT SCHRIFTDATEI\n"
    "  quelle-geometrie ADRESSE NR X Y BREITE HOEHE DREHUNG\n"
    "\n"
    "--wiederholen: Video am Ende wieder von vorne (ohne: nicht wiederholen).\n"
    "NR: Nummer der Quelle aus quelle-liste (1 = ganz hinten).\n"
    "DREHUNG: Grad mit höchstens einer Nachkommastelle, 0 bis 359.9 (zum Beispiel 12.5).\n"
    "Ohne --datenordner: Datenordner des Programms (~/.local/share/dons-streaming-studio).\n"
    "overlay-liste und quelle-liste gehen immer. Alle anderen Befehle ändern Overlays und gehen\n"
    "nur, wenn der Kern angehalten ist (systemctl --user stop dss-core.service).\n"
    "Exit-Code: 0 = erfolgreich, 1 = Fehler, 2 = falscher Aufruf, 3 = Kern läuft (Datenordner\n"
    "gesperrt).\n";

struct Arguments
{
    QString dataDir;
    bool changeAddress = false;
    bool loop = false;
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
        } else if (a == QLatin1String("--adresse-mitaendern")) {
            result->changeAddress = true;
        } else if (a == QLatin1String("--wiederholen")) {
            result->loop = true;
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
            {"quelle-liste", 1, [this](const QStringList &x) { return listSources(x); }},
            {"quelle-bild", 2, [this](const QStringList &x) { return addMedia(x, dss::SourceKind::Image); }},
            {"quelle-video", 2, [this](const QStringList &x) { return addMedia(x, dss::SourceKind::Video); }},
            {"quelle-text", 3, [this](const QStringList &x) { return addText(x); }},
            {"quelle-geometrie", 7, [this](const QStringList &x) { return setGeometry(x); }},
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
        err() << "Fehler: " << message << "\n\n" << kUsage;
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
        QList<QStringList> rows = {{QStringLiteral("Name"), QStringLiteral("Adresse"),
                                    QStringLiteral("Auflösung"), QStringLiteral("Quellen")}};
        for (const dss::Overlay &o : overlays)
            rows.append({o.name, o.address, QStringLiteral("%1x%2").arg(o.width).arg(o.height),
                         QString::number(o.sources.size())});
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
                                    QStringLiteral("Wiederholen"), QStringLiteral("Name")}};
        for (qsizetype i = 0; i < o->sources.size(); ++i) {
            const dss::Source &s = o->sources.at(i);
            rows.append({QString::number(i + 1), dss::sourceKindName(s.kind), yesNo(s.visible),
                         QString::number(s.x), QString::number(s.y), QString::number(s.width),
                         QString::number(s.height), dss::rotationDisplay(s.rotation),
                         s.kind == dss::SourceKind::Video ? yesNo(s.loop) : QStringLiteral("–"),
                         s.name});
        }
        printTable(rows);
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
            out() << QStringLiteral(", Wiederholen %1").arg(yesNo(s.loop));
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
        const QUuid sourceId = kind == dss::SourceKind::Image
                                   ? m_store->addImageSource(overlayId, a.at(1), &error)
                                   : m_store->addVideoSource(overlayId, a.at(1), m_args.loop, &error);
        if (sourceId.isNull())
            return fail(error);
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
        if (!m_store->setSourceGeometry(overlayId, sourceId, x, y, width, height, rotation, &error))
            return fail(error);
        out() << QStringLiteral("Quelle %1: X %2, Y %3, Breite %4, Höhe %5, Drehung %6\n")
                     .arg(number).arg(x).arg(y).arg(width).arg(height)
                     .arg(dss::rotationDisplay(rotation));
        return kExitOk;
    }

    dss::OverlayStore *m_store;
    Arguments m_args;
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("dss-cli"));

    const QStringList rawArgs = QCoreApplication::arguments();
    if (rawArgs.contains(QLatin1String("--help")) || rawArgs.contains(QLatin1String("-h"))) {
        out() << kUsage;
        return kExitOk;
    }
    if (rawArgs.contains(QLatin1String("--version"))) {
        out() << dss::versionLine(QStringLiteral("dss-cli")) << '\n';
        return kExitOk;
    }

    Arguments args;
    QString error;
    if (!parseArguments(rawArgs, &args, &error)) {
        err() << "Fehler: " << error << "\n\n" << kUsage;
        return kExitUsage;
    }
    if (args.positional.isEmpty()) {
        err() << "Fehler: Befehl fehlt.\n\n" << kUsage;
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
                  << "Kern anhalten:  systemctl --user stop " << dss::kCoreUnitName << '\n'
                  << "Danach den Befehl wiederholen und den Kern wieder starten:  "
                     "systemctl --user start "
                  << dss::kCoreUnitName << '\n';
            if (pid > 0)
                err() << "(Läuft der Kern ohne Dienst, mit «kill " << pid << "» beenden.)\n";
            return kExitLocked;
        }
    }

    dss::OverlayStore store(dataDir);
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
