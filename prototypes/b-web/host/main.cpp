// Messprototyp Variante B, Fenster: Qt WebEngine (Chromium) zeigt Seiten des Kerns an.
// Enthält keine Logik; meldet nur Zeitstempel aus der Seite (Konsolenmeldungen) nach aussen.
//
//   --mode t01 --url URL          Fenster zeigen; bei "FIRST_FRAME" aus der Seite die Zeit
//                                 ausgeben ("FIRST_FRAME_NS <CLOCK_MONOTONIC ns>"), 1 s warten, beenden.
//   --mode t02 --url URL --stats DATEI
//                                 Vollbild; bei "STATS <json>" aus der Seite JSON schreiben, beenden.
//
// Quellen:
//   QWebEnginePage::javaScriptConsoleMessage: https://doc.qt.io/qt-6.8/qwebenginepage.html#javaScriptConsoleMessage
//   QWebEngineSettings::PlaybackRequiresUserGesture: https://doc.qt.io/qt-6.8/qwebenginesettings.html

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QTimer>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineView>

#include <chrono>
#include <cstdio>

static long long monotonicNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

class Page : public QWebEnginePage
{
public:
    Page(const QString &statsPath, QObject *parent) : QWebEnginePage(parent), m_statsPath(statsPath) {}

protected:
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel, const QString &message, int, const QString &) override
    {
        if (message == QStringLiteral("FIRST_FRAME")) {
            std::printf("FIRST_FRAME_NS %lld\n", monotonicNs());
            std::fflush(stdout);
            QTimer::singleShot(1000, qApp, &QApplication::quit);
        } else if (message.startsWith(QStringLiteral("STATS "))) {
            QFile out(m_statsPath);
            if (out.open(QIODevice::WriteOnly))
                out.write(message.mid(6).toUtf8());
            // Nicht direkt im Rückruf aus Chromium beenden (führte zu Exit-Code 139),
            // sondern über die Ereignisschleife.
            QTimer::singleShot(0, qApp, &QApplication::quit);
        }
    }

private:
    QString m_statsPath;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addOptions({
        {QStringLiteral("mode"), QStringLiteral("t01 oder t02"), QStringLiteral("mode"), QStringLiteral("t01")},
        {QStringLiteral("url"), QStringLiteral("Seite des Kerns"), QStringLiteral("url")},
        {QStringLiteral("stats"), QStringLiteral("Ausgabedatei JSON"), QStringLiteral("file")},
    });
    parser.process(app);
    const bool t02 = parser.value(QStringLiteral("mode")) == QStringLiteral("t02");

    QWebEngineView view;
    auto *page = new Page(parser.value(QStringLiteral("stats")), &view);
    page->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
    view.setPage(page);
    view.setUrl(QUrl(parser.value(QStringLiteral("url"))));
    if (t02) {
        view.setWindowTitle(QStringLiteral("P01 T-02 B"));
        view.resize(1920, 1080);
        view.showFullScreen();
    } else {
        view.setWindowTitle(QStringLiteral("P01 T-01 B"));
        view.resize(800, 450);
        view.show();
    }
    return app.exec();
}
