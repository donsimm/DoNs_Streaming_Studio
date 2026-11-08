# P10c – Bericht Stufe «Vor der Installation»

Stand: 2026-11-08. Auftrag von Lotse: Stufe «Vor der Installation» für den abgenommenen Stand von P10c (Branch `lotse/p10c`, Commit `1f228c2`), mit 50 Wiederholungen von `tst_fensterbearbeiten` (Entscheid des Nutzers). Kein Code und kein anderes Dokument geändert.

## Geändert

- **Bericht:** `docs/archiv/berichte/P10c-vor-bericht.md` (diese Datei). Sonst nichts im Repo.

## Geprüft

Alle Läufe 2026-11-08 auf dem Studio-PC, nacheinander, ohne Last daneben.

**Vor dem Start (16:20):**

| Prüfung | Ergebnis |
|---|---|
| `git status` | Branch `lotse/p10c`, sauber |
| `git log -1` | `1f228c2` «Übergabe: Handtest P10c abgenommen; CLAUDE.md: Zugang per SSH zum Streaming-PC», der abgenommene Stand |
| `/sys/class/tty/tty0/active` | `tty7` |
| `loginctl show-session 1 -p Active -p LockedHint --value` | `yes`, `no` (auch vor Schritt 2, 3 und 7 geprüft) |
| Prüfsummen vorher | `build/pruefung-vor-install/summen-vorher.txt`, 89 Dateien |

Die Prüfsummen erhob ein kleines Python-Skript (`build/pruefung-vor-install/summen.py`, nicht im Repo, `hashlib.sha256`) mit derselben Dateiliste wie `find .local/opt/dons-streaming-studio .local/share/dons-streaming-studio .config/dons-streaming-studio -type f`, sortiert nach Bytes, Ausgabe im Format von `sha256sum`. Grund: Lotse verlangte Befehle ohne Pipes.

**Schritte:**

| Nr. | Befehl | Ergebnis |
|---|---|---|
| 1 | `cmake --build build -j8`, `ctest --test-dir build` | Bau Exit 0 (nichts neu zu bauen); **42 von 42**, 157 s |
| 2 | `git clone --branch lotse/p10c … build/vorinstall-klon`, `cmake -S … -B … -DCMAKE_CXX_FLAGS="-Wall -Wextra"`, `cmake --build … -j8`, `ctest --test-dir build/vorinstall-klon/build` | Bau Exit 0, keine Warnung in der Ausgabe; **41 von 42**, 176 s, `tst_aenderungen` gescheitert (siehe unten) |
| 3 | `python3 scripts/startpruefung.py --runs 20 --out build/pruefung-vor-install/start/startpruefung` | je **20 von 20 sauber**: `core-sigterm`, `core-sigint`, `app-sigterm`, `app-kern-sigterm`. Start Fenster Median 226 ms bzw. 219 ms mit Kern |
| 4 | `python3 scripts/dienstpruefung.py --kills 5 --out build/pruefung-vor-install/dienst` | **bestanden**. Neustart nach `kill -9` 1.089 bis 1.114 s, Fenster wieder verbunden 1.815 bis 2.886 s |
| 5 | `scripts/installieren-pruefen.sh build/pruefung-vor-install/inst` (Start 16:30:58) | Installation Exit 0, Einsatz Commit `1f228c2`, keine Warnung. Daten nachher gleich vorher, «Format aktuell», `format-pruefen` Exit 0, `medien-liste` Exit 0 (10 Medien). Testeinheit entfernt, Kopien gelöscht. «FEHLER: Sicherung weicht ab», siehe unten |
| 6 | Prüfsummen nachher, `diff summen-vorher.txt summen-nachher.txt` | 89 Dateien, **kein Unterschied** |
| 7 | `scripts/wiederholen.sh /home/dominik/DoNs_Streaming_Studio/build tst_fensterbearbeiten 50 /home/dominik/DoNs_Streaming_Studio/build/pruefung-vor-install/wiederholen` | **50 von 50** bestanden, Ende 17:01:20 |
| Zusatz | `scripts/wiederholen.sh /home/dominik/DoNs_Streaming_Studio/build/vorinstall-klon/build tst_aenderungen 50 /home/dominik/DoNs_Streaming_Studio/build/pruefung-vor-install/wiederholen-aenderungen` | **48 von 50**, Ende 17:05:33. Lauf 40 und 49 scheiterten in `schedulerMaxDelay`, beide mit erstem Zeitpunkt 949 ms («Speichern bei 949 1922 2802» und «949 1912 2791»), siehe unten |

