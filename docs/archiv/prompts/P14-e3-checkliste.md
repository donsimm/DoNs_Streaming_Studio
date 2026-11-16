# Rückmeldung an Lotse: Checkliste P14 e3 («Befehle», «Zeitnachrichten», «Variablen», Dialog «Warteschlangen»)

Eingeordnet als **drei neue Seiten und ein Dialog**, also die ganze Phase nach `ui-consistency:finding-patterns`. Die Übergabe `.claude/HANDOFF.md` habe ich gelesen und berücksichtigt; zu P14 steht dort nichts. Ich habe nur gelesen: keine Datei geschrieben, nichts gebaut, keine Tests, keine Commits.

**Referenz für die Seiten: `src/app/actionspage.{h,cpp}`**, also «Aktionen» links.
- Sie ist der neueste Unterreiter im Reiter «Chat-Bot» (e2-11 bis e2-14) und hat schon die Form, die die neuen Seiten brauchen: Bereiche mit `dockPanel` im `QSplitter`, links eine Liste mit Suchfeld und `buttonBar`, rechts das Gewählte.
- Für die Tabellen zählt «Läufe der Aktionen» mit (`chatbotpage.cpp:280–322`): Das ist die neueste Tabelle mit Spaltenkopf.
- Für die rechte Spalte zählt das Dock «Eigenschaften» mit, denn der Plan sagt dazu «wie das Dock «Eigenschaften»».

**Referenz für den Dialog: `settingsdialog.cpp`.** Er ist der einzige Dialog, der Änderungen an den Kern schickt und dessen Antworten zeigt.

**Art und Familie:** «Bereich wie ein Dock». Das ist der Name des Projekts selbst (Kommentar in `panels.h:38`, Theme `#chatBotPanelTitle`). Gezählt im Fenster `src/app` zusammen mit `dss_common`.
- 12 Kandidaten: 5 Docks, die 3 Bereiche von «Live», 1 Bereich von «Aktionen» (Mitte und rechts sind noch nicht gebaut) und 3 Dialoge.
- **9 davon gehören zur Familie.** Die 3 Dialoge bilden eine eigene Familie für den Dialog «Warteschlangen».

---

## 1. Blockierend: fehlende Logik (kein Auftrag für mich)

| Fehlt | Suche und Fundstelle |
|---|---|
| **Dialog «Warteschlangen»:** Zeilen (Name, nacheinander, Zustand), Neu, Löschen, Umbenennen, Rückfrage beim Löschen, Steuern | `kQueuePut`, `kQueueRemove` und `kQueueControl` gibt es nur im Protokoll (`actionprotocol.h:49–51`). In `actionview.cpp` gibt es 0 Treffer für `queue_put`, `kQueue` oder `UserGroup`. Vorhanden ist nur `queueChoices` (`actionview.h:160`). Den Laufzustand gibt es in `ActionsQueueStatus` (`actionprotocol.h:118–128`), eine Zeilenlogik dafür fehlt. |
| **«Variablen» und Spalte «Benutzt» in «Befehle»:** Variablen und Zuschauer holen und nachführen | Die Logik ist da: `globalVariableRows`, `userVariableRows`, `setVariableValue`, `removeVariable`, `commandRows(…, variables)`. Aber `kGetVariables`, `kVariables`, `kViewers`, `parseVariablesMessage` und `parseViewersMessage` stehen nur in `actionprotocol.*`, `actionservice.cpp`, `cli/main.cpp` und `actions/viewer*`. Weder in `coreclient.*` noch irgendwo in `src/app` gibt es einen Treffer, also kein Abruf und kein Signal. |
| **«▶ Testen als …» bei einem Befehl** | Es gibt nur `action_test` für Aktionen (`actionprotocol.h:59`, `testAction` in `actionview.h:201`). `test_event` gilt nur mit `--testereignisse` (`actionprotocol.h:72`). Eine Funktion `testCommand` oder etwas Ähnliches findet die Suche nicht: Unklar ist, mit welchen Argumenten (`%rawInput%` …) und über welchen Weg getestet wird. |
| **«Wer darf» mit eigenen Gruppen:** Auswahlliste der Benutzergruppen nach Name | `PermissionChoice::groups` gibt es (`actionview.h:344`), `permissionsText` liest `config.group(id)` (`actionview.cpp:1273`). Eine Funktion wie `userGroupChoices` fehlt. |
| **«Gelöscht: <Name>»** (Entscheid 16.11.) | Nur `savedStatusText` gibt es (`actionview.h:63`). `deletedStatusText` hat 0 Treffer; die Funktion ist für e2-15 vorgesehen. e3 hängt davon ab. |
| **Abstand der Zeitnachrichten im Feld** (Einheit und Umrechnung) | `TimerRow::interval` liefert «20 min» (`actionview.h:371`), das Modell speichert `intervalS` (`actionmodel.h:117`). Eine Umrechnung für das Feld fehlt (wie `FieldRow` mit «s» aus e2). Hängt an Frage 2. |

