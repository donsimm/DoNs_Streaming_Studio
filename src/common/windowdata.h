#pragma once

#include "overlay.h"
#include "protocol.h"
#include "systemstats.h"

#include <QList>
#include <QString>
#include <QUuid>

#include <optional>

// Aufbereitung der Daten vom Kern für die Anzeige im Fenster (P07, Teil F). Nur Qt Core
// (CLAUDE.md Regel 9): Das Fenster setzt nur die Texte und Werte, die hier entstehen.

namespace dss {

// Kopfbereich, Werte wie auf docs/bilder/oberflaeche.png; «–», wenn der Wert fehlt.
QString cpuText(const std::optional<double> &percent);                     // «1 %»
QString ramText(const std::optional<qint64> &usedKiB,
                const std::optional<qint64> &totalKiB);                    // «0.8 / 7.8 GiB»
QString lanText(const std::optional<LanStats> &lan);                       // «↓ 0 ↑ 0 kbit/s»
QString lanToolTip(const std::optional<LanStats> &lan);                    // «LAN über enp3s0 …»

// Verlauf der letzten Werte (eine Messung pro Sekunde, Standard 60). Ein fehlender Wert
// bleibt als Lücke stehen.
class ValueHistory
{
public:
    explicit ValueHistory(int capacity = 60);

    void add(const std::optional<double> &value);
    void clear() { m_values.clear(); }
    int capacity() const { return m_capacity; }
    // Älteste zuerst, höchstens capacity() Einträge.
    QList<std::optional<double>> values() const { return m_values; }
    // Grösster vorhandener Wert, 0 ohne Werte.
    double maximum() const;

private:
    int m_capacity;
    QList<std::optional<double>> m_values;
};

// Oberer Rand der Verlaufsfläche: CPU 100 %, RAM gesamt, LAN der grösste Wert im Verlauf
// (mindestens 1 kbit/s, damit eine Linie bei 0 unten liegt).
double lanScaleMaximum(const ValueHistory &history);

// Zeile im Dock «Quellen». Reihenfolge wie in OBS: die vorderste Quelle (letztes Element in
// Overlay::sources) steht oben.
struct SourceRow
{
    QUuid id;
    QString name;
    bool visible = true;
};
QList<SourceRow> sourceRows(const Overlay &overlay);
QString visibilityToolTip(bool visible); // «eingeblendet» / «ausgeblendet»

// Quelle mit der Kennung, nullptr wenn es sie nicht gibt.
const Source *findSource(const Overlay &overlay, const QUuid &sourceId);

// Drehung für das Zahlenfeld in Grad (125 Zehntelgrad → 12.5).
double rotationDegrees(int tenthDegrees);

// Statuszeile rechts.
QString coreStatusText(bool connected); // «Kern-Dienst: verbunden» / «… nicht verbunden»
// P10-vor (Punkt 67): Hinweis, solange der Kern Dateien übernimmt: leer, «Übernehme «a.png» …»,
// «Übernehme 2 Dateien («a.png», «b.mp4») …».
QString importStatusText(const QStringList &fileNames);

// P10-vor, Teil B 1: Dialog «Werkzeuge → Einstellungen …». Nur die Werte, die sich gegenüber dem
// Stand des Kerns geändert haben (Adresse ohne Leerzeichen am Rand); std::nullopt, wenn nichts
// geändert ist. Geprüft wird im Kern, der Dialog zeigt dessen Meldung.
std::optional<SettingsUpdate> settingsUpdateFromForm(const CoreSettings &current, int port,
                                                     const QString &addressText);
// Hinweis im Dialog: Die Werte wirken erst mit der Browserquelle (P10).
QString settingsDialogHint();
// Meldung im Dialog: Datei ungültig (settings_error) bzw. ohne Verbindung; sonst leer.
QString settingsDialogProblem(bool connected, const std::optional<SettingsInfo> &info);

} // namespace dss
