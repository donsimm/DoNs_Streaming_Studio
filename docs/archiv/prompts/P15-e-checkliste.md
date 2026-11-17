# Checkliste P15 e: Reiter «Kameras» (e2, e3)

Ergebnis von Schritt 0 aus Abschnitt e (`P15-kamera.md`), nach `ui-consistency:finding-patterns` und `ui-consistency:design`. Eingeordnet als **neue Seite** (eigener Modul-Reiter mit drei Dialogen), also die ganze Phase. Lesestand 2026-11-17, Branch `lotse/p15` nach d3; nachgeführt in e1b (Entscheide des Nutzers, KI-Verfolgung). Wie bei P14 liegt die Checkliste auf Auftrag im Repo (die Skill legt sonst nichts im Repo ab). Der Code gewinnt: Weicht er ab, gilt er, und die Zeile wird neu geprüft.

**Entwurf (Gestalt):** Variante 1B «Alles sichtbar», gewählt vom Nutzer (Antwort 5 vom 15.11.), aus `lotse/entwurf-kamera` 90e0316: `entwuerfe/kameras/1b-alles-sichtbar.html` (Aufteilung), `4-zustaende.html` (Zustände A bis F), `5-dialoge.html` (Dialoge), `studio.css`. Die Seiten 1A, 2 und 3 (Variante A) habe ich nicht geöffnet; Seite 4 zeigt Variante A, gilt laut Legende für B «in der jeweiligen Spalte». Der Entwurf gibt nur Rollen, Reihenfolge und Inhalt. Wie jede Rolle geschrieben wird, kommt aus der Familie, die Werte aus dem Theme (`theme.h`, `theme.cpp`).

**Referenz:** Reiter «Chat-Bot» (`chatbotpage.cpp`): Er ist der einzige Modul-Reiter neben «Overlays», hat eine Kopfzeile mit Zustand rechts (`#chatBotState`) und Bereiche über `dockPanel()`. Für Bereiche mit Feldern und Bildlauf zählt das Dock «Eigenschaften» (`docks.cpp:140–260`) mit, denn Plan 8 verlangt es («Bildlaufleiste wie das Dock «Eigenschaften»», «Regler … wie «Lautstärke»»). Für die Dialoge zählen `settingsdialog`, `obssourcedialog` und `queuesdialog`.

**Art und Familie:** «Bereich wie ein Dock» (Name des Projekts, `panels.h:60`, `#chatBotPanelTitle`), gezählt in `src/app`. Die Familie ist dieselbe wie in `P14-e3-checkliste.md` (9 Mitglieder) und um die Seiten aus P14 e3 gewachsen (`listdetailpage`, `commandspage`, `timerspage`, `variablespage`). Die Dialoge bilden eine eigene Familie mit 3 Mitgliedern. Die Zählungen aus der e3-Checkliste habe ich nicht neu ausgezählt, nur über die Suchen unten bestätigt.

---

## 1. Blockierend oder offen in der Logik (nicht Teil von e1)

| Fehlt | Suche, Fundstelle |
|---|---|
| **`camera_call` senden** im `CoreClient` und Antwort `camera_result` als Signal | `coreclient.cpp:624–638` verarbeitet nur `camera_service` und `camera_event`. Die Ansichtslogik baut die Anfragen schon (`cameraview.h`, `CameraCall` ohne `requestId`). Gehört an den Anfang von e2. |
| **Ganze Kamera-Presets nach dem Start des Fensters** | `camera_service` bringt nur `id` und `name` (`docs/09`, «Status»). Das Ereignis `presets` kommt nur bei Änderungen. Bis dahin zeigt «Positionen» die Presets bei jeder Kamera mit Position «–» (`cameraPresetRows`). Für e2 offen: `GET camera-presets/{id}` je Preset oder `GET export` beim Verbinden. |
| ~~**KI-Verfolgung**~~ | Erledigt in e1b: Der Dienst hat den Wert `obsbot_ai` («Aus», «Oberkörper»), `cameraControlView` liest ihn (`aiEnabled`, `aiValue`, `aiMenu`, `aiHint`), geschaltet wird mit `cameraAiCall`. Entscheid 5 in Abschnitt 4. |
| **«Spiegeln» (Entwurf, Bereich «OBSBOT»)** | Gibt es im Dienst nicht. Erscheint nicht (Plan 2.5: «nicht freigeschaltet … erscheinen nicht»). |
| **Hinweis «Nur digital: Schwenken und Neigen ±10°» (Entwurf 4 C)** | Ein Merkmal «digital» gibt es im Status nicht (`ptz` ist bei beiden Kameras `true`). In e1 nicht gebaut; ein Text aus `limits` von `GET …/config` wäre möglich, wenn der Nutzer ihn will. |