Nicht blockierend, schon da: Zeilen, Titel, Rückfragen, Neu, Kopieren, Löschen und Speichern für Befehle und Zeitnachrichten; `actionChoices`, `commandAction`/`timerAction`, `setCommandAction`/`setTimerAction`, `commandLocations`, `permissionChoice`/`permissionsFromChoice`, `commandsText`/`commandsFromText`, `commandGroupChoices`, `commandListHint`, `userVariableNames`, `variableNameLabel`.

---

## 2. Bäume der Rollen

### Unterreiter «Befehle» (nach `_e3/3-befehle.png` und Plan 6)
```
Seite                        QWidget mit QSplitter (horizontal, setChildrenCollapsible(false)), über addSubTab()
  links: dockPanel «Befehle»
    Zeile Suche/Filter       QLineEdit «Suchen …» (Löschknopf) + QComboBox «Alle Gruppen» (commandGroupChoices/commandGroupLabel)
    Tabelle                  QTreeWidget mit Spaltenkopf, 8 Spalten aus CommandRow; Spalte «Aktiv» als Kästchen
                             (ItemIsUserCheckable); abgeschaltete Zeile grau; Tooltip aus warning
    Leiste                   buttonBar: symbolButton hinzufügen, kopieren, löschen │ rechts QLabel commandListHint (#hintLabel)
  rechts: dockPanel «Befehl «!discord»»  (commandTitle)
    QScrollArea (NoFrame) → section «Befehl»: Name (QLineEdit), Gruppe (QComboBox, editierbar),
                            Auslöser (QPlainTextEdit, Hinweis «ein Auslöser pro Zeile»), Ort (QComboBox commandLocations), Aktiv (QCheckBox)
                          → section «Wer darf»: QCheckBox Alle/Abonnenten/VIPs/Moderatoren + eigene Gruppen, Hinweis,
                            QCheckBox «Nachrichten des Bots ignorieren»
                          → section «Abklingzeit»: Kanal, Je Person (QSpinBox, Suffix « s», keepValueVisible), Hinweis
                          → section «Ausführen»: Aktion (QComboBox actionChoices) + QPushButton «Öffnen», Hinweis
                          → #fieldProblem je Abschnitt (Zeile immer da)
    Leiste                   buttonBar: rechts QPushButton «▶ Testen als …» (blockiert)
```

### Unterreiter «Zeitnachrichten» (ohne Bild, Plan 6: «wie bei «Befehle»»)
```
Seite                        QSplitter wie «Befehle»
  links: dockPanel «Zeitnachrichten»
    Tabelle                  QTreeWidget, Spalten Aktiv (Kästchen), Name, Abstand, Chat-Zeilen, nur im Stream, Aktion (TimerRow)
    Leiste                   buttonBar: hinzufügen, kopieren, löschen
  rechts: dockPanel «Zeitnachricht «Info»»  (timerTitle)
    QScrollArea → section «Zeitnachricht»: Name, Aktiv, Wiederholen
                → section «Abstand»: Abstand, Zufällig (QCheckBox) mit von/bis, Chat-Zeilen (QSpinBox), Nur im Stream
                → section «Ausführen»: Aktion + «Öffnen»
                → #fieldProblem je Abschnitt
```

### Unterreiter «Variablen» (ohne Bild, Plan 6)
```
Seite                        QSplitter mit zwei dockPanel
  dockPanel «Für alle»
    Suchfeld                 QLineEdit «Suchen …»
    Tabelle                  QTreeWidget: Name, Wert (Zelle änderbar), dauerhaft, Herkunft (VariableRow)
    Leiste                   buttonBar: symbolButton löschen
  dockPanel «Je Zuschauer»
    Zeile Suche/Filter       QLineEdit «Suchen …» + QComboBox «Alle Variablen» (userVariableNames/variableNameLabel)
    Tabelle                  QTreeWidget: Zuschauer, Variable, Wert (Zelle änderbar), Herkunft
    Leiste                   buttonBar: löschen
  Prüfmeldung                #fieldProblem unter jeder Tabelle (Text aus setVariableValue)
```

