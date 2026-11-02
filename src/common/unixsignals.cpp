#include "unixsignals.h"

#include <QSocketNotifier>

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace dss {

namespace {

int g_fds[2] = {-1, -1};

// Nur async-signal-safe Aufrufe (signal-safety(7)): write und errno.
void handleSignal(int signalNumber)
{
    const int savedErrno = errno;
    const char byte = static_cast<char>(signalNumber);
    [[maybe_unused]] const ssize_t written = ::write(g_fds[0], &byte, 1);
    errno = savedErrno;
}

} // namespace

UnixSignalWatcher::UnixSignalWatcher(QObject *parent, bool handleHangup)
    : QObject(parent), m_hangup(handleHangup)
{
    if (g_fds[0] != -1)
        return; // schon ein Objekt aktiv
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, g_fds) != 0) {
        g_fds[0] = g_fds[1] = -1;
        return;
    }
    ::fcntl(g_fds[0], F_SETFL, O_NONBLOCK);

    m_notifier = new QSocketNotifier(g_fds[1], QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &UnixSignalWatcher::readSignal);

    struct sigaction action = {};
    action.sa_handler = handleSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    m_valid = ::sigaction(SIGINT, &action, nullptr) == 0
        && ::sigaction(SIGTERM, &action, nullptr) == 0
        && (!m_hangup || ::sigaction(SIGHUP, &action, nullptr) == 0);
}

UnixSignalWatcher::~UnixSignalWatcher()
{
    if (!m_notifier)
        return;
    struct sigaction action = {};
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    ::sigaction(SIGINT, &action, nullptr);
    ::sigaction(SIGTERM, &action, nullptr);
    if (m_hangup)
        ::sigaction(SIGHUP, &action, nullptr);
    delete m_notifier;
    ::close(g_fds[0]);
    ::close(g_fds[1]);
    g_fds[0] = g_fds[1] = -1;
}

void UnixSignalWatcher::readSignal()
{
    char byte = 0;
    if (::read(g_fds[1], &byte, 1) != 1)
        return;
    if (static_cast<unsigned char>(byte) == SIGHUP)
        emit hangupReceived();
    else
        emit terminationRequested(static_cast<unsigned char>(byte));
}

} // namespace dss
