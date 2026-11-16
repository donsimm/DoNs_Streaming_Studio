---
topic: P14 e3, Logik und Fenster für «Befehle», «Zeitnachrichten», «Variablen» und den Dialog «Warteschlangen» im Reiter «Chat-Bot»
reference: src/app/actionspage.{h,cpp} («Aktionen» links) für die Seiten; src/app/settingsdialog.cpp für den Dialog
written: 2026-11-16 (Stand 24e36bb, Branch lotse/p14-e3-plan; setzt auf e2-15 und e2-16 auf)
---

# P14 e3: Plan

Grundlage: Checkliste `docs/archiv/prompts/P14-e3-checkliste.md` (Abschnitte 1–6) mit den Entscheiden des Nutzers in ihrem Abschnitt 7; Plan `docs/archiv/prompts/P14-chatbot.md` 2.6–2.9, 3.6, 5, 6. Die Checkliste ist eine Momentaufnahme vom 2026-11-16 (Stand 8795660). Gilt der Code etwas anderes, gilt der Code, und der Agent meldet die Abweichung. Das gilt besonders für die Wege aus e2-15 und e2-16 (`deletedStatusText`, `ForegroundDelegate`, `createProblemLine`, rechte Spalte), die beim Schreiben dieses Plans noch auf einem anderen Branch lagen.

Status eines Abschnitts: offen `[ ]`, erledigt `[x]`, `parked — <Grund>` bleibt offen mit Grund auf der Zeile.

## Für jeden Abschnitt

- Ein frischer Agent, **genau ein lokaler Commit** auf dem Bau-Branch von e3 (oder einem Worktree-Branch, wenn parallel), etwa 1–2 h. Kein Push.
- Lesen: `CLAUDE.md`, `.claude/HANDOFF.md`, diesen Abschnitt und die genannten Dateien gezielt (Grep, Ausschnitte).
- Prüfstufe «Klein»: `scripts/bauen.sh` (0 Warnungen), dann `scripts/testen.sh -R '<Tests des Abschnitts>'`. Fenstertests nie gleichzeitig; die X11-Sitzung muss aktiv und nicht gesperrt sein.
- Logik nur in `dss_common` (`src/common`), ohne Qt Widgets, mit Test ohne Fenster (Regel 9). Kein Layout-Wechsel in Ereignis-Behandlern.
- Rückmeldung an Lotse nach der Kurzvorlage in `CLAUDE.md`, mit allen neuen sichtbaren Texten im Wortlaut.

## Decided

Entscheide des Nutzers vom 2026-11-16 (Checkliste, Abschnitt 7), in seinen Worten zusammengefasst:

- «Gemeinsamer Aufbau «Tabelle links, Gewähltes rechts» für «Befehle» und «Zeitnachrichten» (`ListDetailPage`), Dialog `QueuesDialog` in `src/app/queuesdialog.{h,cpp}`; der Farb-Delegat ist schon geteilt (`ForegroundDelegate`, e2-15).» Gibt e3-4 und e3-8 frei.
- «Abstand der Zeitnachrichten im Feld in Minuten mit Komma («20 min», «7,5 min»), Umrechnung in der Logik, intern Sekunden.» Das überstimmt die Vorgabe der Checkliste bis zur Antwort (Sekunden mit `" s"`). Gibt e3-2 und e3-6 frei.
- «Anhalten», «Fortsetzen», «Leeren» bleiben bis P14-2 ausgegraut sichtbar.
- Dialog «Warteschlangen»: Jede Änderung gilt sofort, unten steht nur «Schliessen»; «Neu» legt «Warteschlange 1» an, ein Doppelklick benennt um.
- «Variablen»: «Für alle» und «Je Zuschauer» stehen nebeneinander mit Teiler wie «Live».
- Aus e2 gilt weiter: Prüfmeldung als Zeile `#fieldProblem` unter dem Abschnitt, Kontextmenü mit «…», ein Doppelklick benennt nur um, nach dem Löschen «Gelöscht: <Name>», zugängliche Namen für Felder ohne Beschriftung, `settingsdialog.cpp` bleibt unverändert.

## Schnittstelle

- **Keine neue Version.** Alle Nachrichten gibt es schon (Version 15, b3): `queue_put`, `queue_remove`, `get_variables` → `variables`, `get_viewers` → `viewers`, `variable_set`, `variable_remove`, `action_test`. Der Kern behandelt sie schon (`actionservice.cpp:525–569`, `actionedit.cpp:155–158`). Die nächste freie Nummer wäre **19** (`protocol.h:59`, `docs/08`).
- Der Kern meldet geänderte Variablen nicht von sich aus (Plan 5: «auf Bestellung»). Das Fenster holt sie neu (e3-3); mehr dazu unter «Annahmen».

## Abschnitte, Übersicht

