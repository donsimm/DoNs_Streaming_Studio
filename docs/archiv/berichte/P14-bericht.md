# P14 – Bericht Chat-Bot, Abschnitt f «Mitgeliefert»

Stand: 2026-11-16. Plan: `docs/archiv/prompts/P14-chatbot.md`, Abschnitte 7 und 8. Branch `lotse/p14-emotes`, zwei Commits (f1 Aktionen, f2 Overlay). Dieser Bericht deckt nur Abschnitt f ab; die Berichte der übrigen Abschnitte von P14-1 kamen als Rückmeldung an Lotse und liegen nicht im Repo. Prüfstufe je Commit «Klein»; die Stufe «Baustein» (frischer Klon, Start- und Dienstprüfung) steht für P14-1 noch aus.

## Geändert

**f1, mitgelieferte Aktionen** (eigener Commit): Vorlage `src/actions/vorlage/aktionen.json` (18 Aktionen nach der Tabelle in Plan 7, Hauptschalter aus) als Qt-Ressource, `src/actions/actiontemplate.h/.cpp` (Vorlage, Zurücksetzen, Wiederherstellen), Anlegen im Kern bei fehlender `aktionen.json` (`src/core/actionservice.cpp`), «Zurücksetzen» im Editor, `tst_mitgeliefert`.

**f2, Overlay «Chat-Bot»:**
- `src/common/chatbotoverlay.h/.cpp` (neu): Overlay mit den Kennungen der Vorlage, 1920 × 1080, Ordner «Chat-Bot», 9 unsichtbare Elemente; Anlegen nur, wenn weder das Overlay noch eine Datei mit seiner Kennung da ist.
- `src/common/overlaystore.h/.cpp`: `addOverlay` legt ein fertiges Overlay mit eigenen Kennungen an (Adresse eindeutig, neuer Token, ans Ende der Liste; vorhandene Kennung oder Datei abgewiesen).
- `src/common/platzhalterbild.cpp` (neu), `src/common/CMakeLists.txt`, `CMakeLists.txt`: Werkzeug `dss-platzhalterbild` zeichnet beim Bau das Platzhalterbild (grauer Kreis, helle Figur, 256 × 256, aussen durchsichtig) als Ressource `:/dss/vorlage/chatbot-platzhalter.png`. Qt Gui wird dafür immer gesucht; `dss_common` und der Kern bleiben ohne Qt Gui.
- `src/core/actionservice.h/.cpp`, `src/core/coreservice.h/.cpp`, `src/core/main.cpp`: Nach dem Anlegen der mitgelieferten Aktionen ruft das Aktionssystem `CoreService::createChatBotOverlay` (schickt den neuen Stand an alle Fenster, Browser- und OBS-Quellen wie bei jeder Änderung).
- Tests: `tst_mitgeliefert` (unten), `tst_leinwandbild::chatBotOverlay` (Bild), `tst_kernverbindung`, `tst_bearbeiten`, `tst_fensterbearbeiten`, `tst_medien`, `tst_uebernehmen` legen vor dem Kernstart die leere `aktionen.json` an (`testhelp::writeEmptyActions`), weil sie mit einer leeren Overlay- bzw. Medienliste rechnen.
- Dokumente: `docs/06-overlay-format.md` (Anlegen beim ersten Start), `docs/04-offene-punkte.md` (Punkte 126 bis 128), Plan Abschnitt f erledigt. `docs/08-kern-schnittstelle.md` nicht betroffen (keine neue Nachricht).

## Geprüft

Stufe «Klein», 2026-11-16, Studio-PC:
- `scripts/bauen.sh`: «Bau ok (0 Warnungen)».
- `scripts/testen.sh --voll tst_mitgeliefert`: 33 von 33 Fällen bestanden, darunter neu `overlayCreated`, `existingFolderAndBrokenFile`, `secondOverlay`, `coreKeepsExistingOverlay`, erweitert `references`, `coreCreatesMissingFile`, `existingFileUntouched`.
- `scripts/testen.sh -R tst_leinwandbild` (X11-Sitzung aktiv, `loginctl show-session 1`: Active=yes, LockedHint=no): bestanden; Bild `build/mitgeliefert-overlay.png`.
- `scripts/testen.sh`: 74 von 74 bestanden (349,4 s). Der erste volle Lauf vor der Anpassung der fünf Tests: 69 von 74 (erwartet: zusätzliches Overlay und zwei Mediendateien).

Was die Tests zeigen:
- Overlay mit Kennung, Name, Adresse `chat-bot`, Grösse, Ausgabe ein, 9 Elementen in der Reihenfolge und mit den Kennungen von `kChatBotElements`, alle unsichtbar und innerhalb der Leinwand; Texte weiss in Be Vietnam Pro (Kopie gleich der mitgelieferten Datei), Bild = Ressource (PNG), Name in der Mediathek `chatbot-platzhalter.png`, genau 2 Dateien in `media/`.
- Nicht doppelt: zweiter Aufruf und nach neuem Laden «gibt es schon», Overlay, Liste und `media/` unverändert. Ordner gleichen Namens wird benutzt. Kaputte Datei mit der Kennung bleibt Byte für Byte, nichts angelegt.
- Kern: legt Overlay und Ordner zusammen mit den Aktionen an, Verweise der Aktionen ohne Warnung; bei vorhandener `aktionen.json` kein Overlay; bei fehlender `aktionen.json`, aber vorhandenem (umgestaltetem) Overlay bleiben Datei und `media/` unverändert.
- Plan 7: Element gelöscht → Schritt «zeigen: Element fehlt in Overlay «Chat-Bot» · 6 s», Verweisprüfung «Element 1 fehlt im Overlay»; Overlay gelöscht → «Overlay fehlt» in allen 7 Schritten.
- Plan 8: Alle 7 Schritte «Overlay-Element zeigen» per `action_put` auf ein zweites Overlay (1280 × 720, andere Lage, Grösse, Drehung, Schrift DejaVu, Farbe, Ausrichtung, Bild, eigener Wortlaut) umgestellt, Overlay «Chat-Bot» gelöscht: keine Warnung, der Laufzustand zeigt alle 9 eigenen Elemente mit dem eigenen Aussehen und Wortlaut (zum Beispiel «100 Bits von Testname»), nichts im Overlay «Chat-Bot».
- Bild: ausserhalb der Elemente durchsichtig, deckende Textpixel weiss, Platzhalterbild rund mit grauem Grund und hellem Kopf.

