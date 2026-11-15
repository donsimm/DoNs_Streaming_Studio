# P13 – Bericht Twitch-Anbindung mit Ereignisfluss (Punkt 92)

Stand: 2026-11-15, nach Commit 10 (Dokumente). Plan: `docs/archiv/prompts/P13-twitch.md`. Schritt 0: `docs/archiv/recherchen/2026-11-15-schritt0-twitch-streamerbot.md`. Ausgeführt auf `lotse/p13` in parallelen Spuren neben P14 und P15. Jeder Commit ist in Stufe «Klein» geprüft (Bau und die Tests des Commits). **Prüfstufe «Baustein» und Handtest stehen aus** (folgen durch Lotse).

## Ablauf

| Teil | Inhalt | Tests |
|---|---|---|
| Schritt 0 | Quellen gegen die aktuelle Doku geprüft (Felder, Argumentnamen, Rechte, Abzeichen, IRC-Hinweis); nachgeführt in `eventtypes.h` | `tst_ereignisfluss` |
| 1 | `src/secrets/` (`dss::SecretStore`) aus `ObsSecretStore` herausgelöst, Verhalten unverändert | `tst_obspasswort` unverändert |
| 2 | Ereignisfluss `src/events/` (`dss_events`, nur Qt Core): `EventBus`, Typenliste mit Pflichtfeldern und Argumentnamen, Verlauf 200 | `tst_ereignisfluss` |
| 3 | Kern: Fluss in `CoreService`, Adapter `obs.scene_changed`, `subscribe_events`, `event_history`, `event`, `events_dropped`, `dss-cli ereignisse`, Schnittstelle 14 | `tst_protocol`, `tst_aenderungen`, `tst_kernverbindung` |
| 4 | `dss_twitch`: `eventsubprotocol`, `chatsession` (erste Nachricht im Stream, `twitch-sitzung.json`), Testdaten `tests/daten/twitch/` | `tst_twitchprotokoll` |
| 5 | `twitchauth` (Gerätefluss, Ablage, Erneuern, stündliche Prüfung, Abmelden, zwei Konten), `helixclient`, `tests/twitchtestserver.h` | `tst_twitchanmeldung` (22 Fälle) |
| – | Bauskripte `scripts/bauen.sh`, `scripts/testen.sh` (kurze Ausgabe), Hinweis in `CLAUDE.md` | – |
| 6 | `eventsubconnection` (Welcome, 10 Abos, Stream-Zustand beim Start, Keepalive, Reconnect, Revocation, Close-Codes, Wartezeiten, TLS) | `tst_twitchverbindung` |
| 7 | Kern: `TwitchService`, `kern.ini [twitch]`, drei `.cred`-Dateien, App-Token für das Abzeichen, `twitch_status`, `twitch_login_*`, `connections.twitch`, Schalter `DSS_CORE_TWITCH`, Option `--twitch-testserver`, `dss-cli einstellung-twitch-client-id` | `tst_kerntwitch` (neu), `tst_kerneinstellungen`, `tst_protocol` |
| 8a | Fenster: Bereich «Twitch» im Dialog «Einstellungen», `twitch_status` im `CoreClient` | `tst_windowdata`, `tst_fensterbearbeiten` (`settingsDialogTwitch`) |
| 8b | Fenster: Reiter «Chat-Bot» mit Kopfzeile und Unterreiter «Live» (Liste «Ereignisse»), Docks je Reiter, Twitch-Knopf öffnet den Bereich | `tst_windowdata`, `tst_fensterbearbeiten` (`chatBotTab`) |
| 9 | `scripts/twitch-einrichten.sh` mit Testoptionen; `dss-cli einstellungen` zeigt die Client-ID | `tst_twitcheinrichten` |
| 10 | Dokumente `01`, `02`, `03`, `04` (Punkt 92 ins Archiv, Punkte 113 bis 120), `05`, `06`, `08`; dieser Bericht | – |

## Entscheide des Nutzers

