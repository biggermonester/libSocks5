#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>

#ifdef __linux__
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#elif _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

#include "SocksServer.hpp"

namespace
{
constexpr int TestTimeoutMs = 2000;

#ifdef _WIN32
class SocketRuntime
{
public:
    SocketRuntime()
    {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
            throw std::runtime_error("WSAStartup failed");
    }
    ~SocketRuntime()
    {
        WSACleanup();
    }
};
#else
class SocketRuntime
{
};
#endif

void closeSocket(int sock)
{
#ifdef __linux__
    ::close(sock);
#elif _WIN32
    ::closesocket(sock);
#endif
}

void require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::string bytes(std::initializer_list<uint8_t> values)
{
    std::string output;
    output.reserve(values.size());
    for (uint8_t value : values)
        output.push_back(static_cast<char>(value));
    return output;
}

bool waitFor(const std::function<bool()>& predicate, int timeoutMs = TestTimeoutMs)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (predicate())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

int reserveLocalPort()
{
    const int sock = static_cast<int>(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    require(sock >= 0, "reserve socket failed");

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    require(::bind(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "reserve bind failed");

#ifdef _WIN32
    int addressSize = sizeof(address);
#else
    socklen_t addressSize = sizeof(address);
#endif
    require(::getsockname(sock, reinterpret_cast<sockaddr*>(&address), &addressSize) == 0, "reserve getsockname failed");
    const int port = ntohs(address.sin_port);
    closeSocket(sock);
    return port;
}

int connectLocal(int port)
{
    const int sock = static_cast<int>(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    require(sock >= 0, "client socket failed");

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<uint16_t>(port));
    if (::connect(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
    {
        closeSocket(sock);
        throw std::runtime_error("client connect failed");
    }
    return sock;
}

std::string recvExact(int sock, std::size_t size)
{
    std::string output;
    output.resize(size);
    std::size_t offset = 0;
    while (offset < size)
    {
        fd_set readFds;
        FD_ZERO(&readFds);
        FD_SET(sock, &readFds);

        timeval timeout{};
        timeout.tv_sec = TestTimeoutMs / 1000;
        timeout.tv_usec = (TestTimeoutMs % 1000) * 1000;

#ifdef _WIN32
        const int ready = ::select(0, &readFds, nullptr, nullptr, &timeout);
#else
        const int ready = ::select(sock + 1, &readFds, nullptr, nullptr, &timeout);
#endif
        require(ready > 0, "recv timeout");

        const int got = ::recv(sock, &output[offset], static_cast<int>(size - offset), 0);
        require(got > 0, "recv failed");
        offset += static_cast<std::size_t>(got);
    }
    return output;
}

void sendAll(int sock, const std::string& data)
{
    std::size_t offset = 0;
    while (offset < data.size())
    {
        const int sent = ::send(sock, data.data() + offset, static_cast<int>(data.size() - offset), 0);
        require(sent > 0, "send failed");
        offset += static_cast<std::size_t>(sent);
    }
}

struct SocksReply
{
    uint8_t version = 0;
    uint8_t code = 0;
    uint8_t atyp = 0;
    uint16_t port = 0;
};

SocksReply recvReply(int sock)
{
    const std::string header = recvExact(sock, 4);
    SocksReply reply;
    reply.version = static_cast<uint8_t>(header[0]);
    reply.code = static_cast<uint8_t>(header[1]);
    reply.atyp = static_cast<uint8_t>(header[3]);

    if (reply.atyp == static_cast<uint8_t>(AddressType::IPv4))
        recvExact(sock, 4);
    else if (reply.atyp == static_cast<uint8_t>(AddressType::DName))
    {
        const uint8_t length = static_cast<uint8_t>(recvExact(sock, 1)[0]);
        recvExact(sock, length);
    }
    else if (reply.atyp == static_cast<uint8_t>(AddressType::IPv6))
        recvExact(sock, 16);
    else
        throw std::runtime_error("invalid reply ATYP");

    const std::string portBytes = recvExact(sock, 2);
    uint16_t portNetwork = 0;
    std::memcpy(&portNetwork, portBytes.data(), sizeof(portNetwork));
    reply.port = ntohs(portNetwork);
    return reply;
}

class RunningSocksServer
{
public:
    RunningSocksServer()
        : port(reserveLocalPort()),
          server(port)
    {
        server.launch();
        require(waitFor([this] { return server.isServerLaunched(); }), "server did not launch");
        require(!server.isServerStoped(), "server stopped during launch");
    }

    ~RunningSocksServer()
    {
        server.stop();
    }

    int port;
    SocksServer server;
};

int connectAndNegotiateNoAuth(RunningSocksServer& running)
{
    const int client = connectLocal(running.port);
    sendAll(client, bytes({0x05, 0x01, 0x00}));
    require(recvExact(client, 2) == bytes({0x05, 0x00}), "no-auth negotiation failed");
    return client;
}

void testNoAcceptableMethod()
{
    RunningSocksServer running;
    const int client = connectLocal(running.port);
    sendAll(client, bytes({0x05, 0x01, 0x01}));
    require(recvExact(client, 2) == bytes({0x05, 0xff}), "unsupported auth method should be rejected");
    closeSocket(client);
}

void testIpv4ConnectQueuesTunnelAndSuccessReply()
{
    RunningSocksServer running;
    const int client = connectAndNegotiateNoAuth(running);
    sendAll(client, bytes({0x05, 0x01, 0x00, 0x01, 127, 0, 0, 1, 0x00, 0x50}));

    require(waitFor([&running] { return running.server.tunnelCount() == 1; }), "IPv4 CONNECT did not queue a tunnel");
    SocksTunnelServer* tunnel = running.server.getTunnel(0);
    require(tunnel != nullptr, "queued tunnel is null");
    require(tunnel->getIpDst() == ::inet_addr("127.0.0.1"), "IPv4 destination mismatch");
    require(tunnel->getPort() == htons(80), "destination port should be stored in network byte order");

    tunnel->finishHandshake();
    const SocksReply reply = recvReply(client);
    require(reply.version == 5, "success reply version mismatch");
    require(reply.code == static_cast<uint8_t>(Response::Succeeded), "success reply code mismatch");
    require(reply.atyp == static_cast<uint8_t>(AddressType::IPv4), "success reply ATYP mismatch");
    require(reply.port == static_cast<uint16_t>(running.port), "success reply port should be network ordered");
    closeSocket(client);
}

void testDomainNameConnectReturnsExplicitUnsupportedAddress()
{
    RunningSocksServer running;
    const int client = connectAndNegotiateNoAuth(running);
    std::string request = bytes({0x05, 0x01, 0x00, 0x03, 11});
    request += "example.com";
    request += bytes({0x00, 0x50});
    sendAll(client, request);

    const SocksReply reply = recvReply(client);
    require(reply.version == 5, "domain reject reply version mismatch");
    require(reply.code == static_cast<uint8_t>(Response::AddressTypeNotSupported), "domain reject code mismatch");
    require(running.server.tunnelCount() == 0, "domain CONNECT should not queue a tunnel");
    closeSocket(client);
}

void testUnsupportedCommandReturnsExplicitFailure()
{
    RunningSocksServer running;
    const int client = connectAndNegotiateNoAuth(running);
    sendAll(client, bytes({0x05, 0x02, 0x00, 0x01, 127, 0, 0, 1, 0x00, 0x50}));

    const SocksReply reply = recvReply(client);
    require(reply.version == 5, "command reject reply version mismatch");
    require(reply.code == static_cast<uint8_t>(Response::CommandNotSupported), "command reject code mismatch");
    require(running.server.tunnelCount() == 0, "unsupported command should not queue a tunnel");
    closeSocket(client);
}
} // namespace

int main()
{
    SocketRuntime runtime;
    testNoAcceptableMethod();
    testIpv4ConnectQueuesTunnelAndSuccessReply();
    testDomainNameConnectReturnsExplicitUnsupportedAddress();
    testUnsupportedCommandReturnsExplicitFailure();
    return 0;
}