### Dialog «Warteschlangen» (ohne Bild, Plan 6; geöffnet über «Warteschlangen verwalten …» in «Aktionen», Mitte)
```
Dialog                       QDialog wie SettingsDialog, QVBoxLayout
  Liste                      QTreeWidget: Name, nacheinander (Kästchen); «Standard» ohne Löschen, ohne Kästchen
  Leiste                     buttonBar: symbolButton hinzufügen, löschen │ rechts «Anhalten», «Fortsetzen», «Leeren» (P14-2, siehe Frage 3)
  Meldung des Kerns          QLabel mit fester Höhe wie #settingsProblem
  Knopfzeile                 QDialogButtonBox (siehe Frage 4)
```

---

## 3. Checkliste

Lesestand 2026-11-16, Stand 8795660. Die gemeinsamen Suchen:
- `QTableWidget|QTableView|new QTreeWidget|new QListWidget` (0 Tabellen-Widgets, 4 `QTreeWidget` in 2 Dateien)
- `ItemIsEditable|setEditTriggers|QDialogButtonBox|: public QDialog|new QComboBox|setSuffix|hintLabel` über `src/app`
- `fieldProblem|ForegroundRole|setForeground|new QSplitter|new QCheckBox|QStackedWidget` über `src/app/*.cpp`

Jede Suche hat die Referenz getroffen (`actionsTree`, `actionsSplitter`, `setEditTriggers` in `actionspage.cpp:65`).

### «Befehle»
«Befehle» gegen «Bereich wie ein Dock» (das Projekt nennt die Art so, die Seite rendert dieselben Titel und Inhalte), Referenz `actionspage`, 9 Mitglieder von 12 Kandidaten in `src/app` mit `dss_common`:

