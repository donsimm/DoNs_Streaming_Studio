# 02 – Umgebung (Zielsystem)

Stand: 2026-11-02. Alle Werte wurden auf dem Studio-PC gemessen (Ausgaben des Nutzers im Terminal), ausser wo angegeben.

## Studio-PC

| Punkt | Wert |
|---|---|
| Betriebssystem | Debian GNU/Linux 13 (trixie) |
| Kernel | 6.12.113+deb13-amd64 (gemessen in P01 am 2026-11-02; am Morgen derselben Tages war 6.12.107 gemeldet) |
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

Nicht installiert (Stand der ersten Messung): `pip3`, `cmake`, `node`, `ffmpeg`. Seither installiert: `cmake` und `ffmpeg` (P01), die FFmpeg-Entwicklungspakete (P02), siehe unten. `pip3` und `node` sind weiterhin nicht installiert (nicht erneut geprüft).

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

## Installierte Pakete zum Bauen und Testen

Liste und Einrichtung: `scripts/pakete.txt`, `scripts/einrichten.sh`. Gemessen am 2026-11-02 (P03) mit `LANG=C apt-cache policy <Paket>` und `dpkg-query -W -f='${Package} ${Version}\n' <Paket>`; installierte Version und Kandidat sind jeweils gleich.

| Paket | installiert | Herkunft |
|---|---|---|
| build-essential | 12.12 | war schon installiert |
| cmake | 3.31.6-2 | P01 |
| qt6-base-dev | 6.8.2+dfsg-9+deb13u2 | P01 (enthält auch Qt Test, `Qt6TestConfig.cmake`) |
| libavformat-dev | 7:7.1.5-0+deb13u1 | P02 |
| libavcodec-dev | 7:7.1.5-0+deb13u1 | P02 |
| libavutil-dev | 7:7.1.5-0+deb13u1 | P02 |
| libswscale-dev | 7:7.1.5-0+deb13u1 | P02 |

Ebenfalls vorhanden, aber nicht in der Liste: `qt6-base-dev-tools` 6.8.2+dfsg-9+deb13u2 und `libqt6test6` 6.8.2+dfsg-9+deb13u2 (Abhängigkeiten von `qt6-base-dev`), `pkgconf` 1.8.1-4, `x11-utils` 7.7+7 (`xwininfo`, genutzt von der Startprüfung). Nicht weiterverfolgt, aber weiterhin installiert: `qt6-webengine-dev`, `qt6-multimedia-dev`, PyQt6. Nicht installiert: `xdotool`, `wmctrl`, `shellcheck`.

## Netzwerk

Stand 2026-11-03 (Angaben des Nutzers, Ausgabe von `ip -br addr && ip route` auf dem Studio-PC, 19:55):

| Angabe | Studio-PC |
|---|---|
| Netzwerkkarte | `enp3s0` (Kabel), `UP` |
| Adresse IPv4 | `192.168.1.9/24`, seit 2026-11-04 08:55 **fest auf dem Studio-PC eingetragen** (NetworkManager, Verbindung «Wired connection 1»: `ipv4.method manual`, Router `192.168.1.1`, Namensauflösung `192.168.1.1`; geprüft mit `ip route` → `proto static`, Ping zum Router 2 von 2, `getent hosts github.com`). Am Router weiter reserviert. Vorher per DHCP (Punkt 84). Rückgängig: `sudo nmcli connection modify "Wired connection 1" ipv4.method auto ipv4.addresses "" ipv4.gateway "" ipv4.dns ""` |
| Router (Standardroute) | `192.168.1.1` über `enp3s0` |
| IPv6 | öffentliche Adressen vorhanden (nicht im Repo festgehalten). Ob der Router eingehende IPv6-Verbindungen sperrt: nicht geprüft |
| USB-Tethering | nicht mehr vorhanden |

Streaming-PC (Ausgabe von `ip -br addr && ip route && ping -c 4 192.168.1.9` auf dem Streaming-PC, Angabe des Nutzers 2026-11-03, 19:59):

| Angabe | Streaming-PC |
|---|---|
| Netzwerkkarte | `eno2` (Kabel, Angabe des Nutzers), `UP`; `wlo1` (WLAN) `DOWN` |
| Adresse IPv4 | `192.168.1.116/24`, vom Router per DHCP, am Router fest reserviert (Angabe des Nutzers) |
| Router (Standardroute) | `192.168.1.1` über `eno2` |
| IPv6 | öffentliche Adressen vorhanden (nicht im Repo festgehalten) |
| Ping zum Studio-PC `192.168.1.9` | 4 von 4, 0 % Verlust, Zeit min/Mittel/max 0,139 / 0,313 / 0,582 ms, TTL 64 (gleiches Netz, kein Router dazwischen) |

## Streaming-PC (nicht Teil der Entwicklung)

Angaben des Nutzers: Intel i9-14900KF, NVIDIA RTX 4090 mit 24 GB, 64 GB DDR5, ASUS ROG Maximus Z790 Dark Hero. Betriebssystem am 2026-11-03 noch Zorin OS (Rechnername `Dominik-Desktop`), Wechsel zu Nobara zeitnah geplant (Angabe des Nutzers). Dort läuft das Haupt-OBS, das meist Browserquellen nutzt.

## Nicht geprüft

- Qt/PyQt6 per pip für Python 3.13 in passenden Versionen
- NDI-SDK: Lizenzbedingungen für die private Nutzung und Linux-Bezug
- Alphakanal von WebM in Qt Multimedia mit aktueller Version
- Leistung von Chromium-Darstellung und NDI-Codierung auf dem i7-4771 (NDI rechnet auf der CPU und hängt laut NDI-Dokumentation an CPU und Speicherbandbreite)

## Regeln für die Umgebung

- Entwicklung und Tests laufen auf diesem Gerät, in der Xfce-Sitzung.
- Pakete mit festen Versionen. Ein Einrichtungsskript im Repo baut die Umgebung wieder auf (kommt mit dem ersten Baustein).
- Ein Abbild der sauberen Installation (zum Beispiel mit Clonezilla): Der Nutzer verzichtet darauf (entschieden 2026-11-02). Die Umgebung wird stattdessen per Einrichtungsskript im Repo reproduzierbar gemacht.

## Bildschirmschoner und Sperre

Abgeschaltet am 2026-11-02 (Entscheid des Nutzers, Punkt 26 in `docs/04-offene-punkte.md`), damit Prüfungen mit Fenstern nicht von der Sperre unterbrochen werden. Nur für den Benutzer, keine Systemdateien geändert:

- `~/.config/autostart/light-locker.desktop` mit `Hidden=true`: light-locker startet nicht mehr (Autostart-Spezifikation, https://specifications.freedesktop.org/autostart-spec/latest/).
- `~/.config/autostart/bildschirmschoner-aus.desktop` mit `Exec=xset s off`: Bildschirmschoner von X11 bei jeder Anmeldung aus.
- Abschaltung des Bildschirms (DPMS) in der Energieverwaltung von Xfce war schon aus (`/xfce4-power-manager/dpms-enabled false`).

Geprüft nach Neustart: `xset q` zeigt `timeout: 0`, `pgrep light-locker` findet nichts. Rückgängig: beide Dateien löschen und neu anmelden.