- 2026-11-09: Anmeldung mit Code im Browser; Kanal ist Affiliate; alles lesen und schreiben; Bereich «Twitch» in den Einstellungen und Liste «Ereignisse» im Reiter «Chat-Bot»; eigenes Bot-Konto; Follows direkt von Twitch; Go.
- 2026-11-15: Reihenfolge P13, P14, Kamerasteuerung; P14 als Aktionssystem, die Ereignisse von P13 sind seine Auslöser; Bot nur, solange das Studio offen ist; Oberfläche nach dem Entwurf (Docks im Reiter «Chat-Bot» ausgeblendet); Rechte «wirklich alles» (Frage 1 B); Abzeichen «Chat Bot» mit vertraulicher App und Secret im Studio (Frage 2 B); Bot-Konto aus der Zeit mit Streamer.bot.

Die Entscheide stehen auch in `docs/01-festlegungen.md`, «Twitch (P13)».

## Geändert

- **Geheimnisse:** `src/secrets/secretstore.{h,cpp}`, `src/obs/obssecretstore.{h,cpp}` (nur noch Hülle).
- **Ereignisfluss:** `src/events/` (`event`, `eventbus`, `eventtypes`); Kern `src/core/coreservice.{h,cpp}`, `src/core/obsevents.{h,cpp}`; `src/common/protocol.{h,cpp}`, `src/common/coresession.{h,cpp}`; `src/cli/main.cpp` (`ereignisse`).
- **Twitch-Bibliothek:** `src/twitch/` (`eventsubprotocol`, `chatsession`, `twitchauth`, `helixclient`, `twitchendpoints.h`, `eventsubconnection`, `twitchservice`).
- **Kern-Anbindung:** `src/core/main.cpp` (Schalter, `--twitch-testserver`), `src/common/coresettings.{h,cpp}` (`[twitch]`), `CMakeLists.txt` (`DSS_CORE_TWITCH`).
- **Fenster:** `src/app/settingsdialog.{h,cpp}` (Bereich «Twitch»), `src/app/chatbotpage.{h,cpp}` (neu), `src/app/mainwindow.{h,cpp}`, `src/app/headerbar.{h,cpp}`, `src/app/theme.{h,cpp}` (neuer Wert `fontSizeLarge`), `src/common/coreclient.{h,cpp}` (`twitchStatus`, `sendTwitchAccountRequest`, `subscribeEvents`), `src/common/windowdata.{h,cpp}`.
- **Skripte:** `scripts/twitch-einrichten.sh`, `scripts/bauen.sh`, `scripts/testen.sh`.
- **Tests:** `tst_ereignisfluss`, `tst_twitchprotokoll`, `tst_twitchanmeldung`, `tst_twitchverbindung`, `tst_kerntwitch`, `tst_twitcheinrichten` (`tests/twitch_einrichten_pruefen.py`) neu; `tst_protocol`, `tst_aenderungen`, `tst_kernverbindung`, `tst_kerneinstellungen`, `tst_windowdata`, `tst_fensterbearbeiten` erweitert; `tests/twitchtestserver.h`, `tests/daten/twitch/` (mit `LIESMICH.md`, URL je Datei).
- **Dokumente:** `docs/01`, `02`, `03`, `04`, `05`, `06`, `08`, `docs/archiv/04-erledigte-punkte.md`, `CLAUDE.md` (Bauskripte bei der Prüfstufe «Klein»).
- **Lastprobe (Prüfstufe «Baustein»):** `tests/tst_kerntwitch.cpp` (Testfall `chatLoad`, nur mit `DSS_LASTPROBE=1` aktiv), `tests/CMakeLists.txt` (Eintrag `tst_kerntwitch_last`, nur mit `ctest -C Lastprobe`).

**Neue Texte für den Nutzer:** Bereich «Twitch» (`Mit Twitch verbinden`, Client-ID, Client Secret verdeckt mit «Secret löschen», Zeilen «Kanal» und «Bot» mit «Anmelden …»/«Abmelden», während der Anmeldung Code, Adresse, «Im Browser öffnen», «Code kopieren», Restzeit, «Abbrechen», Zeile «Zustand» mit Abos und Abzeichen); Reiter «Chat-Bot» mit Unterreiter «Live» und Liste «Ereignisse»; Meldungen der Anmeldung (zum Beispiel «Code abgelaufen, neu anmelden»); Ausgaben von `scripts/twitch-einrichten.sh`. Wortlaut im Code (`src/app/settingsdialog.cpp`, `src/common/windowdata.cpp`).

## Geprüft

