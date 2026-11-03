# 05 – Quellen

Stand: 2026-11-02.

## Angaben des Nutzers

Hardware des Studio-PCs (Bildschirmfotos, 32 GB DDR3), Streaming-PC (i9-14900KF, RTX 4090, Nobara), alle Entscheide, Bild der Oberfläche, Dokument mit den Lehren aus dem alten Projekt, Ausgaben der Terminalbefehle auf dem Studio-PC.

## Alter Entwurf (nur gelesen)

https://github.com/donsimm/donstv_Studio (CLAUDE.md, docs/01 bis 05)

## Betriebssysteme und Qt

- Debian 13, PyQt6: https://packages.debian.org/ja/trixie/python3-pyqt6
- Debian 13, PySide6: https://packages.debian.org/tr/trixie/libpyside6-py3-6.8
- Debian 13 Veröffentlichung: https://www.omgubuntu.co.uk/2025/08/debian-13-trixie-released
- Ubuntu 26.04 LTS: https://www.debugpoint.com/ubuntu-26-04-lts/?amp=1
- Fedora 44 (Qt): https://linuxsecurity.com/advisories/fedora/qt6-fedora-44-2026-70776c2dc3 und https://packages.fedoraproject.org/pkgs/qt6-qtdeclarative/qt6-qtdeclarative/
- Fedora Mass Rebuild: https://fedoraproject.org/wiki/Fedora_44_Mass_Rebuild und https://fedoraproject.org/wiki/Fedora_43_Mass_Rebuild
- X11 und Wayland (Sekundärquelle): https://www.besthub.dev/articles/why-linux-desktops-are-abandoning-x11-for-wayland-in-2026-after-40-years-effbf2257ca0
- PyQt-Mailingliste: https://riverbankcomputing.com/pipermail/pyqt/2025-April/046191.html
- Qt Quick Szenengraph: https://doc.qt.io/qt-6.2/qtquick-visualcanvas-scenegraph.html
- Qt Multimedia: https://doc.qt.io/qt-6.4/qtmultimedia-index.html

## OBS

- Browserquelle: https://obsproject.com/kb/browser-source.html
- Installation: https://obsproject.com/wiki/Install-Instructions
- OBS 33 (CEF, Plugin-Umbau): https://www.linuxcompatible.org/story/obs-studio-330rc1-released-major-plugin-rework-and-cef-upgrade-lead-the-way/
- Flatpak-Unterstützung: https://www.gamingonlinux.com/2022/02/obs-studio-272-is-out-bringing-flatpak-support-and-much-more/page=1/
- obs-websocket: https://github.com/obsproject/obs-websocket und https://github.com/obsproject/obs-websocket/blob/release/5.0.0/docs/generated/protocol.md

## NDI

- Leistung und Umsetzung: https://docs.ndi.video/all/developing-with-ndi/sdk/performance-and-implementation
- Bandbreite: https://docs.ndi.video/docs/white-paper/bandwidth/ndi-high-bandwidth-based-on-speedhq7-8bit-4-2-2-4
- Last auf älterer CPU (Forum, alt): https://forums.vmix.com/posts/t7077-CPU-usage-with-multiple-NDI-desktop-capture-s

## WebM mit Alpha

- FFmpeg-Ticket: https://trac.ffmpeg.org/ticket/8344

## Grafikkarte und Treiber

- GTX 1660 SUPER (Datenblatt): https://preisvergleich.heise.de/gigabyte-geforce-gtx-1660-super-oc-6g-gv-n166soc-6gd-a2167345.html?va=b
- NVIDIA-Treiber und offene Kernel-Module: https://hn.jakechampion.name/reader/https://archlinux.org/news/nvidia-590-driver-drops-pascal-support-main-packages-switch-to-open-kernel-modules
- Installation des Debian-13-Treibers (Sekundärquelle): https://linuxcapable.com/install-nvidia-drivers-on-debian/

## Claude Code und Abbild