| Name | Inhalt | Braucht | Schätzung |
|---|---|---|---|
| e3-0 | Lotse: Prüfung kalibrieren für «Bereich wie ein Dock» und «Dialog» (kein Commit) | – | 0,5 h |
| e3-1 | Logik der Warteschlangen und Rückfrage beim Löschen einer Variablen | – | 1,5 h |
| e3-2 | Logik der Befehle und Zeitnachrichten: `userGroupChoices`, «▶ Testen als …», Minuten | e3-1 (gleiche Datei) | 1,5 h |
| e3-3 | `CoreClient`: Variablen und Zuschauer holen und nachführen | – | 1,5 h |
| e3-4 | Fenster: `ListDetailPage`, Unterreiter als Gerüst, Theme | e2-16 | 2 h |
| e3-5 | Fenster: «Befehle» | e3-2, e3-3, e3-4 | 2 h |
| e3-6 | Fenster: «Zeitnachrichten» | e3-2, e3-5 | 1,5 h |
| e3-7 | Fenster: «Variablen» | e3-1, e3-3, e3-6 | 2 h |
| e3-8 | Fenster: Dialog «Warteschlangen» und «Warteschlangen verwalten …» | e3-1, e3-7 | 2 h |
| e3-9 | Lotse: Handtest e3, Plan schliessen | alle | – |

Summe Agenten etwa 14,5 h (etwa 3 Sitzungen).

**Parallel in getrennten Worktrees** (nie dieselben Dateien, nie zwei Fenstertests gleichzeitig):
- Spur Ansichtslogik: e3-1 → e3-2 (`actionview.{h,cpp}`, `tst_aktionsansicht`).
- Spur Client: e3-3 (`coreclient.{h,cpp}`) parallel zur Spur Ansichtslogik.
- e3-4 parallel zu beiden Logik-Spuren (nur `src/app`, `theme.cpp`), sobald e2-16 da ist; sein Fenstertest nicht gleichzeitig mit einem anderen.
- Fenster e3-5 bis e3-8 nacheinander: Alle verdrahten in `src/app/mainwindow.cpp`.
- Gemeinsam berührt: `src/app/CMakeLists.txt` bei neuen Dateien (e3-4 bis e3-8, nacheinander) und `tests/CMakeLists.txt` nur, falls ein Testprogramm neu ist (keines geplant).

## Aufgaben

- [x] **e3-0 Prüfung kalibrieren (Lotse, kein Commit).** Ergebnis 2026-11-16: beide bestanden. «Bereich wie ein Dock» (Kopie `actionspage.cpp`, Suchfeld ohne `setClearButtonEnabled`) gemeldet, dazu echter Nebenbefund `setUniformRowHeights` (behoben 07fbfee); «Dialog» (Kopie `settingsdialog.cpp`, Meldung ohne feste Höhe) gemeldet. Geprüft nur Code, nichts Gerendertes. Zwei Arten, je eine Pflanzung in einer Kopie ausserhalb des Arbeitsordners (ohne Git oder temporärer Worktree), je ein getrennter Agent mit `ui-consistency:verifying`, der Checkliste der Art und nur dem Hinweis «weicht irgendwo ab»:
  - «Bereich wie ein Dock»: Kopie von `src/app/actionspage.cpp`, darin zum Beispiel das Suchfeld ohne `setClearButtonEnabled(true)` oder die Leiste von Hand statt `buttonBar()`. Checkliste: Abschnitt 3, «Befehle».
  - «Dialog»: Kopie von `src/app/settingsdialog.cpp`, darin zum Beispiel die Meldung des Kerns in der Statuszeile statt im `QLabel` mit fester Höhe. Checkliste: Abschnitt 3, «Dialog «Warteschlangen»».
  - Wird die Abweichung nicht gemeldet: anhalten, die Prüfung ist dafür blind. Ergebnis hier vermerken (was geprüft, was nicht). Wer pflanzt, prüft nicht. Der kalibrierte Agent prüft danach keine Seite. Für jede Art behält Lotse einen Prüfer für alle Seiten dieser Art (Seiten: e3-4 bis e3-7; Dialog: e3-8).

### Logik

- [x] **e3-1 Warteschlangen und Rückfrage für Variablen** (`src/common/actionview.{h,cpp}`).
  - `struct QueueRow { QUuid id; QString name; bool blocking; bool isDefault; bool removable; bool blockingEditable; }`; `QList<QueueRow> queueRows(const actions::ActionsConfig &)`: zuerst «Standard» (null, nicht löschbar, ohne Kästchen), dann nach Name ohne Rücksicht auf Gross- und Kleinschreibung.
  - `EditResult newQueue(config)`: `queue_put` mit «Warteschlange 1», sonst der nächsten freien Nummer, nicht blockierend; `created` = neue Kennung.
  - `EditResult renameQueue(config, id, name)`: ohne Leerzeichen am Rand. Prüfmeldungen: leer «Der Name darf nicht leer sein.»; «Standard» und vorhandene Namen ohne Rücksicht auf Schreibweise «Die Warteschlange «<Name>» gibt es schon.» (Entscheid e2 zu Gruppennamen übertragen). Gleich wie vorher: nichts.
  - `EditResult setQueueBlocking(config, id, bool)` (`queue_put`); `EditResult removeQueue(config, id)` (`queue_remove`, «Standard» abgewiesen).
  - `QString removeQueueQuestion(config, id)` nach dem Muster von `removeActionQuestion`: «Warteschlange «Alerts» löschen?», mit Verweisen «Sie wird von 2 Aktionen benutzt. Diese laufen danach in «Standard».» (der Kern macht das schon so, `actionengine.cpp:113`).
  - `QString removeVariableQuestion(const VariableRow &)`: «Variable «firsts» löschen?» bzw. je Zuschauer «Variable «firsts» von «<Zuschauer>» löschen?» (`VariableRow::viewer`).
  - Der Laufzustand (`ActionsQueueStatus`: angehalten, wartend) wird in P14-1 nicht angezeigt (Plan 6: Liste «Name, nacheinander»); er bleibt für P14-2.
  - Tests: `tst_aktionsansicht` (Reihenfolge, «Standard», neuer Name mit Nummer, Umbenennen mit beiden Prüfmeldungen, Löschen, beide Rückfragen). `scripts/testen.sh -R '^tst_aktionsansicht$'`.
  - Handtest: H5, H4 (Rückfrage).