---

## 2. Baum der Rollen (Entwurf 1B mit den Bausteinen des Projekts)

```
Reiter «Kameras»            QWidget im QStackedWidget m_modules (wie ChatBotPage), Hintergrund @window@
  Kopfzeile                 QWidget, feste Höhe (wie #chatBotHeader), QHBoxLayout
    «Kamera»                QLabel + QComboBox (Einträge cameraChoiceText, Kennung als Daten)
    Wecken/Schlafen         QPushButton, feste Breite für «Schlafen» (cameraControlView.wakeText)
    «Einrichten …»          QPushButton → Dialog «Kamera einrichten»
    Dehnung
    Zustand des Dienstes    zwei QLabel übereinander, Platz für zwei Zeilen immer da (text, detail),
                            Farbe über [state] wie #chatBotState (cameraServiceView); e4: Raster mit
                            zwei Zeilen, der Zustand bekommt seine ganze Breite
    Hinweis gesperrt        e4: #hintLabel links in der zweiten Zeile der Kopfzeile (rot über state),
                            Text blockedHint; keine eigene Zeile über den Spalten mehr
  Spalten                   QSplitter waagrecht, setChildrenCollapsible(false)
    links                   QScrollArea (#cameraLeftScroll, wie #propertiesScroll), darin QWidget
                            mit QVBoxLayout aus drei dockPanel
      dockPanel «Bewegen»   Steuerkreuz 3 × 3 oben (8 Pfeile, «Mitte»), rechts davon Zoom «−»/Anzeige/
                            «+», Zahlenfelder «Tempo», «Schritt», darunter Hinweis (moveHint, zwei
                            Zeilen) mit «Wecken» bei moveHintWake (e4, Entscheid 10)
      dockPanel «KI-Verfolgung»  eine Zeile: Modus (QComboBox aus aiMenu, Wert aiValue, frei bei
                            aiEnabled, Wahl → cameraAiCall), rechts der Hinweis aiHint (e4); kein Tempo
      dockPanel «Positionen» QTreeWidget mit Spaltenkopf (Name, Position, Bildwerte), darunter
                            buttonBar mit «Abrufen», «Aktuelle Position speichern …», «Überschreiben»,
                            «Umbenennen», «Löschen», rechts Hinweis recallHint
    Mitte                   dockPanel «Bildwerte · <Kamera>» (cameraImageTitle)
      QScrollArea           wie #propertiesScroll, darin je Bereich section() mit «Zurücksetzen» in der
                            Titelzeile; je Wert eine Zeile aus cameraControlRows (Regler + Wert,
                            Auswahl, Kästchen, Text); Hinweis cameraSectionHint je Bereich rechts in
                            der Titelzeile (Text-Parameter von section(), e3)
    rechts                  dockPanel «Licht», darin QScrollArea (#cameraLightScroll) wie links
      Leiste oben           «Alle ein», «Alle aus», Dehnung, «Lampen …»
      je Lampe section()    Titel = Name, rechts in der Titelzeile «erreichbar»/«nicht erreichbar»;
                            «Ein» (QCheckBox), Helligkeit und Farbtemperatur (Regler + Wert)
      Licht-Presets         section «Licht-Presets», QTreeWidget (Name, je Lampe eine Spalte),
                            buttonBar «Abrufen», «Aktuelles Licht speichern …», «Überschreiben»,
                            «Umbenennen», «Löschen»
Dialog «Kamera einrichten – <Name>»   QDialog: Name; Grenzen (Schwenken, Neigen von/bis in °);
                            Richtung (2 Kästchen); Bewegen (Tempo, Schritt); Verhalten (1 Kästchen
                            «KI-Verfolgung vor dem Bewegen aus», Entscheid 6); Gemerkte Werte
                            (Tabelle, «Vergessen», Hinweis); OK/Abbrechen
Dialog «Position speichern» QDialog: Name, «Bildwerte mitspeichern» (aus), Hinweis; OK/Abbrechen
Dialog «Lampen»             QDialog: Tabelle Name/Adresse/Zustand; Name, Adresse; «Hinzufügen»,
                            «Lampe löschen», «Verbindung prüfen» mit Ergebnis; Hinweis; OK/Abbrechen
```