- [ ] **Seite und Bereiche:** `QSplitter` waagrecht mit `setChildrenCollapsible(false)`, zwei Bereiche über `dockPanel()`. Der Titel links ist «Befehle», der Titel rechts kommt aus `commandTitle()`. Nur der Text ändert sich, wie bei `setAreaTitle`. Dehnung links 2, rechts 1. Entschieden: geteilter Baustein, `dockPanel` mit 6 Bereichen in 2 Dateien. Waagrecht ist die Mehrheit, 2 von 2 Teilern (`chatBotLiveSplitter`, `actionsSplitter`). Die Dehnungszahlen sind wie verlangt (Entwurf: Liste breit).
- [ ] **Suchfeld:** `QLineEdit` mit `setPlaceholderText("Suchen …")`, `setClearButtonEnabled(true)` und `setAccessibleName`. Ein neuer Suchtext füllt nur die Tabelle neu. Entschieden: Referenz, Familie mit 1 Mitglied (`actionsSearch`). Das ist der Weg der Referenz, keine Mehrheit.
- [ ] **Filter «Gruppe»:** `QComboBox` neben dem Suchfeld auf derselben Zeile, Einträge aus `commandGroupChoices()` und `commandGroupLabel()`. Entschieden: wie verlangt (Plan 6, Entwurf ②). Ein Filter über einer Liste hat noch kein Mitglied. Eine `QComboBox` steht bisher nur im Dock «Steuerung» und in der Eingabezeile «Chat» (`chatbotpage.cpp:339`), also kein Muster für diese Stelle.
- [ ] **Tabelle:** `QTreeWidget`, nicht `QTableWidget` (0 Treffer im Projekt). Dazu `setRootIsDecorated(false)`, `setUniformRowHeights(true)`, `SingleSelection`, Spaltenkopf mit `QHeaderView::section`. Spaltenbreiten `digit * n` mit gedehnter letzter Spalte. Die Zeile kommt in die Selektorliste `#mediaTree::item, … #chatBotRuns::item` (`theme.cpp:344–349`, Abstand 2 px / 2 px, ohne Rundung). Entschieden: Mehrheit, 4 von 4 Listen mit Spalten (`#mediaTree`, `#chatBotEvents`, `#chatBotRuns`, `#chatBotChat`), Breiten 3 von 3 in `chatbotpage.cpp`. Die neuen Objektnamen kommen in diese Liste. Das ist eine kleine Änderung an `theme.cpp`, wie in e2-12 bis e2-13.
- [ ] **Spalte «Aktiv»:** Kästchen über `Qt::ItemIsUserCheckable` in Spalte 0. Ein Wechsel sendet über `putCommand` mit `enabled`, beim Füllen geschützt mit `m_filling`. Entschieden: Referenz (`actionspage.cpp:75–79`, `:162–163`), Familie mit 1 Mitglied.
- [ ] **Abgeschaltete Zeile grau:** Der Text ganzer Zeilen steht in `textDisabled` (Entwurf: «!wunschliste»). Mechanismus: `ResultColorDelegate` mit `setForeground(m_theme.…)`, weil das Stylesheet `Qt::ForegroundRole` überschreibt (`chatbotpage.cpp:41–75`, `:501–507`). Die Farbe kommt aus dem Theme (`textDisabled`), der Weg vom Delegaten. Entschieden: Mehrheit, 1 von 1 farbiger Zeilentext. Der Delegat liegt im anonymen Namensraum von `chatbotpage.cpp`, siehe Frage 1.
- [ ] **Leiste unter der Tabelle:** `buttonBar({hinzufügen, kopieren, löschen}, {Hinweis})`, Symbole `hinzufuegen.svg`, `kopieren.svg`, `entfernen.svg` über `symbolButton()`, ausgegraut bis «Kern verbunden und Liste geladen» bzw. bis zur Auswahl. Rechts ein `QLabel` mit `commandListHint()` in `#hintLabel`. Entschieden: geteilter Baustein, `buttonBar`/`listWithButtons` mit 7 Leisten in 3 Dateien, `symbolButton` in `docks.cpp` und `actionspage.cpp`. Die Tooltips folgen der Referenz («Aktion hinzufügen» → «Befehl hinzufügen»).
- [ ] **Rechte Spalte:** `QScrollArea` (`NoFrame`, ohne waagrechte Leiste) mit `section()` je Abschnitt «Befehl», «Wer darf», «Abklingzeit», «Ausführen», darin ein Raster aus Beschriftung und Feld. Hinweise als `QLabel` mit `#hintLabel`. Ohne Auswahl der Hinweis «Kein Befehl gewählt». Der Wechsel der Auswahl füllt nur die Werte, Felder entstehen nie neu (Regel 9). Entschieden: geteilter Baustein, `section` 5-mal im Dock «Eigenschaften». Die Titel stehen in normaler Schreibung, nicht in GROSSBUCHSTABEN wie im Entwurf (Wert des Entwurfs, nicht übernommen).
- [ ] **Felder:** «Auslöser» als `QPlainTextEdit` (Stil im Theme, `theme.cpp:334`), Text über `commandsText`/`commandsFromText`. Abklingzeiten als `QSpinBox` mit Suffix `" s"` und `keepValueVisible`, `setKeyboardTracking(false)`. «Ort» aus `commandLocations()`, «Wer darf» aus `permissionChoice`/`permissionsFromChoice`. Entschieden: geteilter Baustein `keepValueVisible`. Das Suffix mit Leerzeichen ist die Mehrheit, 6 von 6 in 2 Dateien (`" px"`, `" °"`, `" ms"`, `" Bilder/s"`). Sekunden folgen dem Entscheid des Nutzers vom 16.11.; das Modell kennt nur ganze Sekunden, deshalb kein Komma nötig.
- [ ] **«Ausführen»:** `QComboBox` aus `actionChoices()`, Wahl über `setCommandAction()`. «Öffnen» springt wie «Im Editor öffnen» in «Aktionen» und wählt die Aktion (`ActionsPage::selectAction`). Entschieden: wie verlangt (Plan 2.6). Das Springen hat 1 Vorbild (`chatBotRunOpenAction`).
- [ ] **Speichern, Fehler, Löschen:** Rückfrage nur mit `confirm()` und dem Text aus `removeCommandQuestion()`. Statuszeile über `showStatus()` mit «Gespeichert: <Name>» bzw. «Gelöscht: <Name>». Prüfmeldung aus `EditResult::problem` in einer Zeile `#fieldProblem` unter dem Abschnitt, immer da; danach zeigt das Feld wieder den Wert des Kerns. Entschieden: geteilte Bausteine (`confirm` 7 Aufrufe in 2 Dateien, `showStatus` über 30), die Prüfmeldung als Entscheid des Nutzers vom 16.11. (`#fieldProblem` steht im Theme, wird in `src/app` noch nicht benutzt).
- [ ] **Nicht übernehmen:** Werte aus `_e3/studio.css` und dem Bild (Farben, GROSSBUCHSTABEN, Abstände). Auch nicht die Knöpfe zum Lösen und Schliessen am Bereichstitel (Plan 6: fest), den Text «Action» (heisst «Aktion»), die nachgebauten Abschnitte in `settingsdialog.cpp:51–57`/`:87–93` und die festen Spaltenbreiten der «Läufe».

