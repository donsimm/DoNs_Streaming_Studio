// Prüft den Ordner der Einstellungen und die Suche der Symbole ohne Qt Gui (P07, Teil A).
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
        QCOMPARE(configDir(), m_config.path() + QStringLiteral("/dons-streaming-studio"));
        QCOMPARE(userSymbolDir(), m_config.path() + QStringLiteral("/dons-streaming-studio/symbole"));
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
