#include "browsersource.h"

namespace dss {

QString browserSourcePath(const QString &address, const QString &token)
{
    return QStringLiteral("/o/%1/%2/").arg(address, token);
}

QString browserSourceUrl(const QString &host, int port, const QString &address, const QString &token)
{
    return QStringLiteral("http://%1:%2").arg(host).arg(port) + browserSourcePath(address, token);
}

QString browserSourceLogPath(const QString &address)
{
    return QStringLiteral("/o/%1/…").arg(address);
}

QString newTokenQuestion(bool obsFollows)
{
    if (obsFollows)
        return QStringLiteral("Die alte Adresse funktioniert danach nicht mehr. OBS wird nachgeführt. Fortfahren?");
    return QStringLiteral("Die alte Adresse funktioniert danach nicht mehr. In OBS muss die neue "
                          "Adresse eingetragen werden. Fortfahren?");
}

} // namespace dss
