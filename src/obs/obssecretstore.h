#pragma once

#include "secretstore.h"

// Passwort von OBS verschlüsselt ablegen (P10c, Regel 12, Entscheid F1). Seit P13 nur noch Name,
// Datei und Bezeichnungen für OBS; die Ablage selbst ist dss::SecretStore (src/secrets/), das
// Verhalten ist unverändert.

namespace dss::obs {

// Name im verschlüsselten Inhalt: systemd-creds prüft ihn beim Entschlüsseln.
inline constexpr char kDefaultCredentialName[] = "dss-obs-passwort";

class ObsSecretStore : public dss::SecretStore
{
public:
    // filePath: <Ordner der Einstellungen>/obs-passwort.cred, je Version getrennt.
    explicit ObsSecretStore(const QString &filePath, QObject *parent = nullptr);
    // program: anderes Programm statt systemd-creds (Tests).
    ObsSecretStore(const QString &filePath, const QString &program, QObject *parent = nullptr);
};

} // namespace dss::obs
