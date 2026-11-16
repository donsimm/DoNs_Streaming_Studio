# Rückmeldung an Lotse: Checkliste P14 e2, Unterreiter «Live» und «Aktionen»

Eingeschätzt als **zwei neue Seiten**, deshalb die ganze Phase nach `ui-consistency:finding-patterns`. Die Übergabe `.claude/HANDOFF.md` habe ich gelesen und berücksichtigt; zu P14 steht dort nichts. Ich habe nur gelesen: keine Datei geschrieben, nichts gebaut, keine Tests, keine Commits.

**Referenz:** `src/app/chatbotpage.{h,cpp}`, der Reiter «Chat-Bot» mit der Kopfzeile und dem Bereich «Ereignisse».
- Er ist die Seite, die e2 erweitert. Die Ausbaustellen `addSubTab` und `addLivePanel` sind schon für P14 vorgesehen.
- Er ist die neueste Seite dieser Art (P13, 2026-11-15).
- Für die Bereiche und Knopfleisten zähle ich die Docks in `docks.cpp` mit, für die Abschnitte rechts den Dialog «Einstellungen».

**Art und Familie:** «Bereich wie ein Dock», benannt nach dem Projekt selbst (Kommentar in `theme.cpp:302`). Gezählt im Fenster `src/app` mit `dss_common`.
- 9 Kandidaten: 5 Docks, Bereich «Ereignisse», Dialog «Einstellungen», `obssourcedialog`, `dialogs`.
- 6 davon gehören zur Familie: die 5 Docks und «Ereignisse».
- Die drei Dialoge habe ich als Rahmen weggelassen. Die Abschnitte des Dialogs «Einstellungen» zählen nur beim Abschnittstitel mit.

---

## 1. Blockierend: fehlende Logik (gehört nicht zu e2)

| Fehlt | Suche und Fundstelle |
|---|---|
| **Kopfzeile:** Schalter «Chat-Bot aktiv» und der Zustand aus `actions_status`, etwa «stumm: Version im Einsatz läuft» | `actions_set_enabled` und `actions_status` gibt es nur im Protokoll (`actionprotocol.h:31`, `:40`). `chatBotHeaderView` (`windowdata.h:146–152`) kennt nur den Twitch-Zustand. |
| **«Live», Bereich «Läufe der Aktionen»:** Zeilen, Ergebnistext, «Nur Fehler», «Lauf abbrechen» | Die Nachrichten `kActionRuns`, `kActionRun`, `kActionRunCancel` und `kGetActionRuns` gibt es (`actionprotocol.h:36–63`). Eine Zeilenlogik findet die Suche `struct (RunRow\|ActionRunRow)` nicht (Review, Befund 1). |
| **«Live», Bereich «Chat»:** Zeilen mit Abzeichen und Farbe, graue Ereigniszeilen, Senden als Bot oder als Kanal | `struct (ChatRow\|ChatLine)` findet nichts. `chat_send` gibt es nur als Schritttyp (`steptypes.h:45`). In der Liste `actionprotocol.h:30–77` steht keine Nachricht, mit der das Fenster in den Chat sendet. Andere Namen in `protocol.h` habe ich nicht gesucht. |
| **Testen:** «Testwerte: Benutzer», «▶ Testen», «echt ausführen» | `action_test` mit `dry_run` gibt es (`actionprotocol.cpp:53`), in `actionview` aber nichts dazu (Review, Befund 3). |
| **Rechte Spalte: Felder eines Schritts oder Auslösers** (deutsche Beschriftungen, Reihenfolge, Abschnitte wie «Element» und «Inhalt der Quellen») | `FieldSpec` liefert nur Name, Art und Grenzen (`fieldspec.h:31–38`). Ohne eine Funktion für Felder und Beschriftungen müsste das Fenster sie erfinden, was Regel 9 verbietet. |
| **Gruppe umbenennen und löschen, Rückfragetexte beim Löschen** | `kGroupPut` und `kGroupRemove` gibt es (`actionprotocol.h:52–53`), aber nicht in `actionview`. Rückfragetexte gibt es nur für «Zurücksetzen» (`resetActionQuestion`, Review, Befunde 4 und 5). |
| «Warteschlangen verwalten …» hat kein Ziel | Der Dialog gehört zu e3 (Review, Befund 2). In e2 bleibt der Eintrag ohne Wirkung oder wird ausgegraut. |

