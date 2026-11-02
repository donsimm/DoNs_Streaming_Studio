#include "systemdnotify.h"

#include <QtGlobal>

#include <cstddef>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace dss {

bool notifySystemd(const QByteArray &state)
{
    const QByteArray path = qgetenv("NOTIFY_SOCKET");
    if (path.isEmpty() || (path.at(0) != '/' && path.at(0) != '@'))
        return false;
    sockaddr_un address = {};
    if (size_t(path.size()) >= sizeof(address.sun_path))
        return false;
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.constData(), size_t(path.size()));
    if (path.at(0) == '@')
        address.sun_path[0] = '\0'; // abstrakter Namensraum
    const socklen_t length = socklen_t(offsetof(sockaddr_un, sun_path) + size_t(path.size()));

    const int fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return false;
    const ssize_t sent = ::sendto(fd, state.constData(), size_t(state.size()), MSG_NOSIGNAL,
                                  reinterpret_cast<const sockaddr *>(&address), length);
    ::close(fd);
    return sent == state.size();
}

} // namespace dss
