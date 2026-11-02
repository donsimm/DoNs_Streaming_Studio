#pragma once

#include <QString>

class QWidget;

// Kleine Dialoge für das Bearbeiten (P08). Knöpfe mit deutschen Texten, unabhängig von der
// Übersetzung von Qt. Geprüft wird im Kern; hier wird nur gefragt.

// «Overlay anlegen»: Name, Breite, Höhe (Vorgaben aus windowedit.h). false bei «Abbrechen».
bool askNewOverlay(QWidget *parent, QString *name, int *width, int *height);

// Einzeiliger Text, zum Beispiel ein neuer Name. false bei «Abbrechen» oder leerem Text.
bool askText(QWidget *parent, const QString &title, const QString &label, QString *value);

// Frage mit zwei Knöpfen; Standard (Enter) ist immer der zweite, sichere Knopf (rejectText).
bool confirm(QWidget *parent, const QString &title, const QString &text, const QString &acceptText,
             const QString &rejectText);
