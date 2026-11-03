#pragma once

#include "configpaths.h"

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QString>

#include <functional>
#include <optional>

// Einstellungen des Kerns (P10-vor, Teil A 3): Datei kern.ini im Ordner der Einstellungen der
// Version (~/.config/dons-streaming-studio/ im Einsatz, ~/.config/dons-streaming-studio-
// entwicklung/ in Arbeit). Nur der Kern schreibt sie, atomar (QSaveFile); das Fenster ändert sie
// über die Schnittstelle (docs/08-kern-schnittstelle.md, «Einstellungen»). Inhalt: Port des
// Overlay-Servers und Adresse im Heimnetz (nur IPv4). Beide wirken erst mit dem Server (P10).
// Nur Qt Core und Qt Network (QHostAddress, QNetworkInterface), kein Qt Gui (Regel 9).
//
// Aufbau der Datei (UTF-8, eine Angabe je Zeile, «#» beginnt einen Kommentar):
//
//   [overlay-server]
//   port=8081
//   adresse-im-heimnetz=192.168.1.9
//
// Quellen:
//   QSaveFile: https://doc.qt.io/qt-6.8/qsavefile.html
//   QNetworkInterface: https://doc.qt.io/qt-6.8/qnetworkinterface.html (interfaceFromName,
//     addressEntries), QHostAddress: https://doc.qt.io/qt-6.8/qhostaddress.html
//   /proc/net/route: https://man7.org/linux/man-pages/man5/proc_pid_net.5.html (Standardroute
//     wie die LAN-Anzeige, defaultRouteInterface in systemstats.h)
//   Portgrenzen: ip(7), https://man7.org/linux/man-pages/man7/ip.7.html
//     (ip_local_port_range, ip_unprivileged_port_start), Werte auf dem Studio-PC gemessen

namespace dss {

// Gültige Ports (Begründung in docs/08-kern-schnittstelle.md, «Einstellungen»): ab 1024, weil
// tiefere Ports unter Linux besondere Rechte brauchen (ip_unprivileged_port_start = 1024), bis
// 32767, weil Linux die Ports ab 32768 (ip_local_port_range 32768 bis 60999) als Absender
// ausgehender Verbindungen vergibt; ein Port dort kann beim Start des Servers zufällig belegt sein.
inline constexpr int kPortMin = 1024;
inline constexpr int kPortMax = 32767;

struct CoreSettings
{
    int port = 0;
    QString homeAddress; // IPv4 mit Punkten, leer = nicht gesetzt

    bool operator==(const CoreSettings &) const = default;
};

// Standard: Port 8080 im Einsatz, 8081 in Arbeit (Entscheid 2026-11-03), Adresse leer.
CoreSettings defaultCoreSettings(Edition edition = buildEdition());
// <Ordner der Einstellungen>/kern.ini (configDir)
QString coreSettingsPath(Edition edition = buildEdition());

// Prüfungen; leer = gültig, sonst die Meldung auf Deutsch.
QString validatePort(int port);
// Leer ist erlaubt (dann trägt der Kern beim nächsten Start die Adresse der Standardroute ein).
// Sonst vier Zahlen 0 bis 255 mit Punkten, ohne führende Nullen; nicht 0.0.0.0, nicht
// 127.x.x.x, nicht Multicast (224 bis 239) und nicht reserviert (ab 240, auch 255.255.255.255).
QString validateHomeAddress(const QString &address);

// Text der Datei lesen bzw. schreiben. Fehlende Angaben gelten mit dem Standardwert; eine
// unbekannte Angabe, ein Abschnitt ausser [overlay-server], eine doppelte Angabe oder ein
// ungültiger Wert machen die Datei ungültig (false mit Meldung).
bool parseCoreSettings(const QByteArray &text, const CoreSettings &defaults, CoreSettings *settings,
                       QString *error);
QByteArray serializeCoreSettings(const CoreSettings &settings);

// IPv4-Adresse der Netzwerkkarte der Standardroute: Karte aus routeText (/proc/net/route), ihre
// Adressen von addressesOf; die erste IPv4-Adresse, die nicht 127.x.x.x und nicht 169.254.x.x
// ist. Leer, wenn es keine Standardroute oder keine solche Adresse gibt; *interface (falls nicht
// nullptr) bekommt die Karte, *reason den Grund, wenn leer.
QString homeAddressFromRoute(const QByteArray &routeText,
                             const std::function<QList<QHostAddress>(const QString &)> &addressesOf,
                             QString *interface = nullptr, QString *reason = nullptr);
// Dasselbe mit /proc/net/route und QNetworkInterface.
QString detectHomeAddress(QString *interface = nullptr, QString *reason = nullptr);

// Einstellungen im Kern: lädt die Datei beim Start, füllt eine fehlende Adresse einmal aus der
// Standardroute und schreibt die Datei, nimmt Änderungen an (prüfen, atomar schreiben, erst dann
// übernehmen). Ist die Datei ungültig, gelten die Standardwerte, die Datei wird nicht
// überschrieben, und Änderungen werden abgewiesen, bis sie von Hand repariert ist (wie
// liste.json).
class CoreSettingsStore
{
public:
    using AddressDetector = std::function<QString(QString *interface, QString *reason)>;

    CoreSettingsStore(const QString &path, const CoreSettings &defaults,
                      AddressDetector detector = detectHomeAddress);

    // Beim Start des Kerns. *log bekommt die Protokollzeilen (Info), *warnings Warnungen.
    void load(QStringList *log, QStringList *warnings);

    QString path() const { return m_path; }
    CoreSettings settings() const { return m_settings; }
    // Meldung, wenn die Datei ungültig ist; sonst leer.
    QString fileError() const { return m_fileError; }

    // Ändert die angegebenen Werte (mindestens einer). false mit Meldung: ungültiger Wert,
    // Datei ungültig oder nicht schreibbar; dann gilt der alte Stand.
    bool change(std::optional<int> port, std::optional<QString> homeAddress, QString *error);

private:
    bool write(const CoreSettings &settings, QString *error);

    QString m_path;
    CoreSettings m_defaults;
    AddressDetector m_detector;
    CoreSettings m_settings;
    QString m_fileError;
};

} // namespace dss
