// Übersetzung von Qt ohne «ß» (P09, Punkt 44), ohne Qt Gui: Kopie von qtbase_de.qm aus dem
// System umschreiben, mit QTranslator (Qt Core) laden und Texte vergleichen; dazu die Datei,
// die der Bau für das Fenster erzeugt (DSS_BUILD_APP).

#include "qmfile.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTranslator>

using namespace dss;

namespace {

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QString translated(const QByteArray &qm, const char *context, const char *source)
{
    // QTranslator::load(const uchar *, int) liest aus dem Speicher; die Daten müssen bleiben.
    // https://doc.qt.io/qt-6.8/qtranslator.html#load-2
    QTranslator t;
    if (!t.load(reinterpret_cast<const uchar *>(qm.constData()), int(qm.size())))
        return QStringLiteral("(nicht geladen)");
    return t.translate(context, source);
}

} // namespace

class TestQmFile : public QObject
{
    Q_OBJECT

private slots:
    void replacesSharpS()
    {
        const QByteArray original = readAll(QStringLiteral(DSS_QT_TRANSLATION));
        QVERIFY2(!original.isEmpty(), DSS_QT_TRANSLATION);
        QCOMPARE(translated(original, "QPlatformTheme", "Close"), QStringLiteral("Schließen"));

        QByteArray result;
        QString error;
        int replaced = 0;
        QVERIFY2(replaceSharpS(original, &result, &error, &replaced), qPrintable(error));
        qInfo("ersetzt: %d, Grösse %lld → %lld Bytes", replaced, qint64(original.size()), qint64(result.size()));
        QVERIFY(replaced > 0);
        // Jedes ersetzte Zeichen wird zwei (UTF-16: 2 Bytes mehr)
        QCOMPARE(result.size(), original.size() + 2 * replaced);
        QCOMPARE(translated(result, "QPlatformTheme", "Close"), QStringLiteral("Schliessen"));
        // Texte ohne «ß» bleiben gleich, auch mit Kürzel (&)
        QCOMPARE(translated(result, "QLineEdit", "&Copy"), translated(original, "QLineEdit", "&Copy"));
        QCOMPARE(translated(result, "QColorDialog", "&Basic colors"),
                 translated(original, "QColorDialog", "&Basic colors"));
        // Zweimal: nichts mehr zu ersetzen
        QByteArray again;
        int replacedAgain = -1;
        QVERIFY(replaceSharpS(result, &again, &error, &replacedAgain));
        QCOMPARE(replacedAgain, 0);
        QCOMPARE(again, result);
    }

    void rejectsUnknown()
    {
        QByteArray result;
        QString error;
        QVERIFY(!replaceSharpS(QByteArray("keine qm-Datei"), &result, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(result.isEmpty());
        QByteArray cut = readAll(QStringLiteral(DSS_QT_TRANSLATION));
        cut.chop(cut.size() / 2);
        QVERIFY(!replaceSharpS(cut, &result, &error));
    }

#ifdef DSS_BUILT_TRANSLATION
    void builtFile()
    {
        const QByteArray built = readAll(QStringLiteral(DSS_BUILT_TRANSLATION));
        QVERIFY2(!built.isEmpty(), DSS_BUILT_TRANSLATION);
        QCOMPARE(translated(built, "QPlatformTheme", "Close"), QStringLiteral("Schliessen"));
        QCOMPARE(translated(built, "QDialogButtonBox", "&OK"), translated(readAll(QStringLiteral(DSS_QT_TRANSLATION)), "QDialogButtonBox", "&OK"));
    }
#endif
};

QTEST_APPLESS_MAIN(TestQmFile)
#include "tst_qmfile.moc"
