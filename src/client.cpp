#include "p2p/Node.hpp"
#include "p2p/Identity.hpp"
#include "p2p/Peer.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/string.hpp>

#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <iomanip>
#include <algorithm>
#include <iterator>
#include <fstream>
#include <map>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#define close closesocket
#define read(s, b, l) recv(s, b, l, 0)
#else
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#endif
#include <cstring>
#include <mutex>

using namespace ftxui;


std::vector<uint8_t> fromHex(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        uint8_t byte = (uint8_t)strtol(byteString.c_str(), nullptr, 16);
        bytes.push_back(byte);
    }
    return bytes;
}

template<size_t N>
std::array<uint8_t, N> fromHexArray(const std::string& hex) {
    std::vector<uint8_t> v = fromHex(hex);
    std::array<uint8_t, N> arr;
    if (v.size() >= N) {
        std::copy_n(v.begin(), N, arr.begin());
    }
    return arr;
}

std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}


class DiscoveryClient {
public:
    DiscoveryClient(const std::string& host, int port) : host_(host), port_(port) {}

    bool registerUser(const std::string& username, const std::string& peerData) {
        std::string request = "REGISTER " + username + " " + peerData + "\n";
        std::string response = sendRequest(request);
        return response == "OK";
    }

    std::string lookupUser(const std::string& username) {
        std::string request = "LOOKUP " + username + "\n";
        std::string response = sendRequest(request);
        if (response.rfind("FOUND ", 0) == 0) {
            return response.substr(6);
        }
        return "";
    }

private:
    std::string host_;
    int port_;

    std::string sendRequest(const std::string& request) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return "";

        sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port_);
        
        if (inet_pton(AF_INET, host_.c_str(), &serv_addr.sin_addr) <= 0) {
            close(sock);
            return "";
        }

        if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            close(sock);
            return "";
        }

        send(sock, request.c_str(), request.length(), 0);

        char buffer[4096] = {0};
        read(sock, buffer, 4096);
        close(sock);

        std::string response(buffer);
        response.erase(std::remove(response.begin(), response.end(), '\n'), response.end());
        response.erase(std::remove(response.begin(), response.end(), '\r'), response.end());
        return response;
    }
};

struct Friend {
    std::string username;
    std::string peerString;
    p2p::PeerId id;
};

struct Message {
    std::string sender;
    std::string content;
    bool isSelf;
};

std::map<std::string, Friend> friends;
std::map<std::string, std::vector<Message>> chatHistory;
std::string myUsername = "Anon"; // placeholder until registered
std::string myPeerString;
std::mutex dataMutex;

void saveFriends() {
    std::ofstream out("friends.txt");
    for (const auto& [name, f] : friends) {
        out << name << " " << f.peerString << "\n";
    }
}

void loadFriends(p2p::Node& node) {
    std::ifstream in("friends.txt");
    std::string line;
    while (std::getline(in, line)) {
        std::stringstream ss(line);
        std::string name, peerStr;
        ss >> name >> peerStr;
        if (!name.empty() && !peerStr.empty()) {
            auto parts = split(peerStr, '|');
            if (parts.size() == 5) {
                try {
                    p2p::Peer peer;
                    peer.id = fromHexArray<32>(parts[0]);
                    peer.publicKey = fromHexArray<32>(parts[1]);
                    peer.signPublic = fromHexArray<32>(parts[2]);
                    peer.ip = parts[3];
                    peer.port = std::stoi(parts[4]);
                    node.addPeer(peer);
                    
                    friends[name] = {name, peerStr, peer.id};
                } catch (...) {}
            }
        }
    }
}

