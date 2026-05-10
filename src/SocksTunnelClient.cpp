#include <cstring>


#include <signal.h>

#ifdef __linux__
#include <unistd.h>
#include <netdb.h>
#elif _WIN32
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

#include <string>
#include <sstream>

#include "SocksTunnelClient.hpp"
#include "SocksDef.hpp"


using namespace std;


std::string int_to_str(uint32_t ip) 
{
    ostringstream oss;
    for (unsigned i=0; i<4; i++) 
    {
        oss << ((ip >> (i*8) ) & 0xFF);
        if(i != 3)
            oss << '.';
    }
    return oss.str();
}

void close_socket(int sock)
{
#ifdef __linux__
    close(sock);
#elif _WIN32
    closesocket(sock);
#endif
}


int connect_to_target(const std::string& host, uint16_t port)
{
    if(host.empty() || port == 0)
    {
        return -1;
    }

    struct addrinfo hints, *res = nullptr;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    const std::string port_string = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_string.c_str(), &hints, &res) != 0)
    {
        return -1;
    }

    int connected = -1;
    for (addrinfo* current = res; current != nullptr; current = current->ai_next)
    {
        int sockfd = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (sockfd < 0)
            continue;

        if (connect(sockfd, current->ai_addr, static_cast<int>(current->ai_addrlen)) == 0)
        {
            connected = sockfd;
            break;
        }

        close_socket(sockfd);
    }

    freeaddrinfo(res);
    return connected;
}


int connect_to_host(uint32_t ip, uint16_t port)
{
    return connect_to_target(int_to_str(ip), port);
}


//
// SocksTunnelClient
// 
SocksTunnelClient::SocksTunnelClient(int id)
: m_id(id)
{
    m_internalBuffer.resize(BUF_SIZE);

#ifdef __linux__
#elif _WIN32
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
#endif
}


SocksTunnelClient::~SocksTunnelClient()
{
#ifdef _WIN32
    WSACleanup();
#endif
}


int SocksTunnelClient::init(uint32_t ip_dst, uint16_t port)
{
#ifdef __linux__
    signal(SIGPIPE, sig_handler);
#elif _WIN32

#endif

    m_clientfd.reset(connect_to_host(ip_dst, ntohs(port)));
    if(m_clientfd.get() == -1)
    {
        return 0;
    }

    return 1;
}


int SocksTunnelClient::initHostname(const std::string& hostname, uint16_t port)
{
#ifdef __linux__
    signal(SIGPIPE, sig_handler);
#elif _WIN32

#endif

    m_clientfd.reset(connect_to_target(hostname, ntohs(port)));
    if(m_clientfd.get() == -1)
    {
        return 0;
    }

    return 1;
}


int SocksTunnelClient::process(const std::string& dataIn, std::string& dataOut)
{
    if(dataIn.size()>0)
        send_sock(m_clientfd.get(), dataIn.data(), dataIn.size());

    DrainStatus status = readAllAvailableNow_(m_clientfd.get(), dataOut, 0);
    if(status == DrainStatus::Error || status == DrainStatus::Disconnected)
        return -1;

    return 1;
}
