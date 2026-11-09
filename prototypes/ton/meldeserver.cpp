// Quellen: https://www.rfc-editor.org/rfc/rfc9110 (Range, 206 Partial Content, Content-Range),
// https://www.rfc-editor.org/rfc/rfc9112 (Nachrichtenaufbau, Content-Length),
// https://doc.qt.io/qt-6.8/qtcpserver.html
#include "meldeserver.h"

#include "zeit.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTcpSocket>

namespace {
constexpr qint64 kMaxKopf = 16 * 1024;
constexpr qint64 kMaxRumpf = 256 * 1024;

QByteArray antwort(int code, const char *text, const QByteArray &typ, const QByteArray &rumpf,
                   const QByteArray &zusatz = {})
{
    QByteArray a = "HTTP/1.1 " + QByteArray::number(code) + ' ' + text + "\r\n";
    if (!typ.isEmpty())
        a += "Content-Type: " + typ + "\r\n";
    a += "Content-Length: " + QByteArray::number(rumpf.size()) + "\r\n";
    a += "Cache-Control: no-store\r\nConnection: close\r\n" + zusatz + "\r\n";
    return a + rumpf;
}
} // namespace

MeldeServer::MeldeServer(const QString &datenOrdner, const QString &seitenDatei, QObject *parent)
    : QObject(parent), m_daten(datenOrdner), m_seite(seitenDatei)
{
    connect(&m_server, &QTcpServer::newConnection, this, &MeldeServer::annehmen);
}

bool MeldeServer::lauschen(const QHostAddress &adresse, quint16 port, QString *fehler)
{
    if (m_server.listen(adresse, port))
        return true;
    *fehler = m_server.errorString();
    return false;
}

void MeldeServer::annehmen()
{
    while (QTcpSocket *s = m_server.nextPendingConnection()) {
        s->setProperty("puffer", QByteArray());
        connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        connect(s, &QTcpSocket::readyRead, this, [this, s] {
            const qint64 ankunft = steadyNs();
            QByteArray p = s->property("puffer").toByteArray() + s->readAll();
            const qsizetype kopfEnde = p.indexOf("\r\n\r\n");
            if (kopfEnde < 0) {
                if (p.size() > kMaxKopf)
                    s->abort();
                else
                    s->setProperty("puffer", p);
                return;
            }
            const QList<QByteArray> zeilen = p.left(kopfEnde).split('\n');
            const QList<QByteArray> erste = zeilen.value(0).trimmed().split(' ');
            qint64 laenge = 0;
            QByteArray range;
            for (const QByteArray &z : zeilen.mid(1)) {
                const qsizetype dp = z.indexOf(':');
                if (dp < 0)
                    continue;
                const QByteArray name = z.left(dp).trimmed().toLower();
                const QByteArray wert = z.mid(dp + 1).trimmed();
                if (name == "content-length")
                    laenge = wert.toLongLong();
                else if (name == "range")
                    range = wert;
            }
            if (laenge < 0 || laenge > kMaxRumpf) {
                s->write(antwort(413, "Content Too Large", {}, {}));
                s->disconnectFromHost();
                return;
            }
            if (p.size() < kopfEnde + 4 + laenge) {
                s->setProperty("puffer", p);
                return;
            }
            s->setProperty("puffer", QByteArray());
            beantworten(s, erste.value(0), erste.value(1), range, p.mid(kopfEnde + 4, laenge), ankunft);
        });
    }
}

void MeldeServer::beantworten(QTcpSocket *s, const QByteArray &methode, const QByteArray &pfad,
                              const QByteArray &range, const QByteArray &rumpf, qint64 ankunftNs)
{
    const QByteArray ohneAbfrage = pfad.left(pfad.indexOf('?') < 0 ? pfad.size() : pfad.indexOf('?'));
    if (methode == "POST" && ohneAbfrage == "/melden") {
        QJsonParseError fehler;
        const QJsonDocument doc = QJsonDocument::fromJson(rumpf, &fehler);
        if (doc.isObject()) {
            emit meldung(doc.object(), ankunftNs);
        } else if (doc.isArray()) {
            for (const QJsonValue &v : doc.array())
                if (v.isObject())
                    emit meldung(v.toObject(), ankunftNs);
        } else {
            s->write(antwort(400, "Bad Request", {}, {}));
            s->disconnectFromHost();
            return;
        }
        s->write(antwort(204, "No Content", {}, {}));
        s->disconnectFromHost();
        return;
    }
    if (methode != "GET") {
        s->write(antwort(405, "Method Not Allowed", {}, {}));
        s->disconnectFromHost();
        return;
    }
    QString datei;
    QByteArray typ;
    if (ohneAbfrage == "/" || ohneAbfrage == "/messseite.html") {
        datei = m_seite;
        typ = "text/html; charset=utf-8";
    } else if (ohneAbfrage.startsWith("/medien/")) {
        const QString name = QString::fromUtf8(QByteArray::fromPercentEncoding(ohneAbfrage.mid(8)));
        static const QRegularExpression erlaubt(QStringLiteral("^[A-Za-z0-9._-]+$"));
        if (erlaubt.match(name).hasMatch() && !name.startsWith(QLatin1Char('.'))) {
            datei = m_daten + QLatin1Char('/') + name;
            typ = name.endsWith(QLatin1String(".webm")) ? "video/webm" : "video/mp4";
        }
    }
    QFile f(datei);
    if (datei.isEmpty() || !QFileInfo(datei).isFile() || !f.open(QIODevice::ReadOnly)) {
        s->write(antwort(404, "Not Found", "text/plain", "nicht gefunden\n"));
        s->disconnectFromHost();
        return;
    }
    const QByteArray inhalt = f.readAll();
    const qint64 n = inhalt.size();
    static const QRegularExpression bereich(QStringLiteral("^bytes=(\\d*)-(\\d*)$"));
    const QRegularExpressionMatch m = bereich.match(QString::fromLatin1(range));
    if (!range.isEmpty() && m.hasMatch() && !(m.captured(1).isEmpty() && m.captured(2).isEmpty())) {
        qint64 von, bis;
        if (m.captured(1).isEmpty()) { // letzte N Bytes
            von = std::max<qint64>(0, n - m.captured(2).toLongLong());
            bis = n - 1;
        } else {
            von = m.captured(1).toLongLong();
            bis = m.captured(2).isEmpty() ? n - 1 : std::min(n - 1, m.captured(2).toLongLong());
        }
        if (von >= n || von > bis) {
            s->write(antwort(416, "Range Not Satisfiable", {}, {},
                             "Content-Range: bytes */" + QByteArray::number(n) + "\r\n"));
        } else {
            s->write(antwort(206, "Partial Content", typ, inhalt.mid(von, bis - von + 1),
                             "Accept-Ranges: bytes\r\nContent-Range: bytes " + QByteArray::number(von) + '-'
                                 + QByteArray::number(bis) + '/' + QByteArray::number(n) + "\r\n"));
        }
    } else {
        s->write(antwort(200, "OK", typ, inhalt, "Accept-Ranges: bytes\r\n"));
    }
    s->disconnectFromHost();
}