Zustände aus Entwurf Seite 4 (alle in `cameraControlView`, `cameraControlRows`, `lightRows`): A schläft (Kreuz und Zoom grau, Hinweis gelb mit «Wecken», Abrufen geht mit «weckt die Kamera zuerst»), B fehlt (alles zur Kamera grau, grauer Hinweis), C C922 (Wecken/Schlafen grau mit Tooltip, KI grau mit «Diese Kamera hat das nicht.»; bei der Tiny 2 ist die KI nur wach bedienbar, sonst grau mit «Nur bei wacher Kamera»), D C922 Bild («OBSBOT» grau mit Hinweis), E Dienst weg (alles grau, Zustand rot in zwei Zeilen, Hinweis über den Spalten), F Lampe weg (nur diese Lampe grau, «nicht erreichbar» rot).

---

## 3. Checkliste

Die gemeinsamen Suchen über `src/app/*.cpp` (2026-11-17):
- `dockPanel\(|buttonBar\(|section\(QStringLiteral|symbolButton\(QStringLiteral|new QTreeWidget|QDialogButtonBox::Ok|confirm\(this|confirm\(window|new QSplitter|keepValueVisible\(`: 116 Treffer in 16 Dateien
- `new QSlider|new QScrollArea|setAutoRepeat`: 1 Regler (`docks.cpp:244`), 4 Bildlaufbereiche (`docks.cpp:150`, `actionfieldspanel.cpp:87`, `listdetailpage.cpp:124`, `actionspage.cpp:227`), 0 `setAutoRepeat`
- `chatBotState|hintLabel|fieldProblem|chatBotPanelTitle` in `theme.cpp`

Jede Suche hat die Referenz getroffen (`chatbotpage.cpp`: `dockPanel`, `buttonBar`, `new QSplitter`, `new QTreeWidget`; `docks.cpp:244` der Regler).

### Reiter und Kopfzeile
Reiter «Kameras» gegen «Bereich wie ein Dock», Referenz `chatbotpage`, Familie wie in P14 e3:

- [ ] **Modul-Reiter:** dritter Knopf `moduleButton("Kameras")` in `HeaderBar` nach «Chat-Bot», eigener Wert im Aufzählungstyp neben `Overlays` und `ChatBot`. Die Seite kommt als drittes Widget in `m_modules` (`mainwindow.cpp:213–224`). Die Docks des Editors bleiben nur bei «Overlays» (`onModuleSelected`). Entschieden: wie verlangt (Antwort 1 des Nutzers), der Weg ist der Referenz (1 von 1 weiterer Reiter).
- [ ] **Seite:** eigene Klasse `CameraPage` in `src/app/camerapage.{h,cpp}` wie `ChatBotPage` (Objektname, Hintergrund `@window@` wie `#chatBotPage`). Ohne Unterreiter. Entschieden: Referenz; die Klasse ist neu, Zustimmung des Nutzers (Entscheid 1).
- [ ] **Kopfzeile:** `QWidget` mit fester Höhe (`QSizePolicy::Fixed`), `QHBoxLayout`, Abstand 6. Ein neuer Zustand ändert nur Texte, nie die Anordnung (Regel 9). Entschieden: Referenz (`chatbotpage.cpp:563–592`).
- [ ] **Kamera-Auswahl:** `QLabel` «Kamera» und `QComboBox` mit `cameraChoiceText`, Kennung in den Daten des Eintrags, `setAccessibleName("Kamera")`. Nach einer neuen Liste die Wahl aus `cameraChoiceAfterUpdate`. Ein Eintrag mit Zustand als Text, ohne eigene Farbe (der Entwurf färbt «wach», «schläft», «fehlt»; Entscheid 3). Entschieden: wie verlangt (Entwurf), `QComboBox` wie im Dock «Steuerung» und in der Leiste «Chat».
- [ ] **«Wecken»/«Schlafen» und «Einrichten …»:** `QPushButton`. Die Breite wird einmal für den längeren Text gesetzt (wie `#chatBotChatCounter` mit `setMinimumWidth(sizeHint)`). Freigabe, Text und Tooltip nur aus `cameraControlView`. Entschieden: wie verlangt (Plan 8 «feste Breite»), der Weg der festen Breite folgt der Referenz (`chatbotpage.cpp:714–719`).
- [ ] **Zustand des Dienstes:** zwei `QLabel` rechts (`cameraServiceView.text` fett, `detail` darunter), Höhe für zwei Zeilen immer reserviert (`fontMetrics().lineSpacing() * 2`, wie `settingsdialog.cpp:123`). Farbe über die Eigenschaft `state` mit neuen Selektoren neben `#chatBotState` in `theme.cpp:320–323` (ok `@stateConnected@`, warning `@stateConnecting@`, error `@stateError@`, sonst `@textMuted@`), danach `unpolish`/`polish` wie `headerbar.cpp:244–249`. Entschieden: Referenz für die Farben (1 von 1 Zustand in einer Modul-Kopfzeile). Die zwei Zeilen sind wie verlangt (Plan 8 «wie die Zeile «OBS»»). Die Werte `core-connected`/`core-disconnected` des Entwurfs werden nicht übernommen.

