#pragma once

#include <QString>

// Wer belegt einen Port? (P10, Teil A 2; Punkt 77). Sucht in /proc/net/tcp und /proc/net/tcp6
// den lauschenden Socket (Zustand 0A = LISTEN) auf dem Port, danach den Prozess, der ihn offen
// hat (/proc/<pid>/fd → «socket:[Inode]»). Prozesse anderer Benutzer lassen sich nicht lesen;
// dann bleibt pid 0. Aufbau der Dateien: https://docs.kernel.org/networking/proc_net_tcp.html,
// proc_pid_fd(5) und proc_pid_exe(5): https://man7.org/linux/man-pages/man5/proc_pid_fd.5.html,
// https://man7.org/linux/man-pages/man5/proc_pid_exe.5.html

namespace dss {

struct PortOwner
{
    bool listening = false; // ein lauschender Socket auf dem Port gefunden
    qint64 pid = 0;         // 0 = Prozess nicht feststellbar
    QString executable;     // Pfad des Programms (leer ohne pid)
};

// procRoot: «/proc» oder ein Testordner mit demselben Aufbau.
PortOwner findPortOwner(int port, const QString &procRoot = QStringLiteral("/proc"));

// Meldung für das Fenster, zum Beispiel «Port 8081 ist belegt, vermutlich von DoNs Streaming
// Studio im Einsatz (Prozess 1234). Anderen Port im Dialog «Einstellungen» wählen.»
// installedDir: Ordner der Version im Einsatz (~/.local/opt/dons-streaming-studio).
QString portBusyMessage(int port, const PortOwner &owner, const QString &installedDir);

} // namespace dss
