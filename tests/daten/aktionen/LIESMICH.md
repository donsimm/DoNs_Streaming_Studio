# Testdaten Aktionen (P14 a)

Erfundene Dateien für `tst_aktionsformat`. Keine echten Namen, Konten oder Zuschauer-Daten (Entscheid des Nutzers vom 2026-11-15: Tests mit Zuschauer-Daten nur mit erfundenen Werten; Plan P14 2.10). Die Zuschauer-IDs `1000000xx` sind ausgedacht.

| Datei | Inhalt |
|---|---|
| `vollstaendig.json` | alle Typen von Schritten und Auslösern, Verschachtelung bis Tiefe 4 (Gruppe → Wenn → Dann → Pause), unbekannte Felder auf jeder Ebene, `script` und `unsupported`, Befehle mit Rechten, Zeitnachrichten mit Zufall. So geschrieben, wie der Kern schreibt (alle Felder, `weight` überall) |
| `plan-beispiel.json` | das Beispiel aus Plan P14, Abschnitt 2.2, mit ausgefüllten Kennungen: Schritte ohne `weight`, Warteschlange als UUID aus lauter Nullen («Standard») |
