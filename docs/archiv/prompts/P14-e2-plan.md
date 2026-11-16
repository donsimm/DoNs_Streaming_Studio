---
topic: P14 e2, Logik und Fenster für Kopfzeile, «Live» und «Aktionen» im Reiter «Chat-Bot»
reference: src/app/chatbotpage.{h,cpp} (Kopfzeile, Bereich «Ereignisse»); für Abschnitte und Leisten src/app/docks.cpp
written: 2026-11-16 (Stand 1cf4e14, Branch lotse/p14-a)
---

# P14 e2: Plan

Grundlage: Checkliste `docs/archiv/prompts/P14-e2-checkliste.md` (Abschnitte 1–6) mit den Entscheiden des Nutzers in ihrem Abschnitt 7; Plan `docs/archiv/prompts/P14-chatbot.md` 3.4–3.6, 4.1, 5, 6, 10, 12. Die Checkliste ist eine Momentaufnahme vom 2026-11-16; gilt der Code etwas anderes, gilt der Code, und der Agent meldet die Abweichung.

Status eines Abschnitts: offen `[ ]`, erledigt `[x]`, `parked — <Grund>` bleibt offen mit Grund auf der Zeile.

## Für jeden Abschnitt

- Ein frischer Agent, **genau ein lokaler Commit** auf `lotse/p14-a` (oder Worktree-Branch, wenn parallel), etwa 1–2 h. Kein Push.
- Lesen: `CLAUDE.md`, `.claude/HANDOFF.md`, diesen Abschnitt, die genannten Dateien gezielt (Grep, Ausschnitte).
- Prüfstufe «Klein»: `scripts/bauen.sh` (0 Warnungen), dann `scripts/testen.sh -R '<Tests des Abschnitts>'`. Fenstertests nie gleichzeitig; X11-Sitzung aktiv und nicht gesperrt.
- Logik nur in `dss_common` bzw. `src/actions`/`src/core`, ohne Qt Widgets, mit Test ohne Fenster (Regel 9). Kein Layout-Wechsel in Ereignis-Behandlern.
- Rückmeldung an Lotse nach der Kurzvorlage in `CLAUDE.md`, mit allen neuen sichtbaren Texten im Wortlaut.

## Decided

Entscheide des Nutzers vom 2026-11-16 (Checkliste, Abschnitt 7), in seinen Worten zusammengefasst:

- «Aktion abgeschaltet oder gelöscht während eines Laufs: laufende Läufe dieser Aktion sofort abbrechen» — überstimmt die Empfehlung von Lotse (Läufe zu Ende laufen lassen) — 2026-11-16; gibt e2-1 frei.
- «Gruppennamen ohne Rücksicht auf Gross- und Kleinschreibung eindeutig» — überstimmt das Verhalten aus c7 (genauer Vergleich) — 2026-11-16; gibt e2-3 frei.
- «`settingsdialog.cpp` bleibt unverändert» — überstimmt die Herauslösung auch dort (Checkliste Frage 1) — 2026-11-16.

## Schnittstelle

- **Version 18** (neu in e2-2): Anfrage `chat_send` (`request_id`, `text`, `account` `bot`|`broadcaster`, `reply_to` Text oder `null`), Antwort `change_result`. Steht in Plan 5, fehlt im Code (`actionprotocol.h` hat keine Nachricht dafür) und in `docs/08`.
- e2-1 ändert nur Verhalten (keine Felder): keine neue Version, Absatz in `docs/08`, «Aktionen».
- Baut ein anderer Zweig parallel eine Version 18, vergibt Lotse die Nummer beim Zusammenführen (Plan 5).

## Abschnitte, Übersicht

| Name | Inhalt | Braucht | Schätzung |
|---|---|---|---|
| e2-0 | Lotse: Prüfung kalibrieren (kein Commit) | – | 0,5 h |
| e2-1 | Kern bricht Läufe einer abgeschalteten oder gelöschten Aktion ab | – | 1,5 h |
| e2-2 | Schnittstelle 18 `chat_send` im Kern | e2-1 (gleiche Datei) | 2 h |
| e2-3 | Gruppennamen ohne Gross-/Kleinschreibung | – | 1 h |
| e2-4 | Kopfzeile: Zustand und Schalter «Chat-Bot aktiv» | – | 1 h |
| e2-5 | Zeilen «Läufe der Aktionen» | e2-4 (gleiche Datei) | 1,5 h |
| e2-6 | Zeilen «Chat», Senden, Filter «Ereignisse» | e2-2, e2-5 | 2 h |
| e2-7 | Testen: Testwerte, Probelauf, echt | e2-3 (gleiche Datei) | 1 h |
| e2-8 | Felder der Schritte (Beschriftung, Reihenfolge, Abschnitte, Titel) | – | 2 h |
| e2-9 | Felder der Auslöser, Titel | e2-8 (gleiche Datei) | 1,5 h |
| e2-10 | Fenster: gemeinsame Bausteine und Theme | – | 1,5 h |
| e2-11 | Fenster: Kopfzeile, Unterreiter «Aktionen» als Gerüst | e2-4, e2-10 | 1,5 h |
| e2-12 | Fenster: «Live» mit Teilern und «Läufe der Aktionen» | e2-5, e2-11 | 2 h |
| e2-13 | Fenster: «Live», Bereich «Chat» und «Chat-Nachrichten ausblenden» | e2-6, e2-12 | 2 h |
| e2-14 | Fenster: «Aktionen» links | e2-3, e2-11 | 2 h |
| e2-15 | Fenster: «Aktionen» Mitte | e2-7, e2-14 | 2 h |
| e2-16 | Fenster: «Aktionen» rechts | e2-8, e2-9, e2-15 | 2 h |
| e2-17 | Lotse: Handtest e2, Plan schliessen | alle | – |