### Linke Spalte
- [ ] **Spalten:** `QSplitter` waagrecht, `setChildrenCollapsible(false)`, links und rechts mit Mindestbreite, die Mitte dehnt sich. Entschieden: Mehrheit, 2 von 2 Teilern waagrecht (`chatBotLiveSplitter`, `actionsSplitter`; dazu die Seiten aus e3). Drei Spalten wie verlangt (Antwort 5).
- [ ] **Bereiche:** `dockPanel("Bewegen" | "KI-Verfolgung" | "Positionen", …)`, fest ohne Lösen und Schliessen. Entschieden: geteilter Baustein (`dockPanel` in `chatbotpage.cpp`, `listdetailpage.cpp`, `variablespage.cpp`, `actionspage.cpp`).
- [ ] **Steuerkreuz:** 9 Knöpfe im `QGridLayout`, 8 mit Symbol (`nach-oben.svg`, `nach-unten.svg`, neu `nach-links.svg`, `nach-rechts.svg` und vier Schrägen, gedrehte Fassungen von `nach-oben.svg`, Plan 8). Ohne Datei steht der Ersatztext «←» usw. da, wie bei `symbolButton`. In der Mitte «Mitte» (`cameraHomeCall`). Drücken und Loslassen gehen an `CameraHold` (press, tick über einen `QTimer` mit `nextDueMs`, release), nicht über `setAutoRepeat`, denn ein Klick ist ein Schritt und Halten eine Fahrt. Pfeiltasten tun dasselbe, wenn das Kreuz den Fokus hat. Freigabe aus `moveEnabled`. Entschieden: wie verlangt (Plan 8). Das ist eine Stelle ohne Mitglied: 0 Treffer für `setAutoRepeat`, `symbolButton` ist mit 14 px zu klein. Ein neuer Baustein, Entscheid 1.
- [ ] **Zoom, Tempo, Schritt:** Zoom «−»/«+» wie das Kreuz (`CameraHold` mit `zoom`), dazwischen die Anzeige `cameraZoomText`, Mindestbreite für «100 %». «Tempo» und «Schritt» als `QSpinBox`/`QDoubleSpinBox` mit Suffix `" %"` bzw. `" °"`, `keepValueVisible`, `setKeyboardTracking(false)`. Entschieden: geteilter Baustein `keepValueVisible`, Suffix mit Leerzeichen als Mehrheit (6 von 6 laut e3-Checkliste, bestätigt durch `setSuffix` in `docks.cpp:201`). Die Felder ändern die Einstellungen der Kamera (`speed`, `step_deg` über `PUT cameras/<id>/config`, wie der Dialog «Kamera einrichten»), Einheit je Kamera (Entscheid 2).
- [ ] **Hinweise in «Bewegen» und «KI-Verfolgung»:** ein `QLabel` immer da, Text aus `moveHint`/`aiHint`, Objektname `hintLabel` wie die Familie (e4, statt eigener Selektoren). Farbe beim Hinweis in «Bewegen» über `state` (warning/leer) mit `#hintLabel[state="warning"]`, der KI-Hinweis grau. In «Bewegen» steht der Hinweis rechts neben dem Kreuz unter «Schritt», zwei Zeilen hoch mit Umbruch, «Wecken» als `QPushButton` rechts daneben (sichtbar nur bei `moveHintWake`, Breite und Höhe bleiben reserviert; Entscheid 10). In «KI-Verfolgung» steht er in derselben Zeile rechts der Auswahl. Kein farbiger Kasten (Entscheid 3). Entschieden: `#hintLabel` (Theme `@textMuted@`) und die Farben der Kopfzeile, weil das Theme keinen Kasten kennt.
- [ ] **KI-Verfolgung:** `QLabel` «Modus» und `QComboBox` mit den Einträgen aus `aiMenu` (Wert in den Daten), gewählt `aiValue`, frei bei `aiEnabled`, `setAccessibleName("KI-Verfolgung")`. Eine Wahl durch den Nutzer schickt `cameraAiCall`; ein neues Ereignis `controls` setzt nur den Wert (ohne Signal, wie die Bildwerte). Kein Regler «Tempo». Entschieden: wie verlangt (Entscheid 5), `QComboBox` wie die Kamera-Auswahl.
- [ ] **Positionen:** `QTreeWidget` mit Spaltenkopf (Name, Position, Bildwerte aus `cameraPresetRows`), `setRootIsDecorated(false)`, `setUniformRowHeights(true)`, `SingleSelection`, Spaltenbreiten in Ziffern der Schrift, letzte Spalte gedehnt. Der Objektname kommt in die Selektorliste `theme.cpp:350–361`. Doppelklick = Abrufen (Plan 8; nicht umbenennen wie in P14, das ist eine andere Liste). Entschieden: Mehrheit der Listen mit Spalten (e3-Checkliste, 4 von 4, seither 8 in der Selektorliste).
- [ ] **Leiste unter «Positionen»:** `buttonBar({Abrufen, Speichern …, Überschreiben, Umbenennen, Löschen}, {recallHint})` mit Textknöpfen (`QPushButton`), Freigaben aus `cameraControlView`. Löschen fragt mit `confirm()`. Nach dem Speichern bzw. Löschen erscheint in der Statuszeile `showStatus` mit `savedStatusText`/`deletedStatusText`. Entschieden: geteilter Baustein `buttonBar`; Textknöpfe in einer Leiste wie «Läufe der Aktionen» (`chatbotpage.cpp:677`). Fünf Knöpfe: Umbruch auf zwei Zeilen wie im Entwurf (Entscheid 1).