- [x] **e3-2 Befehle und Zeitnachrichten: Gruppen, «▶ Testen als …», Minuten** (`actionview.{h,cpp}`).
  - `struct UserGroupChoice { QUuid id; QString label; bool missing; }`; `QList<UserGroupChoice> userGroupChoices(config, const actions::Permissions &current)`: Benutzergruppen nach Name; eine Gruppe aus `current.groups`, die es nicht mehr gibt, als «Gruppe fehlt» (wie `queueChoices`), damit sie beim Speichern nicht verloren geht.
  - **«▶ Testen als …», Weg:** über das vorhandene `action_test` (keine neue Nachricht). Neu `EditResult testCommand(config, commandId, const TestUserChoice &user, bool real)`: je aktive Aktion mit einem aktiven Auslöser `command` dieses Befehls ein `action_test` (Reihenfolge der Liste wie `TriggerDispatcher::handleChat`). Argumente wie im echten Auslöser (`triggerdispatcher.cpp:198–205`): `command` = erster Eintrag aus `commands`, `commandId`, `commandName`, `rawInput` leer, keine `input0…`; dazu `user`, `userName`, `userId`. Ohne Rechte und Abklingzeit, der Zähler zählt nicht (Testwerte, Plan 3.6). Ohne passende Aktion die Prüfmeldung «Keine aktive Aktion führt diesen Befehl aus.».
  - `bool commandTestAllowed(config, commandId, const ActionsStatus &)` wie `testAllowed`; `realTestAllowed` gilt entsprechend (stumm: nur Probelauf). Die Form im Fenster hängt an Frage 1.
  - **Minuten:** `QString minutesText(int seconds)` («20», «7,5», höchstens eine Nachkommastelle, Komma); `std::optional<int> secondsFromMinutes(const QString &)` (Komma oder Punkt, auf ganze Sekunden gerundet, sonst leer). Für das Feld den Weg der Sekundenfelder aus e2-16 übernehmen (`FieldRow` mit Einheit oder deren Nachfolger, der Code gewinnt): Einheit «min», kleinster Wert 1 (= 60 s, Grenze von `putTimer`). `TimerRow::interval` nutzt dieselbe Schreibweise («7,5 min», «5 bis 7,5 min, zufällig»).
  - Tests: `tst_aktionsansicht` (Gruppen mit fehlender Gruppe; `testCommand` mit 0, 1 und 2 Aktionen, abgeschaltetem Auslöser, Argumenten, dry_run; Minuten hin und zurück mit 60, 450, 1200 s, «7,5», «7.5», «abc», «0,5»). `scripts/testen.sh -R '^tst_aktionsansicht$'`.
  - Handtest: H2, H3.

- [x] **e3-3 `CoreClient`: Variablen und Zuschauer** (`src/common/coreclient.{h,cpp}`).
  - `void requestVariables()`: schickt `get_variables`. Läuft schon eine Anfrage, wird nach deren Antwort genau eine weitere geschickt (Zusammenfassen, keine Flut bei vielen Läufen).
  - Antwort `variables` → `std::optional<VariablesInfo> variables() const`, Signal `variablesChanged()`. Danach `get_viewers` mit den noch unbekannten Twitch-IDs aus `persisted.users` und `volatileUsers`; Antwort `viewers` in einen Speicher (`actions::ViewersData viewers() const`), Signal `viewersChanged()`.
  - Nachführen: Nach einem erfolgreichen `variable_set` oder `variable_remove` aus diesem Fenster (Antwort `change_result`) holt der Client von sich aus neu. Mehr nicht im Client; wann das Fenster sonst holt, steht in e3-5 und e3-7.
  - Bei Trennung leeren wie `m_actionsConfig` (`clearData`).
  - Tests: `tst_kernaktionen` mit `CoreClient` gegen den Kern als eigenen Prozess (Muster `tst_laufzustand`): holen, Zuschauer nachholen, Zusammenfassen von zwei schnellen Anfragen, nach `variable_set` neu, leer nach Trennung. Daten aus `tests/daten/` (Regel 7). `scripts/testen.sh -R '^tst_kernaktionen$'`.
  - Handtest: keiner allein (über H2, H4).

### Fenster

Jede Fenster-Aufgabe trägt ihre Checkliste: eine Momentaufnahme vom 2026-11-16 (Stand 8795660) mit Referenz `src/app/actionspage.{h,cpp}`. Die Art heisst «Bereich wie ein Dock» (9 von 12 Kandidaten). Für Tabellen zählt «Läufe der Aktionen» (`chatbotpage.cpp:280–322`) mit. Für die rechte Spalte gilt der Weg von «Aktionen» rechts aus e2-16. Ablauf jeder Fenster-Aufgabe:
1. Checkliste vor dem Schreiben neu lesen, nicht aus dem Gedächtnis arbeiten.
2. Seite als prüfbereit melden. Lotse schickt sie an den Prüfer dieser Art (ein Agent mit `ui-consistency:verifying`, ab der ersten Seite für alle Seiten derselbe, nie der Schreibende oder der Kalibrierte) und gibt dessen Befund zurück. Der Agent behebt ihn im selben Commit (`--amend`, nur lokal).
3. Status an Lotse: `done` nach bestandener Prüfung oder `parked — <Grund>`.