Summe Agenten etwa 28 h (≈ 6 Sitzungen à 4–5 h; Plan 10.1 schätzte für e2 und e3 zusammen 1,75 Sitzungen, die Logik aus Abschnitt 1 der Checkliste war dort nicht enthalten).

**Parallel in getrennten Worktrees** (nie dieselben Dateien, nie zwei Fenstertests gleichzeitig):
- Spur Kern: e2-1 → e2-2 (`actionservice.cpp`).
- Spur Ansichtslogik: e2-3 → e2-7 (`actionview.{h,cpp}`).
- Spur Live-Logik: e2-4 → e2-5 → e2-6 (`windowdata.{h,cpp}`, `chatbotlive.{h,cpp}`; e2-6 erst nach e2-2).
- Spur Felder: e2-8 → e2-9 (`actionfields.{h,cpp}`).
- e2-10 parallel zu allen Logik-Spuren (nur `src/app`, `theme`); sein Fenstertest nicht gleichzeitig mit einem anderen.
- Fenster e2-11 bis e2-16 nacheinander: Alle verdrahten in `src/app/mainwindow.cpp` und `chatbotpage.cpp`.
- Gemeinsam berührt: `tests/CMakeLists.txt` bei neuen Testprogrammen (e2-5, e2-8, e2-11) und `src/common/CMakeLists.txt` bei neuen Dateien. Lotse führt diese Zeilen beim Zusammenführen zusammen.

## Aufgaben

- [ ] **e2-0 Prüfung kalibrieren (Lotse, kein Commit).** Kopie von `src/app/chatbotpage.cpp` in einem Worktree ausserhalb des Arbeitsordners, darin eine Rolle absichtlich anders (zum Beispiel Bereichstitel mit fester Farbe statt `#chatBotPanelTitle`). Ein getrennter Agent mit `ui-consistency:verifying` und der Checkliste («Live», Abschnitt 3) bekommt nur «weicht irgendwo ab». Wird die Abweichung nicht gemeldet: anhalten, Prüfung ist dafür blind. Ergebnis hier vermerken (was geprüft, was nicht). Wer pflanzt, prüft nicht; der kalibrierte Agent prüft danach keine Seite.

### Logik

- [ ] **e2-1 Kern: Läufe abbrechen, wenn die Aktion abgeschaltet oder gelöscht wird.**
  - `src/actions/actionengine.{h,cpp}`: neu `int cancelAction(const QUuid &actionId, const QString &message)`; bricht laufende und verwirft wartende Läufe dieser Aktion (auch Probeläufe) wie `cancel()` (Töne verstummen, c4), Ergebnis Anzahl.
  - `src/core/actionservice.cpp`, `handleEdit`: nach dem Speichern bei `action_remove` und bei `action_put`, das `enabled` von an auf aus setzt, `cancelAction` aufrufen. Meldung im Lauf: «abgebrochen: Aktion abgeschaltet» bzw. «abgebrochen: Aktion gelöscht». `group_remove` (Gruppe löschen) kommt als einzelne `action_remove` und ist damit abgedeckt.
  - Wartende verwerfen: Plan 3.5 sagt nichts dagegen; `cancel()` verwirft wartende schon so. Entschieden in diesem Plan als Folge des Entscheids.
  - `docs/08-kern-schnittstelle.md`, «Aktionen»: Satz zu `action_put`/`action_remove`.
  - Tests: `tst_aktionsablauf` (Engine: laufend, wartend, andere Aktion unberührt, Ton gestoppt), `tst_kernaktionen` (abschalten und löschen über den Socket, `actions.run_finished` mit `cancelled`). `scripts/testen.sh -R '^(tst_aktionsablauf|tst_kernaktionen)$'`, dazu `scripts/wiederholen.sh` 20× für beide (Lebenslauf der Läufe), ohne Last daneben.
  - Handtest: in «Läufe der Aktionen» die beiden Meldungen (H4).

- [ ] **e2-2 Schnittstelle 18: `chat_send` aus dem Fenster.**
  - `src/common/actionprotocol.{h,cpp}`: `msg::kChatSend`, in `actionsRequestTypes()`, Prüfung der Felder; `src/common/protocol.h`: `kProtocolVersion = 18`.
  - `src/core/actionservice.cpp`: an die vorhandene Sendeschlange (`ChatOutbox`/`ChatSender`, Grenzen aus Plan 4.1) übergeben; Antwort `change_result` (abgewiesen mit Grund: leer, länger als 500, beginnt mit `/` oder `.` → «Twitch-Befehle sind nicht möglich», Konto kann nicht senden → Grund aus `actions_status.accounts`, stumm → «stumm: Version im Einsatz läuft»). Ergebnis auf dem Fluss wie beim Schritt (`twitch.chat_sent`/`chat_dropped`).
  - Kein Chat-Text ins Journal (Plan 3.5, `tst_kernaktionen` prüft das schon für Läufe).
  - `docs/08`: «Version 18», Tabelle «Fenster → Kern». `dss-cli chat-senden` (Plan 5) nur, wenn der Agent es in derselben Zeit schafft, sonst `[OFFEN]`.
  - Tests: `tst_protocol`, `tst_kernaktionen` (Twitch-Testserver: Bot, Kanal, zu lang, `/`, stumm). `scripts/testen.sh -R '^(tst_protocol|tst_kernaktionen)$'`.
  - Handtest: keiner allein (über e2-13).

