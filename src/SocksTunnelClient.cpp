#include <cstring>


#include <signal.h>

#ifdef __linux__
#include <unistd.h>
#include <netdb.h>
#elif _WIN32
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

#include <iostream>
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


int connect_to_host(uint32_t ip, uint16_t port)
{
    struct sockaddr_in serv_addr;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        return -1;
    }

    std::string ip_string = int_to_str(ip);

    struct addrinfo hints, *res = nullptr;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(ip_string.c_str(), nullptr, &hints, &res) != 0)
    {
#ifdef __linux__
        close(sockfd);
#elif _WIN32
        closesocket(sockfd);
#endif
        return -1;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr = ((struct sockaddr_in*)res->ai_addr)->sin_addr;
    serv_addr.sin_port = htons(port);
    freeaddrinfo(res);

    if (connect(sockfd, (const sockaddr*)&serv_addr, sizeof(serv_addr)) != 0)
    {
#ifdef __linux__
        close(sockfd);
#elif _WIN32
        closesocket(sockfd);
#endif
        return -1;
    }

    return sockfd;
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


int SocksTunnelClient::process(const std::string& dataIn, std::string& dataOut)
{
    if(dataIn.size()>0)
        send_sock(m_clientfd.get(), dataIn.data(), dataIn.size());

    int bytes_received;
    SocketReadStatus status = readAllDataFromSocket(m_clientfd.get(), &m_internalBuffer[0], bytes_received, 10);

    if(status == SocketReadStatus::Error || status == SocketReadStatus::Disconnected)
        return -1;
    if(status == SocketReadStatus::Timeout)
        bytes_received = 0;

    dataOut.assign(&m_internalBuffer[0], bytes_received);

    return 1;
}