**Fehlschlag `tst_aenderungen` im frischen Klon (Schritt 2):**

- `TestAenderungen::schedulerMaxDelay()` (`tests/tst_aenderungen.cpp:580`): «Speichern bei 949 1909 2770 ms (letzte Änderung 2476 ms)». Verlangt ist für das erste Speichern 950 bis unter 1200 ms. Gemessen 949 ms, also 1 ms unter der Grenze. Die übrigen 70 Prüfungen des Tests bestanden; im Arbeitsordner (Schritt 1) bestand derselbe Test.
- Ursache: Die Grenze von 950 ms ist der Höchstwert von 1000 ms minus der 5 % Abweichung, die `Qt::CoarseTimer` laut Qt-Dokumentation haben darf, und lässt keine Reserve für die Rundung auf ganze Millisekunden. Gemessen wird mit `QElapsedTimer::elapsed()` (ganze Millisekunden, abgeschnitten); der Zeitgeber selbst rechnet mit eigener Rundung. 1 ms Unterschied liegt damit an der Messung im Test, nicht an der Logik von `SaveScheduler`. Wie Qt 6.8 die Zeitpunkte grober Zeitgeber genau rundet, ist **nicht geprüft** (Quelltext von Qt nicht gelesen).
- `src/common/savescheduler.cpp` und der Test sind in P10c nicht geändert: `git log` nennt für `savescheduler.*` nur einen Commit aus P08 Teil A, für `schedulerMaxDelay` (`git log -L557,583:tests/tst_aenderungen.cpp`) nur einen aus den Tests zu P08. Der Fehlschlag betrifft kein Verhalten, das der Nutzer sieht: gespeichert wird 1 ms früher als die Testgrenze.
- Nach dem Eintrag dieser Ursache: 50 Wiederholungen von `tst_aenderungen` im Klon, 48 von 50. Beide Fehlschläge sind derselbe Fall (949 ms, 1 ms unter der Grenze), alle anderen Prüfungen des Tests bestanden in allen Läufen. Der Fehlschlag tritt also wiederholt auf (3 von 51 Läufen im Klon) und ist ein Fehler der Grenze im Test, kein Fehler der Software. Die Rohausgaben liegen unter `build/pruefung-vor-install/wiederholen-aenderungen/` (nicht im Repo).
- Warum er bisher nicht auffiel, ist **nicht geprüft**: Frühere Berichte nennen keinen Fehlschlag von `schedulerMaxDelay`; 50 Wiederholungen dieses Tests gab es seit P08 laut den Berichten nicht.

**Schritt 5, «FEHLER: Sicherung weicht ab»:**

- Die Kopie der echten Daten ist schon im aktuellen Format 4 (`medien.json` da). Die Installation wandelt nichts um und legt keine Sicherung an; `ls` und `diff` finden `daten-sicherungen` nicht, das Skript meldet trotzdem «FEHLER: Sicherung weicht ab».
- Dass das Format der Kopie vorher aktuell war, folgt aus dem Lauf: `diff -rq` zwischen «vorher» und «daten» nach der Installation ergab keinen Unterschied, und `dss-core --format-pruefen` meldete für «daten» «Format aktuell», Exit 0. Ein eigener Aufruf von `--format-pruefen` vor der Installation fehlt, weil das Skript ihn nicht macht und es nicht geändert werden durfte.
- Bewertung nach Auftrag: **kein Fehler der Installation**, sondern ein Befund im Skript: Es vergleicht auch dann eine Sicherung, wenn keine nötig war. Skript nicht geändert.