- [ ] **e2-3 Gruppennamen ohne Rücksicht auf Gross- und Kleinschreibung.**
  - `src/common/actionview.cpp`: `renameActionGroup` vergleicht mit `Qt::CaseInsensitive` (Meldung «Die Gruppe «Shoutout» gibt es schon.» mit dem bestehenden Namen; nur andere Schreibweise derselben Gruppe = Umbenennen erlaubt). `actionListRows` bildet Gruppen ohne Rücksicht auf Schreibweise; angezeigt wird die Schreibweise der ersten Aktion in der Datei. `removeActionGroup`, `removeActionGroupQuestion`, `actionGroupChoices`, `newAction(…, group)` gleich.
  - Wird im Feld «Gruppe» einer Aktion «shoutout» eingegeben und «Shoutout» gibt es: `putAction` übernimmt «Shoutout» (Interna, siehe unten).
  - Kopfkommentare in `actionview.h` nachführen.
  - Tests: `tst_aktionsansicht` (`renameAndRemoveGroup` erweitern, neuer Fall Liste mit «Shoutout»/«shoutout»). `scripts/testen.sh -R '^tst_aktionsansicht$'`.

- [ ] **e2-4 Kopfzeile: Zustand des Chat-Bots und Schalter.**
  - `src/common/windowdata.{h,cpp}`: `chatBotHeaderView(channelState, twitchStatus, const std::optional<ActionsStatus> &actions)`; `ChatBotHeaderView` neu mit `bool switchChecked`, `bool switchEnabled`. Reihenfolge: `unavailable` → Schalter aus und ausgegraut, Text aus `message`, `error`; `file_error` → `error`; Twitch-Kanal mit Fehler → wie heute; `off` → «aus», `off`; `muted` → «stumm: Version im Einsatz läuft», `warning`; `on` ohne Bot → «Bot nicht angemeldet: sendet als Kanal», `warning`; `on` → «läuft · liest als <Kanal> · sendet als <Bot>», `ok` (Plan 6). Farben nur `ok`/`warning`/`error`/`off`.
  - `ActionsEdit actionsSetEnabledEdit(bool enabled)` (`actions_set_enabled`).
  - Tests: `tst_windowdata` (jeder Zustand, Text, Tooltip, Schalter). `scripts/testen.sh -R '^tst_windowdata$'`.

- [ ] **e2-5 Zeilen «Läufe der Aktionen».**
  - Neu `src/common/chatbotlive.{h,cpp}` (eigene Datei für die Live-Logik; `windowdata.h` bleibt für P13): `struct RunRow { QUuid runId, actionId; QString time, action, trigger, result; QString state; bool cancellable; bool isError; }`; `RunRow runRow(const actions::RunRecord &)`; `class RunRows` mit `kMaxRows = 200`, `reset(QList<RunRecord>)` (aus `action_runs`), `Change apply(const RunRecord &)` (neu anhängen oder bestehende Zeile ersetzen; Rückgabe Index, angehängt ja/nein, `removedFront`), `setOnlyErrors(bool)` und `visibleRows()`.
  - Ergebnistexte (Plan 3.5, neu): «wartet: Warteschlange <Name>», «läuft, Schritt x von y», «fertig (<Dauer>)», «beendet», «Fehler: <Meldung>», «abgewiesen: <Grund>», «stumm», «verworfen: <Grund>», «Grenze: <Grund>», «abgebrochen» (mit Grund, wenn da); Probelauf mit Vorsilbe «Probelauf · ». Zeit `hh:mm:ss` wie `eventRow`. Auslöser mit `triggerType`-Bezeichnung.
  - Zustand für die Farbe: `ok` (ok, break), `warning` (rejected, muted, cancelled), `error` (error, dropped, limit), leer (waiting, running). «Nur Fehler» zeigt `error`. «Lauf abbrechen» erlaubt bei waiting und running.
  - Tests: neu `tst_chatbotlive` (alle Zustände, Ersetzen, 200er-Grenze, Nur Fehler). `scripts/testen.sh -R '^tst_chatbotlive$'`.

