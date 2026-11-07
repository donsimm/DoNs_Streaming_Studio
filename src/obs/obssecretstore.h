#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

class QProcess;
class QTimer;

// Passwort von OBS verschlüsselt ablegen (P10c, Regel 12, Entscheid F1): systemd-creds --user, ohne
// Anmeldedialog und ohne Schlüsselbund. Das Passwort geht nur über stdin und stdout, nie über die
// Befehlszeile und nie ins Protokoll. Es läuft als eigener Prozess: Die Hauptschleife blockiert
// nicht, und die Aufträge laufen der Reihe nach.
//
// Abweichung vom Plan (Abschnitt 1.4): Die verschlüsselte Ausgabe kommt über stdout und wird hier mit
// QSaveFile und Rechten 0600 geschrieben, statt dass systemd-creds die Datei selbst anlegt. So ist die
// Datei nie mit anderen Rechten da, und ein Abbruch lässt die alte Datei stehen.
//
// Grenze: Andere Programme desselben Benutzers können es ebenfalls entschlüsseln, und der Schlüssel
// des Rechners liegt auf derselben Platte. Die Vorgabe «kein Klartext» ist erfüllt.
//
// Quelle: https://www.freedesktop.org/software/systemd/man/latest/systemd-creds.html (--user seit
// 256, --name, --newline, «-» für stdin und stdout)

namespace dss::obs {

class ObsSecretStore : public QObject
{
    Q_OBJECT

public:
    // Antwort auf read: std::nullopt = nicht lesbar, leer = keines gespeichert.
    using ReadDone = std::function<void(const std::optional<QString> &)>;
    // Antwort auf write: leer = geschrieben, sonst die Meldung auf Deutsch.
    using WriteDone = std::function<void(const QString &)>;

    // filePath: <Ordner der Einstellungen>/obs-passwort.cred, je Version getrennt.
    explicit ObsSecretStore(const QString &filePath, QObject *parent = nullptr);
    // program: anderes Programm statt systemd-creds (Tests).
    ObsSecretStore(const QString &filePath, const QString &program, QObject *parent = nullptr);
    ~ObsSecretStore() override;

    QString filePath() const { return m_filePath; }
    bool isSet() const;

    void read(ReadDone done);
    void write(const QString &password, WriteDone done);
    // Sofort; false mit Meldung, wenn die Datei bleibt.
    bool remove(QString *error);

private:
    struct Job
    {
        bool encrypt = false;
        QString password;
        ReadDone readDone;
        WriteDone writeDone;
    };

    void startNext();
    void finishJob(bool ok, const QByteArray &output);
    void finishEncrypt(const Job &job, bool ok, const QByteArray &output);

    QString m_filePath;
    QString m_program;
    QList<Job> m_jobs;
    QProcess *m_process = nullptr;
    QTimer *m_timeout = nullptr;
    bool m_running = false;
};

} // namespace dss::obs
