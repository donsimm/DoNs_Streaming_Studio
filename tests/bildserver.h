#pragma once

// Kleiner HTTP-Server für Profilbilder in Tests (P14 d): nur 127.0.0.1, Pfad → Antwort, 404 für
// Unbekanntes; zählt die Anfragen. So laden die Tests nie aus dem Netz. Nur Qt Core und Network.

#include <QHash>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

namespace testhelp {

class ImageServer : public QObject
{
public:
    ImageServer()
    {
        m_server.listen(QHostAddress::LocalHost, 0);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = m_server.nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, this, [this, s] {
                    m_buffer[s] += s->readAll();
                    if (!m_buffer[s].contains("\r\n\r\n"))
                        return;
                    const QByteArray path = m_buffer.take(s).split(' ').value(1);
                    ++requests;
                    const auto it = bodies.constFind(path);
                    const QByteArray body = it == bodies.constEnd() ? QByteArray() : *it;
                    const QByteArray status = it == bodies.constEnd() ? "404 Not Found" : "200 OK";
                    s->write("HTTP/1.1 " + status + "\r\nContent-Length: " + QByteArray::number(body.size())
                             + "\r\nConnection: close\r\n\r\n" + body);
                    s->disconnectFromHost();
                });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }
    QString url(const char *path) const
    {
        return QStringLiteral("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(QLatin1String(path));
    }

    QHash<QByteArray, QByteArray> bodies;
    int requests = 0;

private:
    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffer;
};

} // namespace testhelp
