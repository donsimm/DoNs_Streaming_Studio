#include "obssecretstore.h"

namespace dss::obs {

namespace {

dss::SecretStore::Labels obsLabels()
{
    return {QStringLiteral("OBS-Passwort"), QStringLiteral("Passwort")};
}

} // namespace

ObsSecretStore::ObsSecretStore(const QString &filePath, QObject *parent)
    : ObsSecretStore(filePath, QStringLiteral("systemd-creds"), parent)
{
}

ObsSecretStore::ObsSecretStore(const QString &filePath, const QString &program, QObject *parent)
    : dss::SecretStore(filePath, QString::fromLatin1(kDefaultCredentialName), obsLabels(), program, parent)
{
}

} // namespace dss::obs