- Installation von Claude Code: https://code.claude.com/docs/en/setup
- Clonezilla Live: https://clonezilla.org/clonezilla-live-doc.php

## Twitch und Chat-Bot

- EventSub per WebSocket: https://dev.twitch.tv/docs/eventsub/handling-websocket-events/
- Streamer.bot: https://docs.streamer.bot/guide/actions und https://streamer.bot/features

## Qt (Bau ab P03)

Abgerufen in P04 (2026-11-02):

- QStandardPaths (AppDataLocation unter Linux `~/.local/share/<APPNAME>`): https://doc.qt.io/qt-6.8/qstandardpaths.html
- QSaveFile (atomares Speichern über `commit()`): https://doc.qt.io/qt-6.8/qsavefile.html
- QRandomGenerator::system() (Zufallsquelle des Betriebssystems, kryptografisch sicher unter Linux): https://doc.qt.io/qt-6.8/qrandomgenerator.html

Genannt in P05 (2026-11-02, nicht abgerufen, siehe Bericht P05):

- Qt Style Sheets, Referenz: https://doc.qt.io/qt-6.8/stylesheet-reference.html
- QMainWindow (saveState/restoreState, Docks): https://doc.qt.io/qt-6.8/qmainwindow.html
- QWidget::grab: https://doc.qt.io/qt-6.8/qwidget.html#grab
- PNG, IHDR-Block: https://www.w3.org/TR/png-3/#11IHDR

Abgerufen in P06 (2026-11-02):

- OBS Studio, Design «Yami» und Standardwerte, Commit `65c320b46e1af5c7a555161aea6182cdf7a8c524`: https://github.com/obsproject/obs-studio (`frontend/data/themes/Yami.obt`, `frontend/OBSApp.cpp` Zeile 420, Lizenz `COPYING`, GPL Version 2)
- Open Sans 3.003, Commit `bd7e37632246368c60fdcbd374dbf9bad11969b6`: https://github.com/googlefonts/opensans (Lizenz `OFL.txt`, SIL Open Font License 1.1)
- Qt 6.8.2, `qstylesheetstyle.cpp`: https://raw.githubusercontent.com/qt/qtbase/v6.8.2/src/widgets/styles/qstylesheetstyle.cpp

Abgerufen in P07 (2026-11-02):

- QLocalServer, QLocalSocket (Unix-Socket, `UserAccessOption`, `removeServer`): https://doc.qt.io/qt-6.8/qlocalserver.html, https://doc.qt.io/qt-6.8/qlocalsocket.html
- Qt 6.8.2, `qlocalsocket_unix.cpp` (Fehler vor Zustandswechsel, Fehlertexte): https://code.qt.io/cgit/qt/qtbase.git/tree/src/network/socket/qlocalsocket_unix.cpp?h=v6.8.2
- QLockFile (`setStaleLockTime`): https://doc.qt.io/qt-6.8/qlockfile.html
- unix(7) (Länge des Socket-Pfads): https://man7.org/linux/man-pages/man7/unix.7.html
- proc_stat(5), proc_meminfo(5), proc_pid_net(5): https://man7.org/linux/man-pages/man5/proc_stat.5.html, https://man7.org/linux/man-pages/man5/proc_meminfo.5.html, https://man7.org/linux/man-pages/man5/proc_pid_net.5.html
- `RTF_UP` aus `/usr/include/linux/route.h` (Paket linux-libc-dev 6.12.113-1, lokal gelesen)
- systemd: systemd.service(5), systemd.unit(5), systemctl(1), sd_notify(3), systemd-system.conf(5), D-Bus-Eigenschaften der Einheiten: https://www.freedesktop.org/software/systemd/man/latest/systemd.service.html, https://www.freedesktop.org/software/systemd/man/latest/systemd.unit.html, https://www.freedesktop.org/software/systemd/man/latest/systemctl.html, https://www.freedesktop.org/software/systemd/man/latest/sd_notify.html, https://www.freedesktop.org/software/systemd/man/latest/systemd-system.conf.html, https://www.freedesktop.org/software/systemd/man/latest/org.freedesktop.systemd1.html
- NDI-Markenhinweis: https://docs.ndi.video/docs/sdk/licensing

