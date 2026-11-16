# Checkliste P14 Emotes: Emotes als Bilder im Bereich «Chat» (Unterreiter «Live»)

Eingeordnet als **neue Funktion über eine Liste und ihre Bedienung**, also die ganze Phase nach `ui-consistency:finding-patterns`. Die Übergabe `.claude/HANDOFF.md` (Stand 2026-11-08) ist gelesen; zu P14 und Emotes steht dort nichts. Grundlage: Recherche `docs/archiv/recherchen/2026-11-16-emotes.md`, Plan `docs/archiv/prompts/P14-chatbot.md` (Tabelle «Unterreiter «Live»», Zeile 398). Kein Code geschrieben, nichts gebaut.

**Referenz: Bereich «Chat» in `src/app/chatbotpage.cpp`** (`m_chat`, `#chatBotChat`, `ChatRowDelegate` Zeilen 66–163, Aufbau 293–337). Die Arbeit ändert genau diese Liste; der Auftrag nennt sie. Ein Entwurf (Bild) zu Emotes gibt es nicht.

**Art und Familie:** «Liste mit selbst gezeichneten Zeilen» (Delegat statt Text der Zelle). Gezählt in `src/app` mit `dss_common`.
- Kandidaten: 10 Listen mit `QTreeWidget` und Delegat oder fester Zeilenhöhe (`setUniformRowHeights(true)` in 7 Dateien).
- Mitglieder mit eigenem Zeichnen des Inhalts: **1** (`ChatRowDelegate`). `ForegroundDelegate` und `NameColumnDelegate` (`panels.h:70`, `:83`) färben nur Text. Für das Zeichnen ist die Referenz also das einzige Mitglied: Dort ist nichts Konvention, es gilt die Referenz (Stufe 3). Für Liste, Kontextmenü und Statuszeile zählen alle 10 Listen bzw. alle Menüs.

---

## 1. Blockierend: fehlende Daten und Logik (gehört in den Plan)

| Fehlt | Suche und Fundstelle |
|---|---|
| **Format der Twitch-Emotes** (`static`/`animated`) | Der Kern übernimmt je Fragment nur `type`, `text`, `emote_id` (`src/twitch/eventsubprotocol.cpp:176–188`, Kommentar `src/events/eventtypes.h:70`). `emote.format` aus EventSub geht verloren. Ohne das Feld nur statische Twitch-Emotes. |
| **Fragmente in `ChatRow`** | `chatRow()` liest nur `text` (`src/common/chatbotlive.cpp:242`), `ChatRow` hat kein Feld für Abschnitte (`chatbotlive.h:87–98`). |
| **Listen von BTTV, 7TV, FFZ, Erkennung im Text, Laden, Zwischenspeicher, Dekodieren** | Suche `emote|Emote|QNetworkDiskCache|QImageReader` über `src/common`, `src/app`: nur die Fragmente oben; kein Laden, kein Index, kein Bildspeicher. |
| **Eigene Nachrichten** (`twitch.chat_sent`) haben keine Fragmente | `chatbotlive.cpp:246–256`; das Echo `twitch.chat_message` mit derselben `message_id` bringt keine zweite Zeile (`ChatRows::add`, `:291`), seine Fragmente gehen also verloren. Ob das Echo kommt: «nicht belegt» (`chatbotlive.h:105–106`). |
| **WebP** | Am Studio-PC fehlt `libqwebp.so`: In `/usr/lib/x86_64-linux-gnu/qt6/plugins/imageformats/` liegen nur `libqgif`, `libqico`, `libqjpeg`, `libqpdf`, `libqsvg` (gelesen 2026-11-16). BTTV und 7TV liefern nur WebP (Recherche 2, 3). |

---

## 2. Baum der Rollen (Bereich «Chat», nach der Änderung)

