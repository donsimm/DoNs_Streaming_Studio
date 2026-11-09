// Kleiner HTTP-Server des Prototyps (HTTP/1.1, je Anfrage eine Verbindung):
//   GET  /  bzw. /messseite.html   Messseite
//   GET  /medien/<Name>            Datei aus dem Datenordner (nur Name, mit Range für Video)
//   POST /melden                   Meldung der Seite (JSON), Zeit des Eintreffens: steady_clock
#pragma once

#include <QHostAddress>
#include <QJsonObject>
#include <QTcpServer>

class MeldeServer : public QObject
{
    Q_OBJECT
public:
    MeldeServer(const QString &datenOrdner, const QString &seitenDatei, QObject *parent = nullptr);
    bool lauschen(const QHostAddress &adresse, quint16 port, QString *fehler);
    quint16 port() const { return m_server.serverPort(); }

signals:
    void meldung(const QJsonObject &m, qint64 ankunftNs);

private:
    void annehmen();
    void beantworten(class QTcpSocket *s, const QByteArray &methode, const QByteArray &pfad,
                     const QByteArray &range, const QByteArray &rumpf, qint64 ankunftNs);

    QTcpServer m_server;
    QString m_daten;
    QString m_seite;
};
