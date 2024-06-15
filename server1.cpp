#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <vector>
#include "bulletinBoard.cpp"
#include <regex>
#include <csignal>

#define DESIRED_ADDRESS "127.0.0.1"
#define DESIRED_PORT 3500
#define BUFSIZE 512

using namespace std;

regex username_pattern("[a-zA-Z0-9]+");

vector<string> bufferSplit(const char *buffer)
{
    int i = 0;
    vector<string> bufferArray;
    string tempbuffer;
    while (buffer[i] != '\0')
    {
        if (buffer[i] == ' ')
        {
            if (!tempbuffer.empty())
            {
                bufferArray.push_back(tempbuffer);
                tempbuffer.clear();
            }
        }
        else
        {
            tempbuffer.push_back(buffer[i]);
        }
        i++;
    }
    if (!tempbuffer.empty())
    {
        bufferArray.push_back(tempbuffer);
    }
    return bufferArray;
}

int handle_commands(vector<string> buffer, bulletinBoard *user, int client_sock)
{
    cout << "handling command" << endl;
    cout << "buffer.size()=>" << buffer.size() << endl;
    string command = buffer[0];
    cout << "command=>" << command << endl;
    string arg1 = buffer.size() > 1 ? buffer[1] : "";
    string arg2 = buffer.size() > 2 ? buffer[2] : "";
    char *message;
    if (command == "USER")
    {
        if (arg1 != "" && buffer.size() == 1)
        {
            if (regex_match(arg1, username_pattern))
            {
                cout << "regix matched" << arg1 << endl;
                user->setName(arg1);
                // message = "HELLO ";
                // strcat(message, arg1.c_str());
                // strcat(message, " Welcome to bulletin board server.\n");
                // send(client_sock, message, strlen(message), 0);
            }
            else
            {
                message = "ERROR USER name does not contains any special characters.\n";
                cout << strlen(message) - 1 << endl;
                send(client_sock, message, strlen(message), 0);
            }
        }
        else
        {
            message = "ERROR USER command takes 1 positional arguments.\n";
            cout << strlen(message) - 1 << endl;
            send(client_sock, message, strlen(message), 0);
        }
    }
    else if (command == "READ")
    {
    }
    else if (command == "WRITE")
    {
    }
    else if (command == "REPLACE")
    {
    }
    else if (command == "QUIT")
    {
    }
    else
    {
        message = "ERROR entered command is incorrect.\n";
        cout << strlen(message) - 1 << endl;
        send(client_sock, message, strlen(message), 0);
    }
    return 1;
}

void *handle_client(void *args)
{

    cout << "connection accepted" << endl;
    int client_sock = *(int *)args;
    delete (int *)args;
    const char *welcomMessage = "Connection establish succesfully! \n";
    send(client_sock, welcomMessage, strlen(welcomMessage), 0);
    const size_t bufferSize = 1024 * 1024;
    char *buffer = new char[bufferSize];
    bulletinBoard *user = new bulletinBoard();
    while (true)
    {
        // char buffer[1024];
        int bytes_received = recv(client_sock, buffer, bufferSize - 1, 0);

        if (bytes_received > 0)
        {
            buffer[bytes_received] = '\0'; // Null-terminate the received data
            vector<string> bufferArray = bufferSplit(buffer);
            if (bufferArray.size() > 0 && bufferArray.size() <= 3)
            {
                handle_commands(bufferArray, user, client_sock);
            }
            else
            {
                char *message;
                message = "ERROR entered command is incorrect.\n";
                cout << strlen(message) - 1 << endl;
                send(client_sock, message, strlen(message), 0);
            }
            // cout << buffer << endl;
        }
        else if (bytes_received == 0)
        {
            // Connection was closed by the client
            cout << "Connection closed by the client" << endl;
            break;
        }
        else
        {
            // An error occurred
            perror("recv");
            break;
        }
    }

    return nullptr;
}

void signalHandler(int signal)
{
    cout << "Received signal " << signal << ", terminating the server." << endl;
    exit(signal);
}

int main()
{
    signal(SIGINT, signalHandler);

    int sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock == -1)
    {
        cerr << "Unable to connect to socket" << endl;
        return 1;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DESIRED_PORT);
    addr.sin_addr.s_addr = inet_addr(DESIRED_ADDRESS);
    // MAIN PART
    if (bind(sock, (sockaddr *)&addr, sizeof(addr)) == -1)
    {
        cerr << "Error binding sockets" << endl;
        close(sock);
        return 1;
    }

    if (listen(sock, 20) == -1)
    {
        cerr << "Error listening on socket" << endl;
        close(sock);
        return 1;
    }

    cout << "server is listening on Port:" << DESIRED_PORT << endl;

    while (true)
    {
        sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_sock = accept(sock, (sockaddr *)&client_addr, &client_addr_len);
        if (client_sock == -1)
        {
            cerr << "err accepting connection" << endl;
            continue;
        }

        pthread_t client_thread;
        int *new_sock = new int;
        *new_sock = client_sock;
        cerr << "thread created" << endl;
        if (pthread_create(&client_thread, nullptr, handle_client, new_sock) == -1)
        {
            cerr << "err creating thread" << endl;
            close(client_sock);
            continue;
        }
        pthread_detach(client_thread);
    }

    close(sock);
    return EXIT_SUCCESS;
}