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

**Neue Texte für den Nutzer:** Bereich «Twitch» (`Mit Twitch verbinden`, Client-ID, Client Secret verdeckt mit «Secret löschen», Zeilen «Kanal» und «Bot» mit «Anmelden …»/«Abmelden», während der Anmeldung Code, Adresse, «Im Browser öffnen», «Code kopieren», Restzeit, «Abbrechen», Zeile «Zustand» mit Abos und Abzeichen); Reiter «Chat-Bot» mit Unterreiter «Live» und Liste «Ereignisse»; Meldungen der Anmeldung (zum Beispiel «Code abgelaufen, neu anmelden»); Ausgaben von `scripts/twitch-einrichten.sh`. Wortlaut im Code (`src/app/settingsdialog.cpp`, `src/common/windowdata.cpp`).

## Geprüft

- **Stufe «Klein», je Commit 1 bis 9:** Bau ok, Tests des Commits grün (Agent, 2026-11-15; Tests je Commit in «Ablauf»). Fenstertests liefen in den Spuren nicht (parallele Agenten).
- **Commit 10:** `scripts/bauen.sh` (Agent, 2026-11-15): «Bau ok (0 Warnungen)». Die in den Dokumenten genannten Dateien mit `ls` geprüft (Agent, 2026-11-15): alle vorhanden. Keine Tests nötig (nur Dokumentation).
- **Prüfstufe «Baustein»** (voller `ctest` mit X11, frischer Klon, `startpruefung.py`, `dienstpruefung.py`, 50 Wiederholungen von `tst_ereignisfluss`, `tst_twitchanmeldung`, `tst_twitchverbindung`, `tst_kerntwitch`, `tst_kernverbindung`, Last 60 s mit 50 Chat-Nachrichten pro Sekunde): **nicht geprüft, folgt durch Lotse.**
- **Fenstertests** `tst_fensterbearbeiten` (`settingsDialogTwitch`, `chatBotTab`): **nicht geprüft, folgt durch Lotse.**
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

- Prüfstufe «Baustein» und Handtest (oben).
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