- **Stufe «Klein», je Commit 1 bis 9:** Bau ok, Tests des Commits grün (Agent, 2026-11-15; Tests je Commit in «Ablauf»). Fenstertests liefen in den Spuren nicht (parallele Agenten).
- **Commit 10:** `scripts/bauen.sh` (Agent, 2026-11-15): «Bau ok (0 Warnungen)». Die in den Dokumenten genannten Dateien mit `ls` geprüft (Agent, 2026-11-15): alle vorhanden. Keine Tests nötig (nur Dokumentation).
- **Prüfstufe «Baustein»** (Agent, 2026-11-16, unten): Bau, voller `ctest` mit X11, frischer Klon, `startpruefung.py`, `dienstpruefung.py` und 50 Wiederholungen bestanden. **Lastprobe** (60 s mit 50 Chat-Nachrichten pro Sekunde): Test gebaut, **nicht geprüft** (Aufruf in dieser Sitzung nicht freigegeben).
- **Fenstertests** `tst_fensterbearbeiten` (`settingsDialogTwitch`, `chatBotTab`): im vollen `ctest` mit X11 grün (Agent, 2026-11-16).

## Prüfstufe Baustein (2026-11-16)

Stand `lotse/p13` mit der Testkorrektur zu `tst_fensterbearbeiten`, dazu der Testfall `chatLoad` (ohne `DSS_LASTPROBE=1` besteht er sofort). Studio-PC, X11-Sitzung aktiv (`/sys/class/tty/tty0/active` = tty7; `wiederholen.sh` prüft vor jedem Lauf `Active` und `LockedHint`), Last nach den Läufen 0,07/0,08/0,13. Daneben keine Bauten oder Tests, kein anderer Agent; die Läufe nacheinander, nie zwei Fenstertests gleichzeitig. Rohdaten unter `build/pruefung-p13/` (nicht im Repo).

| Stufe | Befehl | Ergebnis |
|---|---|---|
| Bau | `scripts/bauen.sh -j 8` (2026-11-15, nach `chatLoad`) | «Bau ok (0 Warnungen)» |
| Tests | `scripts/testen.sh` (ohne Filter) | **55 von 55**, keiner übersprungen, 275 s; `tst_kerntwitch_last` ist ohne `-C Lastprobe` nicht in der Liste |
| Frischer Klon | `/var/tmp/dss-klon-p13` (von Lotse angelegt, Stand vor `chatLoad`): `cmake -S … -B …/build`, `cmake --build …/build -j8`, `ctest --test-dir …/build` | Bau ohne Fehler und ohne Warnung; **55 von 55**, keiner übersprungen, 294 s |
| Startprüfung | `python3 scripts/startpruefung.py --runs 20 --out build/pruefung-p13/start/startpruefung` (2026-11-16, 00:10–00:13) | je **20 von 20** sauber: `core-sigterm` (Start Median 38,7 ms, Ende 4,9 ms), `core-sigint` (38,9 / 4,7 ms), `app-sigterm` (243,4 / 15,4 ms), `app-kern-sigterm` (253,6 / 28,2 ms); keine Restprozesse, keine unerwarteten Zeilen |
| Dienstprüfung | `python3 scripts/dienstpruefung.py --kills 5 --out build/pruefung-p13/dienst` | **bestanden**. Fenster startet den Kern in 0,307 s; Neustart nach `kill -9` 1,117 bis 1,138 s, Fenster wieder verbunden 1,947 bis 3,138 s |
| 50 Wiederholungen | `scripts/wiederholen.sh build <Test> 50 <absoluter Ordner>`, nacheinander bis 00:57 | je **50 von 50**: `tst_ereignisfluss` (Ende 00:13:50, Lauf 0,4 s), `tst_twitchanmeldung` (00:22:12, 9,9 s), `tst_twitchverbindung` (00:32:26, 12,2 s), `tst_kerntwitch` (00:35:07, 3,2 s), `tst_kernverbindung` (00:57:13, 26,5 s) |
| Lastprobe | `ctest --test-dir build -C Lastprobe -R '^tst_kerntwitch_last$' -V` | **nicht geprüft**: Aufruf in dieser Sitzung nicht freigegeben (siehe unten) |

