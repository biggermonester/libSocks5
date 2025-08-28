#include <cstring>
#include <signal.h>

#ifdef __linux__
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#elif _WIN32
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

#include <iostream>
#include <string>
#include <algorithm>

#include "SocksServer.hpp"
#include "SocksDef.hpp"


#define MAXPENDING 200

#ifndef USERNAME
    #define USERNAME "username"
#endif
#ifndef PASSWORD
    #define PASSWORD "password"
#endif


#ifdef __linux__
#elif _WIN32
    #pragma pack(push, 1)
#endif

// Handshake
struct MethodIdentificationPacket 
{
    uint8_t version, nmethods;
    // uint8_t methods[nmethods];
} 
#ifdef __linux__
    __attribute__((packed));
#elif _WIN32
    ;
#endif


struct MethodSelectionPacket 
{
    uint8_t version, method;
    MethodSelectionPacket(uint8_t met) : version(5), method(met) {}
} 
#ifdef __linux__
    __attribute__((packed));
#elif _WIN32
    ;
#endif

// Requests
struct SOCKS5RequestHeader 
{
    uint8_t version, cmd, rsv /* = 0x00 */, atyp;
} 
#ifdef __linux__
    __attribute__((packed));
#elif _WIN32
    ;
#endif


struct SOCK5IP4RequestBody 
{
    uint32_t ip_dst;
    uint16_t port;
} 
#ifdef __linux__
    __attribute__((packed));
#elif _WIN32
    ;
#endif


struct SOCK5DNameRequestBody 
{
    uint8_t length;
    // uint8_t dname[length]; 
} 
#ifdef __linux__
    __attribute__((packed));
#elif _WIN32
    ;
#endif


// Responses
struct SOCKS5Response 
{
    uint8_t version, cmd, rsv /* = 0x00 */, atyp;
    uint32_t ip_src;
    uint16_t port_src;
    
    SOCKS5Response(bool succeded = true) : version(5), cmd(succeded ? static_cast<uint8_t>(Response::Succeded) : static_cast<uint8_t>(Response::GenError)), rsv(0), atyp(static_cast<uint8_t>(AddressType::IPv4)) { }
} 
#ifdef __linux__
    __attribute__((packed));
#elif _WIN32
    ;
#endif


#ifdef __linux__
#elif _WIN32
    #pragma pack(pop)
#endif


using namespace std;


int read_variable_string(int sock, uint8_t *buffer, uint8_t max_sz) 
{
    if(recv_sock(sock, (char*)buffer, 1) != 1 || buffer[0] > max_sz)
        return false;

    uint8_t sz = buffer[0];
    if(recv_sock(sock, (char*)buffer, sz) != sz)
        return -1;

    return sz;
}


bool check_auth(int sock)
{
    uint8_t buffer[128];
    if(recv_sock(sock, (char*)buffer, 1) != 1 || buffer[0] != 1)
        return false;
    int sz = read_variable_string(sock, buffer, 127);
    if(sz == -1)
        return false;
    buffer[sz] = 0;
    if(strcmp((char*)buffer, USERNAME))
        return false;
    sz = read_variable_string(sock, buffer, 127);
    if(sz == -1)
        return false;
    buffer[sz] = 0;
    if(strcmp((char*)buffer, PASSWORD))
        return false;
    buffer[0] = 1;
    buffer[1] = 0;
    return send_sock(sock, (const char*)buffer, 2) == 2;
}


//
// SocksTunnelServer
// 
SocksTunnelServer::SocksTunnelServer(int serverfd, int serverPort, int id)
: m_serverfd(serverfd)
, m_serverPort(serverPort)
, m_state(SocksState::INIT)
, m_id(id)
{
    m_internalBuffer.resize(BUF_SIZE);
}


SocksTunnelServer::~SocksTunnelServer() = default;


