#pragma once

#include <QString>
#include <QStringList>

// Ordner für Einstellungen des Nutzers und Suche der Symbole (P07, Teil A). Nur Qt Core
// (CLAUDE.md Regel 9).

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

// Markenhinweis für «Hilfe → Über», wenn ein eigenes NDI-Symbol vorliegt. Wortlaut aus den
// Lizenzangaben des NDI SDK: https://docs.ndi.video/docs/sdk/licensing
QString ndiTrademarkNotice();

} // namespace dss
