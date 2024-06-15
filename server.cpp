#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 12345
#define BUFFER_SIZE 1024

void handle_client(int client_socket) {
    char buffer[BUFFER_SIZE];
    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received < 0) {
            std::cerr << "Error reading from socket\n";
            break;
        } else if (bytes_received == 0) {
            std::cout << "Client disconnected\n";
            break;
        }
        std::cout << "Received: " << buffer << std::endl;

        ssize_t bytes_sent = send(client_socket, buffer, bytes_received, 0);
        if (bytes_sent < 0) {
            std::cerr << "Error sending to socket\n";
            break;
        }
    }
    close(client_socket);
}

int main() {
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        std::cerr << "Error creating socket\n";
        return 1;
    }

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        std::cerr << "Error binding socket\n";
        close(server_socket);
        return 1;
    }

    if (listen(server_socket, 10) == -1) {
        std::cerr << "Error listening on socket\n";
        close(server_socket);
        return 1;
    }

    std::cout << "Server listening on port " << PORT << std::endl;

    while (true) {
        sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (sockaddr*)&client_addr, &client_addr_len);
        if (client_socket == -1) {
            std::cerr << "Error accepting connection\n";
            continue;
        }

        std::cout << "Client connected\n";
        std::thread(handle_client, client_socket).detach();
    }

    close(server_socket);
    return 0;
}
