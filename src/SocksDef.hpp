#pragma once

#ifdef __linux__
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#elif _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#endif

#include <cstdint>
#include <cstddef>
#include <iostream>

static constexpr std::size_t BUF_SIZE = 2048;

// Command constants
enum class Command : uint8_t
{
    Connect = 1,
    Bind = 2,
    UdpAssociate = 3
};

// Address type constants
enum class AddressType : uint8_t
{
    IPv4 = 1,
    DName = 3,
    IPv6 = 4
};

// Connection methods
constexpr bool ALLOW_NO_AUTH = true;
enum class Method : uint8_t
{
    NoAuth = 0,
    Auth = 2,
    NotAvailable = 0xff
};

// Responses
enum class Response : uint8_t
{
    Succeded = 0,
    GenError = 1
};

// windows compatibility
#ifndef _SSIZE_T_DEFINED
#ifdef  _WIN64
typedef unsigned __int64    ssize_t;
#endif
#define _SSIZE_T_DEFINED
#endif

#ifndef SHUT_RDWR
#define SHUT_RDWR 2 
#endif


// handle sig_pipe that can crash the app otherwise
inline void sig_handler(int signum)
{
    std::cerr << "signal(" << signum << ")" << std::endl;
}

class SocketHandle
{
public:
    SocketHandle() : m_fd(-1) {}
    explicit SocketHandle(int fd) : m_fd(fd) {}
    ~SocketHandle() { reset(); }
    SocketHandle(const SocketHandle&) = delete;
    SocketHandle& operator=(const SocketHandle&) = delete;
    SocketHandle(SocketHandle&& other) noexcept : m_fd(other.m_fd) { other.m_fd = -1; }
    SocketHandle& operator=(SocketHandle&& other) noexcept
    {
        if(this != &other)
        {
            reset();
            m_fd = other.m_fd;
            other.m_fd = -1;
        }
        return *this;
    }
    int get() const { return m_fd; }
    void reset(int fd = -1)
    {
        if(m_fd != -1)
        {
#ifdef __linux__
            ::shutdown(m_fd, SHUT_RDWR);
            ::close(m_fd);
#elif _WIN32
            ::shutdown(m_fd, SHUT_RDWR);
            ::closesocket(m_fd);
#endif
        }
        m_fd = fd;
    }
private:
    int m_fd;
};


inline int recv_sock(int sock, char *buffer, uint32_t size) 
{
    int index = 0, ret;
    while(size) 
    {
        if((ret = recv(sock, &buffer[index], size, 0)) <= 0)
            return (!ret) ? index : -1;

        index += ret;
        size -= ret;
    }
    return index;
}


inline int send_sock(int sock, const char *buffer, uint32_t size) 
{
    int index = 0, ret;
    while(size) 
    {
        if((ret = send(sock, &buffer[index], size, 0)) <= 0)
            return (!ret) ? index : -1;
        index += ret;
        size -= ret;
    }
    return index;
}

// Read all data that is currently available *now* (best-effort), without changing blocking mode.
// - Waits up to timeout_ms for the socket to become readable.
// - Then drains using non-blocking recv *per call*:
//    - Linux: uses MSG_DONTWAIT (does not toggle socket flags)
//    - Windows: uses recv(..., MSG_PEEK) + ioctlsocket(FIONREAD) to avoid blocking
//
// Notes:
// - This is "drain available bytes", NOT "read a full message".
// - Uses a max_bytes cap to prevent unbounded memory growth.

#include <string>
#include <algorithm>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "Ws2_32.lib")
  using sock_t = SOCKET;
#else
  #include <poll.h>
  #include <sys/socket.h>
  #include <errno.h>
  using sock_t = int;
#endif

enum class DrainStatus { ReadSome, NoData, Disconnected, Error, Truncated };

inline DrainStatus readAllAvailableNow_(sock_t s, std::string& out, int timeout_ms = 0, size_t max_bytes = 262144)
{
    out.clear();
    if (max_bytes == 0)
        return DrainStatus::NoData;

    // Wait for readability (poll/WSAPoll). This does not change socket mode.
#ifdef _WIN32
    WSAPOLLFD pfd{};
    pfd.fd = s;
    pfd.events = POLLRDNORM; // readable
    int prc = WSAPoll(&pfd, 1, timeout_ms);
    if (prc == SOCKET_ERROR)
        return DrainStatus::Error;
    if (prc == 0)
        return DrainStatus::NoData;
#else
    pollfd pfd{};
    pfd.fd = s;
    pfd.events = POLLIN;
    int prc;
    do {
        prc = poll(&pfd, 1, timeout_ms);
    } while (prc < 0 && errno == EINTR);
    if (prc < 0)
        return DrainStatus::Error;
    if (prc == 0)
        return DrainStatus::NoData;
#endif

    DrainStatus status = DrainStatus::NoData;
    char buf[4096];

#ifdef _WIN32
    // Windows: without switching to non-blocking, we must avoid a recv() that could block.
    // Strategy:
    //  1) Query bytes available with FIONREAD.
    //  2) Read exactly that many bytes (bounded by max_bytes) in chunks.
    u_long avail = 0;
    if (ioctlsocket(s, FIONREAD, &avail) != 0)
        return DrainStatus::Error;

    if (avail == 0)
    {
        // Either no data, or a close is pending. Probe with MSG_PEEK (safe after poll).
        char tmp;
        int n = recv(s, &tmp, 1, MSG_PEEK);
        if (n == 0)  return DrainStatus::Disconnected;
        if (n < 0)   return DrainStatus::Error;
        return DrainStatus::NoData;
    }

    size_t to_read = std::min<size_t>((size_t)avail, max_bytes);
    out.reserve(std::min<size_t>(to_read, 64 * 1024));

    size_t read_total = 0;
    while (read_total < to_read)
    {
        int want = (int)std::min<size_t>(sizeof(buf), to_read - read_total);
        int n = recv(s, buf, want, 0);
        if (n > 0)
        {
            out.append(buf, n);
            read_total += (size_t)n;
            status = DrainStatus::ReadSome;
            continue;
        }
        if (n == 0)
            return (status == DrainStatus::ReadSome) ? DrainStatus::ReadSome : DrainStatus::Disconnected;

        return (status == DrainStatus::ReadSome) ? DrainStatus::ReadSome : DrainStatus::Error;
    }

    if ((size_t)avail > max_bytes)
        return DrainStatus::Truncated;

    return status;

#else
    // Linux: MSG_DONTWAIT makes this recv call non-blocking without toggling flags.
    out.reserve(std::min<size_t>(max_bytes, 64 * 1024));

    while (out.size() < max_bytes)
    {
        size_t room = max_bytes - out.size();
        int want = (int)std::min<size_t>(room, sizeof(buf));

        ssize_t n = recv(s, buf, want, MSG_DONTWAIT);
        if (n > 0)
        {
            out.append(buf, (size_t)n);
            status = DrainStatus::ReadSome;
            continue;
        }
        if (n == 0)
            return (status == DrainStatus::ReadSome) ? DrainStatus::ReadSome : DrainStatus::Disconnected;

        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;

        return (status == DrainStatus::ReadSome) ? DrainStatus::ReadSome : DrainStatus::Error;
    }

    if (out.size() >= max_bytes)
        return DrainStatus::Truncated;

    return status;
#endif
}
