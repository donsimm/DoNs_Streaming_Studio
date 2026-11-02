# 02 – Umgebung (Zielsystem)

Stand: 2026-11-02. Alle Werte wurden auf dem Studio-PC gemessen (Ausgaben des Nutzers im Terminal), ausser wo angegeben.

## Studio-PC

| Punkt | Wert |
|---|---|
| Betriebssystem | Debian GNU/Linux 13 (trixie) |
| Kernel | 6.12.107+deb13-amd64 |
| Sitzung | X11 (`XDG_SESSION_TYPE=x11`), Xfce (Fenstermanager xfwm4) |
| CPU | Intel Core i7-4771 @ 3,50 GHz, 4 Kerne / 8 Threads, AVX, AVX2, SSE4.2 |
| RAM | 31 GiB nutzbar (32 GB DDR3) |
| Grafik | NVIDIA GeForce GTX 1660 SUPER (TU116), 6 GB; Treiber 550.163.01 (Debian, non-free), `nvidia-smi` zeigt CUDA 12.4 |
| Bildschirm | HDMI-0, 1920x1080 |
| Mainboard | ASUS Z87-A, BIOS-Modus Legacy (Angabe des Nutzers) |
| Datenträger | System `/dev/sda1` 42 GB, `/home` auf `/dev/sda6` 614 GB |
| Netz | Kabel; Heimnetz noch nicht aufgebaut |
| Python | 3.13.5 |
| GCC | 14.2.0 (Debian 14.2.0-19) |
| Git | 2.47.3 |
| Claude Code | 2.1.309, aus dem apt-Repository (Kanal «stable») |

Nicht installiert (Stand der Messung): `pip3`, `cmake`, `node`, `ffmpeg`. Sie werden erst installiert, wenn die Technik feststeht.

## Paketversionen in Debian 13 (Installationskandidaten)

| Paket | Version |
|---|---|
| python3-pyqt6 | 6.9.0-2 |
| python3-pyqt6.qtwebengine | 6.9.0-1 |
| qt6-base-dev | 6.8.2+dfsg-9+deb13u2 |
| qt6-webengine-dev | 6.8.2+dfsg-4 |
| nvidia-driver | 550.163.01-2 (non-free) |
| nvidia-settings | 550.163.01-1 (contrib) |

Ob PyQt6 6.9.0 zusammen mit Qt 6.8.2 im Betrieb Probleme macht, ist nicht geprüft.

## Streaming-PC (nicht Teil der Entwicklung)

Angaben des Nutzers: Intel i9-14900KF, NVIDIA RTX 4090 mit 24 GB, 64 GB DDR5, ASUS ROG Maximus Z790 Dark Hero, Nobara. Dort läuft das Haupt-OBS, das meist Browserquellen nutzt.

## Nicht geprüft

- Qt/PyQt6 per pip für Python 3.13 in passenden Versionen
- NDI-SDK: Lizenzbedingungen für die private Nutzung und Linux-Bezug
- Alphakanal von WebM in Qt Multimedia mit aktueller Version
- Leistung von Chromium-Darstellung und NDI-Codierung auf dem i7-4771 (NDI rechnet auf der CPU und hängt laut NDI-Dokumentation an CPU und Speicherbandbreite)

## Regeln für die Umgebung

- Entwicklung und Tests laufen auf diesem Gerät, in der Xfce-Sitzung.
- Pakete mit festen Versionen. Ein Einrichtungsskript im Repo baut die Umgebung wieder auf (kommt mit dem ersten Baustein).
- Ein Abbild der sauberen Installation (zum Beispiel mit Clonezilla) ist empfohlen, vom Nutzer noch nicht bestätigt.