**Lastprobe (neu, `tst_kerntwitch` `chatLoad`).** Der Kern läuft als eigener Prozess aus `build/` gegen `tests/twitchtestserver.h` wie `loginRestartLogout`; nur das Konto «Kanal» wird angemeldet (`channel.chat.message` liest als Kanal). Das Fenster ist ein roher Socket und bestellt `twitch.chat_message`. Der Testserver schickt 3000 Nachrichten aus `chat_message.json` mit eigener `message_id` (`last-000000` …), 50 pro Sekunde (Takt 20 ms, holt verspätete Takte nach). Alle 200 ms fragt das Fenster `get_overlay_list`; gemessen wird bis zum Lesen der Antwort im Fenster. Jede Sekunde liest der Test `utime + stime` des Kerns aus `/proc/<pid>/stat`. Prüft: 0 `events_dropped`, alle 3000 Ereignisse in Reihenfolge (`origin_id`), mindestens 240 Antworten, längste Antwort höchstens 25 ms; Kern endet mit Exit-Code 0. Ausgabe (`qInfo`): Antwortzeiten min/Median/p95/max, CPU des Kerns in % eines Kerns (Mittel, Median, p95, max und jede Sekunde). Ohne `DSS_LASTPROBE=1` kehrt der Testfall sofort zurück, ohne `QSKIP`, damit der normale `ctest` kurz bleibt und 0 übersprungen meldet. Der ctest-Eintrag `tst_kerntwitch_last` setzt die Variable und läuft nur mit `ctest -C Lastprobe`. Die Option `--einsatz-verbindung` aus dem Auftrag gibt es auf diesem Branch nicht; der Kern im Test hat wie die übrigen Kerntests eigenen Datenordner, eigene Verbindung und eigenes `XDG_CONFIG_HOME`.

**Nicht gelaufen:** Der direkte Aufruf mit `DSS_LASTPROBE=1`, ein Hilfsskript und `ctest -C Lastprobe` wurden in dieser Sitzung nicht freigegeben. Ziele (Reihenfolge, 25 ms) und CPU-Werte sind darum **nicht geprüft**; auch der Code im Lastmodus ist noch nie gelaufen.

**Dienste:** `systemctl --user is-active` war nicht freigegeben. Der installierte Kern lief vor den Prüfungen und läuft am Ende mit derselben PID (`pgrep -a dss-core`: nur `~/.local/opt/dons-streaming-studio/bin/dss-core`); kein Kern aus `build/` blieb zurück. Die Dienstprüfung nutzt die eigene Einheit `dss-core-pruefung.service` und entfernt sie wieder.
- **Echte Verbindung zu Twitch über TLS:** nicht geprüft (Handtest L2).

## Handtest

Aus dem Plan, «Handtest». Was der Agent oder Lotse schon geprüft hat, wiederholt der Nutzer nicht. **Nicht geprüft, folgt durch Lotse.**

### Prüft Lotse

Mit Kern und Fenster aus `build/`, eigenem `XDG_CONFIG_HOME` für `dss-app`, ohne Zugriff auf Twitch-Konten.

**L1 Bildschirmfotos.**
- Was: Bereich «Twitch» in den Zuständen «abgemeldet», «Code angezeigt» (Kanal und Bot), «angemeldet» und «Rechte fehlen», dazu die Liste «Ereignisse». Ablage nur im Scratchpad. Dabei Punkt 113 (Restzeit nach Ablauf) ansehen.
- Warum: Aussehen.

**L2 Ereignisse aus dem echten Chat.**
- Was: `dss-cli ereignisse --folgen` mitlesen, während der Nutzer in N3 schreibt. Erwartet: `twitch.chat_message` mit dem Text des Nutzers innerhalb von 2 s, `role` 4; `twitch.first_message` genau einmal je Konto, das zum ersten Mal schreibt; eine Nachricht des Bot-Kontos mit `is_bot_account: true`; `obs.scene_changed` beim Wechsel auf «test 2» und zurück auf «Szene» (nie während eines Streams).
- Warum: echter Weg über TLS zu Twitch, den `ctest` nicht hat.

**L3 Dauerlauf, nachts, mindestens 5 h.**
- Was: Kern und Fenster laufen, Lotse liest nur das Protokoll. Erwartet: mindestens eine echte Erneuerung beider Token nach etwa 4 h, stündliche Prüfung beider Konten, Verbindung bleibt (Reconnects mit Grund im Protokoll), kein Token im Protokoll.
- Warum: klärt den Widerspruch in der Doku zum Erneuern beim Gerätefluss (Punkt 117).