### Mitte «Bildwerte»
- [ ] **Bildlauf:** `QScrollArea` mit `setWidgetResizable(true)`, `NoFrame`, ohne waagrechte Leiste, Objektnamen in `#propertiesScroll, #propertiesForm` (`theme.cpp:344`) ergänzt. Entschieden: Mehrheit, 4 von 4 Bildlaufbereiche gleich geschrieben.
- [ ] **Bereiche:** `section(title, …, &grid, {Zurücksetzen})` für die 6 Bereiche aus `cameraControlSections()`, `symbolButton("zuruecksetzen.svg", "Z", "<Bereich> zurücksetzen")`, frei nur bei `cameraSectionResettable`; ein Klick schickt `cameraSectionResetCall`. Entschieden: geteilter Baustein `section` mit Titelknöpfen (`docks.cpp:189–190`).
- [ ] **Zeilen fest (Regel 9):** Alle Zeilen aus `cameraControlSpecs()` entstehen einmal. Ein Ereignis setzt nur Wert, Freigabe, Tooltip und Marke (`cameraControlRows`), nie die Anordnung. Was die Kamera nicht hat, ist grau mit «–», ein Regler steht dann ganz links (Bereich 0–0), nie in der Stellung einer anderen Kamera (e4, Entscheid 11). Entschieden: wie verlangt (Plan 8).
- [ ] **Regler:** `QSlider` waagrecht, Wertanzeige `QLabel` rechts, rechtsbündig, Mindestbreite für den längsten Wert (`valueText` von `maximum`, bei der Temperatur «6500 K»), Raster wie «Lautstärke» (Spalte 1 dehnbar). Werte gehen über `CameraBundler` (höchstens alle 100 ms, beim Loslassen `flush`). Entschieden: Referenz für diese Rolle ist das Dock «Eigenschaften» (`docks.cpp:244–257`, 1 von 1 Regler).
- [ ] **Auswahl, Kästchen, Text:** `QComboBox` aus `menu`, `QCheckBox` über die Spalten 1–2 (wie `docks.cpp:236–238`), «Auto-Schlaf» als `QLabel` mit `valueText`. Entschieden: Referenz «Eigenschaften».
- [ ] **Marke «gemerkt»:** Beschriftung mit der Eigenschaft `remembered`. Sie ist ein «•» vor dem Namen in `@primaryLighter@` (Entscheid 3). Das Kontextmenü «Zurücksetzen» schickt `{key: null}`. Entschieden: wie verlangt (Plan 8). Das ist eine Stelle ohne Mitglied.

### Rechte Spalte «Licht»
- [ ] **Bereich:** `dockPanel("Licht", …)`. Oben eine Leiste ohne Rahmen mit «Alle ein», «Alle aus» (`lightsAllCall`), rechts «Lampen …». Entschieden: wie verlangt (Entwurf 1B). Die Leiste folgt `buttonBar` (links/rechts).
- [ ] **Je Lampe:** `section(name, …)`. Rechts in der Titelzeile `stateText`, gefärbt über `stateKey` wie die Kopfzeile (das ist neu, denn `section` nimmt nur Knöpfe; Entscheid 1). Darin «Ein» (`QCheckBox`), «Helligkeit» und «Farbtemperatur» als Regler wie in der Mitte (3–100 %, 2900–7000 K in Schritten von 50 K, Umrechnung `lightMiredFromKelvin`). Gesendet wird über `CameraBundler` an `lights/<id>`. Die Bereiche entstehen nur neu, wenn sich `lightLayoutKey` ändert, also nach dem Dialog «Lampen» (Plan 8). Entschieden: wie verlangt (Plan 8), die Bausteine sind geteilt.
- [ ] **Licht-Presets:** `section("Licht-Presets")`, `QTreeWidget` mit Spaltenkopf (Name, je Lampe eine Spalte aus `lightPresetRows`), darunter `buttonBar` mit fünf Textknöpfen wie bei «Positionen». Entschieden: wie «Positionen».

