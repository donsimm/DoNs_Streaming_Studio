#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

class QProcess;
class QTimer;

// Geheimnis verschlüsselt ablegen (P10c für OBS, seit P13 für alle Geheimnisse; Regel 12, Entscheid
// F1): systemd-creds --user, ohne Anmeldedialog und ohne Schlüsselbund. Das Geheimnis geht nur über
// stdin und stdout, nie über die Befehlszeile und nie ins Protokoll. Es läuft als eigener Prozess:
// Die Hauptschleife blockiert nicht, und die Aufträge laufen der Reihe nach.
//
// Abweichung vom Plan P10c (Abschnitt 1.4): Die verschlüsselte Ausgabe kommt über stdout und wird
// hier mit QSaveFile und Rechten 0600 geschrieben, statt dass systemd-creds die Datei selbst anlegt.
// So ist die Datei nie mit anderen Rechten da, und ein Abbruch lässt die alte Datei stehen.
//
// Grenze: Andere Programme desselben Benutzers können es ebenfalls entschlüsseln, und der Schlüssel
// des Rechners liegt auf derselben Platte. Die Vorgabe «kein Klartext» ist erfüllt.
//
// Qt Core ohne dss_common (P13, Plan 2.2): dss_obs und dss_twitch binden diese Bibliothek ein.
//
// Quelle: https://www.freedesktop.org/software/systemd/man/latest/systemd-creds.html (--user seit
// 256, --name, --newline, «-» für stdin und stdout)

namespace dss {

class SecretStore : public QObject
{
    Q_OBJECT

public:
    // Antwort auf read: std::nullopt = nicht lesbar, leer = keines gespeichert.
    using ReadDone = std::function<void(const std::optional<QString> &)>;
    // Antwort auf write: leer = geschrieben, sonst die Meldung auf Deutsch.
    using WriteDone = std::function<void(const QString &)>;

    // Bezeichnungen für Protokoll und Meldungen; das Geheimnis selbst steht nie darin.
    struct Labels
    {
        QString log;  // Vorsilbe im Protokoll, zum Beispiel «OBS-Passwort»
        QString user; // Wort in der Meldung an den Nutzer, zum Beispiel «Passwort»
    };

    // filePath: Datei neben kern.ini, je Version getrennt. credentialName: Name für --name=,
    // systemd-creds prüft ihn beim Entschlüsseln. program: anderes Programm statt systemd-creds
    // (Tests).
    SecretStore(const QString &filePath, const QString &credentialName, const Labels &labels,
                const QString &program = QStringLiteral("systemd-creds"), QObject *parent = nullptr);
    ~SecretStore() override;

    QString filePath() const { return m_filePath; }
    // Name für --name= (P11): Eine Datei, die mit einem anderen Namen verschlüsselt wurde (zum
    // Beispiel die Zugangsdatei von Lotse, Name «obs-websocket»), lässt sich nur mit diesem Namen
    // lesen. Gilt für Aufträge, die danach beginnen.
    QString credentialName() const { return m_credentialName; }
    void setCredentialName(const QString &name) { m_credentialName = name; }
    bool isSet() const;

    void read(ReadDone done);
    void write(const QString &secret, WriteDone done);
    // Sofort; false mit Meldung, wenn die Datei bleibt.
    bool remove(QString *error);

private:
    struct Job
    {
        bool encrypt = false;
        QString secret;
        ReadDone readDone;
        WriteDone writeDone;
    };

    void startNext();
    void finishJob(bool ok, const QByteArray &output);
    void finishEncrypt(const Job &job, bool ok, const QByteArray &output);

    QString m_filePath;
    QString m_program;
    QString m_credentialName;
    Labels m_labels;
    QList<Job> m_jobs;
    QProcess *m_process = nullptr;
    QTimer *m_timeout = nullptr;
    bool m_running = false;
};

} // namespace dss