### «Zeitnachrichten»
«Zeitnachrichten» gegen dieselbe Art. Plan 6 legt fest: «rechts Bereich des Gewählten wie bei «Befehle»». Es gelten dieselben Zählungen wie oben.

- [ ] **Seite, Bereiche, Titel:** wie «Befehle», links «Zeitnachrichten», rechts `timerTitle()`. Entschieden: wie verlangt (Plan 6) und geteilter Baustein `dockPanel`.
- [ ] **Kein Suchfeld und kein Filter:** Der Plan nennt keine; die Tabelle beginnt direkt unter dem Titel. Entschieden: wie verlangt (Plan 6, Zeile «Zeitnachrichten»).
- [ ] **Tabelle:** wie «Befehle». Spalten Aktiv, Name, Abstand, Chat-Zeilen, nur im Stream, Aktion aus `TimerRow`, Kästchen in Spalte 0 über `putTimer`. Entschieden: Mehrheit, 4 von 4 Listen mit Spalten.
- [ ] **Abgeschaltete Zeile grau:** wie bei «Befehle».
- [ ] **Leiste:** `buttonBar` mit hinzufügen, kopieren, löschen, Tooltips «Zeitnachricht …». Ohne Hinweis rechts, denn es gibt keine Funktion wie `commandListHint` für Zeitnachrichten. Entschieden: geteilter Baustein.
- [ ] **Rechte Spalte:** `section` «Zeitnachricht» (Name, Aktiv, Wiederholen), «Abstand» (Abstand, Zufällig mit von/bis, Chat-Zeilen, Nur im Stream), «Ausführen» (Aktion über `setTimerAction`, «Öffnen»). Die Felder von/bis sind bei abgeschaltetem «Zufällig» nur ausgegraut, nie ausgeblendet (Regel 9, keine Layout-Änderung). Entschieden: wie verlangt (Plan 6) und Regel 9.
- [ ] **Einheit der Abstände:** offen, siehe Frage 2. Bis zur Antwort Sekunden mit Suffix `" s"` und unterer Grenze 60, wie im Modell. Der Text kommt aus der Logik.
- [ ] **Speichern, Fehler, Löschen:** wie «Befehle», mit `removeTimerQuestion()` und der Prüfmeldung aus `putTimer` (Abstand mindestens 60 s, von ≤ bis).
- [ ] **Nicht übernehmen:** wie bei «Befehle».

### «Variablen»
«Variablen» gegen dieselbe Art, Plan 6:

- [ ] **Zwei Bereiche:** `dockPanel` «Für alle» und «Je Zuschauer» in einem waagrechten `QSplitter`. Entschieden: Mehrheit, 2 von 2 Teilern waagrecht. Die Anordnung ist sichtbar und ohne Bild, deshalb Frage 5.
- [ ] **Suchfeld je Bereich:** wie in «Aktionen» (`Suchen …`, Löschknopf). «Je Zuschauer» bekommt zusätzlich den Filter «Alle Variablen» (`userVariableNames`, `variableNameLabel`), auf derselben Zeile wie in «Befehle». Entschieden: Referenz (Suchfeld) und wie verlangt (Filter).
- [ ] **Tabellen:** `QTreeWidget` wie «Befehle», Spalten aus `VariableRow` («Für alle»: Name, Wert, dauerhaft, Herkunft; «Je Zuschauer»: Zuschauer, Variable, Wert, Herkunft). Entschieden: Mehrheit, 4 von 4 Listen mit Spalten.
- [ ] **Wert in der Zelle ändern:** `Qt::ItemIsEditable` nur in der Spalte «Wert». Auslöser Doppelklick und F2 (`DoubleClicked | EditKeyPressed`), Text an `setVariableValue()`. Bei einer Prüfmeldung zeigt die Zelle wieder den Wert des Kerns. Entschieden: wie verlangt (Plan 6). Das ist eine Stelle ohne Mitglied: 0 Treffer für `ItemIsEditable`, die Familie setzt sonst `NoEditTriggers` (2 von 2).
- [ ] **Prüfmeldung:** eine Zeile `#fieldProblem` unter jeder Tabelle, immer da. Entschieden: Entscheid des Nutzers vom 16.11. auf diese Stelle übertragen (Tabelle statt Abschnitt).
- [ ] **Leiste:** `buttonBar` mit `symbolButton` «Löschen» (`entfernen.svg`), frei nur bei einer Auswahl, die `removeVariable` erlaubt. Rückfrage mit `confirm()`; einen Fragetext gibt es nicht [OFFEN, siehe Abschnitt 4]. Entschieden: geteilter Baustein.
- [ ] **Übernommene Werte ohne Funktion** (`watchtime` …) stehen wie alle anderen da, ohne eigene Farbe. Entschieden: wie verlangt (Plan 6).
- [ ] **Nicht übernehmen:** Werte des Entwurfs. Keinen Zuschauernamen im Fenster zusammensetzen, `VariableRow::viewer` kommt aus der Logik.