Abgerufen in P08 (2026-11-02 und 2026-11-03):

- Be Vietnam Pro 1.002 im Repo google/fonts, Commit `c13390277dfb1fcc7415d84ef7ef9cfc1e52f8c1`: https://github.com/google/fonts/tree/c13390277dfb1fcc7415d84ef7ef9cfc1e52f8c1/ofl/bevietnampro (Dateien über https://raw.githubusercontent.com/google/fonts/c13390277dfb1fcc7415d84ef7ef9cfc1e52f8c1/ofl/bevietnampro/, Lizenz `OFL.txt`, SIL Open Font License 1.1, Verweis in der Datei: https://scripts.sil.org/OFL); Projekt der Schrift: https://github.com/bettergui/BeVietnamPro
- fontconfig, Muster und Ausgabeformat von fc-list/fc-query: https://www.freedesktop.org/software/fontconfig/fontconfig-user.html; Umrechnung des Gewichts (FcWeightToOpenType): https://gitlab.freedesktop.org/fontconfig/fontconfig/-/blob/2.15.0/src/fcweight.c
- QAbstractItemView (DragDropMode, DropIndicatorPosition): https://doc.qt.io/qt-6.8/qabstractitemview.html
- QAbstractSpinBox::keyboardTracking: https://doc.qt.io/qt-6.8/qabstractspinbox.html#keyboardTracking-prop
- Qt::TimerType (Genauigkeit von CoarseTimer, ±5 %): https://doc.qt.io/qt-6.8/qt.html#TimerType-enum
- QStatusBar::showMessage: https://doc.qt.io/qt-6.8/qstatusbar.html#showMessage
- QProcess: https://doc.qt.io/qt-6.8/qprocess.html
- Qt Test: https://doc.qt.io/qt-6.8/qttest-index.html

Abgerufen in P09 (2026-11-03):

- OBS Studio, Quellcode Commit `65c320b46e1af5c7a555161aea6182cdf7a8c524` (Skalieren, Seitenverhältnis, Drehen, Zeiger, Pfeiltasten): https://github.com/obsproject/obs-studio/blob/65c320b46e1af5c7a555161aea6182cdf7a8c524/frontend/widgets/OBSBasicPreview.cpp, https://github.com/obsproject/obs-studio/blob/65c320b46e1af5c7a555161aea6182cdf7a8c524/frontend/OBSBasic.cpp
- Qt 6.8.2 Quellcode (Fläche links im Baum, Übersetzungsdateien): https://github.com/qt/qtbase/blob/v6.8.2/src/widgets/itemviews/qtreeview.cpp, https://github.com/qt/qtbase/blob/v6.8.2/src/widgets/styles/qstylesheetstyle.cpp, https://github.com/qt/qtbase/blob/v6.8.2/src/corelib/kernel/qtranslator.cpp
- Qt 6.8: QTranslator::load https://doc.qt.io/qt-6.8/qtranslator.html#load-2, QFont::setVariableAxis https://doc.qt.io/qt-6.8/qfont.html#setVariableAxis, QFontDatabase::addApplicationFont https://doc.qt.io/qt-6.8/qfontdatabase.html#addApplicationFont, QRawFont https://doc.qt.io/qt-6.8/qrawfont.html, QImage::Format https://doc.qt.io/qt-6.8/qimage.html#Format-enum, QPainter https://doc.qt.io/qt-6.8/qpainter.html, QThreadPool::start https://doc.qt.io/qt-6.8/qthreadpool.html#start-1, QMetaObject::invokeMethod https://doc.qt.io/qt-6.8/qmetaobject.html#invokeMethod-5, QMainWindow::setMenuWidget https://doc.qt.io/qt-6.8/qmainwindow.html#setMenuWidget, QWidget::grab https://doc.qt.io/qt-6.8/qwidget.html#grab, QSaveFile https://doc.qt.io/qt-6.8/qsavefile.html
- FFmpeg 7.1 (Paket 7.1.5-0+deb13u1): Dekodieren https://ffmpeg.org/doxygen/7.1/group__lavc__decoding.html, Lesen und Springen https://ffmpeg.org/doxygen/7.1/group__lavf__decoding.html, AVFrame https://ffmpeg.org/doxygen/7.1/structAVFrame.html, libswscale https://ffmpeg.org/doxygen/7.1/group__libsws.html, Pixelformate https://ffmpeg.org/doxygen/7.1/pixdesc_8h.html, Matroska `alpha_mode` https://ffmpeg.org/doxygen/7.1/matroskadec_8c_source.html, VP9-Alpha nur mit libvpx-vp9 https://trac.ffmpeg.org/ticket/8344
- OpenType: Tabelle `fvar` https://learn.microsoft.com/en-us/typography/opentype/spec/fvar, Aufbau der Datei https://learn.microsoft.com/en-us/typography/opentype/spec/otff
- Urbanist[wght] (Testschrift, SIL Open Font License 1.1) im Repo google/fonts, Commit `c13390277dfb1fcc7415d84ef7ef9cfc1e52f8c1`: https://github.com/google/fonts/tree/c13390277dfb1fcc7415d84ef7ef9cfc1e52f8c1/ofl/urbanist
- std::condition_variable: https://en.cppreference.com/w/cpp/thread/condition_variable/wait
- sysstat (pidstat, mpstat) 12.7.5: https://sysstat.github.io/

