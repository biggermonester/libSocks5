# libSocks5

A lightweight **C++ SOCKS5** stack with a “twist”: a public SOCKS server can forward traffic to a remote tunnel client so you can pivot through an intermediate host. Cross-platform build via CMake. 
Beyond plain TCP forwarding, the library is transport-agnostic: traffic between the public SOCKS server and the remote tunnel client can be relayed over **any medium** — HTTP(S), raw TCP, files, or even named pipes. This flexibility allows you to adapt the tunnel to restrictive environments.

[App] ⇆ [SOCKS5 Server] =={ HTTP / HTTPS / TCP / File / Pipe }== [Tunnel Client] ⇆ [Target]

## Features

* SOCKS5 **CONNECT** support
* Optional **username/password** authentication
* **Split architecture**: run the public SOCKS server and the tunnel client on different machines
* **Linux & Windows** builds with CMake

## Build

```bash
# Clone
git clone https://github.com/maxDcb/libSocks5.git
cd libSocks5

# Configure & build (Release)
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j
```

On Windows with MSVC:

```powershell
cmake -B build -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Binaries (tests / examples)

After building, you’ll get small test/example executables (see `tests/`). Typical names include:

* `TestsSocksServer` – runs a local SOCKS5 server (useful for quick checks)
* (If a client example is present) `TestsSocksClient` – simple client/ping through the server

Run the server locally on port `1080`:

```bash
./TestsSocksServer
```

Then try it with curl (any SOCKS5-aware tool works):

```bash
curl --socks5 127.0.0.1:1080 https://example.org
```

## Functional test (local)

You can validate end-to-end locally with a SOCKS-aware app:

1. Start the server:

   ```bash
   ./TestsSocksServer
   ```
2. Verify with curl or a browser configured to use **SOCKS5** at `127.0.0.1:1080`.

If your workflow includes a dedicated functional tester (e.g., `fonctionalTest` in a sibling project), use that in **client** mode against `127.0.0.1:1080`.

## Authentication (optional)

If authentication is enabled in your build/example, set a username and password (details depend on the sample you wire up). Then use:

```bash
curl --socks5 --proxy-user USER:PASS https://example.org
```

## License
MIT
