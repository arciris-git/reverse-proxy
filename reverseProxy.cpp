#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in>
#include <arpa/inet>
#include <fcntl.h>
#include <errno.h>

#include "ThreadPool.hpp"
#include "HttpInspector.hpp"

using namespace std;

constexpr int MAX_EVENTS = 64;
constexpr int BUFFER_SIZE = 4096;

void setNonBlocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

// Handle client connection in a worker thread
void handleClient(int clientSocket, string_view backendHost, int backendPort) {
    char buffer[BUFFER_SIZE];
    ssize_t bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    
    if (bytesRead <= 0) {
        close(clientSocket);
        return;
    }

    buffer[bytesRead] = '\0';
    string_view requestView(buffer, bytesRead);

    // 1. Inspect HTTP Headers/Payload for security violations
    if (!HttpInspector::isRequestSafe(requestView)) {
        string_view response = "HTTP/1.1 403 Forbidden\r\nContent-Length: 15\r\n\r\nAccess Denied\r\n";
        send(clientSocket, response.data(), response.size(), 0);
        close(clientSocket);
        return;
    }

    // 2. Connect to Backend Server
    int backendSock = socket(AF_INET, SOCK_STREAM, 0);
    if (backendSock < 0) {
        close(clientSocket);
        return;
    }

    sockaddr_in backendAddr{};
    backendAddr.sin_family = AF_INET;
    backendAddr.sin_port = htons(backendPort);
    inet_pton(AF_INET, backendHost.data(), &backendAddr.sin_addr);

    if (connect(backendSock, (struct sockaddr*)&backendAddr, sizeof(backendAddr)) < 0) {
        string_view response = "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 19\r\n\r\nBackend Unavailable\r\n";
        send(clientSocket, response.data(), response.size(), 0);
        close(backendSock);
        close(clientSocket);
        return;
    }

    // 3. Forward request to backend
    send(backendSock, buffer, bytesRead, 0);

    // 4. Read response from backend and pipe back to client
    ssize_t backendBytes = recv(backendSock, buffer, sizeof(buffer), 0);
    if (backendBytes > 0) {
        send(clientSocket, buffer, backendBytes, 0);
    }

    close(backendSock);
    close(clientSocket);
}

int main() {
    int port = 8080;
    string_view backendHost = "127.0.0.1";
    int backendPort = 9000;

    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(serverSock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "Bind failed\n";
        return 1;
    }

    listen(serverSock, SOMAXCONN);
    setNonBlocking(serverSock);

    int epollFd = epoll_create1(0);
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = serverSock;
    epoll_ctl(epollFd, EPOLL_CTL_ADD, serverSock, &ev);

    ThreadPool pool(4); // 4 Worker threads
    vector<epoll_event> events(MAX_EVENTS);

    cout << "Reverse Proxy running on port " << port << "...\n";

    while (true) {
        int nfds = epoll_wait(epollFd, events.data(), MAX_EVENTS, -1);
        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == serverSock) {
                sockaddr_in clientAddr{};
                socklen_t clientLen = sizeof(clientAddr);
                int clientSock = accept(serverSock, (struct sockaddr*)&clientAddr, &clientLen);
                if (clientSock >= 0) {
                    setNonBlocking(clientSock);
                    pool.enqueue(handleClient, clientSock, backendHost, backendPort);
                }
            }
        }
    }

    close(serverSock);
    close(epollFd);
    return 0;
}