int SocksTunnelServer::init()
{
    // std::cout << "handle_connection" << std::endl;
    std::cout << "[+] SocksTunnelServer init" << std::endl;   

    // Here we need to split ! 
    // we are in the TeamServer

    MethodIdentificationPacket packet;
    int read_size = recv_sock(m_serverfd.get(), (char*)&packet, sizeof(MethodIdentificationPacket));

    if(read_size != sizeof(MethodIdentificationPacket) || packet.version != 5)
    {
        std::cout << "[-] Wrong version of proxychain, only proxychain5 is supported." << std::endl;
        m_serverfd.reset();
        return 0;
    }

    read_size = recv_sock(m_serverfd.get(), &m_internalBuffer[0], packet.nmethods);

    if(read_size != packet.nmethods)
    {
        m_serverfd.reset();
        return 0;
    }

    MethodSelectionPacket methode_response(static_cast<uint8_t>(Method::NotAvailable));
    for(unsigned i(0); i < packet.nmethods; ++i)
    {
        if(ALLOW_NO_AUTH && m_internalBuffer[i] == static_cast<uint8_t>(Method::NoAuth))
            methode_response.method = static_cast<uint8_t>(Method::NoAuth);
        if(m_internalBuffer[i] == static_cast<uint8_t>(Method::Auth))
            methode_response.method = static_cast<uint8_t>(Method::Auth);
    }

    int write_size = send_sock(m_serverfd.get(), (const char*)&methode_response, sizeof(MethodSelectionPacket)) ;

    // std::cout << "MethodSelectionPacket " << std::to_string(write_size) << std::endl;

    if(write_size != sizeof(MethodSelectionPacket) || methode_response.method == static_cast<uint8_t>(Method::NotAvailable))
    {
        m_serverfd.reset();
        return 0;
    }

    if(methode_response.method == static_cast<uint8_t>(Method::Auth))
        if(!check_auth(m_serverfd.get()))
        {
            m_serverfd.reset();
            return 0;
        }


    std::cout << "[+] HandShake ok" << std::endl;   

    SOCKS5RequestHeader header;
    read_size = recv_sock(m_serverfd.get(), (char*)&header, sizeof(SOCKS5RequestHeader));

    if(read_size != sizeof(SOCKS5RequestHeader) || header.version != 5 || header.cmd != static_cast<uint8_t>(Command::Connect) || header.rsv != 0)
    {
        m_serverfd.reset();
        return 0;
    }

    // std::cout << "SOCKS5RequestHeader " << std::to_string(read_size) << std::endl;

    if(header.atyp != static_cast<uint8_t>(AddressType::IPv4))
    {
        m_serverfd.reset();
        return 0;
    }

    SOCK5IP4RequestBody req;
    read_size = recv_sock(m_serverfd.get(), (char*)&req, sizeof(SOCK5IP4RequestBody));

    // std::cout << "SOCK5IP4RequestBody " << std::to_string(read_size) << std::endl;

    if(read_size != sizeof(SOCK5IP4RequestBody))
    {
        m_serverfd.reset();
        return 0;
    }

    m_ipDst = req.ip_dst;
    m_port = req.port;

    return 1;
}


int SocksTunnelServer::finishHandshake()
{
    SOCKS5Response response;
    response.ip_src = 0;
    response.port_src = m_serverPort;
    send_sock(m_serverfd.get(), (const char*)&response, sizeof(SOCKS5Response));

    return 1;
}


int SocksTunnelServer::process(std::string& dataIn, std::string& dataOut)
{
    if(dataIn.size()>0)
        send_sock(m_serverfd.get(), dataIn.data(), dataIn.size());

    int bytes_received;
    SocketReadStatus status = readAllDataFromSocket(m_serverfd.get(), &m_internalBuffer[0], bytes_received, 10);
    if(status == SocketReadStatus::Error || status == SocketReadStatus::Disconnected)
        return -1;
    if(status == SocketReadStatus::Timeout)
        bytes_received = 0;

    dataOut.assign(&m_internalBuffer[0], bytes_received);

    return 1;
}


// https://fr.manpages.org/fd_isset/2
// https://man7.org/linux/man-pages/man2/recv.2.html
//
// SocksServer
// 
SocksServer::SocksServer(int serverPort)
: m_serverPort(serverPort)
, m_isStoped(true)
, m_isLaunched(false)
{
    #ifdef __linux__
    #elif _WIN32
        WSADATA wsaData;
        int iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    #endif
}


