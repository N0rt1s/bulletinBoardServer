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
#include "threadPool.cpp"
#include "threadPool.cpp"
#include <regex>
#include <csignal>
#include <unordered_map>
#include <fstream>
#include <utility>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define DESIRED_ADDRESS "127.0.0.1"
#define BBPORT 9000
#define BUFSIZE 512
#define DAEMON true

using namespace std;
regex pattern("[a-zA-Z][a-zA-Z0-9]*");
unordered_map<int, pair<string, string>> indexes;
unordered_map<int, pair<int, int>> indexes1;
unordered_map<string, string> config;
vector<string> serverAddresses;
pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;

struct clientData
{
    int socket;
    string address;
};

void daemonize()
{
    pid_t pid = fork();

    if (pid < 0)
    {
        exit(EXIT_FAILURE);
    }

    if (pid > 0)
    {
        // Parent process
        exit(EXIT_SUCCESS);
    }

    // Child process
    if (setsid() < 0)
    {
        // Create a new session
        exit(EXIT_FAILURE);
    }

    // Fork again to ensure the daemon cannot reacquire a terminal
    pid = fork();

    if (pid < 0)
    {
        exit(EXIT_FAILURE);
    }

    if (pid > 0)
    {
        exit(EXIT_SUCCESS);
    }

    // Change the file mode mask
    umask(0);

    // Close all open file descriptors
    for (int fd = sysconf(_SC_OPEN_MAX); fd >= 0; fd--)
    {
        close(fd);
    }

    // Redirect standard file descriptors to /dev/null
    int fd0 = open("/dev/null", O_RDWR);
    int fd1 = dup(0);
    int fd2 = dup(0);
}

void updateIndexes(int messageId, int oldLength, int newLength)
{
    int lengthDiff = newLength > oldLength ? newLength - oldLength : oldLength - newLength;

    for (int i = messageId + 1; i < indexes1.size(); i++)
    {
        if (newLength > oldLength)
            indexes1[i].first += lengthDiff;
        else
            indexes1[i].first -= lengthDiff;
    }
}

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

unordered_map<int, pair<int, int>> createIndexes1(string filename)
{
    ifstream file(filename);
    unordered_map<int, pair<int, int>> indexMap;

    if (!file.is_open())
    {
        // If file does not exist, create a new empty file
        ofstream newFile(filename);
        if (!newFile)
        {
            cerr << "Unable to create file: " << filename << endl;
            return indexMap;
        }
        newFile.close();
        cout << "File created: " << filename << endl;
        return indexMap;
    }

    string line;
    int lineNumber = 1;
    long pos = file.tellg();

    while (getline(file, line))
    {
        long startPos = pos;
        long lineLength = file.tellg() - pos;
        indexMap[lineNumber] = make_pair(startPos, lineLength);
        pos = file.tellg();
        lineNumber++;
    }

    return indexMap;
}

// unordered_map<int, pair<string, string>> createIndexes(string filename)
// {
//     ifstream file(filename);
//     unordered_map<int, pair<string, string>> indexMap;

//     if (!file.is_open())
//     {
//         // If file does not exist, create a new empty file
//         ofstream newFile(filename);
//         if (!newFile)
//         {
//             cerr << "Unable to create file: " << filename << endl;
//             return indexMap;
//         }
//         newFile.close();
//         cout << "File created: " << filename << endl;
//         return indexMap;
//     }

//     string line;
//     while (getline(file, line))
//     {
//         size_t commaPos = line.find(',');
//         int id = stoi(line.substr(0, commaPos));
//         string lineWithoutId = line.substr(commaPos + 1);
//         commaPos = lineWithoutId.find(',');
//         string name = lineWithoutId.substr(0, commaPos);
//         string message = remove_char(lineWithoutId.substr(commaPos + 1), '\"');
//         indexMap[id] = make_pair(name, message);
//     }
//     file.close();

//     return indexMap;
// }

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