Die Teile ohne Logik baut e2 nur als festes Gerüst (ausgegraut) oder gar nicht. Die Checkliste unten gilt für alles Sichtbare.

---

## 2. Bäume der Rollen

### Kopfzeile des Reiters «Chat-Bot» (gibt es schon, e2 ändert sie)
```
Kopfzeile #chatBotHeader     QWidget, Höhe fest (QSizePolicy::Fixed), keine Abstände
  Unterreiter #chatBotTabs   QFrame mit moduleButton() (headerbar.h:19) über addSubTab()
                             «Live», «Aktionen» (in e2), später «Befehle» usw.
  Dehnung
  [neu] Kästchen «Chat-Bot aktiv»   QCheckBox, links vom Zustand
  Zustand #chatBotState      QLabel, Farbe über die Eigenschaft [state], abgeschnitten, Tooltip
```

### Unterreiter «Live»
```
Seite                         QWidget im QStackedWidget m_pages
  Zeile der Bereiche          heute QHBoxLayout m_liveRow → wird QSplitter (Plan: «Teiler verschiebbar»)
    Bereich «Chat»            addLivePanel(): #chatBotPanelTitle + #chatBotPanelBody
      Liste                   QTreeWidget oder QListWidget, mitlaufend wie appendEvent()
      Eingabezeile            QComboBox «als Bot / als Kanal», QLineEdit, Zähler QLabel, QPushButton «Senden»
    Bereich «Ereignisse»      gibt es (m_events, 4 Spalten)
      [neu] Leiste            #dockButtons: QCheckBox «Chat-Nachrichten ausblenden»
    Bereich «Läufe der Aktionen»  addLivePanel()
      Tabelle                 QTreeWidget, 4 Spalten wie m_events
      Leiste                  #dockButtons: QPushButton «Im Editor öffnen», «Lauf abbrechen», Dehnung, QCheckBox «Nur Fehler»
```

### Unterreiter «Aktionen»
```
Seite                           QSplitter mit drei Bereichen, jeder wie addLivePanel()
  links: Bereich «Aktionen»
    Suchfeld                    QLineEdit mit Platzhalter
    Baum                        QTreeWidget, Gruppen → Aktionen, Kästchen je Aktion
    Leiste #dockButtons         symbolButton: hinzufügen, Ordner neu, kopieren, entfernen │ Dehnung │ zurücksetzen
  Mitte: Bereich «Aktion «Name»»
    Abschnitt «Aktion»          section(): Raster Name | Gruppe, Warteschlange | Aktiv, Hinweis
    Abschnitt «Auslöser»        section() mit den Titelknöpfen + und −, Liste mit Kästchen
    Abschnitt «Schritte»        section(): QTreeWidget, eingerückt, Kästchen, abgeschaltet durchgestrichen
    Leiste #dockButtons         symbolButton mit Menü (hinzufügen), entfernen, hoch, runter │ Dehnung │
                                «Testwerte: Benutzer» QComboBox, QPushButton «▶ Testen», QCheckBox «echt ausführen»
  rechts: Bereich «Schritt «…»» / «Auslöser «…»»
    QScrollArea (NoFrame) → QStackedWidget je Typ → section() je Abschnitt, Raster aus Beschriftung und Feld
```

---

## 3. Checkliste

### Kopfzeile
Kopfzeile gegen «Bereich wie ein Dock», Referenz `chatbotpage`, gelesen 2026-11-16 (Stand a100c29):

- [ ] **Unterreiter «Aktionen»:** nur über `addSubTab()` mit `moduleButton()`, Stil `#chatBotTabs QToolButton` mit `buttonDisabled`, gewählt `primary`. Entschieden: geteilter Baustein, `moduleButton` wird in 2 Dateien benutzt (`headerbar.cpp`, `chatbotpage.cpp`). Der Text heisst **«Aktionen»**, nicht «Actions» wie im Entwurf: wie verlangt (Plan 6, Tabelle).
- [ ] **Schalter «Chat-Bot aktiv»:** `QCheckBox` links von `#chatBotState`, Abstand 6 px aus dem Abstand der Zeile (`chatbotpage.cpp:32`). Entschieden: wie verlangt (Entwurf Seite 1, ③; Plan 6). Die Stelle hat noch kein Teil der Familie.
- [ ] **Kopfzeile bleibt fest hoch:** Ein neuer Zustand ändert nur Text, Tooltip und `[state]` mit `unpolish`/`polish`, nie die Anordnung. Entschieden: Regel 9 und die Referenz (`chatbotpage.cpp:34`, `:116–124`).
- [ ] **Zustandsfarben:** nur `#chatBotState[state=ok|warning|error]`, also `stateConnected`, `stateConnecting`, `stateError`. Neue Zustände wie «stumm» oder «aus» bekommen einen dieser Werte und keine eigene Farbe. Entschieden: Theme (`theme.cpp:298–301`).

