// Prüft den Ordner der Einstellungen und die Suche der Symbole ohne Qt Gui (P07, Teil A),
// seit P10a die Orte und Namen je Version (in Arbeit, im Einsatz).
// Jeder Fall hat einen eigenen Ordner als XDG_CONFIG_HOME; die echten Symbole des Nutzers
// unter ~/.config/dons-streaming-studio/symbole/ werden nie gelesen oder verändert.

#include "configpaths.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace dss;

namespace {

bool touch(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write("<svg/>") > 0;
}

} // namespace

class TestConfigPaths : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_config;   // XDG_CONFIG_HOME
    QTemporaryDir m_builtin;  // steht für src/app/symbole/

private slots:
    void initTestCase()
    {
        QVERIFY(m_config.isValid() && m_builtin.isValid());
        qputenv("XDG_CONFIG_HOME", m_config.path().toLocal8Bit());
    }

    void configDirFollowsXdgConfigHome()
    {
        // Tests laufen im Bau aus dem Repo: Version in Arbeit.
        QCOMPARE(buildEdition(), Edition::Development);
        QCOMPARE(configDir(), m_config.path() + QStringLiteral("/dons-streaming-studio-entwicklung"));
        QCOMPARE(configDir(Edition::Installed), m_config.path() + QStringLiteral("/dons-streaming-studio"));
        // Eigene Symbole: derselbe Ordner für beide Versionen
        QCOMPARE(userSymbolDir(), m_config.path() + QStringLiteral("/dons-streaming-studio/symbole"));
    }

    // P10a, Teil B: Orte und Namen je Version. Die Version im Einsatz hat dieselben wie bis P09b.
    void editions()
    {
        QTemporaryDir runtime;
        QVERIFY(runtime.isValid());
        // QStandardPaths verlangt für XDG_RUNTIME_DIR die Rechte 0700 (QTemporaryDir hat sie).
        const QByteArray previous = qgetenv("XDG_RUNTIME_DIR");
        qputenv("XDG_RUNTIME_DIR", runtime.path().toLocal8Bit());
        const QString development = defaultConnectionPath();
        const QString installed = defaultConnectionPath(Edition::Installed);
        if (previous.isEmpty())
            qunsetenv("XDG_RUNTIME_DIR");
        else
            qputenv("XDG_RUNTIME_DIR", previous);
        QCOMPARE(development, runtime.path() + QStringLiteral("/dons-streaming-studio-entwicklung/kern.sock"));
        QCOMPARE(installed, runtime.path() + QStringLiteral("/dons-streaming-studio/kern.sock"));

        QCOMPARE(appDirName(Edition::Installed), QStringLiteral("dons-streaming-studio"));
        QCOMPARE(appDirName(Edition::Development), QStringLiteral("dons-streaming-studio-entwicklung"));
        QCOMPARE(appDirName(), appDirName(Edition::Development));
        QCOMPARE(coreUnitName(Edition::Installed), QStringLiteral("dss-core.service"));
        QCOMPARE(coreUnitName(), QStringLiteral("dss-core-entwicklung.service"));
        QCOMPARE(windowTitle(Edition::Installed), QStringLiteral("DoNs Streaming Studio"));
        QCOMPARE(windowTitle(), QStringLiteral("DoNs Streaming Studio – Entwicklung"));
        QCOMPARE(editionLabel(Edition::Installed), QStringLiteral("Einsatz"));
        QCOMPARE(editionLabel(), QStringLiteral("Entwicklung"));
        // Keine zwei Versionen teilen einen Ort
        QVERIFY(development != installed);
        QVERIFY(configDir() != configDir(Edition::Installed));
    }

    void builtinWhenUserDirMissing()
    {
        QVERIFY(touch(m_builtin.filePath(QStringLiteral("twitch.svg"))));
        QVERIFY(!QDir(userSymbolDir()).exists());
        QCOMPARE(findSymbolFile(QStringLiteral("twitch.svg"), {userSymbolDir(), m_builtin.path()}),
                 m_builtin.filePath(QStringLiteral("twitch.svg")));
    }

    void userFileWins()
    {
        QVERIFY(QDir().mkpath(userSymbolDir()));
        QVERIFY(touch(m_builtin.filePath(QStringLiteral("ndi.svg"))));
        QVERIFY(touch(userSymbolDir() + QStringLiteral("/ndi.svg")));
        QCOMPARE(findSymbolFile(QStringLiteral("ndi.svg"), {userSymbolDir(), m_builtin.path()}),
                 userSymbolDir() + QStringLiteral("/ndi.svg"));
        // Andere Namen kommen weiter aus dem mitgelieferten Ordner.
        QCOMPARE(findSymbolFile(QStringLiteral("twitch.svg"), {userSymbolDir(), m_builtin.path()}),
                 m_builtin.filePath(QStringLiteral("twitch.svg")));
        QCOMPARE(findSymbolFile(QStringLiteral("ndi.svg"), {userSymbolDir()}),
                 userSymbolDir() + QStringLiteral("/ndi.svg"));
    }

    void folderWithSymbolNameIsNoFile()
    {
        QVERIFY(QDir().mkpath(userSymbolDir() + QStringLiteral("/obs.svg")));
        QVERIFY(touch(m_builtin.filePath(QStringLiteral("obs.svg"))));
        QCOMPARE(findSymbolFile(QStringLiteral("obs.svg"), {userSymbolDir(), m_builtin.path()}),
                 m_builtin.filePath(QStringLiteral("obs.svg")));
    }

    void missingEverywhere()
    {
        QCOMPARE(findSymbolFile(QStringLiteral("fehlt.svg"), {userSymbolDir(), m_builtin.path()}),
                 QString());
    }

    void pathsAreNotNames()
    {
        QVERIFY(touch(m_builtin.filePath(QStringLiteral("youtube.svg"))));
        QCOMPARE(findSymbolFile(QStringLiteral("../youtube.svg"), {userSymbolDir()}), QString());
        QCOMPARE(findSymbolFile(QString(), {m_builtin.path()}), QString());
    }

    void ndiNoticeWording()
    {
        // Wortlaut nach https://docs.ndi.video/docs/sdk/licensing
        QCOMPARE(ndiTrademarkNotice(), QStringLiteral("NDI® is a registered trademark of Vizrt NDI AB"));
    }
};

QTEST_GUILESS_MAIN(TestConfigPaths)
#include "tst_configpaths.moc"