### Dialog «Warteschlangen»
Dialog «Warteschlangen» gegen «Dialog» (eigener Rahmen `QDialog`), Referenz `settingsdialog`, 3 Mitglieder von 3 Kandidaten (`settingsdialog`, `obssourcedialog`, `dialogs.cpp`):

- [ ] **Rahmen:** eigene Klasse `QueuesDialog : public QDialog` wie `SettingsDialog`/`ObsSourceDialog`. Fenstertitel «Warteschlangen». Entschieden: Mehrheit, 2 von 3 als eigene Klasse (`dialogs.cpp` baut einen Dialog in einer Funktion).
- [ ] **Liste:** `QTreeWidget` mit Spalten Name und «nacheinander» (Kästchen über `ItemIsUserCheckable`, sendet `queue_put`). «Standard» hat kein Kästchen und keinen Löschknopf. Entschieden: wie verlangt (Plan 6, 2.8). Tabelle wie oben.
- [ ] **Umbenennen:** Doppelklick auf den Namen öffnet denselben Weg wie «Gruppe umbenennen» in «Aktionen». Entschieden: Entscheid des Nutzers vom 16.11. («Doppelklick nur umbenennen») auf diese Liste übertragen. Siehe Frage 4.
- [ ] **Leiste:** `buttonBar` mit `symbolButton` hinzufügen und löschen; rechts «Anhalten», «Fortsetzen», «Leeren» als `QPushButton`, siehe Frage 3. Entschieden: geteilter Baustein; Textknöpfe wie in «Läufe der Aktionen» (1 von 1 mit Textknöpfen in einer Leiste).
- [ ] **Meldung des Kerns:** `QLabel` unter der Liste mit fester Höhe von 3 Zeilen, Antworten nur im Dialog und nicht in der Statuszeile. Entschieden: Referenz `settingsdialog.cpp:146–153` mit Punkt 76, 1 von 1 Dialog mit Kernantworten.
- [ ] **Knopfzeile:** `QDialogButtonBox`. Entschieden: Mehrheit, 3 von 3 in 3 Dateien. Welche Knöpfe, hängt an Frage 4.
- [ ] **Löschen:** `confirm()` mit einem Text aus der Logik (blockiert, Abschnitt 1).
- [ ] **Öffnen:** aus dem Eintrag «Warteschlangen verwalten …» in «Aktionen», Mitte. Danach steht die Auswahl wieder auf der vorherigen Warteschlange. Entschieden: wie verlangt (Plan 6, Abweichungen). Ob der Dialog modal ist wie «Einstellungen»: nicht geprüft.
- [ ] **Nicht übernehmen:** `QFormLayout` und die von Hand nachgebauten Abschnitte aus `settingsdialog` (kein Formular), «OK/Abbrechen» ungeprüft (siehe Frage 4).

---

## 4. Fragen an den Nutzer (sichtbar oder bedienbar)

