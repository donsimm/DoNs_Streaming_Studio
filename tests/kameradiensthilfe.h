#pragma once

// Hilfen für Tests gegen den echten Kamera-Dienst in der Attrappe (P15 d1, seit d2 gemeinsam für
// tst_kameradienst und tst_kernkamera): freier Port, Geräteordner mit Tiny 2 und C922, Dienst als
// eigener Prozess (python3 -m dss_camera --attrappe --geraeteordner) auf 127.0.0.1 mit eigenen
// XDG-Ordnern im Testordner. Braucht DSS_PYTHON3 und DSS_CAMERA_SERVICE_DIR. Nur Qt Core und Qt
// Network.
//
// Quellen: kamera-dienst/LIESMICH.md, kamera-dienst/tests/test_api.py (Service, free_port, ini),
// kamera-dienst/dss_camera/fake.py (FakeDeviceFolder).

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRandomGenerator>
#include <QString>
#include <QTcpServer>

#include <memory>

namespace kameradienst {

inline constexpr int kStartTimeoutMs = 20000;
// Kennung der C922 in der Attrappe (kamera-dienst/dss_camera/fake.py, C922_ID).
inline const QString kC922 = QStringLiteral("bbbb:0002:SERIE0001");

// Im Bereich, den dienst.ini erlaubt (1024 bis 32767), wie free_port() in test_api.py.
inline int freePort()
{
    for (int i = 0; i < 200; ++i) {
        const int port = QRandomGenerator::global()->bounded(20000, 32000);
        QTcpServer probe;
        if (probe.listen(QHostAddress::LocalHost, static_cast<quint16>(port)))
            return port;
    }
    return 0;
}

// Geräteordner der Attrappe mit Tiny 2 und C922 (FakeDeviceFolder.add_standard_cameras).
// Leer = gelungen, sonst die Ausgabe von Python.
inline QString createFakeDevices(const QString &folder)
{
    QProcess p;
    p.setWorkingDirectory(QStringLiteral(DSS_CAMERA_SERVICE_DIR));
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONDONTWRITEBYTECODE"), QStringLiteral("1"));
    p.setProcessEnvironment(env);
    p.start(QStringLiteral(DSS_PYTHON3),
            {QStringLiteral("-c"),
             QStringLiteral("import sys; from pathlib import Path; from dss_camera.fake import FakeDeviceFolder; "
                            "FakeDeviceFolder(Path(sys.argv[1])).add_standard_cameras()"),
             folder});
    if (!p.waitForFinished(kStartTimeoutMs))
        return QStringLiteral("Geräteordner: Python antwortet nicht");
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return QStringLiteral("Geräteordner: ") + QString::fromUtf8(p.readAllStandardError());
    return {};
}

// Ein Dienst-Prozess mit eigenem Testordner (wie Service in test_api.py); Geräte aus
// <folder>/geraete (createFakeDevices).
class Service
{
public:
    Service(const QString &folder, int port)
        : m_folder(folder)
    {
        QDir().mkpath(folder + QStringLiteral("/laufzeit"));
        QFile ini(folder + QStringLiteral("/dienst.ini"));
        if (ini.open(QIODevice::WriteOnly))
            ini.write(QStringLiteral("[dienst]\nport=%1\nlauschadressen=127.0.0.1\nerlaubte-adressen=127.0.0.1\n")
                          .arg(port)
                          .toUtf8());
    }

    ~Service() { stop(); }