SocksServer::~SocksServer() 
{ 
    stop();
}


void SocksServer::launch()
{
    m_socks5Server = std::make_unique<std::thread>(&SocksServer::handleConnection, this);
}


void SocksServer::stop()
{
    m_isStoped=true;
    m_listen_sock.reset();

    if(m_socks5Server)
    {
        m_socks5Server->join();
        m_socks5Server.reset();
    }
}


int SocksServer::createServerSocket(struct sockaddr_in &echoclient) 
{
    int serversock;
    struct sockaddr_in echoserver;

    if ((serversock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) 
    {
        std::cout << "[-] Could not create socket.\n";
        return -1;
    }

    // Set the SO_REUSEADDR option
    int opt = 1;
    if (setsockopt(serversock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt)) < 0) 
    {
        std::cout << "[-] Could not set socket option.\n";
        #ifdef __linux__
            close(serversock);
        #elif _WIN32
            closesocket(serversock);
        #endif
        return -1;
    }

    // Construct the server sockaddr_in structure 
    memset(&echoserver, 0, sizeof(echoserver));       // Clear struct 
    echoserver.sin_family = AF_INET;                  // Internet/IP 
    echoserver.sin_addr.s_addr = htonl(INADDR_ANY);   // Incoming addr 
    echoserver.sin_port = htons(m_serverPort);       // server port 

    // Bind the server socket 
    if (bind(serversock, (struct sockaddr *) &echoserver, sizeof(echoserver)) < 0) 
    {
        #ifdef __linux__
            close(serversock);
        #elif _WIN32
            closesocket(serversock);
        #endif
        std::cout << "[-] Bind error.\n";
        return -1;
    }

    // Listen on the server socket 
    if (listen(serversock, MAXPENDING) < 0) 
    {
        #ifdef __linux__
            close(serversock);
        #elif _WIN32
            closesocket(serversock);
        #endif
        std::cout << "[-] Listen error.\n";
        return -1;
    }
    return serversock;
}


int SocksServer::handleConnection()
{
    struct sockaddr_in echoclient;
    m_listen_sock.reset(createServerSocket(echoclient));

    if(m_listen_sock.get() == -1)
    {
        std::cout << "[-] Failed to create server\n";
        return -1;
    }

#ifdef __linux__
    signal(SIGPIPE, sig_handler);
#elif _WIN32
#endif

    m_isStoped = false;
    m_isLaunched = true;
    int idSocksTunnelServer=0;
    while(!m_isStoped)
    {
        int clientlen = sizeof(echoclient);
        int clientsock;
#ifdef __linux__
        if ((clientsock = accept(m_listen_sock.get(), (struct sockaddr *) &echoclient, (uint*)&clientlen)) > 0)
#elif _WIN32
        if ((clientsock = accept(m_listen_sock.get(), (struct sockaddr *) &echoclient, &clientlen)) > 0)
#endif
        {
            std::unique_ptr<SocksTunnelServer> socksTunnelServer = std::make_unique<SocksTunnelServer>(clientsock, m_serverPort, idSocksTunnelServer);
            int initResult = socksTunnelServer->init();
            if(initResult)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_socksTunnelServers.push_back(std::move(socksTunnelServer));
            }
            idSocksTunnelServer++;
        }
    }

    std::cout << "[+] handleConnection stoped\n";

    m_listen_sock.reset();

    return 1;
}


void SocksServer::cleanTunnel()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_socksTunnelServers.erase(std::remove_if(m_socksTunnelServers.begin(), m_socksTunnelServers.end(),
                             [](const std::unique_ptr<SocksTunnelServer>& ptr) { return ptr == nullptr; }),
              m_socksTunnelServers.end());
}

std::size_t SocksServer::tunnelCount()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_socksTunnelServers.size();
}

SocksTunnelServer* SocksServer::getTunnel(std::size_t idx)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if(idx >= m_socksTunnelServers.size())
        return nullptr;
    return m_socksTunnelServers[idx].get();
}

void SocksServer::resetTunnel(std::size_t idx)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if(idx < m_socksTunnelServers.size())
        m_socksTunnelServers[idx].reset();
}