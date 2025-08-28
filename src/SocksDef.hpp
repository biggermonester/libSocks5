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


enum class SocketReadStatus
{
    Timeout,
    Success,
    Disconnected,
    Error
};

inline SocketReadStatus readAllDataFromSocket(int sockfd, char* buffer, int &bytes_received, int timeout_ms)
{
    fd_set readfds;
    bytes_received = 0;
    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);

    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    int activity = select(sockfd + 1, &readfds, NULL, NULL, &timeout);
    if (activity < 0)
        return SocketReadStatus::Error;
    if (activity == 0)
        return SocketReadStatus::Timeout;
    if (FD_ISSET(sockfd, &readfds))
    {
        bytes_received = recv(sockfd, buffer, BUF_SIZE, 0);
        if (bytes_received > 0)
            return SocketReadStatus::Success;
        if (bytes_received == 0)
            return SocketReadStatus::Disconnected;
        return SocketReadStatus::Error;
    }
    return SocketReadStatus::Timeout;
}