**L4 Neustart.**
- **Eigener Schritt:** `systemctl --user restart dss-core-entwicklung.service`.
- Was: Beide Anmeldungen bleiben, keine neue Anmeldung nötig, Abos innerhalb von 10 s wieder aktiv.
- Warum: Ablage mit `systemd-creds` im echten Dienst.

### Prüft der Nutzer

**N1 App registrieren.**
- Was: `scripts/twitch-einrichten.sh`. Dabei Kategorie und «Client Type» notieren (Punkt 115).
- Warum: Konto und Zwei-Faktor-Anmeldung gehören dem Nutzer.

**N2 Anmelden, beide Konten.**
- Was: Twitch-Knopf, bei «Kanal» «Anmelden …», Code im Browser bestätigen, Rechte ansehen. Bei «Bot» «Anmelden …», Code in einem privaten Browserfenster mit dem Bot-Konto bestätigen. Im eigenen Chat `/mod <Botname>` eingeben.
- Warum: Nur der Nutzer kann sich anmelden. Klärt Punkt 116 (Secret beim Gerätecode).

**N3 Ereignisse auslösen.**
- Was: eine Nachricht im eigenen Chat schreiben, dann eine mit dem Bot-Konto im Browser; Follow, Sub, Raid und Cheer im nächsten Stream bei Gelegenheit, Lotse liest danach das Protokoll.
- Warum: Echte Ereignisse lassen sich nur über Konten auslösen (Punkt 120).

**N4 Abmelden und Freigabe entziehen.**
- **Eigener Schritt:** «Abmelden» beim Bot, danach wieder anmelden.
- **Eigener Schritt:** Auf twitch.tv mit dem Kanal-Konto unter «Einstellungen → Verbindungen» die App trennen. Erwartet: Knopf rot und «Anmeldung abgelaufen …», spätestens bei der nächsten stündlichen Prüfung. Danach wieder anmelden.
- Warum: Bedienung und Konto.

## Nicht erledigt, `[OFFEN]`

- Lastprobe der Prüfstufe «Baustein»: gebaut, nicht gelaufen (oben). Aufruf: `ctest --test-dir build -C Lastprobe -R '^tst_kerntwitch_last$' -V`, ohne Last daneben.
- Handtest (oben).
- Punkt 113: Restzeit des Codes nach Ablauf läuft nur nach der Uhr des PCs, bis der Kern `login_pending` entfernt.
- Punkt 114: Client-ID im Skript nur Kleinbuchstaben und Ziffern, im Kern auch Grossbuchstaben; angleichen?
- Punkt 115: Kategorie («z. B. Application Integration») und «Client Type» bei der Registrierung nicht belegt.
- Punkt 116: Client Secret beim Tausch des Gerätecodes nicht belegt.
- Punkt 117: Erneuern beim Gerätefluss, Doku widersprüchlich (L3).
- Punkt 118: Abzeichen «Chat Bot», Seite der Doku 404; sichtbar erst beim Senden mit P14.
- Punkt 119: Argumentnamen «eigener Name» nicht bei Streamer.bot belegt.
- Punkt 120: Felder von Sub, Gift, Raid und Cheer nur nach der Doku nachgebaut.
- «Letzter Follower» über einen Neustart (TX-16) braucht einen gespeicherten Verlauf; nicht in P13, der Verlauf ist nur im Speicher.
- Schnittstelle: Beim Zusammenführen mit P14 ist Version 14 in P14 in Version 15 aufgegangen (Lotse). `docs/08` steht auf diesem Branch bei Version 14; die endgültige Nummer setzt Lotse.
- `docs/07-bauplan.md` (Stand von P13) ist nicht nachgeführt; das führt der Projektleiter-Agent.

## Annahmen, Interna-Entscheide

- Lastprobe als Testfall in `tst_kerntwitch` statt als Skript: nutzt den vorhandenen Testserver und Kernstart; eigener ctest-Eintrag mit `CONFIGURATIONS Lastprobe`, damit der normale `ctest` unverändert 55 Tests ohne Übersprungene zählt. Antwortzeit wird beim Lesen der Antwort im Fenster genommen (nicht über `qWaitFor`, das in Schritten von bis zu 10 ms prüft). Overlay-Liste leer (frischer Datenordner).

