#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <vector>
#include "bulletinBoard.h"
#include <regex>
#include <csignal>
#include <unordered_map>
#include <fstream>
#include <utility>

#define DESIRED_ADDRESS "127.0.0.1"
#define BBPORT 9000
#define BUFSIZE 512

using namespace std;
regex pattern("[a-zA-Z][a-zA-Z0-9]*");
unordered_map<int, pair<string, string>> indexes;
pthread_mutex_t fileMutex = PTHREAD_MUTEX_INITIALIZER;

string remove_char(const string &s, char ch)
{
    string result = s;
    size_t pos = 0;
    while ((pos = result.find(ch, pos)) != string::npos)
    {
        result.erase(pos, 1);
    }
    return result;
}

unordered_map<int, pair<string, string>> createIndexes(string filename)
{
    ifstream file(filename);
    unordered_map<int, pair<string, string>> indexMap;

    if (file.is_open())
    {
        string line;
        while (getline(file, line))
        {
            size_t commaPos = line.find(',');
            int id = stoi(line.substr(0, commaPos));
            string lineWithoutId = line.substr(commaPos + 1);
            commaPos = lineWithoutId.find(',');
            string name = lineWithoutId.substr(0, commaPos);
            string message = remove_char(lineWithoutId.substr(commaPos + 1), '\"');
            indexMap[id] = make_pair(name, message);
        }
        file.close();
    }
    else
    {
        std::cerr << "Unable to open file" << std::endl;
    }
    return indexMap;
}

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
        else if (buffer[i] == '\"')
        {
            tempbuffer.clear();
            i++;
            while (buffer[i] != '\"')
            {
                tempbuffer.push_back(buffer[i]);
                i++;
            }
            bufferArray.push_back(tempbuffer);
            tempbuffer.clear();
        }
        else
        {
            tempbuffer.push_back(buffer[i]);
        }
        i++;
    }
    if (!tempbuffer.empty())
    {
        string lastbuffer = remove_char(remove_char(tempbuffer, '\r'), '\n');
        if (lastbuffer.length() > 0)
            bufferArray.push_back(lastbuffer);
    }
    return bufferArray;
}