- [x] **e3-4 `ListDetailPage`, Unterreiter als Gerüst, Theme.** Schreibt neu `src/app/listdetailpage.{h,cpp}`, `chatbotpage.{h,cpp}` (drei Unterreiter), `theme.cpp`, `src/app/CMakeLists.txt`.
  - `ListDetailPage : QWidget`: `QSplitter` waagrecht (`setChildrenCollapsible(false)`, Dehnung links 2, rechts 1). Links `dockPanel(Titel)` mit optionaler Zeile Suche/Filter, Tabelle und Leiste. Rechts `dockPanel(Titel)` mit `QScrollArea` (`NoFrame`, ohne waagrechte Leiste), darin `QStackedWidget` mit «leer» (`QLabel#hintLabel`) und dem Formular, das die Seite einsetzt. API ohne Logik: `setRows(…)` (Zellen, Kästchen, abgeschaltet, Tooltip, Kennung), `selectRow(id)`, `selectedRow()`, `setListButtons(add, copyRemove)`, `setHint(text)`, `setDetailTitle(text)`, `setDetailForm(QWidget*)`, `showDetail(bool)`; Signale `searchChanged`, `filterChanged`, `selectionChanged(QUuid)`, `enabledToggled(QUuid,bool)`, `addClicked`, `copyClicked`, `removeClicked`.
  - Unterreiter «Befehle», «Zeitnachrichten», «Variablen» nach «Aktionen» über `addSubTab()` (Reihenfolge Plan 6). Inhalt zunächst: zwei `ListDetailPage` ohne Zeilen und für «Variablen» zwei leere `dockPanel` im `QSplitter`.
  - Theme: die neuen Objektnamen der Tabellen (`#commandsTable`, `#timersTable`, `#globalVariables`, `#userVariables`, `#queuesList`) in die Selektorliste `#mediaTree::item, … #chatBotChat::item` (`theme.cpp:344–349`).
  - Checkliste:
    - Bereiche nur über `dockPanel()`; Titel ändern nur den Text (wie `setAreaTitle`); fest, ohne Knöpfe zum Lösen und Schliessen.
    - Suchfeld `QLineEdit`, `setPlaceholderText("Suchen …")`, `setClearButtonEnabled(true)`, `setAccessibleName`. Filter `QComboBox` auf derselben Zeile. Ein neuer Suchtext füllt nur die Tabelle neu.
    - Tabelle `QTreeWidget` (kein `QTableWidget`): `setRootIsDecorated(false)`, `setUniformRowHeights(true)`, `SingleSelection`, Kopf über `QHeaderView::section`, Breiten `digit * n`, letzte Spalte gedehnt, `NoEditTriggers`.
    - Spalte 0 als Kästchen (`Qt::ItemIsUserCheckable`), beim Füllen geschützt (`m_filling`). Abgeschaltete Zeilen ganz in `textDisabled` über `ForegroundDelegate` aus `panels.h` (Theme-Wert hereingereicht, nie `QColor` in der Seite).
    - Leiste `buttonBar({hinzufügen, kopieren, löschen}, {Hinweis})`, `symbolButton` mit `hinzufuegen.svg`, `kopieren.svg`, `entfernen.svg`, Tooltip und `setAccessibleName`, ausgegraut bis «verbunden und geladen» bzw. bis zur Auswahl. Rechts `QLabel#hintLabel`.
    - Ein Auswahlwechsel füllt nur Werte und schaltet die Seite des `QStackedWidget` um. Felder entstehen nie neu (Regel 9).
  - Nicht von der Referenz übernehmen: den Baum mit Gruppen (`setRootIsDecorated(true)`, Gruppenzeilen, Kontextmenü), «Neue Gruppe» und «Zurücksetzen»; Werte aus `_e3/studio.css` und dem Bild.
  - Nicht in diesem Teil: die Zeilen der Checkliste zu Feldern, «Ausführen», Speichern und Löschen (gehören zu e3-5 bis e3-7).
  - Tests: `tst_fensterchatbot` (fünf Unterreiter in der Reihenfolge; `ListDetailPage`: Spalten, Kästchen sendet `enabledToggled`, nicht beim Füllen; Auswahl bleibt beim Neufüllen; Wechsel ohne neue Widgets). `scripts/testen.sh -R '^tst_fensterchatbot$'`.
  - Handtest: H1.