### «Live»
«Live» gegen «Bereich wie ein Dock» (gleicher Titel und Inhalt wie ein Dock, so in `theme.cpp:302` festgehalten), Referenz `chatbotpage`, 6 Familienmitglieder von 9 Kandidaten in `src/app` mit `dss_common`, gelesen 2026-11-16:

- [ ] **Bereichstitel und Rahmen:** jeder Bereich über `addLivePanel()`, das `#chatBotPanelTitle` setzt (`dockTitle`, `border`, 2 px / 4 px, `fontSizeSmall` fett) und `#chatBotPanelBody` (`base`). Entschieden: Mehrheit, 6 von 6 (5 Docks über `QDockWidget::title`, 1 über `addLivePanel` mit denselben Werten). Gesucht: `QDockWidget::title|chatBotPanelTitle` in `theme.cpp`.
- [ ] **Teiler zwischen den Bereichen:** `m_liveRow` wird ein `QSplitter`. `addLivePanel` behält seine Signatur, Breite 4 px wie `QMainWindow::separator` (`theme.cpp:113`). Entschieden: wie verlangt (Plan 6, «Teiler verschiebbar»). Kein Teil der Familie nutzt bisher `QSplitter` (0 Treffer in `src/app`). Die Werte des Teilers stammen vom Dock-Teiler. Ein Stil `QSplitter::handle` fehlt im Theme (siehe Vorschlag in Abschnitt 4).
- [ ] **Tabelle «Läufe der Aktionen»:** `QTreeWidget` wie `m_events` (`setRootIsDecorated(false)`, `setUniformRowHeights(true)`, `SingleSelection`, `NoFocus`), Spaltenbreiten aus `digit * n`, letzte Spalte gedehnt, Zeilenabstand 2 px / 2 px wie `#chatBotEvents::item`. Entschieden: Mehrheit, 2 von 2 Listen mit Spalten (`#mediaTree`, `#chatBotEvents`, beide in `theme.cpp:329`). Kopfzeilen über `QHeaderView::section` mit `dockTitle`.
- [ ] **Mitlaufen:** neue Zeile anhängen, vorne `removedFront` wegnehmen, nur nach unten rollen, wenn die Liste schon unten stand. Höchstens 200 Zeilen wie `EventRows::kMaxRows`, die Zahl kommt aus der Logik. Entschieden: Referenz (`chatbotpage.cpp:137–147`), gilt für «Chat» und «Läufe».
- [ ] **Ergebnisfarbe in «Läufe»** (fertig, abgewiesen, Fehler, läuft): nur `stateConnected`, `stateConnecting` und `stateError` über eine Eigenschaft `[state]` und das Stylesheet, nie `QColor` in der Seite. Entschieden: Theme. Die Zustandsfarben stehen in 2 Selektoren (`#connectionButton`, `#chatBotState`). Die Farbe je Zeile im `QTreeWidget` braucht einen Weg ohne Wert in der Seite: ein Delegat oder `setForeground` mit einem Theme-Wert, den das Fenster hereinreicht. [OFFEN] für den Bauenden, siehe Frage 2.
- [ ] **Leisten unter den Listen:** `#dockButtons` mit `WA_StyledBackground`, Abstände 6, 4, 6, 4, Abstand 6, Dehnung vor dem Rechtsbündigen («Nur Fehler»). Entschieden: geteilter Aufbau, 4 von 4 Docks mit Leiste (3 über `listWithButtons`, 1 von Hand in `docks.cpp:409–428`). Die Knöpfe sind Text-`QPushButton`, wie verlangt (Entwurf ⑦ ⑨). Ohne Auswahl ausgegraut. Entschieden: Mehrheit, alle Knöpfe der Docks beginnen ausgegraut (`docks.h:28`).
- [ ] **Eingabezeile «Chat»:** `QComboBox` als Bot/als Kanal, `QLineEdit` mit Platzhalter, Zähler als `QLabel` in `textMuted` mit fester Mindestbreite für «500 / 500», `QPushButton` «Senden», Enter sendet. Entschieden: wie verlangt (Plan 6). Die feste Breite ist die Mehrheit, 1 von 1 (`volumeValue`, `docks.cpp:332–335`). Text und «zu lang» kommen aus der Logik (blockierend, Abschnitt 1).
- [ ] **Abzeichen «Bot», «Kanal», «Mod» und graue Ereigniszeilen im Chat:** Diese Stelle hat noch kein Teil der Familie, und das Theme hat keine Werte dafür. Siehe Frage 2.
- [ ] **Nicht von der Referenz übernehmen:** die festen Spaltenbreiten 10, 16, 16 von «Ereignisse» (eigene Zahlen je Spalte der Läufe) und `QHBoxLayout` als Zeile (wird `QSplitter`).

