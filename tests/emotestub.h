#pragma once

// Lokaler HTTP-Stub für die Emotes (P14 Emotes em-4, gemeinsam mit tst_fensterchatbot seit em-7):
// lauscht auf 127.0.0.1, liefert die Antworten aus tests/daten/emotes/ (Abrufe em-0, 2026-11-16)
// und selbst gemachte Bild-Bytes, zählt Anfragen und Methoden. Nie echtes Netz (Plan «Für jeden
// Abschnitt»). Braucht DSS_TEST_EMOTES_DIR.

#include "emoteloader.h"

#include <QFile>
#include <QHash>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>

namespace emotestub {

inline QByteArray readFile(const char *name)
{
    QFile f(QStringLiteral(DSS_TEST_EMOTES_DIR "/") + QLatin1String(name));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// Pfad beim Stub für eine echte Adresse: /<host><Pfad>, passend zu HttpStub::endpoints()
inline QByteArray stubPath(const QUrl &real)
{
    return ('/' + real.host() + real.path()).toUtf8();
}

class HttpStub : public QObject
{
public:
    struct Response
    {
        int status = 200;
        QByteArray body;
        QByteArray headers; // zusätzliche Kopfzeilen, je mit \r\n
    };
    struct Request
    {
        QByteArray method;
        QByteArray path;
        QByteArray head;
    };

    HttpStub()
    {
        m_server.listen(QHostAddress::LocalHost, 0);
        m_port = m_server.serverPort();
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = m_server.nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, this, [this, s] { readRequest(s); });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }

    QUrl base(const QString &host) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/%2").arg(m_port).arg(host));
    }
    dss::EmoteEndpoints endpoints() const
    {
        dss::EmoteEndpoints endpoints;
        for (const dss::EmoteEndpoint &e : dss::EmoteEndpoints::standard().list)
            endpoints.list.append({e.host, base(e.host)});
        return endpoints;
    }
    void close() { m_server.close(); }
    // Nach close() wieder an (derselbe Port, die Adressen von endpoints() bleiben gültig)
    bool listen() { return m_server.listen(QHostAddress::LocalHost, m_port); }
    void release()
    {
        hold = false;
        for (const auto &[socket, response] : std::exchange(m_held, {}))
            if (socket)
                send(socket, response);
    }
    int count(const QByteArray &prefix) const
    {
        int n = 0;
        for (const Request &r : requests)
            n += r.path.startsWith(prefix) ? 1 : 0;
        return n;
    }

    QHash<QByteArray, Response> routes;
    QList<Request> requests;
    bool hold = false; // Antworten zurückhalten bis release()

private:
    void readRequest(QTcpSocket *s)
    {
        m_buffer[s] += s->readAll();
        const qsizetype end = m_buffer[s].indexOf("\r\n\r\n");
        if (end < 0)
            return;
        const QByteArray head = m_buffer.take(s).left(end);
        const QList<QByteArray> first = head.left(head.indexOf("\r\n")).split(' ');
        const Request request{first.value(0), first.value(1), head};
        requests.append(request);
        const Response response = routes.value(request.path, Response{404, "{}", {}});
        if (hold)
            m_held.append({s, response});
        else
            send(s, response);
    }
    static void send(QTcpSocket *s, const Response &r)
    {
        static const QHash<int, QByteArray> reasons = {
            {200, "OK"}, {302, "Found"}, {404, "Not Found"}, {502, "Bad Gateway"}};
        s->write("HTTP/1.1 " + QByteArray::number(r.status) + ' ' + reasons.value(r.status, "X")
                 + "\r\nContent-Length: " + QByteArray::number(r.body.size()) + "\r\nConnection: close\r\n"
                 + r.headers + "\r\n" + r.body);
        s->disconnectFromHost();
    }

    QTcpServer m_server;
    quint16 m_port = 0;
    QHash<QTcpSocket *, QByteArray> m_buffer;
    QList<QPair<QPointer<QTcpSocket>, Response>> m_held;
};

} // namespace emotestub