### Dialoge
Dialoge gegen «Dialog» (`QDialog`), Referenz `queuesdialog` (der neueste), 3 Mitglieder:

- [ ] **Rahmen:** je eine eigene Klasse `CameraSetupDialog`, `PresetNameDialog`, `LightsDialog` in `src/app`. Fenstertitel «Kamera einrichten – <Name>», «Position speichern» (bzw. «Licht speichern»), «Lampen». Entschieden: Mehrheit, 2 von 3 als eigene Klasse (e3-Checkliste; `queuesdialog` seither die dritte).
- [ ] **Abschnitte im Dialog:** Titel und Linie wie im Dialog «Einstellungen» (`settingsdialog.cpp:49–57`, `#propertySectionTitle`/`#propertySectionLine`) für «Grenzen», «Richtung», «Bewegen», «Verhalten», «Gemerkte Werte». Entschieden: Referenz `settingsdialog`, 1 von 1 Dialog mit Abschnitten.
- [ ] **Knopfzeile:** `QDialogButtonBox` mit OK/Abbrechen (Entwurf Seite 5). Die Meldung des Dienstes steht im Dialog in einer Zeile mit fester Höhe wie `#settingsProblem` (`settingsdialog.cpp:174`). Entschieden: Mehrheit, 3 von 3 `QDialogButtonBox`; OK/Abbrechen wie «Einstellungen», weil der Entwurf es so zeigt.
- [ ] **Felder:** Grenzen als `QDoubleSpinBox` mit Suffix `" °"`, Bereich aus `ranges` von `GET …/config`. «Standard» ist der Sonderwert des Felds (`setSpecialValueText("Standard")` am kleinsten Wert, schickt `null`; entschieden in e2). «Verhalten» ist bei der C922 grau. Die gemerkten Werte stehen als `QTreeWidget` wie oben, mit «Vergessen» und `#hintLabel`. Entschieden: geteilte Bausteine.
- [ ] **Lampen:** Tabelle wie oben, Name und Adresse direkt änderbar in `QLineEdit` unter der Tabelle (Entwurf 8), «Hinzufügen», «Lampe löschen», «Verbindung prüfen» als `QPushButton`. Das Ergebnis steht daneben (`#lightsCheckResult`, grau bzw. mit der Farbe von `state` wie die Kopfzeile). Änderungen gelten mit OK (Anfragen nacheinander, `lightsDialogCalls`). Seit e4 liest der Dialog die Adressen aus `host` in `status` (Entscheid Lotse, statt `GET export`), und «Verbindung prüfen» fragt sofort die Adresse im Feld mit `POST lights/check`, ohne zu schalten, auch vor OK (Entscheid 13). Entschieden: wie verlangt (Entwurf 5); «Lampe löschen» statt «Entfernen» wie «… löschen» in P14 (Entscheid 4).