### «Aktionen»
«Aktionen» gegen «Bereich wie ein Dock». Links und Mitte richten sich nach den Docks «Overlays» und «Quellen», rechts nach dem Dock «Eigenschaften», das Entwurf ⓖ und Plan 6 nennen. Gelesen 2026-11-16:

- [ ] **Drei Bereiche:** `QSplitter` mit drei Bereichen und Titeln wie `addLivePanel` (`#chatBotPanelTitle`, `#chatBotPanelBody`). Mittlerer Titel aus `actionTitle()` («Aktion «Begrüssung»»). Entschieden: Mehrheit, 6 von 6 (wie bei «Live»). Wie verlangt fest ohne Lösen und Schliessen (Plan 6, Abweichungen). Braucht die Herauslösung aus Frage 1.
- [ ] **Baum der Aktionen:** `QTreeWidget` mit den Zeilen aus `actionListRows()`, Stil `QTreeView::item` (Abstand 3 px / 2 px, Radius 4, gewählt `primary`). Kästchen je Aktion über `Qt::ItemIsUserCheckable`. Entschieden: Familie Bäume, der Baum der Overlays ist Vorbild (Entwurf Ⓐ, «wie das Dock «Overlays»»). Kästchen hat noch kein Teil der Familie (0 Treffer für `ItemIsUserCheckable`), wie verlangt (Plan 6).
- [ ] **Suchfeld:** `QLineEdit` über dem Baum mit `setPlaceholderText` und Platzhalterstil aus dem Theme. Der Wechsel des Suchtexts füllt nur den Baum neu (`actionListRows(…, search)`), keine Änderung der Anordnung. Entschieden: wie verlangt (Plan 6). Ein Suchfeld hat noch kein Teil der Familie. Platzhalter schreibt die Familie mit `setPlaceholderText` (4 Treffer in `settingsdialog.cpp`, 1 in `mainwindow.cpp`).
- [ ] **Knopfleisten links und Mitte:** `symbolButton()` mit 14 px Symbol, Tooltip und `setAccessibleName`, ausgegraut zu Beginn. Vorhandene Symbole: `hinzufuegen.svg`, `ordner-neu.svg`, `kopieren.svg`, `entfernen.svg`, `nach-oben.svg`, `nach-unten.svg`, `zuruecksetzen.svg`. «Zurücksetzen» rechts nach einer Dehnung. Freigabe der Knöpfe nur aus `stepButtons()` bzw. `actionResettable()`. Entschieden: geteilter Baustein, `symbolButton` 20-mal in `docks.cpp`, 3 von 3 Docks mit Liste. Braucht Frage 1, denn `symbolButton` liegt im anonymen Namensraum.
- [ ] **«Schritt hinzufügen»:** `QToolButton` mit `QMenu` und `InstantPopup`, Einträge aus `stepChoices()` nach Abschnitten. Entschieden: Mehrheit, 1 von 1 Hinzufügen-Knöpfen mit Menü (Dock «Quellen», `docks.cpp:166–173`). Für «+» bei den Auslösern gilt dasselbe mit `triggerChoices()`.
- [ ] **Abschnitte «Aktion», «Auslöser», «Schritte» und rechts:** `section()`: Titel `#propertySectionTitle` (`fontSizeSmall`, fett, `textMuted`), Linie `#propertySectionLine` (1 px, `border`), Abstände 8, 6, 8, 8, Raster waagrecht 8 und senkrecht 6, Titelknöpfe rechts in der Titelzeile (wie «+» und «−» bei «Auslöser»). Entschieden: geteilter Baustein, 5 Aufrufe in `docks.cpp`; der Dialog «Einstellungen» schreibt ihn zweimal von Hand nach (`settingsdialog.cpp:52–55`, `:88–91`). Braucht Frage 1.
- [ ] **Rechte Spalte:** `QScrollArea` mit `NoFrame` und ohne waagrechte Leiste, darin ein `QStackedWidget` mit einer Seite je Art des Schritts oder Auslösers. Der Wechsel der Auswahl schaltet nur die Seite um und baut nie Felder auf oder ab. Ohne Auswahl der Hinweis `#hintLabel` (`textMuted`). Zahlenfelder mit `keepValueVisible` und `setKeyboardTracking(false)`. Entschieden: Referenz für diesen Teil ist das Dock «Eigenschaften», wie verlangt (Plan 6, «wie das Dock «Eigenschaften»»), dazu Regel 9 (`docks.cpp:304–306`). Die Felder selbst sind blockiert (Abschnitt 1).
- [ ] **Liste der Schritte:** `QTreeWidget`, eingerückt für Gruppe, «Dann» und «Sonst» aus `stepRows()`, zwei Spalten (Art fett, Kurzinhalt), Kästchen je Schritt. Abgeschaltet: Text durchgestrichen und rot. Entschieden: wie verlangt (Plan 6). Durchstreichen hat noch kein Teil der Familie (0 Treffer für `setStrikeOut`). Die Farbe ist offen, siehe Frage 2.
- [ ] **Rückfrage vor dem Löschen:** nur `confirm()` aus `dialogs.h`, mit dem sicheren Knopf als Standard. Entschieden: geteilter Baustein, 7 Aufrufe in 2 Dateien (`mainwindowedit.cpp`, `mainwindowmedia.cpp`). Die Texte sind blockiert (Abschnitt 1).
- [ ] **Gespeichert und Fehler:** «Gespeichert: <Name>» aus `savedStatusText()` und Fehler des Kerns nur über `MainWindow::showStatus()` in der Statuszeile, 10 s, keine `QMessageBox`. Entschieden: geteilter Baustein, `showStatus` 12-mal in `mainwindow.cpp`, 13-mal in `mainwindowedit.cpp`, 6-mal in `mainwindowmedia.cpp`. `QMessageBox` steht nur für «Über». Die Prüfmeldung am Feld ist eine eigene Stelle (Frage 3).
- [ ] **Nicht übernehmen:** die Werte aus `_entwurf/studio.css` und dem Bild. Alle Farben, Abstände und Schriften kommen aus `theme.{h,cpp}`. Auch nicht die von Hand nachgebauten Abschnitte im Dialog «Einstellungen» und die von Hand gebaute Leiste im Dock «Eigenschaften»: Dafür gibt es `section()` bzw. die Leiste von `listWithButtons`.