- [x] **e3-5 «Befehle».** Schreibt neu `src/app/commandspage.{h,cpp}` (Formular rechts, setzt `ListDetailPage` ein), `chatbotpage.cpp`, `mainwindow.cpp` (Zeilen, Bearbeitungen, Rückfragen, Testen, «Öffnen», Variablen holen beim Zeigen des Unterreiters und nach `actions.run_finished`, solange er sichtbar ist).
  - Checkliste:
    - Links «Befehle», rechts `commandTitle()`; ohne Auswahl «Kein Befehl gewählt».
    - Suchfeld wie oben; Filter `QComboBox` aus `commandGroupChoices()`/`commandGroupLabel()` («Alle Gruppen»).
    - Tabelle `#commandsTable`, 8 Spalten aus `CommandRow`: «Aktiv» (Kästchen → `putCommand` mit `enabled`), «Auslöser», «Gruppe», «Wer darf», «Abkl. Kanal», «Abkl. Person», «Aktion», «Benutzt» (aus `client->variables()`, ohne Daten wie `commandRows(…, nullptr)`); abgeschaltet grau; Tooltip aus `warning`.
    - Leiste: Tooltips «Befehl hinzufügen», «Befehl kopieren», «Befehl löschen»; rechts `commandListHint()`.
    - Rechts je Abschnitt `section()` mit Raster Beschriftung | Feld: «Befehl» (Name `QLineEdit`, Gruppe `QComboBox` editierbar, Auslöser `QPlainTextEdit` mit Hinweis «ein Auslöser pro Zeile» über `commandsText`/`commandsFromText`, Ort aus `commandLocations()`, Aktiv `QCheckBox`); «Wer darf» (Kästchen «Alle», «Abonnenten», «VIPs», «Moderatoren» und je Gruppe aus `userGroupChoices()`, Hinweis «Mit «Alle» sind die anderen eingeschlossen. Der Kanal darf immer.», «Nachrichten des Bots ignorieren»; Werte über `permissionChoice`/`permissionsFromChoice`); «Abklingzeit» (Kanal, Je Person: `QSpinBox`, Suffix `" s"`, `keepValueVisible`, `setKeyboardTracking(false)`, Hinweis «0 s = keine Abklingzeit. Abgewiesene Aufrufe stehen in «Läufe der Aktionen».»); «Ausführen» (Aktion `QComboBox` aus `actionChoices()` über `setCommandAction()`, `QPushButton` «Öffnen» springt nach «Aktionen» und wählt die Aktion wie `chatBotRunOpenAction`, Hinweis «Die Aktion bekommt %rawInput%, %input0% …, %user%.»).
    - Die Kästchen der Gruppen entstehen beim Laden von `actions_config`, nicht beim Wechsel der Auswahl. Eine geänderte Gruppenliste füllt die Kästchen beim nächsten `actionsConfigChanged` neu.
    - Prüfmeldung je Abschnitt `#fieldProblem` über `createProblemLine` (e2-15), immer da; danach zeigt das Feld wieder den Wert des Kerns.
    - Textfelder speichern beim Verlassen oder mit Enter, Kästchen und Auswahl sofort.
    - Löschen nur mit `confirm()` und `removeCommandQuestion()`; Statuszeile `showStatus()` mit `savedStatusText`/`deletedStatusText`.
    - Leiste rechts unten: «▶ Testen als …» nach Frage 1 (Freigabe aus `commandTestAllowed`, Anfragen aus `testCommand`, Statuszeile `testStartedStatusText`).
  - Nicht übernehmen: Werte aus dem Entwurf (GROSSBUCHSTABEN, Farben, Abstände), den Text «Action», die Abschnitte von Hand aus `settingsdialog.cpp`, die festen Spaltenbreiten der «Läufe».
  - Nicht in diesem Teil: «verbieten», einzelne Zuschauer, Dialog «Benutzergruppen» (P14-2).
  - Tests: `tst_fensterchatbot` (Zeilen, Filter, Kästchen, Felder ohne Neuaufbau, Prüfmeldung unter dem Abschnitt, Löschen mit Rückfrage, «Öffnen», Testen gegen den Kern im Probelauf). `scripts/testen.sh -R '^tst_fensterchatbot$'`.
  - Handtest: H2.

- [x] **e3-6 «Zeitnachrichten».** Schreibt neu `src/app/timerspage.{h,cpp}`, `chatbotpage.cpp`, `mainwindow.cpp`.
  - Checkliste:
    - Links «Zeitnachrichten», rechts `timerTitle()`; ohne Auswahl «Keine Zeitnachricht gewählt».
    - Kein Suchfeld und kein Filter (Plan 6).
    - Tabelle `#timersTable`, Spalten «Aktiv» (Kästchen → `putTimer`), «Name», «Abstand», «Chat-Zeilen», «Nur im Stream», «Aktion» aus `TimerRow`; abgeschaltet grau; Tooltip aus `warning`.
    - Leiste: Tooltips «Zeitnachricht hinzufügen», «Zeitnachricht kopieren», «Zeitnachricht löschen»; kein Hinweis rechts.
    - Rechts: «Zeitnachricht» (Name, Aktiv, Wiederholen); «Abstand» (Abstand in Minuten mit Komma und Einheit «min» nach e3-2; «Zufällig» `QCheckBox` mit «von»/«bis» in Minuten, bei aus nur ausgegraut, nie ausgeblendet; «Chat-Zeilen» `QSpinBox`; «Nur im Stream»); «Ausführen» (Aktion über `setTimerAction`, «Öffnen»).
    - Prüfmeldung `#fieldProblem` je Abschnitt aus `putTimer` (mindestens 60 s, von ≤ bis), danach Wert des Kerns.
    - Löschen mit `confirm()` und `removeTimerQuestion()`; «Gespeichert: …»/«Gelöscht: …».
  - Nicht übernehmen: wie bei «Befehle»; keine Umrechnung im Fenster.
  - Tests: `tst_fensterchatbot` (Zeilen, Minutenfeld «7,5» → 450 s im `timer_put`, «Zufällig» schaltet nur die Freigabe, Prüfmeldung). `scripts/testen.sh -R '^tst_fensterchatbot$'`.
  - Handtest: H3.

