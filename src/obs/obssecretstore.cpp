#include "obssecretstore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QTimer>

namespace dss::obs {

namespace {

constexpr int kProcessTimeoutMs = 10000;

} // namespace

ObsSecretStore::ObsSecretStore(const QString &filePath, QObject *parent)
    : ObsSecretStore(filePath, QStringLiteral("systemd-creds"), parent)
{
}

ObsSecretStore::ObsSecretStore(const QString &filePath, const QString &program, QObject *parent)
    : QObject(parent)
    , m_filePath(filePath)
    , m_program(program)
    , m_process(new QProcess(this))
    , m_timeout(new QTimer(this))
{
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(kProcessTimeoutMs);
    connect(m_timeout, &QTimer::timeout, m_process, &QProcess::kill);
    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        m_timeout->stop();
        finishJob(status == QProcess::NormalExit && exitCode == 0, m_process->readAllStandardOutput());
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return; // sonst kommt finished
        m_timeout->stop();
        qWarning("OBS-Passwort: %s nicht ausführbar", qPrintable(m_program));
        finishJob(false, {});
    });
}

ObsSecretStore::~ObsSecretStore()
{
    m_jobs.clear();
    m_process->disconnect(this);
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

bool ObsSecretStore::isSet() const
{
    return QFileInfo::exists(m_filePath);
}

void ObsSecretStore::read(ReadDone done)
{
    m_jobs.append(Job{false, QString(), std::move(done), nullptr});
    startNext();
}

void ObsSecretStore::write(const QString &password, WriteDone done)
{
    m_jobs.append(Job{true, password, nullptr, std::move(done)});
    startNext();
}

bool ObsSecretStore::remove(QString *error)
{
    QFile file(m_filePath);
    if (!file.exists() || file.remove())
        return true;
    *error = QStringLiteral("%1 kann nicht gelöscht werden (%2)").arg(m_filePath, file.errorString());
    return false;
}

void ObsSecretStore::startNext()
{
    while (!m_running && !m_jobs.isEmpty()) {
        // Kopie: Scheitert der Start, ist der Auftrag nach start() schon beantwortet und weg.
        const bool encrypt = m_jobs.first().encrypt;
        const QByteArray input = m_jobs.first().password.toUtf8();
        if (!encrypt && !isSet()) {
            const Job finished = m_jobs.takeFirst();
            finished.readDone(QString());
            continue;
        }
        m_running = true;
        QStringList arguments{QStringLiteral("--user"),
                              QStringLiteral("--name=") + m_credentialName};
        if (encrypt)
            arguments << QStringLiteral("encrypt") << QStringLiteral("-") << QStringLiteral("-");
        else
            arguments << QStringLiteral("--newline=no") << QStringLiteral("decrypt") << m_filePath
                      << QStringLiteral("-");
        m_timeout->start();
        m_process->start(m_program, arguments);
        if (m_process->state() != QProcess::NotRunning) {
            if (encrypt)
                m_process->write(input);
            m_process->closeWriteChannel();
        }
    }
}

void ObsSecretStore::finishJob(bool ok, const QByteArray &output)
{
    if (!m_running)
        return;
    m_running = false;
    const QByteArray errors = m_process->readAllStandardError();
    const Job job = m_jobs.takeFirst();
    if (!ok)
        qWarning("OBS-Passwort: %s fehlgeschlagen: %s", job.encrypt ? "Verschlüsseln" : "Entschlüsseln",
                 errors.trimmed().constData());
    if (job.encrypt) {
        finishEncrypt(job, ok, output);
    } else if (ok) {
        job.readDone(QString::fromUtf8(output));
    } else {
        job.readDone(std::nullopt);
    }
    startNext();
}

void ObsSecretStore::finishEncrypt(const Job &job, bool ok, const QByteArray &output)
{
    if (!ok) {
        job.writeDone(QStringLiteral("Passwort nicht verschlüsselt (systemd-creds, siehe Protokoll des Kerns)."));
        return;
    }
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    QSaveFile file(m_filePath);
    const bool written = file.open(QIODevice::WriteOnly)
                         && file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
                         && file.write(output) == output.size() && file.commit();
    if (!written) {
        job.writeDone(QStringLiteral("%1 kann nicht geschrieben werden (%2)").arg(m_filePath, file.errorString()));
        return;
    }
    job.writeDone(QString());
}

} // namespace dss::obs