bool syncWithServers(const string &command, const string &arg1, const string &arg2 = "")
{
    string message = command + " " + arg1;
    if (!arg2.empty())
    {
        message += " " + arg2;
    }
    message += "\n";

    int sockets[serverAddresses.size()];
    for (const string &serverAddress : serverAddresses)
    {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == -1)
        {
            cerr << "Unable to create socket for server " << serverAddress << endl;
            return false;
        }
        string po = serverAddress.substr(serverAddress.find(",")+1);
        int port = stoi(serverAddress.substr(serverAddress.find(",") + 1));
        string address = serverAddress.substr(0, serverAddress.find(","));
        sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port); // Assume same port for simplicity, modify if needed
        addr.sin_addr.s_addr = inet_addr(address.c_str());

        if (connect(sock, (sockaddr *)&addr, sizeof(addr)) == -1)
        {
            cerr << "Unable to connect to server " << serverAddress << endl;
            close(sock);
            return false;
        }
    }

    for (size_t i = 0; i < serverAddresses.size(); i++)
    {

        send(sockets[i], message.c_str(), message.length(), 0);

        // Read response
        char buffer[BUFSIZE];
        int bytes_received = recv(sockets[i], buffer, BUFSIZE - 1, 0);
        if (bytes_received > 0)
        {
            buffer[bytes_received] = '\0';
            cout << "Response from " << serverAddresses[i] << ": " << buffer << endl;
        }
        else
        {
            cerr << "Error receiving response from " << serverAddresses[i] << endl;
        }

        close(sockets[i]);
    }

    return true;
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
        pthread_rwlock_rdlock(&rwlock);
        if (arg1 != "" && buffer.size() == 2)
        {
            int messageId = stoi(arg1);
            if (!indexes1.empty() && indexes1.find(messageId) != indexes1.end())
            {
                pair<int, int> data = indexes1[messageId];
                pair<string, string> message1 = user->readMessage(data.first, data.second, config["BBFILE"]);
                string message = "MESSAGE " + to_string(messageId) + " " + message1.first + " || " + message1.second + "\n";
                send(client_sock, message.c_str(), message.length(), 0);
            }
            else
            {
                string message = "UNKNOWN " + to_string(messageId) + " message not found.\n";
                send(client_sock, message.c_str(), message.length(), 0);
            }
            // if (indexes.find(messageId) != indexes.end())
            // {
            //     pair<string, string> data = indexes[messageId];
            //     string message = "MESSAGE " + to_string(messageId) + " " + data.first + " || " + data.second + "\n";
            //     send(client_sock, message.c_str(), message.length(), 0);
            // }
            // else
            // {
            //     string message = "UNKNOWN " + to_string(messageId) + " message not found.\n";
            //     send(client_sock, message.c_str(), message.length(), 0);
            // }
        }
        else
        {
            string message = "ERROR READ command takes only 1 positional arguments.\n";
            send(client_sock, message.c_str(), message.length(), 0);
        }
        pthread_rwlock_unlock(&rwlock);
    }
    else if (command == "WRITE")
    {
        pthread_rwlock_wrlock(&rwlock);
        cout << "it is " << (user->getIsServer() ? "" : "not ") << "a server" << endl;
        if (arg1 != "" && buffer.size() == 2)
        {
            if (syncWithServers(command, arg1, arg2))
            {
                string message = "," + user->getName() + ",\"" + arg1 + "\"" + "\n";
                int messageId = user->writeMessage(message, config["BBFILE"]);
                message = to_string(messageId) + "," + user->getName() + ",\"" + arg1 + "\"" + "\n";
                long startPos = 0;
                if (!indexes1.empty())
                {
                    auto lastEntry = indexes1[indexes1.size()]; // Get the last entry
                    startPos = lastEntry.first + lastEntry.second;
                }

                indexes1[messageId] = make_pair(startPos, message.length());
                string messageResponse = "WROTE " + to_string(messageId) + '\n';
                send(client_sock, messageResponse.c_str(), messageResponse.length(), 0);
            }
            else
            {
                string message = "ERROR syncing servers.\n";
                send(client_sock, message.c_str(), message.length(), 0);
            }
        }
        else
        {
            string message = "ERROR WRITE command takes only 1 positional arguments.\n";
            send(client_sock, message.c_str(), message.length(), 0);
        }

        pthread_rwlock_unlock(&rwlock);
    }
    else if (command == "REPLACE")
    {
        pthread_rwlock_wrlock(&rwlock);
        if (buffer.size() == 3)
        {
            int messageId = stoi(arg1);
            string new_message = arg2;
            if (indexes1.find(messageId) != indexes1.end())
            {
                if (syncWithServers(command, arg1, arg2))
                {
                    int startpos = indexes1[messageId].first;
                    int messageLength = indexes1[messageId].second;
                    string message = to_string(messageId) + "," + user->getName() + ",\"" + new_message + "\"" + "\n";
                    bool replaced = user->replaceMessage(startpos, messageLength, message, config["BBFILE"]);
                    if (replaced)
                    {
                        updateIndexes(messageId, messageLength, message.length());
                        indexes1[messageId] = make_pair(startpos, message.length());
                        string response = "WROTE " + to_string(messageId) + '\n';
                        send(client_sock, response.c_str(), response.length(), 0);
                    }
                    else
                    {
                        string response = "ERROR REPLACE " + to_string(messageId) + " some error occured.\n";
                        send(client_sock, response.c_str(), response.length(), 0);
                    }
                }
                else
                {
                    string response = "ERROR syncing servers.\n";
                    send(client_sock, response.c_str(), response.length(), 0);
                }
            }
            else
            {
                string response = "UNKNOWN " + to_string(messageId) + " message not found.\n";
                send(client_sock, response.c_str(), response.length(), 0);
            }
            // if (indexes.find(messageId) != indexes.end())
            // {
            //     if (syncWithServers(command, arg1, arg2))
            //     {

            //         bool replaced = user->replaceMessage(messageId, new_message, config["BBFILE"]);
            //         if (replaced)
            //         {

            //             indexes[messageId] = make_pair(user->getName(), arg2);
            //             string message = "WROTE " + to_string(messageId) + '\n';
            //             send(client_sock, message.c_str(), message.length(), 0);
            //         }
            //         else
            //         {
            //             string message = "ERROR REPLACE " + to_string(messageId) + " some error occured.\n";
            //             send(client_sock, message.c_str(), message.length(), 0);
            //         }
            //     }
            //     else
            //     {
            //         string message = "ERROR syncing servers.\n";
            //         send(client_sock, message.c_str(), message.length(), 0);
            //     }
            // }
            // else
            // {
            //     string message = "UNKNOWN " + to_string(messageId) + " message not found.\n";
            //     send(client_sock, message.c_str(), message.length(), 0);
            // }
        }
        else
        {
            string message = "ERROR REPLACE command takes only 2 positional arguments.\n";
            send(client_sock, message.c_str(), message.length(), 0);
        }
        pthread_rwlock_unlock(&rwlock);
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
    clientData *data = (clientData *)args;
    int client_sock = data->socket;
    string address = data->address;
    bool isServer = find(serverAddresses.begin(), serverAddresses.end(), address) != serverAddresses.end();
    delete data;
    string helpMessage =
        "USER    <name>                    Set the name\n"
        "READ    <messageId>               Read a message using Message Id\n"
        "WRITE   \"<message>\"               Write a message to bulletin board\n"
        "REPLACE <messageId> \"<message>\"   Replace a message with a new one\n"
        "QUIT                              Quit from the server\n";
    string welcomMessage = "Connection establish succesfully! \n" + helpMessage;
    send(client_sock, welcomMessage.c_str(), welcomMessage.length(), 0);
    const size_t bufferSize = 1024 * 1024;
    char *buffer = new char[bufferSize];
    bulletinBoard *user = new bulletinBoard(isServer);
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
                    if (key == "PEER")
                    {
                        serverAddresses.push_back(value);
                    }
                    else
                    {
                        config[key] = value;
                    }
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
    config = readConfig("bbserv.conf");
    if (config.find("BBFILE") == config.end())
    {
        cerr << "Could not find the BBFILE in the configuration. Add the BBFILE=<your BB file> in bbserv.conf file" << endl;
        return 1;
    }

    // indexes = createIndexes(config["BBFILE"]);
    indexes1 = createIndexes1(config["BBFILE"]);
    uint16_t port = config.find("BBPORT") == config.end() ? BBPORT : stoi(config["BBPORT"]);
    bool daemon = config.find("DAEMON") == config.end() ? DAEMON : config["DAEMON"] == "true" ? true
                                                                                              : false;

    if (daemon)
    {
        daemonize();
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1)
    {
        cerr << "Unable to create socket" << endl;
        return 1;
    }

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
    ThreadPool pool = ThreadPool(2);
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
        getpeername(sock, (struct sockaddr *)&client_addr, &client_addr_size);

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        int client_port = ntohs(client_addr.sin_port);
        ostringstream oss;
        oss << client_ip << "," << client_port;
        string client_ipaddr = oss.str();
        cout << "Connected to client with IP address: " << client_ipaddr << endl;

        // int *pclient = new int;
        // *pclient = client_sock;
        clientData *pclient = new clientData;
        pclient->address = client_ipaddr;
        pclient->socket = client_sock;

        pool.enqueue([pclient]
                     { handle_client(pclient); });
    }

    close(sock);
    return 0;
}