#pragma once

#include <QString>
#include <QStringList>

// Ordner und Pfade: Einstellungen des Nutzers, Suche der Symbole (P07, Teil A), Verbindung
// zum Kern und Name der Einheit (P07, Teile C und D). Nur Qt Core (CLAUDE.md Regel 9).

namespace dss {

// Ordner der Einstellungen: ~/.config/dons-streaming-studio (bzw. $XDG_CONFIG_HOME/…).
// Dort liegen fenster.ini (P05) und der Ordner symbole/ mit eigenen Symbolen des Nutzers.
// https://doc.qt.io/qt-6.8/qstandardpaths.html (GenericConfigLocation)
QString configDir();

// Eigene Symbole des Nutzers: configDir() + "/symbole". Liegt ausserhalb des Repos, weil das
// Repo öffentlich ist (docs/04-offene-punkte.md, «Entschieden nach P06»).
QString userSymbolDir();

// Pfad der ersten vorhandenen Datei fileName in dirs (in dieser Reihenfolge), leer, wenn
// keiner der Ordner sie enthält. fileName ist ein reiner Dateiname ohne «/».
QString findSymbolFile(const QString &fileName, const QStringList &dirs);

// Verbindung zum Kern (Unix-Socket): $XDG_RUNTIME_DIR/dons-streaming-studio/kern.sock.
// XDG_RUNTIME_DIR gehört nur dem Benutzer (Rechte 0700).
// https://doc.qt.io/qt-6.8/qstandardpaths.html (RuntimeLocation)
QString defaultConnectionPath();

// Benutzerdienst von systemd, den das Fenster startet (scripts/dienst-einrichten.sh).
inline constexpr char kCoreUnitName[] = "dss-core.service";

// Markenhinweis für «Hilfe → Über», wenn ein eigenes NDI-Symbol vorliegt. Wortlaut aus den
// Lizenzangaben des NDI SDK: https://docs.ndi.video/docs/sdk/licensing
QString ndiTrademarkNotice();

} // namespace dss