### Nicht übernehmen
- [ ] Die Werte aus `studio.css`: Grössen 56/64 px des Kreuzes (die Grösse kommt aus der Schrift: `fontMetrics().height() * n`), farbige Kästen `.hinweis` (#3a3220 …), Farbverlauf des Kelvin-Reglers, Farben `.zust` (`core-connected` …), `.btn.fest` 96 px, die Breiten der Spalten (600/540 px), die gelben Markierungen. Auch nicht «Spiegeln» und einen eigenen Bereich «Zustand» (der Zustand steht in der Auswahl). Die Kopfzeile wird nicht von Hand nachgebaut: Sie ist eine eigene Zeile der Seite wie `#chatBotHeader`.

---

## 4. Entscheide des Nutzers (2026-11-17)

In der Sitzung mit Lotse beantwortet; sie ersetzen die Fragen aus e1.

1. **Neue Bausteine: ja**, alle nur in `src/app`:
   - Seite `CameraPage` (`camerapage.{h,cpp}`) und die Dialoge `CameraSetupDialog`, `PresetNameDialog` (auch für «Aktuelles Licht speichern …», ohne Kästchen), `LightsDialog`.
   - Ein grosser Richtungsknopf für Kreuz und Zoom (Symbol und Ersatztext wie `symbolButton`, Grösse aus der Schrift), mit 6 neuen Symbolen (gedrehte Fassungen von `nach-oben.svg`).
   - Eine Reglerzeile «Regler + Wert fester Breite» als Hilfe in `camerapage.cpp`; sie kommt etwa 13-mal vor, die «Lautstärke» bleibt unverändert.
   - `section()` mit einem Text rechts in der Titelzeile («erreichbar») als neuer Parameter in `panels.{h,cpp}`. Betroffen ist nur der neue Aufruf.

   Leisten mit fünf Knöpfen («Positionen», «Licht-Presets»): **Umbruch auf zwei Zeilen** wie im Entwurf.
2. **«Tempo» und «Schritt» im Bereich «Bewegen»** ändern die Einstellungen der Kamera (`speed`, `step_deg`, dieselben Werte wie im Dialog «Kamera einrichten»), mit der Einheit je Kamera: Tiny 2 in °/s (1–100, Standard 10), C922 in % (Standard 50).
3. **Farben:** Hinweise als Text ohne Kasten in den Farben der Kopfzeile (`@stateConnecting@`, `@stateError@`, `@textMuted@`). Die Marke «•» vor gemerkten Werten in `@primaryLighter@`. Die Kamera-Auswahl ohne Farben. Keine neuen Theme-Werte.
4. **Dialog «Lampen»:** «Lampe löschen» statt «Entfernen».
5. **KI-Verfolgung** ist bei der Tiny 2 bedienbar, Auswahl nur «Aus» und «Oberkörper» (Wert `obsbot_ai` des Dienstes, e1b). Weitere Modi erst nach einem Test am Gerät. Kein Regler «Tempo» für die KI (nicht geprüft). Bei der C922 ausgegraut mit «Diese Kamera hat das nicht.».

Nach e2 (Entscheide des Nutzers 2026-11-17, umgesetzt in e3):

6. **Dialog «Kamera einrichten»:** Das Kästchen «Presets wecken die Kamera» entfällt. Abrufen weckt die Tiny 2 immer; im Reiter bleibt der Hinweis «weckt die Kamera zuerst».
7. **Texte wie im Reiter:** überall «Schritt» und «Positionen», auch im Dialog (statt «Schrittweite» und «Presets»).
8. **Linke Spalte:** Aussehen bestätigt («passt so»).

**Entscheid Lotse (Interna, e3):** Lehnt der Dienst eine Fahrt beim Halten ab, erscheint die Meldung in der Statuszeile nur einmal je Halten, nicht bei jeder Wiederholung (`CameraHold::serial`, `reportFailure`, Test `holdFailureOncePerHold`).

Nach der UI-Prüfung von e2/e3 (Entscheide des Nutzers 2026-11-17, umgesetzt in e4):

9. **Dienst nicht erreichbar:** wie Entwurf 4 E. Die Auswahl zeigt nur den Namen der Kamera (ohne «wach»), Lampen (Zustand und Werte) und Zoom «–», bis der Dienst wieder antwortet (`cameraChoiceText` mit `serviceUsable`, `CameraControlView::zoomText`, `lightRows`).
10. **Bewegen:** Hinweis («Kamera schläft» usw.) und «Wecken» rechts neben dem Kreuz in der freien Fläche unter «Tempo» und «Schritt»; das Kreuz rückt nach oben. Der Platz bleibt reserviert. «KI-Verfolgung» ebenso knapp: Hinweis in der Zeile der Auswahl.
11. **Gesperrte Regler** ohne Wert: grau ganz links, daneben «–»; nie die Stellung einer anderen Kamera.
12. **Dialog «Kamera einrichten»:** «Tempo» und «Schritt» so breit wie die Grenzen (alle sechs Felder so breit wie das breiteste Grenzfeld, aus der Schrift), Spaltenkopf «Gemerkt». Minuszeichen in den Grenzfeldern wie sonst im Projekt (siehe Bericht e4: die Zahlenfelder des Projekts schreiben das Minus von Qt).
13. **«Verbindung prüfen»** fragt sofort: `POST lights/check` mit `host` liest die Lampe, ohne sie zu schalten, auch unter einer noch nicht gespeicherten Adresse (`docs/09-kamera-dienst.md`, «Lampen»).
14. **«Diese Kamera hat das nicht.»** bleibt in der Titelzeile der Bereiche (bestätigt).

**Entscheide Lotse (e4):** Der Zustand des Dienstes rechts in der Kopfzeile bekommt die Breite, die er braucht; der Hinweis «… gesperrt …» steht links in der zweiten Zeile der Kopfzeile. Adressen der Lampen als `host` in `status`. Graue Hinweise über `#hintLabel`, gelb und rot über `#hintLabel[state=…]`; `setStyleState` gemeinsam in `panels.{h,cpp}`. Meldungszeile der Dialoge «Kamera einrichten» und «Lampen» drei Zeilen hoch wie `#settingsProblem`. Zugänglichkeitsnamen der Lampenregler mit Lampenname («Helligkeit Key Light links»).

### Neue Texte (zum Lesen durch den Nutzer; die aus der Logik stehen in `cameraview.cpp`)
- Kopfzeile:
  - «Kamera», «Wecken», «Schlafen», «Einrichten …»
  - «Kamera-Dienst: verbunden», «Kamera-Dienst: Verbindungsversuch läuft», «Kamera-Dienst nicht erreichbar:» mit der Meldung des Kerns in der zweiten Zeile
  - «Kamera-Dienst: aus» / «Einschalten im Dialog «Einstellungen»»
  - «Kamera-Dienst: in dieser Version nicht eingebaut», «Kamera-Dienst: –»
  - Zustände «wach», «schläft», «Privatsphäre», «fehlt», «unbekannt»
- Hinweise:
  - «Steuerung, Bild und Licht sind gesperrt, bis der Dienst wieder antwortet.» (der zweite Satz des Entwurfs fällt weg)
  - «Kamera schläft», «Kamera im Modus «Privatsphäre»», «Zustand der Kamera unbekannt»
  - «Kamera nicht gefunden. Sie erscheint wieder, sobald sie eingesteckt ist.», «Der Dienst meldet keine Kamera.»
  - «Diese Kamera hat das nicht.», «Nur bei wacher Kamera» (auch als Hinweis in «KI-Verfolgung»)
  - KI-Verfolgung: «Aus», «Oberkörper» (vom Dienst), bei einem nicht geprüften Modus «Unbekannt (m/u)»
  - «weckt die Kamera zuerst»
  - Tooltips «Bei eingeschalteter Automatik nicht einstellbar», «Nur bei wacher Kamera»
- Bereiche:
  - «Bewegen», «KI-Verfolgung», «Positionen», «Bildwerte · <Kamera>», «Licht», «Licht-Presets»
  - Bildwerte: «Bild», «Belichtung», «Fokus», «Weissabgleich», «Weitere», «OBSBOT»
- Werte:
  - Namen in `cameraControlSpecs`: «Modus», «Temperatur», «Automatisch», «Auto-Schlaf» …
  - Anzeigen «ein», «aus», «nie», «120 s», «4600 K»
  - Spalten «Name», «Position», «Bildwerte» mit «✔»/«–»
- Licht: «erreichbar», «nicht erreichbar», «Alle ein», «Alle aus», «Lampen …», Zellen «ein · 40 % · 4500 K»

---

## 5. Nicht gelesen, Annahmen, Kosten

- Nicht geöffnet: `1a-…`, `2-bild.html`, `3-licht.html`, `index.html`, die Fotos bei Lotse. `docks.cpp` habe ich nur in Zeilen 140–270 gelesen, `theme.cpp` in 300–388, `chatbotpage.cpp` in 560–790, `headerbar.cpp` in 140–250.
- `DESIGN.md` gibt es nicht. Die Werte des Entwurfs habe ich den Theme-Einträgen nach ihrer Rolle zugeordnet. Die Werte, die das Theme nicht hat, kommen nicht dazu (Entscheid 3).
- ~~**[OFFEN] Grenzen im Dialog:**~~ Erledigt in e2: «Standard» ist der Sonderwert `setSpecialValueText("Standard")` am kleinsten Wert des Felds.
- **[OFFEN] Schlüssel der Bildwerte:** Die feste Liste nutzt die Schlüssel des Dienstes (Name des Treibers, `controls.control_key`). `exposure_dynamic_framerate`, `backlight_compensation`, `contrast`, `saturation`, `sharpness`, `gain`, `gamma` und `hue` sind aus den Kernel-Namen abgeleitet und **nicht an den echten Kameras geprüft**. Die Attrappe kennt nur einen Teil davon. Lotse kann das mit `dss-cli kamera-aufruf GET cameras/<id>/controls` am Dienst prüfen.
- **Kosten:** etwa 22 geöffnete Dateien bzw. Ausschnitte im Projekt und 4 Seiten des Entwurfs, etwa 16 Suchen (geschätzt), Skill-Dateien: `finding-patterns`, `words.md`, `checklist.md`, `design`, `design/reading.md`. Keine Subagenten. `values`, `conventions` und `decisions` habe ich nicht neu geöffnet; ihre Regeln stammen aus der e3-Checkliste.
