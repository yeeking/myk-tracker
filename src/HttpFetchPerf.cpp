#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace
{
// Simple RAII wrapper to close a socket on scope exit.
struct SocketHandle
{
    int fd{-1};
    ~SocketHandle()
    {
        if (fd >= 0)
            ::close(fd);
    }
};

bool connectToHost(const std::string& host, const std::string& port, SocketHandle& handle)
{
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res = nullptr;
    const auto gaiErr = ::getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
    if (gaiErr != 0)
    {
        std::cerr << "getaddrinfo failed: " << gai_strerror(gaiErr) << '\n';
        return false;
    }

    for (auto p = res; p != nullptr; p = p->ai_next)
    {
        handle.fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (handle.fd < 0)
            continue;

        if (::connect(handle.fd, p->ai_addr, p->ai_addrlen) == 0)
        {
            ::freeaddrinfo(res);
            return true;
        }

        ::close(handle.fd);
        handle.fd = -1;
    }

    ::freeaddrinfo(res);
    std::cerr << "Unable to connect to " << host << ':' << port << '\n';
    return false;
}

bool sendRequest(int fd, const std::string& request)
{
    const char* data = request.c_str();
    std::size_t remaining = request.size();
    while (remaining > 0)
    {
        const auto sent = ::send(fd, data, remaining, 0);
        if (sent < 0)
            return false;
        data += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
    return true;
}

std::size_t readResponse(int fd)
{
    std::size_t total = 0;
    std::vector<char> buffer(4096);
    while (true)
    {
        const auto received = ::recv(fd, buffer.data(), buffer.size(), 0);
        if (received < 0)
            return total;
        if (received == 0)
            break;
        total += static_cast<std::size_t>(received);
    }
    return total;
}
} // namespace

int main()
{
    const std::string host = "127.0.0.1";
    const std::string port = "8080";
    const std::string request = "GET /state HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";

    while (true)
    {
        SocketHandle socketHandle;
        const auto loopStart = std::chrono::steady_clock::now();

        if (!connectToHost(host, port, socketHandle))
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        if (!sendRequest(socketHandle.fd, request))
        {
            std::cerr << "Failed to send request\n";
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        const auto readStart = std::chrono::steady_clock::now();
        const auto bytes = readResponse(socketHandle.fd);
        const auto readEnd = std::chrono::steady_clock::now();

        const auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(readEnd - loopStart).count();
        const auto fetchMs = std::chrono::duration_cast<std::chrono::milliseconds>(readEnd - readStart).count();

        std::cout << "Received " << bytes << " bytes in " << fetchMs << "ms (total loop " << totalMs << "ms)\n";
        std::cout.flush();

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