- [x] **e3-7 «Variablen».** Schreibt neu `src/app/variablespage.{h,cpp}`, `chatbotpage.cpp`, `mainwindow.cpp` (holen beim Zeigen des Unterreiters und nach `actions.run_finished`, solange er sichtbar ist).
  - Checkliste:
    - Zwei `dockPanel` «Für alle» und «Je Zuschauer» nebeneinander im waagrechten `QSplitter` (Entscheid Frage 5), fest.
    - Je ein Suchfeld wie oben; «Je Zuschauer» dazu der Filter «Alle Variablen» aus `userVariableNames()`/`variableNameLabel()` auf derselben Zeile.
    - Tabellen `#globalVariables` («Name», «Wert», «Dauerhaft», «Herkunft») und `#userVariables` («Zuschauer», «Variable», «Wert», «Herkunft») aus `globalVariableRows`/`userVariableRows`; Texte nur aus `VariableRow`, kein Zuschauername im Fenster zusammengesetzt.
    - Wert ändern: `Qt::ItemIsEditable` nur in «Wert», `DoubleClicked | EditKeyPressed`, Text an `setVariableValue()`. Bei einer Prüfmeldung zeigt die Zelle wieder den Wert des Kerns.
    - Prüfmeldung `#fieldProblem` unter jeder Tabelle, immer da.
    - Leiste je Bereich `buttonBar` mit `symbolButton` «Variable löschen» (`entfernen.svg`), frei nur bei einer Auswahl; `confirm()` mit `removeVariableQuestion()`, dann `removeVariable()`.
    - Übernommene Werte ohne Funktion stehen ohne eigene Farbe da.
  - Nicht übernehmen: `NoEditTriggers` der übrigen Tabellen (hier gewollt anders, nur in «Wert»); Werte des Entwurfs.
  - Nicht in diesem Teil: Bereich «Zitate» (P14-2).
  - Tests: `tst_fensterchatbot` (Zeilen aus `tests/daten/`, Filter, Zelle ändern → `variable_set`, Zahl mit Text → Prüfmeldung und alter Wert, Löschen mit Rückfrage, nachgeführt nach einem Lauf). `scripts/testen.sh -R '^tst_fensterchatbot$'`.
  - Handtest: H4, L2.

- [x] **e3-8 Dialog «Warteschlangen».** Schreibt neu `src/app/queuesdialog.{h,cpp}`, `actionspage.cpp` (Eintrag «Warteschlangen verwalten …» wirkt; danach steht die Auswahl wieder auf der vorherigen Warteschlange), `mainwindow.cpp`. Eigene Checkliste der Art «Dialog», Referenz `src/app/settingsdialog.cpp`, 3 von 3 Kandidaten; Prüfer der Art «Dialog» aus e3-0.
  - Checkliste:
    - Eigene Klasse `QueuesDialog : public QDialog` wie `SettingsDialog`, Titel «Warteschlangen», `QVBoxLayout`; modal wie `SettingsDialog` (der Agent übernimmt dessen Art zu öffnen).
    - Liste `#queuesList` als `QTreeWidget` wie die Tabellen oben, Spalten «Name», «Nacheinander»; Kästchen in «Nacheinander» → `setQueueBlocking`; «Standard» ohne Kästchen. Zeilen aus `queueRows()`.
    - Doppelklick auf den Namen benennt um über denselben Weg wie «Gruppe umbenennen» in «Aktionen» (`renameQueue`), `setExpandsOnDoubleClick(false)`.
    - Leiste `buttonBar`: `symbolButton` «Warteschlange hinzufügen» (`newQueue`, neue Zeile gewählt), «Warteschlange löschen» (frei nur bei `removable`) │ rechts `QPushButton` «Anhalten», «Fortsetzen», «Leeren», fest ausgegraut bis P14-2.
    - Meldung des Kerns in einem `QLabel` unter der Liste, feste Höhe 3 Zeilen (wie `settingsdialog.cpp:146–153`), nicht in der Statuszeile.
    - `QDialogButtonBox::Close`, nur «Schliessen»; jede Änderung gilt sofort.
    - Löschen mit `confirm()` und `removeQueueQuestion()`.
    - Neue `actions_config` füllt nur die Liste neu, die Auswahl bleibt (`keptSelection`).
  - Nicht übernehmen: `QFormLayout` und die Abschnitte von Hand aus `settingsdialog`; «OK/Abbrechen».
  - Tests: `tst_fensterchatbot` (Öffnen aus «Aktionen», Neu → «Warteschlange 1», Umbenennen mit Prüfmeldung im Dialog, Kästchen, Löschen mit Rückfrage, «Standard» nicht löschbar, Steuerknöpfe ausgegraut, Auswahl in «Aktionen» danach wie vorher). `scripts/testen.sh -R '^tst_fensterchatbot$'`.
  - Handtest: H5.

- [ ] **e3-9 Handtest e3 und Plan schliessen (Lotse).** Nach dem Handtest gehen die Entscheide unter «Decided» mit dem Ergebnis an den Nutzer, offene Punkte nach `docs/04-offene-punkte.md`.

## Handtest e3 (Regel 10)

Kern und Fenster aus `build/` mit eigenem `XDG_CONFIG_HOME`, Datenordner der Version in Arbeit, nie während eines Streams. Was die Tests prüfen (Zeilen, Umrechnung, Prüfmeldungen, Anfragen), wiederholt der Nutzer nicht.