- [ ] **e2-6 Zeilen «Chat», Senden, Filter «Ereignisse».**
  - `chatbotlive.{h,cpp}`: `struct ChatRow { QString time, badges (Liste «Bot»/«Kanal»/«Mod»), name, color, text; bool isEvent; }` aus `twitch.chat_message` (`is_bot_account` → «Bot», `is_broadcaster` → «Kanal», `is_moderator` → «Mod», `color`, `text`) und als graue Zeile (`isEvent`) aus den Ereignissen von `chatBotEventFilters()` und `actions.run_started` («Aktion «<Name>» ausgelöst»). `class ChatRows` mit 200 Zeilen wie `EventRows`.
  - Senden: `struct ChatSendView { QString counter; bool tooLong; bool sendEnabled; QList<…> accounts; QString problem; }` aus Text und `ActionsStatus.accounts` («0 / 500»; UTF-16-Einheiten wie Plan 4.1; «als Bot» ausgegraut, wenn der Bot nicht senden kann). `ActionsEdit chatSendEdit(text, account)` (`chat_send`), Prüfung wie im Kern (leer, 500, `/` und `.`).
  - `windowdata.cpp`: Fenster bestellt zusätzlich `twitch.chat_message`, `twitch.chat_sent`, `twitch.chat_dropped` und `actions.` (neu `chatBotLiveFilters()`); `EventRows` bekommt `setHideChat(bool)`/`visibleRows()` für «Chat-Nachrichten ausblenden» (Standard an).
  - `[OFFEN]` für den Agenten: ob eigene Nachrichten über EventSub `channel.chat.message` zurückkommen (Quelle nennen, Regel 4). Wenn nicht: Zeile aus `twitch.chat_sent` bilden.
  - Tests: `tst_chatbotlive`, `tst_windowdata`. `scripts/testen.sh -R '^(tst_chatbotlive|tst_windowdata)$'`.

- [ ] **e2-7 Testen: «Testwerte: Benutzer», «▶ Testen», «echt ausführen».**
  - `actionview.{h,cpp}`: `QList<TestUserChoice> testUserChoices(…)` (Einträge nach Frage 4 unten); `EditResult testAction(const actions::Action &, const TestUserChoice &, bool real)` → `action_test` mit `action_id`, `arguments` (Argumente des Auslösers «Test» der Aktion, darüber `user`, `userName`, `userId` des gewählten Benutzers, Namen aus `eventtypes.h`), `dry_run = !real`; `bool testAllowed(const actions::Action &, const ActionsStatus &)` (stumm: nur Probelauf, «echt ausführen» ausgegraut).
  - Statuszeile nach Antwort: «Probelauf gestartet: <Aktion>» bzw. «Gestartet: <Aktion>» (neu).
  - Tests: `tst_aktionsansicht` (Argumente, dry_run, stumm). `scripts/testen.sh -R '^tst_aktionsansicht$'`.

- [ ] **e2-8 Felder der Schritte für die rechte Spalte.**
  - Neu `src/common/actionfields.{h,cpp}`: `struct FieldRow { QString name, label; actions::FieldKind kind; QList<QPair<QString,QString>> values (Wert, deutsche Bezeichnung); double min, max; QString suffix; QString choices (overlay|source|media|action|queue|leer) }`, `struct FieldSection { QString title; QList<FieldRow> rows; }`, `QList<FieldSection> stepForm(const QString &type)`, `QString stepTitle(const actions::Step &)` → «Schritt «<Art>»» (Art = `StepTypeInfo::label`), `QString fieldSection(form, fieldName)` für die Prüfmeldung.
  - Beschriftungen und Abschnitte aus Plan 4.1–4.10 und Plan 6 (Overlay: «Overlay», «Elemente», «Inhalt der Quellen», «Dauer», «Warten, bis ausgeblendet»; Ton: «Datei», «Lautstärke»). Jedes Feld aus `StepTypeInfo::fields` kommt genau einmal vor (Test), `ElementList` als eigene Art.
  - Tests: neu `tst_aktionsfelder` (jeder Typ: alle Felder, keine Doppelten, Reihenfolge, Titel). `scripts/testen.sh -R '^tst_aktionsfelder$'`.

- [ ] **e2-9 Felder der Auslöser.**
  - `actionfields.{h,cpp}`: `triggerForm(type)` aus `TriggerTypeInfo::filters`, `triggerTitle(const actions::Trigger &)` → «Auslöser «<Art>»». Testwerte (`StringMap`) als eigene Art.
  - Tests: `tst_aktionsfelder`. `scripts/testen.sh -R '^tst_aktionsfelder$'`.

### Fenster

Jede Fenster-Aufgabe trägt ihre Checkliste (Momentaufnahme 2026-11-16, Stand a100c29/1cf4e14, Referenz `src/app/chatbotpage.{h,cpp}`, Familie «Bereich wie ein Dock», 6 von 9 Kandidaten). Ablauf jeder Fenster-Aufgabe:
1. Checkliste vor dem Schreiben neu lesen, nicht aus dem Gedächtnis arbeiten.
2. Seite als prüfbereit melden. Lotse schickt sie an den Prüfer dieser Art (ein Agent mit `ui-consistency:verifying`, ab der ersten Seite für alle Seiten derselbe, nie der Schreibende oder der Kalibrierte) und gibt dessen Befund zurück; der Agent behebt ihn im selben Commit (`--amend`, nur lokal).
3. Status an Lotse: `done` nach bestandener Prüfung oder `parked — <Grund>`.