int main(int argc, char** argv) {
    uint16_t port = 0;
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }

    p2p::Node node("0.0.0.0", port);
    DiscoveryClient discovery("127.0.0.1", 8000);
    
    auto screen = ScreenInteractive::Fullscreen();

    node.onTypedMessage([&](const p2p::PeerId& from, p2p::MessageType type, const std::vector<uint8_t>& payload) {
        if (type == p2p::MessageType::TEXT) {
            std::string msg(payload.begin(), payload.end());
            std::string senderName = p2p::toHex(from).substr(0, 8);
            
            std::lock_guard<std::mutex> lock(dataMutex);
            for(const auto& [name, f] : friends) {
                 if(f.id == from) {
                     senderName = name;
                     break;
                 }
            }
            
            chatHistory[senderName].push_back({senderName, msg, false});
            screen.Post(Event::Custom);
        }
    });

    node.start();

    auto id = node.identity();
    myPeerString = p2p::toHex(id.id) + "|" + p2p::toHex(id.publicKey) + "|" + p2p::toHex(id.signPublic) + "|127.0.0.1|" + std::to_string(node.port());
    
    loadFriends(node);

    std::vector<std::string> friendNames;
    int selectedFriendIdx = 0;
    std::string inputMessage;
    std::string statusMessage = "Welcome! Use /register <name> to start.";

    auto refreshFriendList = [&]() {
        friendNames.clear();
        for (const auto& [name, f] : friends) {
            friendNames.push_back(name);
        }
        if (friendNames.empty()) {
            friendNames.push_back("(No friends)");
        }
    };
    refreshFriendList();

    InputOption inputOption;
    inputOption.on_enter = [&] {
        if (inputMessage.empty()) return;

        if (inputMessage[0] == '/') {
            if (inputMessage.rfind("/register ", 0) == 0) {
                std::string username = inputMessage.substr(10);
                if (discovery.registerUser(username, myPeerString)) {
                    myUsername = username;
                    statusMessage = "Registered as " + username;
                } else {
                    statusMessage = "Registration failed";
                }
            } else if (inputMessage.rfind("/add ", 0) == 0) {
                std::string username = inputMessage.substr(5);
                std::string peerStr = discovery.lookupUser(username);
                if (!peerStr.empty()) {
                    auto parts = split(peerStr, '|');
                    if (parts.size() == 5) {
                        try {
                            p2p::Peer peer;
                            peer.id = fromHexArray<32>(parts[0]);
                            peer.publicKey = fromHexArray<32>(parts[1]);
                            peer.signPublic = fromHexArray<32>(parts[2]);
                            peer.ip = parts[3];
                            peer.port = std::stoi(parts[4]);
                            
                            node.addPeer(peer);
                            
                            std::lock_guard<std::mutex> lock(dataMutex);
                            friends[username] = {username, peerStr, peer.id};
                            saveFriends();
                            refreshFriendList();
                            statusMessage = "Added friend " + username;
                        } catch (...) {
                            statusMessage = "Invalid peer data";
                        }
                    }
                } else {
                    statusMessage = "User not found";
                }
            } else if (inputMessage == "/quit") {
                screen.ExitLoopClosure()();
            } else {
                statusMessage = "Unknown command";
            }
        } else {
            if (friendNames[0] != "(No friends)" && selectedFriendIdx < friendNames.size()) {
                std::string targetName = friendNames[selectedFriendIdx];
                if (friends.count(targetName)) {
                    p2p::PeerId targetId = friends[targetName].id;
                    if (node.sendText(targetId, inputMessage)) {
                        std::lock_guard<std::mutex> lock(dataMutex);
                        chatHistory[targetName].push_back({myUsername, inputMessage, true});
                    } else {
                        statusMessage = "Failed to send";
                    }
                }
            } else {
                statusMessage = "No friend selected";
            }
        }
        inputMessage = "";
    };
    
    auto input_component = Input(&inputMessage, "Type a message or command...", inputOption);

    auto friend_list_component = Menu(&friendNames, &selectedFriendIdx);

    auto container = Container::Horizontal({
        friend_list_component,
        input_component
    });

    auto renderer = Renderer(container, [&] {
        
        Elements chat_elements;
        std::string currentFriend = (friendNames.size() > 0 && friendNames[0] != "(No friends)") ? friendNames[selectedFriendIdx] : "";
        
        if (!currentFriend.empty()) {
            std::lock_guard<std::mutex> lock(dataMutex);
            for (const auto& msg : chatHistory[currentFriend]) {
                if (msg.isSelf) {
                    chat_elements.push_back(hbox({
                        text(msg.content) | color(Color::Cyan),
                        text(" :Me") | bold
                    }) | align_right);
                } else {
                    chat_elements.push_back(hbox({
                        text(msg.sender + ": ") | bold,
                        text(msg.content)
                    }));
                }
            }
        } else {
            chat_elements.push_back(text("Select a friend to chat") | center);
        }

        auto chat_win = window(text("Chat: " + currentFriend), 
            vbox(std::move(chat_elements)) | yframe | flex
        );

        auto friends_win = window(text("Friends"), 
            friend_list_component->Render() | vscroll_indicator | frame | size(WIDTH, GREATER_THAN, 20)
        );

        return vbox({
            hbox({
                friends_win,
                chat_win | flex
            }) | flex,
            window(text("Input"), input_component->Render()),
            text(statusMessage) | color(Color::Yellow),
            text("Nav: [Up/Down] Select Friend | [Right] Focus Chat | [Enter] Send") | dim
        });
    });

    screen.Loop(renderer);

    node.stop();
    return 0;
}