1. **Neue Bausteine und Herauslösungen** (eine Frage, drei Punkte):
   - `ResultColorDelegate` aus dem anonymen Namensraum von `chatbotpage.cpp` nach `src/app/panels.{h,cpp}` verschieben. Betroffen: `chatbotpage.cpp`, die neuen Seiten.
   - Ein gemeinsamer Aufbau «Tabelle links, Gewähltes rechts» für «Befehle» und «Zeitnachrichten» (zwei Seiten dieser Arbeit schreiben ihn sonst je selbst), etwa eine Klasse `ListDetailPage` in `src/app`. Betroffen: nur die neuen Dateien.
   - Neu: ein Dialog `QueuesDialog` in `src/app/queuesdialog.{h,cpp}`.

   **Empfehlung:** Ja zu allen drei. Bei Nein schreibt jede Seite den Aufbau selbst und der Delegat wird kopiert.
2. **Abstand der Zeitnachrichten:** Feld in **Minuten mit Komma** («20 min», «7,5 min»), passend zur Tabelle («20 min»), oder in Sekunden wie die übrigen Zeiten? **Empfehlung:** Minuten. Die Umrechnung kommt aus der Logik, nicht aus dem Fenster.
3. **«Anhalten», «Fortsetzen», «Leeren» im Dialog** (Steuern kommt erst mit P14-2): **Empfehlung:** ausgegraut sichtbar wie «weich» und «Anhören» (Entscheide vom 16.11.). Die andere Möglichkeit wäre, sie wegzulassen.
4. **Dialog «Warteschlangen», Speichern und Namen:**
   - **Empfehlung:** Jede Änderung gilt sofort wie in «Aktionen». Unten steht nur «Schliessen» (`QDialogButtonBox::Close`, das hat noch kein Dialog). Die andere Möglichkeit wäre «OK/Abbrechen» wie «Einstellungen».
   - «Neu» legt «Warteschlange 1» an, Doppelklick benennt um.

5. **«Variablen», Anordnung:** **Empfehlung:** «Für alle» und «Je Zuschauer» nebeneinander mit Teiler wie «Live». Die andere Möglichkeit wäre übereinander.

### Neue Texte (zum Lesen durch den Nutzer)
- «Befehle»:
  - Bereichstitel «Befehle», Platzhalter «Suchen …», Filter «Alle Gruppen» (Logik)
  - Spalten «Aktiv», «Auslöser», «Gruppe», «Wer darf», «Abkl. Kanal», «Abkl. Person», «Aktion», «Benutzt»
  - Abschnitte «Befehl», «Wer darf», «Abklingzeit», «Ausführen»
  - Beschriftungen «Name», «Gruppe», «Auslöser», «Ort», «Aktiv», «Kanal», «Je Person», «Aktion»
  - Kästchen «Alle», «Abonnenten», «VIPs», «Moderatoren», «Nachrichten des Bots ignorieren»
  - Hinweise:
    - «ein Auslöser pro Zeile»
    - «Mit «Alle» sind die anderen eingeschlossen. Der Kanal darf immer.»
    - «0 s = keine Abklingzeit. Abgewiesene Aufrufe stehen in «Läufe der Aktionen».»
    - «Die Aktion bekommt %rawInput%, %input0% …, %user%.»
  - Knöpfe «Öffnen», «▶ Testen als …»
  - leer «Kein Befehl gewählt»
  - Tooltips «Befehl hinzufügen», «Befehl kopieren», «Befehl löschen»
- «Zeitnachrichten»:
  - Bereichstitel «Zeitnachrichten»
  - Spalten «Aktiv», «Name», «Abstand», «Chat-Zeilen», «Nur im Stream», «Aktion»
  - Abschnitte «Zeitnachricht», «Abstand», «Ausführen»
  - Beschriftungen «Wiederholen», «Zufällig», «von», «bis», «Chat-Zeilen», «Nur im Stream»
  - leer «Keine Zeitnachricht gewählt»
  - Tooltips «Zeitnachricht hinzufügen/kopieren/löschen»
- «Variablen»:
  - Bereichstitel «Für alle», «Je Zuschauer», Filter «Alle Variablen» (Logik)
  - Spalten «Name», «Wert», «Dauerhaft», «Herkunft», «Zuschauer», «Variable»
  - Tooltip «Variable löschen»
  - Rückfrage «Variable «<Name>» löschen?» [OFFEN: Text aus der Logik fehlt]
- Dialog «Warteschlangen»:
  - Titel «Warteschlangen», Spalten «Name», «Nacheinander»
  - Knöpfe «Anhalten», «Fortsetzen», «Leeren», «Schliessen»
  - Tooltips «Warteschlange hinzufügen», «Warteschlange löschen»
  - neuer Name «Warteschlange 1»
  - Rückfrage beim Löschen (Text aus der Logik, fehlt)