Abgerufen in P09b (2026-11-03):

- OBS Studio, Quellcode Commit `65c320b46e1af5c7a555161aea6182cdf7a8c524` (Menü «Transformieren», Spiegeln durch Ziehen, Seitenverhältnis der Quelle): https://github.com/obsproject/obs-studio/blob/65c320b46e1af5c7a555161aea6182cdf7a8c524/frontend/widgets/OBSBasic_SceneItems.cpp, https://github.com/obsproject/obs-studio/blob/65c320b46e1af5c7a555161aea6182cdf7a8c524/frontend/widgets/OBSBasic_Scenes.cpp, https://github.com/obsproject/obs-studio/blob/65c320b46e1af5c7a555161aea6182cdf7a8c524/frontend/widgets/OBSBasic_Clipboard.cpp, https://github.com/obsproject/obs-studio/blob/65c320b46e1af5c7a555161aea6182cdf7a8c524/frontend/forms/OBSBasic.ui, https://github.com/obsproject/obs-studio/blob/65c320b46e1af5c7a555161aea6182cdf7a8c524/frontend/data/locale/de-DE.ini, https://github.com/obsproject/obs-studio/blob/65c320b46e1af5c7a555161aea6182cdf7a8c524/libobs/obs-scene.c
- EXIF 2.32 (CIPA DC-008-2019), Tag Orientation: https://www.cipa.jp/std/documents/e/DC-X008-Translation-2019-E.pdf
- PNG Third Edition, Chunk `eXIf`: https://www.w3.org/TR/png-3/
- CSS `image-orientation`: https://developer.mozilla.org/en-US/docs/Web/CSS/image-orientation
- Chromium, `eXIf` in PNG: https://chromium.googlesource.com/chromium/src/+/694d9d3a5cc2bc5fa8b36a5dd11b55d437555ca6
- Qt 6.8: QImageReader::setAutoTransform https://doc.qt.io/qt-6.8/qimagereader.html#setAutoTransform, eigenes Layout https://doc.qt.io/qt-6.8/layout.html#how-to-write-a-custom-layout-manager

Weitere Qt-Quellen je Baustein: `docs/berichte/`.
