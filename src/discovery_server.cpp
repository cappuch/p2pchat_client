#include <iostream>
#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
#include <sstream>
#include <cstring>
#include <algorithm>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
using ssize_t = SSIZE_T;
#else
#include <netinet/in.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#endif

std::map<std::string, std::string> user_registry; // this might be the worst idea ever -- not storing it to disk)
std::mutex registry_mutex;

void handle_client(int client_socket) {
    char buffer[4096];
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read <= 0) {
            break;
        }

        std::string request(buffer);
        // Remove newlines
        request.erase(std::remove(request.begin(), request.end(), '\n'), request.end());
        request.erase(std::remove(request.begin(), request.end(), '\r'), request.end());

        std::stringstream ss(request);
        std::string command;
        ss >> command;

        std::string response;

        if (command == "REGISTER") {
            std::string username, peer_data;
            ss >> username;
            std::getline(ss, peer_data);
            if (!peer_data.empty() && peer_data[0] == ' ') {
                peer_data = peer_data.substr(1);
            }

            if (!username.empty() && !peer_data.empty()) {
                std::lock_guard<std::mutex> lock(registry_mutex);
                user_registry[username] = peer_data;
                std::cout << "[REG] Registered " << username << "\n";
                response = "OK\n";
            } else {
                response = "ERROR Invalid format\n";
            }
        } else if (command == "LOOKUP") {
            std::string username;
            ss >> username;
            std::lock_guard<std::mutex> lock(registry_mutex);
            if (user_registry.count(username)) {
                response = "FOUND " + user_registry[username] + "\n";
                std::cout << "[LOOKUP] Found " << username << "\n";
            } else {
                response = "NOT_FOUND\n";
                std::cout << "[LOOKUP] Not found " << username << "\n";
            }
        } else {
            response = "ERROR Unknown command\n";
        }

        send(client_socket, response.c_str(), response.length(), 0);
    }
#ifdef _WIN32
    closesocket(client_socket);
#else
    close(client_socket);
#endif
}

int main(int argc, char** argv) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }
#endif

    int port = 8000;
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Socket creation failed\n";
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed\n";
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        std::cerr << "Listen failed\n";
        return 1;
    }

    std::cout << "Discovery Server listening on port " << port << "...\n";

    while (true) {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket >= 0) {
            std::thread(handle_client, client_socket).detach();
        }
    }

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
