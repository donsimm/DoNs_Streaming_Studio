// dss-qm-ohne-ss: Werkzeug beim Bau (P09, Punkt 44). Kopiert eine Übersetzungsdatei von Qt und
// ersetzt dabei «ß» durch «ss» (CLAUDE.md Regel 13). Logik in dss_common (qmfile.h).
//   dss-qm-ohne-ss QUELLE.qm ZIEL.qm

#include "qmfile.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    const QStringList args = app.arguments();
    if (args.size() != 3) {
        err << "Aufruf: dss-qm-ohne-ss QUELLE.qm ZIEL.qm\n";
        return 2;
    }
    QFile in(args.at(1));
    if (!in.open(QIODevice::ReadOnly)) {
        err << "Fehler: " << args.at(1) << " nicht lesbar (" << in.errorString() << ")\n";
        return 1;
    }
    QByteArray result;
    QString error;
    int replaced = 0;
    if (!dss::replaceSharpS(in.readAll(), &result, &error, &replaced)) {
        err << "Fehler: " << args.at(1) << ": " << error << '\n';
        return 1;
    }
    QDir().mkpath(QFileInfo(args.at(2)).absolutePath());
    QSaveFile out(args.at(2));
    if (!out.open(QIODevice::WriteOnly) || out.write(result) != result.size() || !out.commit()) {
        err << "Fehler: " << args.at(2) << " nicht schreibbar (" << out.errorString() << ")\n";
        return 1;
    }
    QTextStream(stdout) << args.at(2) << ": " << replaced << " Mal «ß» ersetzt\n";
    return 0;
}