---

## 4. Fragen an den Nutzer (sichtbar oder bedienbar)

**Frage 1, neue Bausteine und Herauslösungen** (eine Frage mit drei Punkten):
1. `section()`, `symbolButton()` und die Leiste `#dockButtons` aus `listWithButtons()` aus dem anonymen Namensraum von `docks.cpp` herauslösen, zum Beispiel nach `src/app/panels.{h,cpp}`. Betroffen sind `docks.cpp` und `chatbotpage.cpp`, auf Wunsch auch `settingsdialog.cpp` (2 Nachbauten).
2. `ChatBotPage::addLivePanel` zu einem «Bereich wie ein Dock» verallgemeinern, das in jeden `QSplitter` passt (auch für «Aktionen»). Betroffen ist nur `chatbotpage.{h,cpp}`.
3. Neu: ein Stil `QSplitter::handle` im Theme mit dem Wert von `QMainWindow::separator`. Betroffen ist `theme.cpp`.

**Empfehlung:** Ja zu allen drei. Bei Nein bleiben die Kopien, und e2 baut Abschnitt und Leiste in der Seite selbst nach.

**Frage 2, Farben ohne Theme-Eintrag:**
- Abzeichen «Bot» (lila), «Kanal» (blau), «Mod» (grün).
- Hintergrund der grauen Ereigniszeilen im Chat.
- Roter Text bei abgeschalteten Schritten.
- Farben für die Ergebnisse der Läufe.