```
dockPanel «Chat»                    (addLivePanel, unverändert)
  Liste #chatBotChat                QTreeWidget, ohne Kopf, 2 Spalten (Zeit, Nachricht), setUniformRowHeights(true)
    Zeile                           ChatRowDelegate zeichnet:
      Zeit                          Spalte 0, Standard des Stils (unverändert)
      Abzeichen                     Kästchen badgeBot/badgeChannel/badgeMod, Rundung 4, Innenabstand 4, Abstand 4 (unverändert)
      Name                          fett in der Chatfarbe (unverändert)
      Nachricht                     NEU: Abschnitte Text | Emote; Emote als Bild in fester Höhe,
                                    Zero-Width darüber gelegt; ohne Bild das Wort als Text
    Tooltip über einem Emote        NEU: «<Name> (<Anbieter>)»
    Kontextmenü                     NEU: «Emotes neu laden», «Emotes animieren» (Frage 1)
  Leiste                            buttonBar: Konto, Eingabe │ Zähler, «Senden» (unverändert)
Statuszeile des Hauptfensters       NEU: Ergebnis des Ladens über showStatus()
```

---

## 3. Checkliste

Lesestand 2026-11-16, Stand 4923161 (Branch `lotse/p14-emotes-plan`). Die gemeinsamen Suchen über `src/app`:
- `setItemDelegate|class \w+Delegate|setUniformRowHeights\(true\)` (3 Delegat-Klassen, 10 Listen mit fester Zeilenhöhe in 7 Dateien)
- `setCheckable\(true\)|QMenu menu|menu\.exec|menu->exec|\.popup\(` (6 Kontextmenüs in 5 Dateien, 0 ankreuzbare Menüeinträge)
- `CustomContextMenu|customContextMenuRequested` (8 Treffer in 6 Dateien)

Jede Suche hat die Referenz oder ihre Nachbarn in `chatbotpage.cpp` getroffen (`setUniformRowHeights` `:301`, `setItemDelegate` `:306`).

«Chat» gegen «Liste mit selbst gezeichneten Zeilen» (Referenz `chatbotpage.cpp`, 1 Mitglied mit eigenem Zeichnen von 10 Listen in `src/app`), gelesen 2026-11-16:

- [ ] **Liste:** bleibt `QTreeWidget` `#chatBotChat` mit `setUniformRowHeights(true)`, `setHeaderHidden(true)`, `NoFocus`, Spaltenbreite Zeit `digit * 10`. Keine zweite Liste, kein `QListView` mit eigenem Modell. Entschieden: Referenz (Stufe 3); feste Zeilenhöhe Mehrheit, 10 von 10 Listen in 7 Dateien.
- [ ] **Zeichnen:** nur im vorhandenen `ChatRowDelegate` (Abzeichen, Name, dann die Abschnitte der Nachricht); kein `QLabel`, kein `QMovie`, kein Widget je Zeile oder Emote. Farben nur aus dem Theme und der Chatfarbe, keine neue Farbe. Entschieden: Referenz (Stufe 3, Kommentar `:87–89`), Animation ohne `QMovie` nach Recherche 6 («Animation»).
- [ ] **Text der Zelle:** `chatItem` schreibt weiter die ganze Nachricht als Text der Zelle (Suche, Tooltip, Tests, `:75–77`); die Abschnitte kommen als weitere Rolle dazu (`kChatSegmentsRole`). Entschieden: Referenz.
- [ ] **Emote-Grösse und Zeilenhöhe:** eine feste Emote-Höhe für alle Zeilen; die Zeilenhöhe kommt aus `sizeHint` des Delegaten, gleich für jede Zeile (sonst bricht `setUniformRowHeights`). Breite nach Seitenverhältnis, höchstens 4-mal die Höhe. Abstand zwischen Emote und Text 4 px wie zwischen den Abzeichen (`:134`). Entschieden: Höhe nach Frage 2; Abstand Referenz. Werte: Theme ohne Abstandstabelle; Literale im Theme und Delegaten 2, 3, 4, 6 px (Basis 2). 4 px und 28 px liegen darauf. Die heutige Zeilenhöhe setzt der Code nicht selbst (Stylesheet `QTreeView::item { padding: 3px 2px; border: 1px }` plus Schrift 10 pt): nicht gemessen.
- [ ] **Zero-Width:** über das vorherige Emote gelegt, beide mittig, Breite die grössere der beiden; ohne Emote davor normal gezeichnet. Entschieden: wie verlangt (Auftrag) nach Recherche 5 («Zero-Width»). Keine Konvention im Projekt.
- [ ] **Ohne Bild** (lädt noch, Fehler, kein WebP, ohne Netz): das Wort als Text in `m_theme.text`, genau wie heute. Kein Platzhalter-Kästchen, keine Fehlerfarbe in der Zeile. Sobald das Bild da ist, zeichnet die Liste neu (`viewport()->update()`), die Zeilen entstehen nicht neu. Entschieden: Referenz (heutiges Verhalten) und Regel 9.
- [ ] **Zu lang:** eine Zeile ohne Umbruch wie heute; Text mit `elidedText(…, Qt::ElideRight, …)`; ein Emote, das nicht mehr ganz passt, fällt weg und an seiner Stelle steht «…». Entschieden: Referenz (`:146–147`).
- [ ] **Tooltip über einem Emote:** «<Name> (Twitch|BTTV|7TV|FFZ)», über `helpEvent` des Delegaten; sonst wie heute der Text der Zelle. Entschieden: keine Konvention (Tooltips stehen bisher an Zellen, Knöpfen und Feldern, nie an einem Teil einer Zelle); Vorschlag in Frage 2.
- [ ] **Animation:** ein gemeinsamer Takt für alle sichtbaren animierten Emotes, nur `m_chat->viewport()->update()`; Takt läuft nur, wenn «Live» sichtbar ist, «Emotes animieren» an ist und ein animiertes Emote in der Liste steht. Aus: erstes Bild bzw. statische Variante. Entschieden: wie verlangt (Auftrag, Recherche 6); Standard «an» nach Frage 1.
- [ ] **Kontextmenü der Liste:** `setContextMenuPolicy(Qt::CustomContextMenu)`, `customContextMenuRequested` → `QMenu menu(this); … menu.exec(m_chat->viewport()->mapToGlobal(position))`. Einträge «Emotes neu laden», «Emotes animieren» (ankreuzbar). Entschieden: Mehrheit, 6 von 6 Kontextmenüs in 5 Dateien für den Weg; der Ort (Menü statt Leiste) hängt an Frage 1. Ein ankreuzbarer Menüeintrag hat kein Vorbild (0 von 6), siehe Frage 1.
- [ ] **Rückmeldung:** Ergebnis des Ladens über `showStatus()` in der Statuszeile, Text aus der Logik (`emoteStatusText`), nie im Fenster zusammengesetzt. Fehlt WebP oder ist ein Anbieter nicht erreichbar, steht das dort und zusätzlich einmal im Protokoll (`qWarning`, ohne Kanal-ID). Entschieden: geteilter Baustein `showStatus` (Stufe 4, über 30 Aufrufe in `mainwindow*.cpp`).
- [ ] **Einstellung «Emotes animieren»:** gespeichert in `fenster.ini` (Fenstereinstellung, nicht im Kern), nicht im Dialog «Einstellungen». Entschieden: `settingsdialog.cpp` bleibt unverändert (Entscheid e2, P14-e3-Checkliste Abschnitt 7); `fenster.ini` ist der Ort der Fenstereinstellungen (`mainwindow.cpp:70–74`).
- [ ] **Nicht übernehmen:** das Aussehen von Chatterino oder jChat (Skalierung, Abstände, Hintergrund hinter Emotes); keine Werte aus der Recherche als Abstände; keine `QMovie`; keine Emote-Bilder von Hand in eine Zelle (`setIcon`); keine Emotes in «Ereignisse» (Spalte «Details») und in grauen Ereigniszeilen.

---

## 4. Fragen an den Nutzer (sichtbar oder bedienbar)

1. **Bedienung «neu laden» und «animieren»:**
   - **Empfehlung:** Kontextmenü der Chat-Liste mit «Emotes neu laden» und «Emotes animieren» (ankreuzbar, Standard an). Das Ergebnis des Ladens steht in der Statuszeile. Später kommt «Antworten» (P14-2) in dasselbe Menü.
   - Die andere Möglichkeit: eine zweite Leiste unter der Eingabezeile mit Kästchen «Emotes animieren» und Knopf «Emotes neu laden», wie «Chat-Nachrichten ausblenden» und «Nur Fehler» in den Leisten der anderen Bereiche (2 von 2 Ansichtsschaltern in «Live» sind Kästchen in der Leiste). Kostet eine Zeilenhöhe im Bereich «Chat».
2. **Grösse der Emotes:**
   - **Empfehlung:** Emotes 28 px hoch (Twitch-Grösse 1x, ohne Skalieren scharf). Dafür werden alle Chat-Zeilen gleich etwas höher (geschätzt um 8 bis 10 px, nicht gemessen). Über einem Emote zeigt ein Tooltip Name und Anbieter («Beispiel (7TV)»).
   - Die andere Möglichkeit: Emotes auf Zeilenhöhe verkleinert, die Zeilen bleiben wie heute; Emotes sind dann klein und leicht unscharf.