- [ ] **e2-10 Gemeinsame Bausteine und Theme.**
  - Neu `src/app/panels.{h,cpp}`: `section()`, `symbolButton()`, `keepValueVisible()` aus dem anonymen Namensraum von `docks.cpp`; `buttonBar(QList<QWidget*> left, QList<QWidget*> right, parent)` (`#dockButtons`, `WA_StyledBackground`, Abstände 6, 4, 6, 4, Abstand 6, Dehnung vor rechts); `listWithButtons` nutzt `buttonBar`. `dockPanel(title, content, parent)` aus `ChatBotPage::addLivePanel` (`#chatBotPanelTitle`, `#chatBotPanelBody`); `addLivePanel` behält seine Signatur und ruft `dockPanel`.
  - `src/app/theme.{h,cpp}`: `QSplitter::handle` mit den Werten von `QMainWindow::separator` (4 px, transparent, hover-Rand `text`); neue Farben `badgeBot` (--purple1), `badgeChannel` (--blue3), `badgeMod` (--green3), `chatEventRow` (--grey5) mit Wert aus Yami.obt (Quelle mit URL, Regel 4); Selektor `#fieldProblem { color: @stateError@ }`.
  - **`settingsdialog.cpp` bleibt unverändert** (Entscheid).
  - Checkliste: Docks sehen danach gleich aus wie vorher (nur verschobener Code); kein neuer Wert in einer Seite.
  - Tests: `tst_fensterbearbeiten`, `tst_windowdata`. `scripts/testen.sh -R '^(tst_fensterbearbeiten|tst_windowdata)$'`.
  - Handtest: keiner allein.

- [ ] **e2-11 Kopfzeile und Unterreiter «Aktionen» als Gerüst.** Schreibt `src/app/chatbotpage.{h,cpp}`, `src/app/mainwindow.cpp`, neu `src/app/actionspage.{h,cpp}` (leere Seite mit drei `dockPanel` im `QSplitter`), neu `tests/tst_fensterchatbot.cpp` (Muster `tst_fensterbearbeiten`).
  - Checkliste:
    - Unterreiter nur über `addSubTab()` mit `moduleButton()`; Text «Aktionen».
    - `QCheckBox` «Chat-Bot aktiv» links von `#chatBotState`, Abstand 6 px aus der Zeile; Klick sendet `actionsSetEnabledEdit`, Zustand nur aus `chatBotHeaderView`.
    - Kopfzeile fest hoch; neuer Zustand ändert nur Text, Tooltip und `[state]` mit `unpolish`/`polish`.
    - Zustandsfarben nur `#chatBotState[state=ok|warning|error]`; «stumm» und «aus» ohne eigene Farbe.
  - Nicht von der Referenz übernehmen: nichts in diesem Teil.
  - Tests: `tst_fensterchatbot` (Schalter, Zustände, Unterreiter). `scripts/testen.sh -R '^tst_fensterchatbot$'`.
  - Handtest: H1.

- [ ] **e2-12 «Live»: Teiler und «Läufe der Aktionen».** Schreibt `chatbotpage.{h,cpp}`, `mainwindow.cpp` (Läufe bestellen: `get_action_runs`, Fluss `actions.`; «Lauf abbrechen» → `action_run_cancel`; «Im Editor öffnen» → Unterreiter «Aktionen», Aktion gewählt).
  - Checkliste:
    - Jeder Bereich über `addLivePanel()`/`dockPanel()` (`#chatBotPanelTitle`, `#chatBotPanelBody`).
    - `m_liveRow` wird `QSplitter`, Teiler aus `QSplitter::handle` (4 px wie `QMainWindow::separator`).
    - Tabelle `QTreeWidget` wie `m_events`: `setRootIsDecorated(false)`, `setUniformRowHeights(true)`, `SingleSelection`, `NoFocus`; Spalten «Zeit», «Aktion», «Auslöser», «Ergebnis», Breiten aus `digit * n` je Spalte, letzte gedehnt; Kopf über `QHeaderView::section`.
    - Mitlaufen: anhängen, vorne `removedFront` entfernen, nur nach unten rollen, wenn schon unten; 200 aus `RunRows`.
    - Ergebnisfarbe nur `stateConnected`/`stateConnecting`/`stateError`, als Theme-Wert hereingereicht (`setForeground` mit dem Wert aus `Theme`), nie `QColor` in der Seite.
    - Leiste `buttonBar`: `QPushButton` «Im Editor öffnen», «Lauf abbrechen», Dehnung, `QCheckBox` «Nur Fehler»; Knöpfe ohne Auswahl ausgegraut, Freigabe aus `RunRow::cancellable` bzw. Aktion vorhanden.
  - Nicht übernehmen: die festen Spaltenbreiten 10, 16, 16 von «Ereignisse»; `QHBoxLayout` als Zeile.
  - Tests: `tst_fensterchatbot`. Handtest: H2, H4.

- [ ] **e2-13 «Live»: Bereich «Chat» und «Chat-Nachrichten ausblenden».** Schreibt `chatbotpage.{h,cpp}`, `mainwindow.cpp` (Bestellung `chatBotLiveFilters()`, Senden `chatSendEdit`, Fehler in die Statuszeile).
  - Checkliste:
    - Bereich «Chat» links über `dockPanel()`; Liste mitlaufend wie `appendEvent()`, 200 aus `ChatRows`.
    - Abzeichen «Bot»/«Kanal»/«Mod» mit `badgeBot`/`badgeChannel`/`badgeMod`, graue Zeilen mit `chatEventRow`, Name in der Chatfarbe aus der Zeile; keine Farbe in der Seite ausser Theme und Chatfarbe.
    - Eingabezeile: `QComboBox` «als Bot»/«als Kanal», `QLineEdit` mit Platzhalter «Nachricht an den Chat …», Zähler `QLabel` in `textMuted` mit fester Mindestbreite für «500 / 500» (bei zu lang `stateError` über `[state]`), `QPushButton` «Senden»; Enter sendet; Text und «zu lang» nur aus `ChatSendView`.
    - In «Ereignisse» Leiste `buttonBar` mit `QCheckBox` «Chat-Nachrichten ausblenden»; Umschalten füllt nur die Liste neu.
  - Nicht übernehmen: `QHBoxLayout` als Zeile.
  - Tests: `tst_fensterchatbot`. Handtest: H2, H3.