Am nächsten im Theme liegen `graphRam` (`--purple1`), `primary` (`--blue3`), `stateConnected` (`--green3`), `dockTitle` bzw. `base` sowie `stateError` (`--red3`). **Empfehlung:** neue Theme-Einträge mit Yami-Werten (`badgeBot`, `badgeChannel`, `badgeMod`, `chatEventRow`). Für Abgeschaltet und die Ergebnisse die vorhandenen `stateError`, `stateConnecting` und `stateConnected`.

**Frage 3, Prüfmeldung am Feld** (zum Beispiel ein leerer Name oder eine Zahl ausserhalb der Grenzen): Die Familie hat dafür noch keinen Ort (0 Treffer für `[error=`, `[invalid` oder einen Fehlerstil). **Empfehlung:** eine Zeile unter dem Abschnitt, immer da (die Anordnung bleibt gleich), Text aus `EditResult::problem` in `stateError`. Das Feld zeigt danach wieder den Wert des Kerns. Die andere Möglichkeit wäre nur die Statuszeile.

### Neue Texte
Die Texte des Plans gelten als vorgegeben. Neu formuliert sind nur die Platzhalter und die Tooltips.

- Kopfzeile: «Aktionen», «Chat-Bot aktiv»
- «Live»:
  - Bereichstitel «Chat» und «Läufe der Aktionen»
  - Spalten «Zeit», «Aktion», «Auslöser», «Ergebnis»
  - «als Bot», «als Kanal», Platzhalter «Nachricht an den Chat …», «0 / 500», «Senden»
  - «Chat-Nachrichten ausblenden», «Im Editor öffnen», «Lauf abbrechen», «Nur Fehler»
- «Aktionen»:
  - Bereichstitel «Aktionen», Platzhalter «Suchen …»
  - Abschnitte «Aktion», «Auslöser», «Schritte»
  - Beschriftungen «Name», «Gruppe», «Warteschlange», «Aktiv»
  - «Testwerte: Benutzer», «▶ Testen», «echt ausführen»
  - leer: «Keine Aktion gewählt», «Kein Schritt gewählt» (wie «Keine Quelle gewählt»)
- Tooltips:
  - «Aktion hinzufügen», «Neue Gruppe», «Aktion kopieren», «Aktion löschen», «Mitgelieferte Aktion zurücksetzen»
  - «Auslöser hinzufügen», «Auslöser entfernen»
  - «Schritt hinzufügen», «Schritt entfernen», «Schritt nach oben», «Schritt nach unten»
- Titel rechts: «Schritt «<Art>»» und «Auslöser «<Art>»». Eine Funktion dazu wie `actionTitle` habe ich nicht gefunden [OFFEN].

---

## 5. Nicht gelesen, Annahmen

- `_entwurf/*.html`, `teil-editor.html` und `studio.css` habe ich nicht geöffnet. Den Entwurf habe ich aus `1-gesamtansicht.png` und `2a-editor-im-reiter.png` gelesen. Die Werte des Entwurfs habe ich nicht einzeln dem Theme zugeordnet; die Checkliste schreibt dafür überall das Theme vor.
- `DESIGN.md` gibt es nicht.
- Vom Theme habe ich `theme.h` ganz gelesen, von `theme.cpp` nur die Stellen der Suche.
- `dockviews.cpp`, `settingsdialog.cpp` und `headerbar.cpp` habe ich nur über Suchen gelesen.
- Die Nullen bei `QSplitter`, `setStrikeOut` und `ItemIsUserCheckable` sind nicht nachgeprüft: An diesen Stellen gibt es noch nichts in der Familie, an dem die Suche ihre Treffer belegen könnte.

## 6. Kosten

- **Geöffnete Projektdateien:** 15, ganz oder in Ausschnitten. Dazu gehören `.claude/HANDOFF.md`, Plan Abschnitt 6, das Review, 2 Bilder, `chatbotpage.h` und `.cpp`, `theme.h`, `docks.h` und `.cpp`, `dialogs.h`, `mainwindow.cpp`, `windowdata.h`, `fieldspec.h` und `actionprotocol.h`.
- **Suchen:** etwa 16, einige davon in einem Aufruf gebündelt. Die Zahl ist geschätzt.
- **Skill-Dateien:** 7, nämlich `finding-patterns`, `words.md`, `design/reading.md`, `checklist.md`, `conventions`, `values` und `decisions`. Keine Subagenten.

**Geänderte Dateien:** keine. **Commits:** keine.
---