int handle_commands(vector<string> buffer, bulletinBoard *user, int client_sock)
{
    // cout << "buffer.size()=>" << buffer.size() << endl;
    string command = buffer[0];
    // cout << "command=>" << command << endl;
    string arg1 = buffer.size() > 1 ? buffer[1] : "";
    string arg2 = buffer.size() > 2 ? buffer[2] : "";
    if (command == "USER")
    {
        if (arg1 != "" && buffer.size() == 2)
        {
            if (regex_match(arg1, pattern))
            {
                user->setName(arg1);
                string message = "HELLO " + arg1 + " Welcome to bulletin board server.\n";
                send(client_sock, message.c_str(), message.length(), 0);
            }
            else
            {
                string message = "ERROR USER name should not contain any special character.\n";
                send(client_sock, message.c_str(), message.length(), 0);
            }
        }
        else
        {
            string message = "ERROR USER command takes only 1 positional arguments.\n";
            send(client_sock, message.c_str(), message.length(), 0);
        }
    }
    else if (command == "READ")
    {
        if (arg1 != "" && buffer.size() == 2)
        {
            int messageId = stoi(arg1);
            if (indexes.find(messageId) != indexes.end())
            {
                pair<string, string> data = indexes[messageId];
                string message = "MESSAGE " + to_string(messageId) + " " + data.first + " || " + data.second + "\n";
                send(client_sock, message.c_str(), message.length(), 0);
            }
            else
            {
                string message = "UNKNOWN " + to_string(messageId) + " message not found.\n";
                send(client_sock, message.c_str(), message.length(), 0);
            }
        }
        else
        {
            string message = "ERROR READ command takes only 1 positional arguments.\n";
            send(client_sock, message.c_str(), message.length(), 0);
        }
    }
    else if (command == "WRITE")
    {
        if (arg1 != "" && buffer.size() == 2)
        {
            pthread_mutex_lock(&fileMutex);
            int messageId = user->writeMessage(arg1, "bbfile.txt");
            string message = "WROTE " + to_string(messageId) + '\n';
            indexes[messageId] = make_pair(user->getName(), arg1);
            send(client_sock, message.c_str(), message.length(), 0);
            pthread_mutex_unlock(&fileMutex);
        }
        else
        {
            string message = "ERROR WRITE command takes only 1 positional arguments.\n";
            send(client_sock, message.c_str(), message.length(), 0);
        }
    }
    else if (command == "REPLACE")
    {
        if (buffer.size() == 3)
        {
            int messageId = stoi(arg1);
            string new_message = arg2;
            if (indexes.find(messageId) != indexes.end())
            {
                pthread_mutex_lock(&fileMutex);
                bool replaced = user->replaceMessage(messageId, new_message, "bbfile.txt");
                if (replaced)
                {

                    indexes[messageId] = make_pair(user->getName(), arg2);
                    string message = "WROTE " + to_string(messageId) + '\n';
                    send(client_sock, message.c_str(), message.length(), 0);
                }
                else
                {
                    string message = "ERROR REPLACE " + to_string(messageId) + " some error occured.\n";
                    send(client_sock, message.c_str(), message.length(), 0);
                }
                pthread_mutex_unlock(&fileMutex);
            }
            else
            {
                string message = "UNKNOWN " + to_string(messageId) + " message not found.\n";
                send(client_sock, message.c_str(), message.length(), 0);
            }
        }
        else
        {
            string message = "ERROR REPLACE command takes only 2 positional arguments.\n";
            send(client_sock, message.c_str(), message.length(), 0);
        }
    }
    else if (command == "QUIT" || command == "\377\364\377\375\006")
    {
        // delete user;
        close(client_sock);
    }
    else
    {
        string message = "ERROR entered command is incorrect.\n";
        send(client_sock, message.c_str(), message.length(), 0);
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
                string message;
                message = "ERROR entered command is incorrect.\n";
                send(client_sock, message.c_str(), message.length(), 0);
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
    delete[] buffer;
    delete user;
    return nullptr;
}

void signalHandler(int signal)
{
    cout << "Received signal " << signal << ", terminating the server." << endl;
    exit(signal);
}

unordered_map<string, string> readConfig(const string &filename)
{
    ifstream file(filename);
    unordered_map<string, string> config;
    if (file.is_open())
    {
        string line;
        while (getline(file, line))
        {
            // Skip empty lines and comments (lines starting with #)
            if (!line.empty() && line[0] != '#')
            {
                size_t delimiterPos = line.find('=');
                if (delimiterPos != string::npos)
                {
                    string key = line.substr(0, delimiterPos);
                    string value = line.substr(delimiterPos + 1);
                    // Trim leading and trailing whitespace from key and value
                    key.erase(0, key.find_first_not_of(" \t\r\n"));
                    key.erase(key.find_last_not_of(" \t\r\n") + 1);
                    value.erase(0, value.find_first_not_of(" \t\r\n"));
                    value.erase(value.find_last_not_of(" \t\r\n") + 1);
                    config[key] = value;
                }
            }
        }
        file.close();
    }
    else
    {
        cerr << "Unable to open config file or file could not be found: " << filename << endl;
    }

    return config;
}

int main()
{
    signal(SIGINT, signalHandler);
    unordered_map<string, string> config = readConfig("bbserv.conf");
    if (config.find("BBFILE") == config.end())
    {
        cerr << "Could not find the BBFILE in the configuration. Add the BBFILE=<your BB file> in config file" << endl;
        return 1;
    }
    indexes = createIndexes(config["BBFILE"]);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1)
    {
        cerr << "Unable to create socket" << endl;
        return 1;
    }
    uint16_t port = config.find("BBPORT") == config.end() ? BBPORT : stoi(config["BBPORT"]);
    sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(DESIRED_ADDRESS);

    if (bind(sock, (sockaddr *)&addr, sizeof(addr)) == -1)
    {
        cerr << "Error binding socket" << endl;
        close(sock);
        return 1;
    }

    if (listen(sock, 20) == -1)
    {
        cerr << "Error listening on socket" << endl;
        close(sock);
        return 1;
    }

    cout << "Server listening on " << DESIRED_ADDRESS << ":" << port << endl;

    while (true)
    {
        sockaddr_in client_addr = {0};
        socklen_t client_addr_size = sizeof(client_addr);
        int client_sock = accept(sock, (sockaddr *)&client_addr, &client_addr_size);

        if (client_sock == -1)
        {
            cerr << "Error accepting client connection" << endl;
            continue;
        }

        int *pclient = new int;
        *pclient = client_sock;
        pthread_t thread_id;
        if (pthread_create(&thread_id, nullptr, handle_client, pclient) != 0)
        {
            cerr << "Error creating thread" << endl;
            delete pclient;
            close(client_sock);
        }
        else
        {
            pthread_detach(thread_id);
        }
    }

    close(sock);
    return 0;
}