3. **Neu laden im Takt:** **Empfehlung:** kein Takt. Listen beim Start, bei einem anderen Kanal (Anmeldung) und auf «Emotes neu laden» (höchstens alle 30 s, sonst Hinweis). Die andere Möglichkeit: zusätzlich alle 60 min.
4. **Bedingungen der Anbieter:** Die Bedingungen von BTTV, 7TV und das Developer Agreement von Twitch waren für die Recherche nicht lesbar; FFZ verbietet «downloading … redistributing» ohne Erlaubnis. **Empfehlung:** Bilder und Listen aller vier Anbieter nur im HTTP-Zwischenspeicher (`QNetworkDiskCache`), keine eigene Bildsammlung, nichts davon ins Repo; der Nutzer liest die drei Seiten vor dem Handtest im Browser. Die andere Möglichkeit: FFZ weglassen.

### Neue Texte (zum Lesen durch den Nutzer)

- Kontextmenü: «Emotes neu laden», «Emotes animieren»
- Tooltip: «<Name> (Twitch)», «<Name> (BTTV)», «<Name> (7TV)», «<Name> (FFZ)»
- Statuszeile (aus der Logik, `emoteStatusText`, Stand em-7): «Emotes» steht nur einmal vorne, weitere Teile folgen mit « · »:
  - «Emotes geladen: BTTV 74, 7TV 312, FFZ 15» (Zahlen als Beispiel)
  - «Emotes: 7TV nicht erreichbar, die Wörter bleiben Text»
  - «Emotes: Ohne Netz, Listen vom letzten Mal» bzw. «Emotes: Ohne Netz und ohne gespeicherte Listen»
  - «Emotes: WebP fehlt (Paket qt6-image-formats-plugins), BTTV und 7TV bleiben Text»
  - «Emotes: kein Kanal angemeldet»
  - zusammen zum Beispiel «Emotes geladen: BTTV 56, 7TV 45, FFZ 9 · kein Kanal angemeldet» oder «Emotes geladen: FFZ 9 · WebP fehlt (…), BTTV und 7TV bleiben Text»
  - entfallen: «Emotes erst in 30 s wieder ladbar» (es gibt keinen Knopf zum Neuladen)

---

## 5. Nicht gelesen, Annahmen

- Kein Entwurf zu Emotes; `DESIGN.md` gibt es nicht (P14-e3-Checkliste, Abschnitt 5). Vom Theme gelesen: Suchtreffer zu Schrift, Abständen und Farben in `theme.{h,cpp}`.
- Die Zeilenhöhe heute und nach der Änderung ist nicht gemessen; der Agent misst sie in em-6 (`visualItemRect`) und meldet sie.
- `mainwindow*.cpp` nur über Suchen gelesen. Die Zahl der `showStatus`-Aufrufe stammt aus der P14-e3-Checkliste und einer Zählsuche über `src/app` (54 Treffer für eine gemeinsame Suche, nicht einzeln getrennt).
- Ob ein ankreuzbarer Eintrag im `QMenu` im Theme richtig aussieht (`QMenu::indicator`), ist nicht geprüft; das Theme hat dafür keinen Eintrag (Suche `QMenu` in `theme.cpp`: `QMenu`, `QMenu::item`, kein `::indicator`).

## 6. Kosten

- **Geöffnete Projektdateien:** 13 (`HANDOFF.md`, Recherche, `P14-e3-plan.md`, `P14-e3-checkliste.md`, `chatbotlive.h`, `chatbotlive.cpp` Ausschnitt, `chatbotpage.cpp` 2 Ausschnitte, `eventsubprotocol.cpp` Ausschnitt, `docs/08` 2 Ausschnitte, `configpaths.h`, `mainwindow.cpp` Ausschnitt, `P14-chatbot.md` Ausschnitt, `pakete.txt`, `tests/daten/twitch/chat_message.json`).
- **Suchen:** etwa 22 mit Grep und Glob (gezählt, nicht genau festgehalten).
- **Skill-Dateien:** `finding-patterns`, `words.md`, `values`, `checklist.md`. Keine Subagenten.