**Prüft Lotse**

**L1 Bildschirmfotos.** «Befehle» mit gewähltem Befehl, «Zeitnachrichten» mit gewählter Zeitnachricht, «Variablen» mit Werten, Dialog «Warteschlangen». Vergleich mit `_e3/3-befehle.png` (nur Aufbau, nicht die Werte des Entwurfs), Ablage nur im Scratchpad. Warum: Aussehen und Abstände gegen den Entwurf.

**L2 Echte Daten und Nachführen.** Zuschauer-Daten aus Streamer.bot (ohne `auth*.db`) in den Datenordner der Version in Arbeit kopieren und importieren (Recht aus `CLAUDE.md`, P13/P14). Einen Testbefehl `!dsstest` mit dauerhaftem Zähler anlegen und als Kanal «DSS-Test» schreiben. Erwartet: «Benutzt» steigt in «Befehle», ohne den Unterreiter neu zu öffnen. Testbefehl und Testaktion danach löschen. Warum: echter Weg über Twitch und Kern.

**Prüft der Nutzer**

**H1 Unterreiter.** Reihenfolge «Live», «Aktionen», «Befehle», «Zeitnachrichten», «Variablen»; Teiler in allen drei neuen Unterreitern ziehen. Warum: Bedienung.

**H2 «Befehle».**
- Suchen («Suchen …»), Filter «Alle Gruppen», Kästchen «Aktiv»; abgeschaltete Zeile grau.
- Spalten «Aktiv», «Auslöser», «Gruppe», «Wer darf», «Abkl. Kanal», «Abkl. Person», «Aktion», «Benutzt»; Hinweis rechts unten aus `commandListHint`.
- Rechts die Abschnitte «Befehl», «Wer darf», «Abklingzeit», «Ausführen»; Beschriftungen «Name», «Gruppe», «Auslöser», «Ort», «Aktiv», «Kanal», «Je Person», «Aktion»; Kästchen «Alle», «Abonnenten», «VIPs», «Moderatoren», eigene Gruppen, «Nachrichten des Bots ignorieren».
- Hinweise lesen: «ein Auslöser pro Zeile»; «Mit «Alle» sind die anderen eingeschlossen. Der Kanal darf immer.»; «0 s = keine Abklingzeit. Abgewiesene Aufrufe stehen in «Läufe der Aktionen».»; «Die Aktion bekommt %rawInput%, %input0% …, %user%.».
- «Öffnen» springt zur Aktion; «▶ Testen als …» (nach Frage 1) startet einen Probelauf, «Läufe der Aktionen» zeigt ihn; ohne Aktion: «Keine aktive Aktion führt diesen Befehl aus.».
- Leeren Namen eingeben → Prüfmeldung unter «Befehl». Befehl löschen → Rückfrage «Befehl «!so» löschen? Er wird von 1 Aktion benutzt.», danach «Gelöscht: <Name>».
- Leer: «Kein Befehl gewählt»; Tooltips «Befehl hinzufügen», «Befehl kopieren», «Befehl löschen».
- Warum: Bedienung, Wortlaut.

**H3 «Zeitnachrichten».** Spalten «Aktiv», «Name», «Abstand», «Chat-Zeilen», «Nur im Stream», «Aktion»; rechts «Zeitnachricht», «Abstand», «Ausführen» mit «Wiederholen», «Zufällig», «von», «bis», «Chat-Zeilen», «Nur im Stream». Abstand «7,5» eingeben → Tabelle «7,5 min»; «0,5» → Prüfmeldung; «Zufällig» an und aus (Felder nur ausgegraut). Rückfrage «Zeitnachricht «Info» löschen? Sie wird von 1 Aktion benutzt.». Leer: «Keine Zeitnachricht gewählt»; Tooltips «Zeitnachricht hinzufügen/kopieren/löschen». Warum: Bedienung, Einheit Minuten.

**H4 «Variablen».** Nach L2 einige bekannte Werte ansehen (eigene First-Chatter-Zähler, zwei bekannte Zuschauer). «Für alle» und «Je Zuschauer» nebeneinander; Spalten «Name», «Wert», «Dauerhaft», «Herkunft», «Zuschauer», «Variable»; Filter «Alle Variablen». Einen Wert per Doppelklick ändern; Text in einer Zahl → Prüfmeldung unter der Tabelle. Löschen mit Rückfrage «Variable «<Name>» löschen?» bzw. «Variable «<Name>» von «<Zuschauer>» löschen?». Tooltip «Variable löschen». Warum: echte Daten, Bedienung, Wortlaut.

**H5 Dialog «Warteschlangen».** In «Aktionen» bei «Warteschlange» den Eintrag «Warteschlangen verwalten …» wählen. Titel «Warteschlangen», Spalten «Name», «Nacheinander»; «Warteschlange hinzufügen» → «Warteschlange 1», per Doppelklick umbenennen, auf einen vorhandenen Namen → «Die Warteschlange «<Name>» gibt es schon.»; Kästchen «Nacheinander»; «Standard» ohne Kästchen und nicht löschbar; «Anhalten», «Fortsetzen», «Leeren» ausgegraut; Löschen mit Rückfrage «Warteschlange «<Name>» löschen? Sie wird von <n> Aktionen benutzt. Diese laufen danach in «Standard».»; «Schliessen». Danach steht die Auswahl in «Aktionen» wie vorher. Tooltips «Warteschlange hinzufügen», «Warteschlange löschen». Warum: Bedienung, Wortlaut.

