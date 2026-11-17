#pragma once

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QPushButton;

// P15 e2: Dialog «Position speichern» (Entwurf Seite 5, Checkliste «Dialoge»): Name und, für
// Kamera-Presets, «Bildwerte mitspeichern» (Standard aus, Entscheid 15.11.); in e3 auch für
// «Aktuelles Licht speichern …» ohne Kästchen. Fragt nur; gesendet wird im Reiter.
class PresetNameDialog : public QDialog
{
    Q_OBJECT

public:
    PresetNameDialog(const QString &title, const QString &hint, bool withControlsBox, QWidget *parent = nullptr);

    QString name() const;     // ohne Leerzeichen am Rand
    bool withControls() const; // false ohne Kästchen

private:
    QLineEdit *m_name = nullptr;
    QCheckBox *m_withControls = nullptr;
    QPushButton *m_ok = nullptr;
};
