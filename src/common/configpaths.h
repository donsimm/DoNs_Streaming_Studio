#pragma once

#include <QString>
#include <QStringList>

// Ordner und Pfade: Einstellungen des Nutzers, Suche der Symbole (P07, Teil A), Verbindung
// zum Kern und Name der Einheit (P07, Teile C und D), getrennt nach Version (P10a, Teil B).
// Nur Qt Core (CLAUDE.md Regel 9).

namespace dss {

// Version in Arbeit (Bau aus dem Repo) oder im Einsatz (installiert mit scripts/installieren.sh).
// Festgelegt beim Bau mit dem Schalter DSS_INSTALLED_EDITION, nicht nach dem Ort zur Laufzeit:
// Ein Bau aus dem Repo nutzt so immer den Entwicklungsordner, auch wenn er von einem anderen Ort
// aus startet oder dorthin kopiert wird, und kann die echten Daten nie treffen (P10a, Teil B).
enum class Edition { Development, Installed };

// Version dieses Baus.
Edition buildEdition();
// Name der Version für Protokoll und --version: «Entwicklung» bzw. «Einsatz».
QString editionLabel(Edition edition = buildEdition());

// Name der Ordner für Daten, Einstellungen und Verbindung: «dons-streaming-studio» im Einsatz,
// «dons-streaming-studio-entwicklung» in Arbeit.
QString appDirName(Edition edition = buildEdition());

// Ordner der Einstellungen: ~/.config/<appDirName> (bzw. $XDG_CONFIG_HOME/…).
// Dort liegt fenster.ini (P05), getrennt je Version.
// https://doc.qt.io/qt-6.8/qstandardpaths.html (GenericConfigLocation)
QString configDir(Edition edition = buildEdition());

// Eigene Symbole des Nutzers: ~/.config/dons-streaming-studio/symbole für beide Versionen
// (nur lesen, P10a). Liegt ausserhalb des Repos, weil das Repo öffentlich ist
// (docs/04-offene-punkte.md, «Entschieden nach P06»).
QString userSymbolDir();

// Pfad der ersten vorhandenen Datei fileName in dirs (in dieser Reihenfolge), leer, wenn
// keiner der Ordner sie enthält. fileName ist ein reiner Dateiname ohne «/».
QString findSymbolFile(const QString &fileName, const QStringList &dirs);

// Verbindung zum Kern (Unix-Socket): $XDG_RUNTIME_DIR/<appDirName>/kern.sock.
// XDG_RUNTIME_DIR gehört nur dem Benutzer (Rechte 0700).
// https://doc.qt.io/qt-6.8/qstandardpaths.html (RuntimeLocation)
QString defaultConnectionPath(Edition edition = buildEdition());

// Benutzerdienst von systemd, den das Fenster startet (scripts/dienst-einrichten.sh):
// dss-core.service im Einsatz, dss-core-entwicklung.service in Arbeit.
QString coreUnitName(Edition edition = buildEdition());

// Titel des Hauptfensters: «DoNs Streaming Studio», in Arbeit mit « – Entwicklung».
QString windowTitle(Edition edition = buildEdition());

// Markenhinweis für «Hilfe → Über», wenn ein eigenes NDI-Symbol vorliegt. Wortlaut aus den
// Lizenzangaben des NDI SDK: https://docs.ndi.video/docs/sdk/licensing
QString ndiTrademarkNotice();

} // namespace dss