**Nicht geprüft:**

- `scripts/schalter-lauf.sh` (nur auf Wunsch).
- 50 Wiederholungen anderer Tests als `tst_fensterbearbeiten` und `tst_aenderungen`.
- Alles auf dem Streaming-PC und gegen das echte OBS (für diese Stufe nicht nötig, im Handtest P10c abgenommen).
- Die echte Installation (Prüfung 11), sie macht nur der Nutzer.

## Handtest

Kein Handtest nötig. Die echte Installation (Prüfung 11 im Bericht P10c) macht der Nutzer ohnehin selbst.

**Bewertung für die Installation:** Installation, Daten, Start, Dienst und Fenstertest sind ohne Befund. Offen bleiben zwei Befunde in Tests und Prüfskripten (`tst_aenderungen`, `installieren-pruefen.sh`), die kein Verhalten der installierten Software betreffen. Ob vor Prüfung 11 zuerst die Grenze im Test angepasst wird, entscheidet Lotse bzw. der Nutzer.

## Nicht erledigt, `[OFFEN]`

- `[OFFEN]` `scripts/installieren-pruefen.sh` meldet «FEHLER: Sicherung weicht ab», wenn keine Sicherung nötig war (Daten schon im aktuellen Format). Vorschlag für einen späteren Auftrag: den Vergleich nur machen, wenn `daten-sicherungen` existiert, sonst «keine Sicherung nötig» melden, und `--format-pruefen` der Kopie vor der Installation ausgeben.
- `[OFFEN]` Grenze in `TestAenderungen::schedulerMaxDelay()` (950 ms) ohne Reserve für die Rundung auf ganze Millisekunden. Vorschlag: untere Grenzen um einige Millisekunden senken. Nicht geändert (kein Auftrag).
- Reste in `build/` (Git ignoriert sie, `rm` war nicht erlaubt): `build/vorinstall-klon` (Klon mit Bau), `build/pruefung-vor-install/` (Prüfsummen, `summen.py`, `start/`, `dienst/`, `inst/` mit Release-Bau und Installation unter `ziel/`, `wiederholen/`, `wiederholen-aenderungen/`). Lotse entfernt sie.

## Annahmen, Interna-Entscheide

| Entscheid | Begründung |
|---|---|
| Sitzung 1 (`seat0`) für `loginctl` | einzige Sitzung mit Sitz; `wiederholen.sh` wählt dieselbe |
| Prüfsummen mit eigenem Python-Skript statt `find … \| sort -z \| xargs -0 sha256sum` | Vorgabe von Lotse (keine Pipes); gleiche Dateiliste, nur reguläre Dateien |
| Schritte 3 bis 7 nach dem Fehlschlag in Schritt 2 ausgeführt | Sie wiederholen nichts; die Ursache war vor jedem erneuten Lauf von `tst_aenderungen` hier eingetragen |
| 50 Wiederholungen von `tst_aenderungen` im Klon | `CLAUDE.md`, Prüfstufen: 50 Wiederholungen für betroffene Tests nach einem Fehlschlag; im Klon, weil er dort scheiterte |
| Schritt 5 trotz Meldung «FEHLER» als bestanden gewertet | Vorgabe im Auftrag: nur Skriptfehler, wenn das Format der Kopie vorher nicht aktuell war; das war es (siehe oben) |

## Quellen

- Qt::TimerType, Genauigkeit von `Qt::CoarseTimer` (5 %): https://doc.qt.io/qt-6.8/qt.html#TimerType-enum
- QElapsedTimer: https://doc.qt.io/qt-6.8/qelapsedtimer.html
- Projekt: `CLAUDE.md` (Prüfstufen, Regeln 6, 7, 10, 18), `docs/archiv/berichte/P10c-bericht.md`, `docs/archiv/berichte/P08-bericht.md` (Grenzen in `tst_aenderungen`), `scripts/installieren-pruefen.sh`, `scripts/wiederholen.sh`.
