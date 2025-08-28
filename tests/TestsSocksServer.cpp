#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <cstdint>

#include "SocksServer.hpp"
#include "SocksTunnelClient.hpp"


int main(int argc, char *argv[]) 
{
    SocksServer socksServer(1080);
    socksServer.launch();

    int iterations = 0;
    while(iterations < 50)
    {
        for(std::size_t i=0; i<socksServer.tunnelCount(); i++)
        {
            std::cout << "main loop idx " << i << std::endl;
            SocksTunnelServer* tunnel = socksServer.getTunnel(i);
            if(tunnel)
            {
                std::cout << "GO " << i << std::endl;

                uint32_t ip = tunnel->getIpDst();
                uint16_t port = tunnel->getPort();

                SocksTunnelClient socksTunnelClient;
                socksTunnelClient.init(ip, port);

                tunnel->finishHandshake();

                std::string dataIn;
                std::string dataOut;
                while(true)
                {
                    int res = tunnel->process(dataIn, dataOut);
                    if(res<=0)
                        break;

                    res = socksTunnelClient.process(dataOut, dataIn);
                    if(res<=0)
                        break;
                }

                std::cout << "End    " << i << std::endl;

                socksServer.resetTunnel(i);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        iterations++;
    }

    return 0;
}