    // Wartet auf «bereit» im Protokoll.
    bool start()
    {
        m_process = std::make_unique<QProcess>();
        m_process->setWorkingDirectory(QStringLiteral(DSS_CAMERA_SERVICE_DIR));
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("XDG_RUNTIME_DIR"), m_folder + QStringLiteral("/laufzeit"));
        env.insert(QStringLiteral("XDG_CONFIG_HOME"), m_folder + QStringLiteral("/config"));
        env.insert(QStringLiteral("XDG_DATA_HOME"), m_folder + QStringLiteral("/daten"));
        env.insert(QStringLiteral("PYTHONDONTWRITEBYTECODE"), QStringLiteral("1"));
        m_process->setProcessEnvironment(env);
        m_process->setProcessChannelMode(QProcess::SeparateChannels);
        m_process->setStandardOutputFile(QProcess::nullDevice());
        m_process->setReadChannel(QProcess::StandardError); // für waitForReadyRead
        m_process->start(QStringLiteral(DSS_PYTHON3),
                         {QStringLiteral("-m"), QStringLiteral("dss_camera"), QStringLiteral("--konfig"),
                          m_folder + QStringLiteral("/dienst.ini"), QStringLiteral("--attrappe"),
                          QStringLiteral("--geraeteordner"), m_folder + QStringLiteral("/geraete")});
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < kStartTimeoutMs) {
            if (m_process->state() == QProcess::NotRunning)
                break;
            m_process->waitForReadyRead(200);
            m_stderr += QString::fromUtf8(m_process->readAllStandardError());
            if (m_stderr.contains(QStringLiteral("bereit")))
                return true;
        }
        return false;
    }

    // Beendet mit SIGTERM; gibt den Exit-Code zurück (-1: lief nicht oder hing).
    int stop()
    {
        if (!m_process)
            return -1;
        if (m_process->state() != QProcess::NotRunning) {
            m_process->terminate();
            if (!m_process->waitForFinished(kStartTimeoutMs)) {
                m_process->kill();
                m_process->waitForFinished();
                m_process.reset();
                return -1;
            }
        }
        m_stderr += QString::fromUtf8(m_process->readAllStandardError());
        const int code = m_process->exitStatus() == QProcess::NormalExit ? m_process->exitCode() : -1;
        m_process.reset();
        return code;
    }

    // Protokoll des Dienstes bis jetzt
    QString log()
    {
        if (m_process)
            m_stderr += QString::fromUtf8(m_process->readAllStandardError());
        return m_stderr;
    }

private:
    QString m_folder;
    std::unique_ptr<QProcess> m_process;
    QString m_stderr;
};

// P15 e3: Attrappen-Lampen (FakeLight in fake.py, HTTP-Server auf 127.0.0.1, freier Port) in einem
// eigenen Python-Prozess; er läuft, bis stop() seine Eingabe schliesst. hosts() wie «127.0.0.1:<port>».
// P15 f2a: Die erste Lampe antwortet wahlweise langsam (hang_s); nach stop() stehen in puts() die
// Werte, die jede Lampe in der Reihenfolge des Eingangs bekommen hat (FakeLight.puts).
class FakeLights
{
public:
    ~FakeLights() { stop(); }

    // count Lampen, eingeschaltet mit 40 % und 222 Mired (4500 K); die erste antwortet erst nach
    // firstHangS Sekunden (unter 1 s: der Dienst wartet, docs/09-kamera-dienst.md, «Lampen»).
    bool start(int count, double firstHangS = 0.0)
    {
        m_process = std::make_unique<QProcess>();
        m_process->setWorkingDirectory(QStringLiteral(DSS_CAMERA_SERVICE_DIR));
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("PYTHONDONTWRITEBYTECODE"), QStringLiteral("1"));
        m_process->setProcessEnvironment(env);
        m_process->start(QStringLiteral(DSS_PYTHON3),
                         {QStringLiteral("-c"),
                          QStringLiteral("import json, sys; from dss_camera.fake import FakeLight; "
                                         "lights = [FakeLight(on=True, brightness=40, temperature=222) "
                                         "for _ in range(int(sys.argv[1]))]; "
                                         "lights[0].hang_s = float(sys.argv[2]); "
                                         "print(' '.join(l.host for l in lights), flush=True); sys.stdin.read(); "
                                         "print(json.dumps([l.puts for l in lights]), flush=True)"),
                          QString::number(count), QString::number(firstHangS)});
        QElapsedTimer timer;
        timer.start();
        QByteArray out;
        while (timer.elapsed() < kStartTimeoutMs && !out.contains('\n')) {
            if (m_process->state() == QProcess::NotRunning)
                return false;
            m_process->waitForReadyRead(200);
            out += m_process->readAllStandardOutput();
        }
        m_hosts = QString::fromUtf8(out).trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        return m_hosts.size() == count;
    }

    QStringList hosts() const { return m_hosts; }
    // Nach stop(): je Lampe die gesendeten Werte, zum Beispiel [[{"brightness": 50}], []]
    QJsonArray puts() const { return m_puts; }

    void stop()
    {
        if (!m_process)
            return;
        m_process->closeWriteChannel();
        if (!m_process->waitForFinished(5000)) {
            m_process->kill();
            m_process->waitForFinished();
        }
        m_puts = QJsonDocument::fromJson(m_process->readAllStandardOutput().trimmed()).array();
        m_process.reset();
    }

private:
    std::unique_ptr<QProcess> m_process;
    QStringList m_hosts;
    QJsonArray m_puts;
};

} // namespace kameradienst
