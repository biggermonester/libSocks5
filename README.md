# libSocks5

A lightweight C++ SOCKS5 server and tunnel client for pivoting traffic through an intermediate host. The tunnel server accepts SOCKS5 connections, negotiates authentication and forwards data to a remote `SocksTunnelClient` running closer to the target network.

## Features
- SOCKS5 CONNECT support with optional username/password authentication
- Split architecture: public SOCKS server and tunnel client can run on different machines
- Cross-platform build (Linux/Windows) using CMake

## Build
```bash
mkdir build && cd build
cmake ..
make -j4
```

## Usage
1. Launch the test server:
```bash
./TestsSocksServer
```
2. Point a SOCKS-aware client at `localhost:1080` (e.g., `proxychains curl http://example.com`).

## Notes
This project is a proof-of-concept; error handling and robustness are limited. Run behind a firewall and avoid exposing it to untrusted networks.
