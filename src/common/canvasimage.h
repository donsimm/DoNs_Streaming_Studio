#pragma once

#include <QDateTime>
#include <QString>

// Ablage und Name des Bildes der Leinwand (P10-vor, Teil B 2, «Datei → Leinwand als Bild
// speichern»). Nur Qt Core; gezeichnet wird in dss_render (overlayimage.h).
//
// Ordner: <Bilder>/DoNs Streaming Studio. <Bilder> ist QStandardPaths::PicturesLocation; Qt liest
// dafür XDG_PICTURES_DIR aus $XDG_CONFIG_HOME/user-dirs.dirs (sonst ~/.config/user-dirs.dirs),
// dieselbe Datei wie xdg-user-dir PICTURES, und nimmt ~/Pictures, wenn dort nichts steht.
// Auf dem Studio-PC am 2026-11-03 gemessen: beide /home/dominik/Bilder.
// Quellen: https://doc.qt.io/qt-6.8/qstandardpaths.html (PicturesLocation),
//   https://github.com/qt/qtbase/blob/v6.8.2/src/corelib/io/qstandardpaths_unix.cpp
//   (user-dirs.dirs, Schlüssel XDG_PICTURES_DIR),
//   https://www.freedesktop.org/wiki/Software/xdg-user-dirs/

namespace dss {

QString canvasImageDir();

// <adresse>_<JJJJ-MM-TT_hh-mm-ss>.png in Ortszeit, zum Beispiel test-szene_2026-11-03_21-05-09.png.
QString canvasImageFileName(const QString &address, const QDateTime &time);

// Pfad in dir für canvasImageFileName. Gibt es die Datei schon (zweites Bild in derselben
// Sekunde), kommt «-2», «-3» … vor «.png»; ein vorhandenes Bild wird nie überschrieben.
QString uniqueCanvasImagePath(const QString &dir, const QString &address, const QDateTime &time);

} // namespace dss
