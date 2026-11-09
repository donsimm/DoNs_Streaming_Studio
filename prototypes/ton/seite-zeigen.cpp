// seite-zeigen: Messseite für M2 in Qt WebEngine (Chromium 122), Fenster wie tst_browserquelle
// (transparenter Hintergrund, Abspielen ohne Benutzergeste). Standard: Fenster ausserhalb des
// Bildschirms (Plan P12a, M2); ob Chromium dort weiter Bilder meldet, prüft nachtlauf.sh.
//
//   seite-zeigen --url http://127.0.0.1:24990/messseite.html?datei=blitz-piep.webm
//                [--dauer-s 0 = bis SIGTERM] [--lage -1400,0] [--groesse 1280x720]
//
// Quellen: https://doc.qt.io/qt-6.8/qwebengineview.html,
// https://doc.qt.io/qt-6.8/qwebenginesettings.html (PlaybackRequiresUserGesture)
#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QTimer>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineView>

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
    QApplication app(argc, argv);
    std::signal(SIGTERM, signalEnde);
    std::signal(SIGINT, signalEnde);
    QCommandLineParser p;
    p.addHelpOption();
    p.addOptions({
        {QStringLiteral("url"), QStringLiteral("Adresse der Messseite"), QStringLiteral("url")},
        {QStringLiteral("dauer-s"), QStringLiteral("Dauer"), QStringLiteral("s"), QStringLiteral("0")},
        {QStringLiteral("lage"), QStringLiteral("Lage des Fensters x,y"), QStringLiteral("x,y"), QStringLiteral("-1400,0")},
        {QStringLiteral("groesse"), QStringLiteral("Grösse BxH"), QStringLiteral("BxH"), QStringLiteral("1280x720")},
    });
    p.process(app);
    if (!p.isSet(QStringLiteral("url")))
        p.showHelp(64);
    const QStringList lage = p.value(QStringLiteral("lage")).split(QLatin1Char(','));
    const QStringList groesse = p.value(QStringLiteral("groesse")).split(QLatin1Char('x'));

    QWebEngineView view;
    view.page()->setBackgroundColor(Qt::transparent);
    view.page()->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
    view.resize(groesse.value(0).toInt(), groesse.value(1).toInt());
    view.move(lage.value(0).toInt(), lage.value(1).toInt());
    view.show();
    QObject::connect(&view, &QWebEngineView::loadFinished, &view, [&](bool ok) {
        const QRect g = view.frameGeometry();
        std::printf("Seite geladen: %s, Fenster bei %d,%d %dx%d\n", ok ? "ja" : "nein", g.x(), g.y(), g.width(),
                    g.height());
        std::fflush(stdout);
    });
    view.load(QUrl(p.value(QStringLiteral("url"))));

    const qint64 dauerMs = p.value(QStringLiteral("dauer-s")).toLongLong() * 1000;
    QTimer pruefen;
    QElapsedTimer uhr;
    uhr.start();
    QObject::connect(&pruefen, &QTimer::timeout, &app, [&] {
        if (g_ende || (dauerMs > 0 && uhr.elapsed() >= dauerMs))
            app.quit();
    });
    pruefen.start(200);
    return app.exec();
}
