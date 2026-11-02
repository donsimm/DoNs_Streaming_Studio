#pragma once

#include <QString>

#include <memory>

class QLockFile;

// Sperre im Datenordner (P07, Entscheid 2): Solange der Kern läuft, hält er die Sperre und
// ist der einzige, der die Overlay-Dateien ändert. dss-cli nimmt dieselbe Sperre für die
// Dauer eines Änderungs-Befehls; ist sie belegt, bricht der Befehl ab.
//
// Umsetzung mit QLockFile (Datei <Datenordner>/kern.lock mit PID, Rechnername und
// Programmname). Die Sperre gilt dauerhaft, deshalb staleLockTime 0: Nur eine Sperre,
// deren Prozess nicht mehr läuft, gilt als verwaist und wird von QLockFile entfernt
// (zum Beispiel nach kill -9 des Kerns). https://doc.qt.io/qt-6.8/qlockfile.html

namespace dss {

inline constexpr char kLockFileName[] = "kern.lock";

// <dataDir>/kern.lock
QString lockFilePath(const QString &dataDir);

class DataLock
{
public:
    explicit DataLock(const QString &dataDir);
    ~DataLock();

    // Nimmt die Sperre ohne zu warten. Legt den Datenordner an, wenn er fehlt.
    // false: *error nennt den Grund; *holderPid ist die PID des Halters (0, wenn unbekannt).
    bool tryLock(QString *error, qint64 *holderPid = nullptr);
    bool isLocked() const;
    void unlock();

private:
    QString m_dataDir;
    std::unique_ptr<QLockFile> m_lock;
};

} // namespace dss