- [ ] **e2-14 «Aktionen» links.** Schreibt `actionspage.{h,cpp}`, `mainwindow.cpp` (Bearbeitungen senden, Fehler und «Gespeichert: <Name>» über `showStatus()`).
  - Checkliste:
    - Bereich über `dockPanel()` mit Titel «Aktionen»; Bereiche im `QSplitter`, fest (kein Lösen, Schliessen).
    - Suchfeld `QLineEdit` mit `setPlaceholderText("Suchen …")`; Wechsel füllt nur den Baum (`actionListRows(…, search)`).
    - Baum `QTreeWidget`, Stil `QTreeView::item`, Kästchen je Aktion (`Qt::ItemIsUserCheckable`) → `putAction` mit `enabled`.
    - Leiste `buttonBar` mit `symbolButton` (14 px, Tooltip, `setAccessibleName`, ausgegraut): `hinzufuegen.svg` «Aktion hinzufügen», `ordner-neu.svg` «Neue Gruppe», `kopieren.svg` «Aktion kopieren», `entfernen.svg` «Aktion löschen» │ Dehnung │ `zuruecksetzen.svg` «Mitgelieferte Aktion zurücksetzen» (Freigabe nur aus `actionResettable()`).
    - Gruppenzeile: Kontextmenü «Umbenennen», «Löschen» und Doppelklick zum Umbenennen wie das Dock «Overlays» (`renameActionGroup`, `removeActionGroup`; mehrere Befehle nacheinander, beim ersten Fehler Meldung).
    - Vor dem Löschen nur `confirm()` aus `dialogs.h`, sicherer Knopf als Standard, Text aus `removeActionQuestion`/`removeActionGroupQuestion`/`resetActionQuestion`; leerer Text = ohne Rückfrage.
    - Gespeichert und Fehler nur über `MainWindow::showStatus()`, 10 s, keine `QMessageBox`.
  - Nicht übernehmen: Werte aus `_entwurf/studio.css` und dem Bild.
  - Tests: `tst_fensterchatbot`. Handtest: H5.

- [ ] **e2-15 «Aktionen» Mitte.** Schreibt `actionspage.{h,cpp}`, `mainwindow.cpp` (Testen).
  - Checkliste:
    - Titel aus `actionTitle()` («Aktion «Begrüssung»»), ohne Auswahl `#hintLabel` «Keine Aktion gewählt».
    - Abschnitte über `section()`: «Aktion» (Raster Name | Gruppe, Warteschlange | Aktiv, Hinweis), «Auslöser» (Titelknöpfe «+» `QToolButton` mit `QMenu`/`InstantPopup` aus `triggerChoices()`, Tooltip «Auslöser hinzufügen», «−» «Auslöser entfernen»; Liste mit Kästchen), «Schritte».
    - Warteschlange: Eintrag «Warteschlangen verwalten …» ausgegraut (Wirkung mit e3).
    - Schritte `QTreeWidget` aus `stepRows()`, eingerückt (Gruppe, «Dann», «Sonst»), zwei Spalten (Art fett, Kurzinhalt), Kästchen je Schritt; abgeschaltet durchgestrichen in `stateError` (Theme-Wert hereingereicht).
    - Leiste `buttonBar`: «Schritt hinzufügen» `QToolButton` mit Menü aus `stepChoices()` nach Abschnitten, «Schritt entfernen», «Schritt nach oben», «Schritt nach unten» (`symbolButton`, Freigabe aus `stepButtons()`) │ Dehnung │ «Testwerte: Benutzer» `QComboBox`, `QPushButton` «▶ Testen», `QCheckBox` «echt ausführen» (Freigabe aus `testAllowed`).
    - Rückfrage vor «Schritt entfernen» aus `removeStepQuestion`.
    - Prüfmeldung je Abschnitt: `QLabel#fieldProblem` unter dem Abschnitt, immer da (Höhe reserviert), Text aus `EditResult::problem` bzw. `change_result`; danach zeigt das Feld wieder den Wert des Kerns.
    - Textfelder speichern beim Verlassen oder mit Enter, Kästchen und Auswahl sofort (Plan 5).
  - Nicht übernehmen: von Hand gebaute Abschnitte aus «Einstellungen»; Leiste von Hand aus «Eigenschaften».
  - Tests: `tst_fensterchatbot`. Handtest: H5, H6.