Aus dem Plan («Annahmen und Interna-Entscheide») unverändert umgesetzt: EventSub statt IRC; Gerätefluss selbst gebaut ohne Qt NetworkAuth; eigene Bibliothek `dss_events` mit Qt Core; Zustellung immer verzögert; Fluss nur für Ereignisse; Typenliste mit Pflichtfeldern und Argumentnamen im Code; eigene Feldnamen im Fluss; «erste Nachricht» im Kern abgeleitet, gespeichert ohne Namen; Geschenk einzeln als `twitch.gift_sub`; Rollen und `is_bot_account` schon in P13; `dss_obs` unverändert; Ereignisse zum Fenster nur auf Bestellung mit Gegendruck 1 MiB; Verlauf 200 im Speicher; `SecretStore` herausgelöst; Access- und Refresh-Token in einer Datei je Konto; Client-ID in `kern.ini`; Keepalive +5 s; Wartezeit 2 bis 60 s mit Zufall; doppelte `message_id` (500) übergangen; Chat-Texte nie ins Protokoll; eigener Testserver; `--twitch-testserver` nur `127.0.0.1`; Schalter `DSS_CORE_TWITCH`; Abos nur für Ereignisse von P14, Rechte für alles.

Dazu in der Ausführung:

| Entscheid | Begründung |
|---|---|
| Nach Schritt 0: `tier` als Zahl (`subscriptionTier`) und `tier_name` als Text («tier 1», Argument `tier`), Resub mit `cumulative`, `monthStreak`, `multiMonthDuration`, Gift Bomb mit `anonymous`, `gifts`, `totalGifts`, `is_highlight`, `stream_offline` mit `ended_at` | so bei Streamer.bot belegt (Schritt 0, Punkt 19) |
| Client Secret geht beim Tausch und Erneuern mit, wenn gesetzt | Erneuern verlangt es bei vertraulichen Apps; beim Tausch nicht belegt, ob nötig oder störend (`[OFFEN]`, Punkt 116, klärt N2) |
| Rechte des Bots nur `user:write:chat`, `user:bot`, `moderator:manage:announcements` | der Bot schreibt nur; Kanal hält alle übrigen Rechte |
| App-Token nur im Speicher, erneuert 10 min vor Ablauf und nach 401 | Client Credentials liefern kein Refresh-Token; neu holen kostet nichts |
| Secret im Bereich «Twitch» wird mit «OK» gespeichert, nicht mit eigenem Knopf | wie der Bereich «OBS» (Passwort) |
| Zwei Knöpfe je Konto (Anmelden/Abmelden), Abos und Abzeichen als Textzeile in «Zustand» | knapp, eine Zeile je Konto |
| Neuer Theme-Wert `fontSizeLarge` (16 pt, fett) für den Anmeldecode | Code gross zum Abtippen im Browser, Grösse im Thema statt im Stylesheet |
| Nur Unterreiter «Live»; weitere mit P14 (`addSubTab`, `addLivePanel`) | Auftrag P13; P14 hängt sich dort ein |
| Docks im Reiter «Chat-Bot» ausgeblendet, Menü «Docks» dort gesperrt, beim Zurück kommen die vorher offenen wieder | Entscheid 2026-11-15; der Nutzer verliert seine Anordnung nicht |
| `CoreClient::subscribeEvents`: eine Bestellung je Verbindung | so legt die Schnittstelle es fest (neue Bestellung ersetzt die alte) |
| Twitch-Knopf öffnet den Dialog beim Bereich «Twitch» mit Fokus | wie F3 bei OBS |
| `scripts/twitch-einrichten.sh` mit Testoptionen (`--client-id`, `--ohne-browser`, `--cli`, `--verbindung`) | Prüfung mit eigenem `XDG_CONFIG_HOME` und eigenem Kern ohne Browser |
| `dss-cli einstellungen` zeigt die Client-ID; Meldung von `einstellung-twitch-client-id` berichtigt | der Nutzer sieht nach dem Skript, was eingetragen ist |
| Bauskripte `scripts/bauen.sh`, `scripts/testen.sh` | kurze Ausgabe für Agenten, volles Protokoll in `build/` |

## Quellen

Alle Quellen: `docs/05-quellen.md`, «Twitch und Chat-Bot» (Stand P13), Plan «Quellen», Bericht Schritt 0 und die Kommentare in `src/twitch/`, `src/events/eventtypes.h`, `src/secrets/secretstore.h`. Paketversionen: `docs/02-umgebung.md`, «Twitch: TLS und Adressen (P13)».
