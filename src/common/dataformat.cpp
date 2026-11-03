// Quellen:
//   QSaveFile (atomar schreiben): https://doc.qt.io/qt-6.8/qsavefile.html
//   QJsonDocument::fromJson: https://doc.qt.io/qt-6.8/qjsondocument.html

#include "dataformat.h"
#include "dataformattesting.h"

#include "datalock.h"
#include "overlayjson.h"
#include "overlaylist.h"
#include "tokenstore.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace dss {

namespace {

// Testmöglichkeit (P10-vor, Punkt 70): 0 = aus. Nur tst_datenformat setzt sie über
// dataformattesting.h; im Programm bleibt sie 0 und hat keine Wirkung.
int g_failWriteAt = 0;

struct FileFormat
{
    QString relative; // Pfad im Datenordner
    QString path;
    int version = 0;  // 0 = nicht lesbar
    QByteArray data;
    QString problem;
};

// format_version einer JSON-Datei; 0 und *problem, wenn das nicht geht.
FileFormat readFormat(const QString &dataDir, const QString &relative)
{
    FileFormat f;
    f.relative = relative;
    f.path = dataDir + QLatin1Char('/') + relative;
    QFile file(f.path);
    if (!file.open(QIODevice::ReadOnly)) {
        f.problem = file.errorString();
        return f;
    }
    f.data = file.readAll();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(f.data, &parseError);
    const QJsonValue version = doc.object().value(QStringLiteral("format_version"));
    if (!doc.isObject() || !version.isDouble() || version.toDouble() != version.toInt()
        || version.toInt() < 1) {
        f.problem = doc.isObject() ? QStringLiteral("format_version fehlt oder ist ungültig")
                                   : parseError.errorString();
        return f;
    }
    f.version = version.toInt();
    return f;
}

QString describe(const FileFormat &f)
{
    return QStringLiteral("%1 (Format %2)").arg(f.relative).arg(f.version);
}

struct Scan
{
    DataFormatStatus status;
    QList<FileFormat> older; // Overlay-Dateien zum Umwandeln, mit neuem Inhalt in data
    QList<QByteArray> converted;
};

Scan scan(const QString &dataDir)
{
    Scan s;
    if (!QDir(dataDir).exists()) {
        s.status.error = QStringLiteral("Datenordner %1 fehlt").arg(dataDir);
        return s;
    }
    // Einzelne Dateien mit nur einer Version (liste.json, tokens.json): älter gibt es nicht.
    const auto single = [&](const QString &relative, int current) {
        if (!QFile::exists(dataDir + QLatin1Char('/') + relative))
            return;
        const FileFormat f = readFormat(dataDir, relative);
        if (f.version == 0)
            s.status.unreadable.append(f.relative + QStringLiteral(": ") + f.problem);
        else if (f.version > current)
            s.status.newer.append(describe(f));
        else if (f.version < current)
            s.status.older.append(describe(f)); // gibt es heute nicht; Umwandeln lehnt ab
    };
    single(QStringLiteral("liste.json"), kListFormatVersion);
    single(QStringLiteral("tokens.json"), kTokenFormatVersion);

    const QDir overlays(dataDir + QStringLiteral("/overlays"));
    for (const QString &name :
         overlays.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        FileFormat f = readFormat(dataDir, QStringLiteral("overlays/") + name);
        if (f.version == 0) {
            s.status.unreadable.append(f.relative + QStringLiteral(": ") + f.problem);
            continue;
        }
        if (f.version > kOverlayFormatVersion) {
            s.status.newer.append(describe(f));
            continue;
        }
        // Streng lesen wie beim Laden; ungültige Dateien überspringt auch das Programm.
        Overlay overlay;
        QString error;
        if (!parseOverlay(f.data, &overlay, &error)) {
            s.status.unreadable.append(f.relative + QStringLiteral(": ") + error);
            continue;
        }
        if (f.version < kOverlayFormatVersion) {
            s.status.older.append(describe(f));
            s.converted.append(serializeOverlay(overlay));
            s.older.append(f);
        }
    }
    return s;
}

bool writeFile(const QString &path, const QByteArray &data, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        *error = QStringLiteral("%1 kann nicht geschrieben werden (%2)").arg(path, file.errorString());
        return false;
    }
    return true;
}

} // namespace

DataFormatStatus checkDataFormat(const QString &dataDir)
{
    return scan(QDir(dataDir).absolutePath()).status;
}

bool convertDataFormat(const QString &dataDir, QStringList *converted, QString *error)
{
    converted->clear();
    const QString dir = QDir(dataDir).absolutePath();
    if (!QDir(dir).exists()) {
        *error = QStringLiteral("Datenordner %1 fehlt").arg(dir);
        return false;
    }
    DataLock lock(dir);
    QString lockError;
    if (!lock.tryLock(&lockError)) {
        *error = QStringLiteral("Datenordner gesperrt: %1").arg(lockError);
        return false;
    }
    const Scan s = scan(dir);
    if (!s.status.error.isEmpty()) {
        *error = s.status.error;
        return false;
    }
    if (!s.status.newer.isEmpty()) {
        *error = QStringLiteral("neueres Format, als dieses Programm lesen kann: %1")
                     .arg(s.status.newer.join(QStringLiteral(", ")));
        return false;
    }
    if (s.older.size() != s.status.older.size()) {
        *error = QStringLiteral("kein Umwandeln für: %1").arg(s.status.older.join(QStringLiteral(", ")));
        return false;
    }
    for (qsizetype i = 0; i < s.older.size(); ++i) {
        const bool testFailure = g_failWriteAt == i + 1;
        if (testFailure)
            *error = QStringLiteral("%1 kann nicht geschrieben werden (Testfehler)")
                         .arg(s.older.at(i).path);
        if (testFailure || !writeFile(s.older.at(i).path, s.converted.at(i), error)) {
            // Schon geschriebene Dateien zurück auf den alten Inhalt
            for (qsizetype k = 0; k < i; ++k) {
                QString undoError;
                if (!writeFile(s.older.at(k).path, s.older.at(k).data, &undoError))
                    *error += QStringLiteral("; nicht zurückgeschrieben: ") + undoError;
            }
            return false;
        }
    }
    *converted = s.status.older;
    return true;
}

namespace testing {

void setConvertFailWriteAt(int n)
{
    g_failWriteAt = n;
}

} // namespace testing

} // namespace dss