- [ ] **e2-16 «Aktionen» rechts.** Schreibt neu `src/app/actionfieldspanel.{h,cpp}`, `actionspage.cpp`.
  - Checkliste:
    - Titel aus `stepTitle()`/`triggerTitle()`; ohne Auswahl `#hintLabel` «Kein Schritt gewählt».
    - `QScrollArea` `NoFrame`, ohne waagrechte Leiste, darin `QStackedWidget` mit einer Seite je Art; Auswahlwechsel schaltet nur die Seite um, baut nie Felder auf oder ab.
    - Je `FieldSection` ein `section()`, Raster Beschriftung | Feld aus `FieldRow`; Zahlenfelder mit `keepValueVisible` und `setKeyboardTracking(false)`; Werte nur über `setStepField`/`setTriggerField`.
    - Prüfmeldung `#fieldProblem` unter dem Abschnitt aus `fieldSection()`.
    - `ElementList` (Overlay-Schritt): Liste mit «+» und Inhalt je Element; `StringMap` (Testwerte): Tabelle Name | Wert.
  - Nicht übernehmen: die von Hand gebaute Leiste des Docks «Eigenschaften».
  - Tests: `tst_fensterchatbot` (jede Art hat eine Seite, Wechsel ohne Neuaufbau). Handtest: H6.

- [ ] **e2-17 Handtest e2 und Plan schliessen (Lotse).** Nach dem Handtest: Entscheide unter «Decided» mit dem Ergebnis an den Nutzer, offene Punkte nach `docs/04-offene-punkte.md`.

## Handtest e2 (Regel 10)

Kern und Fenster aus `build/` mit eigenem `XDG_CONFIG_HOME`, nie während eines Streams. Was die Tests prüfen (Zeilen, Grenzen, Abbruch, Protokoll), wiederholt der Nutzer nicht.

**Prüft Lotse**

**L1 Bildschirmfotos.** Kopfzeile in «aus», «läuft», «stumm», «Bot nicht angemeldet»; «Live» mit allen drei Bereichen; «Aktionen» mit Aktion, Schritt und Auslöser rechts. Vergleich mit `_e2/1-gesamtansicht.png` und `2a-editor-im-reiter.png`, Ablage nur im Scratchpad. Warum: Aussehen, Abstände gegen den Entwurf.

**L2 Senden über den echten Weg.** «DSS-Test …» als Bot und als Kanal in den eigenen Chat (Recht aus `CLAUDE.md`, P14). Erwartet: je eine Zeile mit Abzeichen in «Chat», `twitch.chat_sent`. Warum: echter Weg über Helix.

**L3 Abbruch beim Abschalten.** Testaktion mit Pause 30 s und Ton, «▶ Testen», dann Kästchen der Aktion aus; danach gleiche Aktion löschen. Erwartet in «Läufe»: «abgebrochen: Aktion abgeschaltet» bzw. «… gelöscht», Ton verstummt (Zustand des Tons). Testaktion danach weg. Warum: echter Dienst und Ton.

**Prüft der Nutzer**

**H1 Kopfzeile.** «Chat-Bot aktiv» aus- und einschalten; Texte «aus», «läuft · liest als … · sendet als …». Warum: Bedienung.

**H2 «Live» ansehen und Teiler ziehen.** Die Teiler zwischen «Chat», «Ereignisse», «Läufe der Aktionen» verschieben. Texte: Spalten «Zeit», «Aktion», «Auslöser», «Ergebnis»; «Im Editor öffnen», «Lauf abbrechen», «Nur Fehler», «Chat-Nachrichten ausblenden». Warum: Bedienung, Lesbarkeit.

**H3 Chat.** Im eigenen Chat schreiben; Abzeichen «Bot» (lila), «Kanal» (blau), «Mod» (grün), Name in Chatfarbe, graue Zeilen für Ereignisse. Aus dem Fenster senden mit «als Bot» und «als Kanal», Enter, Zähler «0 / 500», Platzhalter «Nachricht an den Chat …». Warum: Aussehen, Bedienung.

**H4 Läufe.** Eine Aktion testen; Ergebnistexte lesen («läuft, Schritt x von y», «fertig (…)», «Probelauf · …»), «Nur Fehler», «Lauf abbrechen», «Im Editor öffnen». Warum: Lesbarkeit der neuen Texte.

**H5 «Aktionen» links und Rückfragen.**
- Suchen («Suchen …»), Kästchen aktiv, Tooltips «Aktion hinzufügen», «Neue Gruppe», «Aktion kopieren», «Aktion löschen», «Mitgelieferte Aktion zurücksetzen».
- Gruppe per Doppelklick und Kontextmenü «Umbenennen» umbenennen; «shoutout», wo «Shoutout» schon besteht → «Die Gruppe «Shoutout» gibt es schon.»
- Rückfragen lesen: «Aktion «<Name>» löschen? Sie wird von <Verweise> benutzt.»; «Gruppe «<Name>» mit <n Aktion|Aktionen> löschen?» (mit «Sie wird …» bzw. «Sie werden von <Verweise> benutzt.»); «Schritt «<Art>» samt <n Schritt|Schritten> darin löschen?»; Verweise wie «1 Befehl, 1 Zeitnachricht und 2 Aktionen».
- Warum: Bedienung, Wortlaut. (Die Rückfragen bei Befehl und Zeitnachricht kommen mit e3.)