## 7. Entscheide Dominik (2026-11-16, Sitzung Lotse)

- **Frage 1, Bausteine:** Ja zu 1–3. `section()`, `symbolButton()` und die Leiste `#dockButtons` herauslösen (z. B. `src/app/panels.{h,cpp}`), `addLivePanel` verallgemeinern, Theme-Stil `QSplitter::handle`. **Ohne** `settingsdialog.cpp` (bleibt unverändert).
- **Frage 2, Farben:** neue Theme-Einträge mit Yami-Werten (`badgeBot` lila, `badgeChannel` blau, `badgeMod` grün, `chatEventRow` grau). Abgeschaltete Schritte und Ergebnisse der Läufe mit `stateError`, `stateConnecting`, `stateConnected`. Rot durchgestrichen ist schon im Plan 6 entschieden.
- **Frage 3, Prüfmeldung am Feld:** eine Zeile unter dem Abschnitt, immer da, Text aus `EditResult::problem` in `stateError`; danach zeigt das Feld wieder den Wert des Kerns.
- **Gruppe umbenennen/löschen:** Kontextmenü an der Gruppenzeile («Umbenennen», «Löschen») und Doppelklick zum Umbenennen, wie das Dock «Overlays».
- **Gruppennamen:** ohne Rücksicht auf Gross- und Kleinschreibung eindeutig. «shoutout» gegen bestehendes «Shoutout» → «Die Gruppe «Shoutout» gibt es schon.» Ändert `renameActionGroup` (c7) und die Gruppenbildung der Liste; betrifft die Logik, nicht nur das Fenster.
- **Aktion abgeschaltet oder gelöscht während eines Laufs:** laufende Läufe dieser Aktion **sofort abbrechen** (wie «Lauf abbrechen», Töne verstummen, Entscheid Abbruch 16.11.). Abweichend von Lotses Empfehlung; Änderung im Kern.
- **«Warteschlangen verwalten …»:** in e2 ausgegraut sichtbar, Wirkung mit e3.
- **Texte:** alle neuen Texte aus Abschnitt 4 und die Rückfragetexte aus c7 so übernehmen.

## 8. Nachträge Dominik (2026-11-16, im Lauf von e2)

- Plan-Fragen: «Chat-Bot aktiv» aus bricht alle Läufe ab (e2-1); Senden bei «Chat-Bot aus» erlaubt (e2-2); «Anhören» ausgegraut bis e3; Testwerte «Testzuschauer» und «eigener Kanal» (e2-7).
- Verschachtelter Lauf (Aktion ausführen → abgeschaltete Aktion): A läuft fertig, nichts ändern.
- Kopfzeile «stumm»: Schalter mit echtem Stand, ausgegraut (e2-4b); «aus» grau passt.
- Beschriftungen der rechten Spalte aus e2-8 und der Auslöser aus e2-9 so übernommen.
- Zeiten (Pause, Dauer, Dauer weich) in Sekunden mit Komma eingeben («8,5 s»), intern ms; Umrechnung in der Logik (`FieldRow`, e2-16).
- Einblenden/Ausblenden «weich»: in e2 ausgegraut sichtbar bis P14-2 (Flag in `FieldRow`).
- Cheer: Hinweis unter «Bits» «Nur «bis»: genau dieser Wert» aus `FieldSection::hint` (e2-5b).
- «Läufe»: bei «error» die erste Fehlermeldung als Meldung des Laufs («Fehler: …», e2-5b); bei stumm nur «stumm».
- Dauern mit Komma: `durationText` («1,5 s», e2-5b) und `mediaDurationText` im Fenster («2,5 s», e2-11).
- Nach dem Löschen (Aktion, Gruppe) Statuszeile «Gelöscht: <Name>» aus `deletedStatusText` (e2-15).
- Kontextmenü der Gruppe «Umbenennen …», «Löschen …» mit Auslassung wie Dock «Overlays»; Doppelklick auf eine Gruppe nur umbenennen, nicht auf-/zuklappen (e2-14).
- Knöpfe zum Entfernen heissen «… löschen»: «Aktion löschen», «Schritt löschen», «Auslöser löschen», «Element löschen», «Testwert löschen» (e2-15, e2-16).
- Rechte Spalte: ohne Auswahl «Kein Schritt oder Auslöser gewählt»; Art ohne Einstellungen zeigt grau «Keine Einstellungen» aus `emptyFormHint()` (e2-16).