## Fragen an den Nutzer (gebündelt, je mit Empfehlung)

1. **«▶ Testen als …» bei einem Befehl:** Der Entwurf zeigt nur den Knopf. **Empfehlung:** Knopf mit Menü (wie «+» bei den Auslösern) mit denselben Einträgen wie «Testwerte: Benutzer» («Testzuschauer», «eigener Kanal»); immer als Probelauf, «echt ausführen» nur in «Aktionen». Ohne Eingabe nach dem Befehl (`%rawInput%` leer); eigene Eingaben kommen mit P14-2 («alle Testwerte»). Die andere Möglichkeit wäre eine Zeile wie in «Aktionen» mit Auswahl, «▶ Testen» und «echt ausführen». Betrifft e3-2 (nur `real`) und e3-5; ohne Antwort baut e3-5 die Empfehlung.

## `[OFFEN]`

- Checkliste Abschnitt 1 nennt beim Dialog «Zeilen (Name, nacheinander, Zustand)», Baum und Plan 6 nur «Name, nacheinander». Dieser Plan zeigt den Zustand nicht (e3-1); er steht schon in «Läufe der Aktionen».
- Checkliste Abschnitt 3 und Frage 1 nennen `ResultColorDelegate` in `chatbotpage.cpp`; nach e2-15 heisst er `ForegroundDelegate` in `panels.{h,cpp}` (Abschnitt 7).
- Checkliste «Zeitnachrichten», «Einheit der Abstände» (bis zur Antwort Sekunden) ist durch Frage 2 überholt: Minuten mit Komma.
- Checkliste «Variablen»: Fragetext beim Löschen fehlte. Wortlaut neu in e3-1, der Nutzer liest ihn in H4. Gleich beim Dialog (H5).
- Checkliste «Dialog», «Öffnen»: ob modal, nicht geprüft. e3-8 übernimmt die Art von `SettingsDialog`.
- Der Kern meldet geänderte Variablen nicht (kein Ereignis, keine Nachricht an alle). Das Fenster holt sie bei Bedarf neu (e3-3, e3-5, e3-7). Reicht das in L2 nicht, wäre eine Nachricht des Kerns nötig (Version 19) als eigener Punkt.
- «▶ Testen als …» prüft keine Rechte und Abklingzeiten und zählt nicht (Testwerte). Soll ein Test diese einschliessen, wäre eine neue Anfrage im Kern nötig (Version 19), etwa für P14-2.

## Annahmen, Interna-Entscheide

- «▶ Testen als …» über `action_test` statt einer neuen Nachricht: Der Kern hat den Weg schon, die Argumente sind dieselben wie beim echten Befehl; `test_event` gilt nur mit `--testereignisse`.
- Variablen nachführen: nach eigenem `variable_set`/`variable_remove` im Client, sonst durch das Fenster beim Zeigen von «Befehle» oder «Variablen» und nach `actions.run_finished`, solange einer davon sichtbar ist. Der Client fasst Anfragen zusammen. Kein Abruf im Takt.
- Zuschauernamen nur für die IDs aus den Variablen (Plan 5: «nur die angezeigten»), zwischengespeichert bis zur Trennung.
- Namen der Warteschlangen ohne Rücksicht auf Schreibweise eindeutig, «Standard» vergeben: Entscheid zu Gruppennamen (e2) übertragen.
- Eine fehlende Benutzergruppe bleibt als «Gruppe fehlt» in «Wer darf», damit Speichern sie nicht still entfernt (wie `queueChoices`).
- Unterreiter als Gerüst in e3-4: Die Reihenfolge der fünf Unterreiter bleibt fest, auch wenn die Seiten nacheinander kommen.
- Prüfbefunde werden im selben Commit behoben (`--amend`, lokal), damit je Abschnitt genau ein Commit bleibt.
- Abweichung vom Skill `ui-consistency:planning`: Der Plan liegt auf Auftrag von Lotse im Repo (`docs/archiv/prompts/`), nicht ausserhalb. Ende 3 des Skills: Die Aufgaben gehören in den Ablauf des Projekts (Plan-Dateien mit Abschnitten für Agenten).

## Quellen

- Checkliste `docs/archiv/prompts/P14-e3-checkliste.md`; Nachträge `_e3/nachtraege-e2.md` (Arbeitsdatei, nicht im Repo); Vorbild `docs/archiv/prompts/P14-e2-plan.md`.
- Plan `docs/archiv/prompts/P14-chatbot.md` 2.6–2.9, 3.6, 5, 6.
- Code: `src/common/actionprotocol.h:28–181`, `protocol.h:59`, `actionview.h:30–65`, `:150–203`, `:336–427`, `coreclient.h:118–137`, `src/actions/actionmodel.h:111–165`, `actionedit.cpp:118–175`, `actionengine.cpp:113`, `actionrefs.cpp:66`, `triggerdispatcher.cpp:158–229`, `src/core/actionservice.cpp:506–699`, `:816–843`, `src/app/panels.h`, `theme.cpp:344–349`, `chatbotpage.cpp:247`, `:370–374`, `mainwindow.cpp:559–786`.
- `docs/08-kern-schnittstelle.md`, «Version 18».