**H6 «Aktionen» Mitte und rechts.** Eigene Aktion: Abschnitte «Aktion», «Auslöser», «Schritte»; Beschriftungen «Name», «Gruppe», «Warteschlange», «Aktiv»; «Warteschlangen verwalten …» ausgegraut; Schritt hinzufügen, verschachteln, abschalten (rot durchgestrichen), «▶ Testen» als Probelauf und mit «echt ausführen»; rechts die Felder je Art lesen (Titel «Schritt «<Art>»», «Auslöser «<Art>»»); leeren Namen eingeben → Prüfmeldung unter dem Abschnitt; leer: «Keine Aktion gewählt», «Kein Schritt gewählt». Tooltips «Auslöser hinzufügen», «Auslöser entfernen», «Schritt hinzufügen», «Schritt entfernen», «Schritt nach oben», «Schritt nach unten». Warum: Bedienung, Beschriftungen.

## Fragen an den Nutzer (gebündelt, je mit Empfehlung)

1. **Hauptschalter «Chat-Bot aktiv» aus während eines Laufs:** Läufe auch abbrechen wie beim Abschalten einer Aktion? **Empfehlung:** ja, gleich behandeln (wer den Bot ausschaltet, will Ruhe). Betrifft e2-1; ohne Antwort baut e2-1 nur Aktion abschalten/löschen.
2. **Senden aus dem Fenster bei «Chat-Bot aus»:** erlaubt? **Empfehlung:** ja (Hand des Nutzers, kein Lauf des Bots); bei «stumm» abgewiesen (Plan 3.4). Betrifft e2-2.
3. **«Anhören» beim Schritt «Ton abspielen»** (Plan 6): Es gibt keine Nachricht dafür. **Empfehlung:** in e2 ausgegraut sichtbar wie «Warteschlangen verwalten …», Wirkung mit e3 als eigener Punkt. Betrifft e2-8, e2-16.
4. **Einträge bei «Testwerte: Benutzer»:** **Empfehlung:** «Testzuschauer» (erfunden, fester Login und Kennung, Plan 3.6) und «eigener Kanal» (aus dem Twitch-Status). Betrifft e2-7.

## `[OFFEN]`

- Checkliste Abschnitt 1, «Rechte Spalte»: `fieldspec.h` liegt in `src/actions/`, nicht in `src/common/` (Zeilen 31–39 statt 31–38).
- Checkliste Abschnitt 1, «Gruppe umbenennen und löschen, Rückfragetexte»: inzwischen mit c7 (f96f318) erledigt; bleibt dort als «fehlt» stehen (Momentaufnahme a100c29).
- Checkliste «Live», «Ergebnisfarbe»: Weg festgelegt in e2-12 (Theme-Wert hereingereicht, `setForeground`); Delegat nicht nötig.
- «Chat-Nachrichten ausblenden» braucht Chat-Nachrichten in der Bestellung; heute schliesst `chatBotEventFilters()` sie aus. Gelöst in e2-6.
- Ob eigene Nachrichten über EventSub zurückkommen: prüft e2-6 mit Quelle.
- `dss-cli chat-senden` (Plan 5): in e2-2 nur, wenn Zeit bleibt.
- Plan 12 N8 erwartet in «Läufe» «Bot aus»; der Kern schreibt «abgewiesen: Chat-Bot aus» (`triggerdispatcher.cpp:328`). Text gilt so; Plan 12 bei Gelegenheit nachführen.
- Mehrere `action_remove` (Gruppe löschen) sind nicht atomar (c7); e2-14 schickt nacheinander und meldet den ersten Fehler.

## Annahmen, Interna-Entscheide

- Live-Logik in eigener Datei `chatbotlive.{h,cpp}`, Felder in `actionfields.{h,cpp}`: kleinere Dateien, parallele Spuren ohne gemeinsame Datei.
- Wartende Läufe einer abgeschalteten oder gelöschten Aktion werden verworfen: `cancel()` macht es schon so, Plan 3.5 sagt nichts anderes.
- Gruppe mit anderer Schreibweise im Feld «Gruppe» übernimmt die bestehende Schreibweise: Folge aus «eindeutig»; Abweisen wäre die andere Möglichkeit.
- Farbzuordnung der Ergebnisse (e2-5): Fehler rot, Abweisungen und Abbruch orange, fertig grün, laufend und wartend ohne Farbe.
- Fenster-Abschnitte nacheinander, weil alle `mainwindow.cpp` verdrahten.
- Prüfbefunde werden im selben Commit behoben (`--amend`, lokal), damit je Abschnitt genau ein Commit bleibt.
- Abweichung vom Skill `ui-consistency:planning`: Der Plan liegt auf Auftrag von Lotse im Repo (`docs/archiv/prompts/`), nicht ausserhalb.

## Quellen

- Checkliste `docs/archiv/prompts/P14-e2-checkliste.md`; Review und Rückmeldung c7 (Arbeitsdateien, nicht im Repo).
- Plan `docs/archiv/prompts/P14-chatbot.md` 3.4–3.6, 4.1, 5, 6, 10.1, 10.4, 12.
- Code: `src/common/actionprotocol.h`, `protocol.h:58`, `windowdata.h:144–183`, `actionview.h`, `src/actions/fieldspec.h`, `steptypes.h`, `triggertypes.h`, `actionengine.h:124–140`, `src/core/actionservice.cpp:510–593`, `:784–811`, `src/events/eventtypes.h:68–80`, `:188–198`, `src/app/docks.cpp:27–143`, `chatbotpage.h`, `theme.{h,cpp}`.
- `docs/08-kern-schnittstelle.md`, «Version 17», «Aktionen».
- Yami-Werte: prüft e2-10 gegen Yami.obt aus OBS Studio (URL im Bericht).