Nicht geprüft: Stufe «Baustein» (frischer Klon, `scripts/startpruefung.py`, `scripts/dienstpruefung.py`), Release-Bau, Bau mit `DSS_BUILD_APP=OFF` (Qt Gui wird jetzt immer gesucht), Anlegen beim Neu laden mit SIGHUP (gleicher Weg wie beim Start, kein eigener Test).

## Handtest

### Prüft Lotse: mitgelieferte Aktionen und Overlay im Entwicklungskern
Warum: Zusammenspiel Kern, Fenster und Browserquelle mit einem leeren Datenordner sieht nur ein echter Start. Entwicklungskern mit leerem Testdatenordner (nicht dem des Nutzers) starten, im Fenster prüfen: Reiter «Chat-Bot» zeigt 18 Aktionen, Hauptschalter aus; Overlay-Liste zeigt Ordner «Chat-Bot» mit Overlay «Chat-Bot» und 9 Elementen, alle unsichtbar; Dock «Medien» zeigt `chatbot-platzhalter.png` und die Schrift. Probelauf «Alert Follow»: Element «Alert Follow» 6 s sichtbar (Marke «live»). Bildschirmfotos nur im Scratchpad.

### Prüft der Nutzer: Aussehen und Wortlaut
Warum: sichtbar, entscheidet der Nutzer (Punkt 126). Bild `build/mitgeliefert-overlay.png` (alle Elemente sichtbar auf dunklem Grund; die fünf Alerts liegen absichtlich übereinander, weil sie nacheinander laufen) und die Chat-Texte der Vorlage ansehen; Lage, Grösse und Texte bestätigen oder ändern lassen.

### Prüft der Nutzer: Zurücksetzen und Wiederherstellen
Warum: bedienbar. Eine mitgelieferte Aktion ändern und «Zurücksetzen» (Rückfrage), eine löschen und im Menü der Liste wiederherstellen; Statuszeile «Gespeichert: N Aktionen» beurteilen (Punkt 127).

## Nicht erledigt, `[OFFEN]`

- `[OFFEN]` Punkt 126: Wortlaut, Lage und Grösse sind vom Agenten gewählt.
- `[OFFEN]` Punkt 127: Statuszeile beim Wiederherstellen ohne Vorbild.
- `[OFFEN]` Punkt 128: Kein Weg, ein gelöschtes Overlay «Chat-Bot» allein wiederherzustellen (Plan 7 sieht «Element fehlt» vor).
- `[OFFEN]` Argumentnamen `%gifts%`, `%bits%`, `%viewers%` sind vorläufig (Plan 7, Punkt 119/120); stimmen sie nicht, bleiben sie im Overlay wörtlich stehen.

## Annahmen, Interna-Entscheide

- Anlegen in `dss_common` statt im Kern: braucht `OverlayStore` und die Kennungen der Vorlage (`dss_actions`), beide nur dort zusammen; so testbar ohne Kernprozess (Regel 9).
- Auslöser über `ActionService::Options::templateCreated` statt eines Zeigers auf den Overlay-Speicher: Das Aktionssystem bleibt ohne Overlays, der Kern schickt den neuen Stand wie bei jeder Änderung an die Fenster.
- Nur nach erfolgreichem Schreiben der Vorlage (sonst versucht es der nächste Start ohnehin wieder).
- Platzhalterbild beim Bau gezeichnet statt als Binärdatei im Repo: kein Markenlogo, nachvollziehbar aus Code; Qt Gui nur im Bauwerkzeug.
- Alerts an derselben Stelle (Warteschlange «Alerts» zeigt sie nacheinander), Begrüssung links und Shoutout rechts unten; Profilbilder quadratisch 180 × 180.
- Fehlt nur der Ordner (zum Beispiel ungültige `liste.json`), gilt das Overlay als angelegt, mit Warnung im Protokoll.
- Eine vorhandene Overlay-Datei mit der Kennung (auch kaputt) zählt als «gibt es schon» (kaputte Dateien werden nie überschrieben).
- Fünf bestehende Tests legen die leere `aktionen.json` an, statt ihre Zahlen anzupassen: Sie prüfen Listen und Medien, nicht die mitgelieferten Inhalte (wie f1 bei `tst_kernaktionen`, `tst_kerntwitch`, `tst_fensterchatbot`).

## Quellen

- Qt-Ressourcen in statischen Bibliotheken: https://doc.qt.io/qt-6.8/qt-add-resources.html
- QPainter, QImage (Zeichnen ohne QGuiApplication auf ein QImage): https://doc.qt.io/qt-6.8/qpainter.html, https://doc.qt.io/qt-6.8/qimage.html
- QSaveFile (atomares Schreiben, bestehender Weg): https://doc.qt.io/qt-6.8/qsavefile.html