---

## 5. Nicht gelesen, Annahmen

- `_e3/3-befehle.html`, `_e3/studio.css` und `1-gesamtansicht.*` habe ich nicht geöffnet. Den Entwurf habe ich aus dem Bild gelesen. Seine Werte habe ich den Theme-Einträgen nach ihrer Rolle zugeordnet (Auswahl `primary`, grau `textDisabled`, Hinweis `textMuted`, Titel `#propertySectionTitle`), nicht Wert für Wert.
- `DESIGN.md` gibt es nicht. Vom Theme habe ich `theme.h` ganz und `theme.cpp:290–374` gelesen.
- `docks.cpp`, `dockviews.cpp`, `coreclient.cpp` und `mainwindow*.cpp` habe ich nur über Suchen gelesen. Die Zählungen zu `section`, `symbolButton` und `confirm` stammen teils aus der e2-Checkliste und sind nur über Suchtreffer in denselben Dateien bestätigt.
- «Aktionen», Mitte und rechts (e2-15, e2-16) sind im Stand 8795660 nicht drin. Kommt dort ein eigener Weg für Abschnitte, Prüfmeldung oder rechte Spalte dazu, gilt er (der Code gewinnt), und die Zeilen oben sind neu zu prüfen.
- Die Nullen bei `ItemIsEditable` und `QTableWidget` sind belegt: Dieselben Suchen haben `setEditTriggers` und `QTreeWidget` gefunden.

## 6. Kosten

- **Geöffnete Projektdateien:** 14. Das sind `HANDOFF.md`, `P14-chatbot.md` (2 Ausschnitte), `3-befehle.png`, `checkliste-e2-mit-entscheiden.md`, `actionspage.h` und `.cpp`, `panels.h`, `actionview.h`, `chatbotpage.cpp` (2 Ausschnitte), `actionmodel.h`, `actionprotocol.h` (Ausschnitt über die Shell), `settingsdialog.cpp`, `theme.h` und `theme.cpp`.
- **Suchen:** etwa 17 (10 mit Grep, etwa 7 in 3 Shell-Aufrufen); die Zahl ist geschätzt.
- **Skill-Dateien:** 7, nämlich `finding-patterns`, `words.md`, `design/reading.md`, `values`, `conventions`, `decisions` und `checklist.md`. Keine Subagenten.

**Geänderte Dateien:** keine. **Commits:** keine. **Offene Probleme:** die fehlende Logik aus Abschnitt 1, die vor dem Bau von e3 geklärt sein muss, und die Fragen 1 bis 5 an den Nutzer.
## 7. Entscheide Dominik (2026-11-16, Sitzung Lotse)

- **Frage 1, Bausteine:** Ja. Gemeinsamer Aufbau «Tabelle links, Gewähltes rechts» für «Befehle» und «Zeitnachrichten» (z. B. `ListDetailPage` in `src/app`), Dialog `QueuesDialog` in `src/app/queuesdialog.{h,cpp}`. Der Farb-Delegat ist schon geteilt: `ForegroundDelegate` in `src/app/panels.{h,cpp}` (e2-15).
- **Frage 2, Abstand der Zeitnachrichten:** im Feld in **Minuten mit Komma** («20 min», «7,5 min»), Umrechnung in der Logik, intern Sekunden.
- **Frage 3, «Anhalten», «Fortsetzen», «Leeren»:** ausgegraut sichtbar bis P14-2.
- **Frage 4, Dialog «Warteschlangen»:** jede Änderung gilt sofort, unten nur «Schliessen»; «Neu» legt «Warteschlange 1» an, Doppelklick benennt um.
- **Frage 5, «Variablen»:** «Für alle» und «Je Zuschauer» nebeneinander mit Teiler wie «Live».
- Aus e2 weiter gültig: Zeiten in Sekunden mit Komma («8,5 s»), Prüfmeldung am Feld als Zeile unter dem Abschnitt (`#fieldProblem`), Kontextmenü mit «…», Doppelklick nur umbenennen (`setExpandsOnDoubleClick(false)`), «Gelöscht: <Name>» nach dem Löschen (`deletedStatusText`, e2-15), zugängliche Namen für Felder ohne Beschriftung, `settingsdialog.cpp` bleibt unverändert.
