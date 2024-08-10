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
#include "threadPool.h"
#include <regex>
#include <csignal>
#include <unordered_map>
#include <fstream>
#include <utility>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cctype>
#include <atomic>
#include <thread>
#include <iomanip>
#include <ctime>
#include <sstream>

#define DESIRED_ADDRESS "127.0.0.1"
#define BBPORT 9000
#define SYNCPORT 10000
#define BUFSIZE 2024
#define THMAX 20
#define DAEMON true
#define DELAY false
#define DEBUG false

using namespace std;

std::atomic<bool> running(true);
bool delay = DELAY;
bool debug = DEBUG;
bool daemon_ = DAEMON;
ThreadPool *clientPool = nullptr;
ThreadPool *serverPool = nullptr;
unordered_map<string, string> config;
regex pattern("[a-zA-Z][a-zA-Z0-9]*");
unordered_map<int, pair<int, int>> indexes1;
vector<string> serverAddresses;
vector<int> allsockets;
pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;
mutex logMutex;

struct clientData
{
    int socket;
    bool isServer;
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
    ofstream outfile;

    struct stat aa;
    if (stat("bbserv.pid", &aa) != 0)
    {
        // File doesn't exist, create it
        ofstream createFile("bbserv.pid");
        createFile.close();
    }

    outfile.open("bbserv.pid", ios_base::out); // append instead of overwrite
    outfile << getpid();
    outfile.close();

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

void addLog(string message)
{
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%d-%m-%Y %H-%M-%S");
    auto time = oss.str();
    string output = "[" + time + "] " + message + "\n";
    if (daemon_)
    {

        logMutex.lock();
        ofstream outfile;

        struct stat aa;
        if (stat("bbserv.log", &aa) != 0)
        {
            // File doesn't exist, create it
            ofstream createFile("bbserv.log");
            createFile.close();
        }

        outfile.open("bbserv.log", ios_base::app); // append instead of overwrite
        outfile << output;
        outfile.close();
        logMutex.unlock();
    }
    else
    {
        std::cout << output;
    }
}

void updateIndexes(int messageId, int oldLength, int newLength)
{
    int lengthDiff = newLength > oldLength ? newLength - oldLength : oldLength - newLength;

    for (int i = messageId + 1; i <= indexes1.size(); i++)
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
            addLog("Unable to create file: " + filename);
            return indexMap;
        }
        newFile.close();
        addLog("File created: " + filename);
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

string filterNonPrintable(const string &command)
{
    string filtered;
    for (char c : command)
    {
        if (isprint(c)) //|| c == '\n' || c == '\r')
        {
            filtered += c;
        }
    }
    return filtered;
}

vector<string> bufferSplit(const char *command)
{
    int i = 0;
    vector<string> bufferArray;
    string tempbuffer;
    bool commandcheck = false;
    while (command[i] != '\0')
    {
        if (command[i] == ' ' && !commandcheck)
        {
            if (!tempbuffer.empty())
            {
                commandcheck = true;
                bufferArray.push_back(tempbuffer);
                tempbuffer.clear();
            }
        }
        else
        {
            tempbuffer.push_back(command[i]);
        }
        i++;
    }
    if (!tempbuffer.empty())
    {
        int splitIndex = tempbuffer.find("/");
        if (splitIndex != -1 && bufferArray[0] != "WRITE")
        {
            bufferArray.push_back(tempbuffer.substr(0, splitIndex));
            bufferArray.push_back(tempbuffer.substr(splitIndex + 1));
        }
        else
            bufferArray.push_back(tempbuffer);
    }
    return bufferArray;
}

vector<string> serverBufferSplit(const char *buffer)
{
    int i = 0;
    vector<string> bufferArray;
    string tempbuffer;
    bool commandcheck = false;
    while (buffer[i] != '\0')
    {
        if (buffer[i] == ',')
        {
            if (!tempbuffer.empty())
            {
                commandcheck = true;
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

        bufferArray.push_back(tempbuffer);
    }
    return bufferArray;
}

bool syncWithServers(string message, string command)
{

    // int syncport = config.find("SYNCPORT") == config.end() ? SYNCPORT : stoi(config["SYNCPORT"]);

    int sockets[serverAddresses.size()];
    int count = 0;
    bool syncError = false;

    for (const string &serverAddress : serverAddresses)
    {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == -1)
        {
            if (debug)
                addLog("Unable to create socket for server " + serverAddress);
            return false;
        }
        string po = serverAddress.substr(serverAddress.find(":") + 1);
        int port = stoi(serverAddress.substr(serverAddress.find(":") + 1));
        string address = serverAddress.substr(0, serverAddress.find(":"));
        address = address == "localhost" ? "127.0.0.1" : address;
        sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port); // Assume same port for simplicity, modify if needed
        addr.sin_addr.s_addr = inet_addr(address.c_str());

        if (connect(sock, (sockaddr *)&addr, sizeof(addr)) == -1)
        {
            if (debug)
                addLog("Unable to connect to server " + serverAddress);
            close(sock);
            syncError = true;
            break;
        }
        sockets[count] = sock;
        count++;
    }

    if (syncError)
    {
        for (size_t i = 0; i < serverAddresses.size(); i++)
        {
            close(sockets[i]);
        }
        return false;
    }
    if (debug)
        addLog("precommit phase done");

    for (size_t i = 0; i < serverAddresses.size(); i++)
    {

        char buffer[BUFSIZE];
        int bytes_received = recv(sockets[i], buffer, BUFSIZE - 1, 0);
        if (bytes_received > 0)
        {
            buffer[bytes_received] = '\0';
            if (debug)
                addLog("Response from " + serverAddresses[i] + " recieved");
        }
        else
        {
            if (debug)
                addLog("Error receiving response from " + serverAddresses[i]);
            syncError = true;
            break;
        }
    }

    if (syncError)
    {
        for (size_t i = 0; i < serverAddresses.size(); i++)
        {
            close(sockets[i]);
        }
        return false;
    }
    if (debug)
        addLog("commit phase done \nsending message to servers");

    std::vector<int> sentSockets;
    addLog("Sending message " + message + "to servers.");
    for (size_t i = 0; i < serverAddresses.size(); i++)
    {

        send(sockets[i], message.c_str(), message.length(), 0);

        // Read response
        char buffer[BUFSIZE];
        int bytes_received = recv(sockets[i], buffer, BUFSIZE - 1, 0);
        if (bytes_received > 0)
        {
            sentSockets.push_back(sockets[i]);
            buffer[bytes_received] = '\0';
            if (debug)
                addLog("Response from " + serverAddresses[i] + ": " + buffer);
        }
        else
        {
            if (debug)
                addLog("Error receiving response from " + serverAddresses[i]);
            syncError = true;
            break;
        }

        // close(sockets[i]);
    }
    // to_string(messageId) + "," + user->getName() + ",\"" + new_message + "\""
    if (syncError)
    {
        if (debug)
            addLog("an error occured, rolling back changes");
        if (command == "write")
            message = "rollback," + message;
        else
        {
            pair<int, int> data = indexes1[stoi(message.substr(0, message.find(',')))];

            pair<string, string> message1 = bulletinBoard::readMessage(data.first, data.second, config["BBFILE"]);
            message = message.substr(0, message.find(',')) + "," + message1.first + ",\"" + message1.second + "\"";
        }
        addLog("Sending rollback message " + message + "to servers.");
        for (const int &sock : sentSockets)
        {
            send(sock, message.c_str(), message.length(), 0);
        }
    }

    for (size_t i = 0; i < serverAddresses.size(); i++)
    {
        close(sockets[i]);
    }
    if (syncError)
        return false;
    return true;
}

int handle_commands(vector<string> data, bulletinBoard *user, int client_sock)
{
    string command = data[0];
    string arg1 = data.size() > 1 ? data[1] : "";
    string arg2 = data.size() > 2 ? data[2] : "";
    if (command == "USER")
    {
        if (regex_match(arg1, pattern))
        {
            user->setName(arg1);
            if (debug)
                addLog("CLient " + to_string(client_sock) + ": Username changed.");
            string message = "1.0 HELLO " + arg1 + " Welcome to bulletin board server.\n";
            send(client_sock, message.c_str(), message.length(), 0);
        }
        else
        {
            string message = "1.2 ERROR USER name should not contain any special character.\n";
            send(client_sock, message.c_str(), message.length(), 0);
        }
    }

    else if (command == "READ")
    {
        pthread_rwlock_rdlock(&rwlock);
        // if (arg1 != "" && data.size() == 2)
        // {
        try
        {
            if (debug)
                addLog("CLient " + to_string(client_sock) + ": Beginning read operation.");

            int messageId = stoi(arg1);
            if (!indexes1.empty() && indexes1.find(messageId) != indexes1.end())
            {
                pair<int, int> data = indexes1[messageId];
                pair<string, string> message1 = bulletinBoard::readMessage(data.first, data.second, config["BBFILE"]);
                if (delay)
                    sleep(3);
                string message = "2.0 MESSAGE " + to_string(messageId) + " " + message1.first + "/" + message1.second + "\n";
                if (debug)
                    addLog("MESSAGE " + to_string(messageId) + " found.");
                send(client_sock, message.c_str(), message.length(), 0);
            }
            else
            {
                string message = "2.1 UNKNOWN " + to_string(messageId) + " message not found.\n";
                if (debug)
                    addLog(message);
                send(client_sock, message.c_str(), message.length(), 0);
            }
        }
        catch (exception e)
        {
            string message = "2.2 ERROR READ " + arg1 + "\n";
            send(client_sock, message.c_str(), message.length(), 0);
        }
        // }
        // else
        // {
        //     string message = "ERROR READ command takes only 1 positional arguments.\n";
        //     send(client_sock, message.c_str(), message.length(), 0);
        // }
        pthread_rwlock_unlock(&rwlock);
    }
    else if (command == "WRITE")
    {
        pthread_rwlock_wrlock(&rwlock);
        if (debug)
            addLog("Client " + to_string(client_sock) + ": Beginning write operation.");
        int messageId = indexes1.size() + 1;
        string message = to_string(messageId) + "/" + user->getName() + "/\"" + arg1 + "\"" + "\n";
        if (syncWithServers(user->getName() + ",\"" + arg1 + "\"", "write"))
        {
            user->writeMessage(message, config["BBFILE"]);
            long startPos = 0;
            if (delay)
                sleep(6);
            if (!indexes1.empty())
            {
                auto lastEntry = indexes1[indexes1.size()]; // Get the last entry
                startPos = lastEntry.first + lastEntry.second;
            }

            indexes1[messageId] = make_pair(startPos, message.length());
            string messageResponse = "3.0 WROTE " + to_string(messageId) + '\n';
            if (debug)
                addLog(messageResponse);
            send(client_sock, messageResponse.c_str(), messageResponse.length(), 0);
        }
        else
        {
            string messageResponse = "3.2 ERROR WRITE Unable to sync servers.\n";
            if (debug)
                addLog(messageResponse);
            send(client_sock, messageResponse.c_str(), messageResponse.length(), 0);
        }

        pthread_rwlock_unlock(&rwlock);
    }
    else if (command == "REPLACE")
    {
        pthread_rwlock_wrlock(&rwlock);
        try
        {
            int messageId = stoi(arg1);
            string new_message = arg2;
            if (indexes1.find(messageId) != indexes1.end())
            {
                string message = to_string(messageId) + "/" + user->getName() + "/\"" + new_message + "\"" + "\n";
                if (syncWithServers(to_string(messageId) + "," + user->getName() + ",\"" + new_message + "\"", "replace"))
                {
                    int startpos = indexes1[messageId].first;
                    int messageLength = indexes1[messageId].second;
                    bool replaced = user->replaceMessage(startpos, messageLength, message, config["BBFILE"]);
                    if (replaced)
                    {
                        updateIndexes(messageId, messageLength, message.length());
                        indexes1[messageId] = make_pair(startpos, message.length());
                        if (delay)
                            sleep(6);
                        string response = "WROTE " + to_string(messageId) + '\n';
                        if (debug)
                            addLog(response);
                        send(client_sock, response.c_str(), response.length(), 0);
                    }
                    else
                    {
                        string response = "ERROR REPLACE " + to_string(messageId) + " some error occured.\n";
                        if (debug)
                            addLog(response);
                        send(client_sock, response.c_str(), response.length(), 0);
                    }
                }
                else
                {
                    string response = "ERROR Replace Unable to sync servers.\n";
                    if (debug)
                        addLog(response);
                    send(client_sock, response.c_str(), response.length(), 0);
                }
            }
            else
            {
                string response = "3.1 UNKNOWN " + to_string(messageId) + "\n";
                send(client_sock, response.c_str(), response.length(), 0);
            }
        }
        catch (const std::exception &e)
        {
            string response = "3.1 UNKNOWN " + arg1 + "\n";
            send(client_sock, response.c_str(), response.length(), 0);
        }
        pthread_rwlock_unlock(&rwlock);
    }
    else if (command == "QUIT" || command == "\377\364\377\375\006")
    {

        if (debug)
            addLog("Client " + to_string(client_sock) + " disconnected.");
        string byeMessage = "4.0 BYE Thank you for visiting our bulletin board.\n";
        send(client_sock, byeMessage.c_str(), byeMessage.length(), 0);
        shutdown(client_sock, SHUT_RDWR);
        // close(client_sock);
    }
    else
    {
        string message = "ERROR entered command is incorrect.\n";
        if (debug)
            addLog(message);
        send(client_sock, message.c_str(), message.length(), 0);
    }
    return 1;
}

void handle_server_commands(vector<string> buffer, int client_sock)
{
    string arg1 = buffer[0];
    string arg2 = buffer.size() > 1 ? buffer[1] : "";
    string arg3 = buffer.size() > 2 ? buffer[2] : "";
    string arg4 = buffer.size() > 3 ? buffer[3] : "";
    const string filename = config["BBFILE"];
    if (arg1 == "rollback")
    {
        if (arg1 != "" && arg2 != "" && arg3 != "" && arg4 == "")
        {
            int messageId = indexes1.size();
            int startPos = indexes1[messageId].first;
            int fd = open(filename.c_str(), O_RDWR);
            if (fd == -1)
            {
                addLog("Could not open the file for writing!");
                return;
            }

            // Truncate the file at the specified position
            if (ftruncate(fd, startPos) == -1)
            {
                addLog("Could not truncate the file!");
                close(fd);
                return;
            }

            close(fd);
            indexes1.erase(messageId);
        }
        else
        {
        }
    }
    else if (arg1 != "" && arg2 != "" && arg3 == "")
    {
        ofstream outfile;

        struct stat buffer;
        if (stat(filename.c_str(), &buffer) != 0)
        {
            // File doesn't exist, create it
            ofstream createFile(filename);
            createFile.close();
        }
        int id = indexes1.size() + 1;
        string message = to_string(id) + "," + arg1 + ",\"" + arg2 + "\"\n";
        outfile.open(filename, ios_base::app);
        outfile << message;
        outfile.close();
        long startPos = 0;
        if (!indexes1.empty())
        {
            auto lastEntry = indexes1[indexes1.size()];
            startPos = lastEntry.first + lastEntry.second;
        }

        indexes1[id] = make_pair(startPos, message.length());
        string messageResponse = "WROTE " + to_string(id) + '\n';
        send(client_sock, messageResponse.c_str(), messageResponse.length(), 0);
    }
    else
    {
        int messageId = stoi(arg1);
        int startPos = indexes1[messageId].first;
        int messageLength = indexes1[messageId].second;
        string message = to_string(messageId) + "," + arg2 + ",\"" + arg3 + "\"\n";
        addLog(message);
        fstream file(filename, ios::in);

        if (!file.is_open())
        {
            file.open(filename, ios::in);
        }

        file.seekg(0, ios::beg);
        string beforeContent(startPos, '\0');
        file.read(&beforeContent[0], startPos);

        file.seekg(startPos + messageLength);
        string remainingContent;
        getline(file, remainingContent, '\0');
        file.close();

        file.open(filename, ios::out);

        if (!file.is_open())
        {
            file.open(filename, ios::out);
        }
        file << beforeContent;
        file.seekp(startPos);
        file << message << remainingContent;
        file.close();
        updateIndexes(messageId, messageLength, message.length());
        indexes1[messageId] = make_pair(startPos, message.length());
        string response = "WROTE " + to_string(messageId) + '\n';
        send(client_sock, response.c_str(), response.length(), 0);
    }
    // close(client_sock);
}

void *handle_client(void *args)
{

    if (debug)
        addLog("connection accepted");
    clientData *data = (clientData *)args;
    int client_sock = data->socket;
    bool isServer = data->isServer;
    delete data;
    string helpMessage =
        "USER    <name>                    Set the name\n"
        "READ    <messageId>               Read a message using Message Id\n"
        "WRITE   <message>               Write a message to bulletin board\n"
        "REPLACE <messageId>/<message>   Replace a message with a new one\n"
        "QUIT                              Quit from the server\n";
    string welcomMessage = "0.0 greeting Connection establish succesfully! \n" + helpMessage;
    send(client_sock, welcomMessage.c_str(), welcomMessage.length(), 0);
    const size_t bufferSize = 1024 * 1024;
    char buffer[bufferSize];
    // char *buffer = new char[bufferSize];
    bulletinBoard *user = new bulletinBoard();
    while (true)
    {
        // char buffer[1024];
        memset(buffer, 0, bufferSize);
        int bytes_received = recv(client_sock, buffer, bufferSize - 1, 0);

        if (bytes_received > 0)
        {
            string filtered = filterNonPrintable(buffer);
            char *filteredbuffer = new char[filtered.size() + 1];
            strcpy(filteredbuffer, filtered.c_str());
            filteredbuffer[filtered.size()] = '\0'; // Null-terminate the received data
            vector<string> bufferArray = bufferSplit(filteredbuffer);
            delete filteredbuffer;
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
        }
        else if (bytes_received == 0)
        {
            // Connection was closed by the client
            if (debug)
                addLog("Connection closed by the client");
            string byeMessage = "4.0 BYE Thank you for visiting our bulletin board.\n";
            send(client_sock, byeMessage.c_str(), byeMessage.length(), 0);
            shutdown(client_sock, SHUT_RDWR);
            break;
        }
        else
        {
            // An error occurred
            if (debug)
                perror("recv");
            break;
        }
    }
    // delete buffer;
    close(client_sock);
    allsockets.erase(std::remove(allsockets.begin(), allsockets.end(), client_sock), allsockets.end());
    delete user;
    return nullptr;
}

void *handle_server(void *args)
{

    addLog("connection accepted");
    clientData *data = (clientData *)args;
    int client_sock = data->socket;
    delete data;
    string welcomMessage = "Connection establish succesfully! \n";
    send(client_sock, welcomMessage.c_str(), welcomMessage.length(), 0);
    const size_t bufferSize = 1024 * 1024;
    char buffer[bufferSize];
    pthread_rwlock_wrlock(&rwlock);
    while (true)
    {
        memset(buffer, 0, bufferSize);
        int bytes_received = recv(client_sock, buffer, bufferSize - 1, 0);

        if (bytes_received > 0)

        {
            string aa = buffer;
            addLog("Recieved command " + aa + " from server");
            buffer[bytes_received] = '\0'; // Null-terminate the received data
            vector<string> bufferArray = serverBufferSplit(buffer);
            if (bufferArray.size() > 0 && bufferArray.size() <= 3)
            {
                handle_server_commands(bufferArray, client_sock);
            }
            else
            {
                string message;
                message = "ERROR entered command is incorrect.\n";
                send(client_sock, message.c_str(), message.length(), 0);
            }
        }
        else if (bytes_received == 0)
        {
            // Connection was closed by the client
            addLog("Connection closed by the client");
            break;
        }
        else
        {
            // An error occurred
            perror("recv");
            break;
        }
    }
    // delete[] buffer;
    allsockets.erase(std::remove(allsockets.begin(), allsockets.end(), client_sock), allsockets.end());
    close(client_sock);
    pthread_rwlock_unlock(&rwlock);
    return nullptr;
}

void signalHandler(int signum)
{
    if (debug)
        addLog("Shutting and closing down all sockets...\n");

    for (int socket1 : allsockets)
    {
        string byeMessage = "4.0 BYE Thank you for visiting our bulletin board.\n";
        send(socket1, byeMessage.c_str(), byeMessage.length(), 0);
        shutdown(socket1, SHUT_RDWR);
        close(socket1);
    }
    if (debug)
        addLog("Closed all sockets.\n");
    running = false;

    if (clientPool)
    {
        clientPool->shutdown(); // Custom function to stop the thread pool
    }
    if (serverPool)
    {
        serverPool->shutdown(); // Custom function to stop the thread pool
    }
    if (signum == SIGHUP)
    {
        addLog("Received SIGHUP, restarting program...\n");
        // Restart the program by using exec
        execl("./a.out", "./a.out", NULL);
        // If execl fails
        perror("execl");
        exit(EXIT_FAILURE);
    }
    else
    {

        addLog("Interrupt signal (" + to_string(signum) + ") received. Cleaning up and exiting...");
        exit(signum);
    }
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
        addLog("Unable to open config file or file could not be found: " + filename);
    }

    return config;
}

bool findServerIp(string ip)
{
    for (const string &serverAddress : serverAddresses)
    {
        string address = serverAddress.substr(0, serverAddress.find(","));
        if (address == ip)
            return true;
    }
    return false;
}

void startServer(int port, const std::string &purpose)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1)
    {
        if (debug)
            addLog("Unable to create socket");
        return;
    }

    int optval = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0)
    {
        if (debug)
            addLog("Unable to create socket");
        // perror("setsockopt");
        close(sock);
        return;
    }

    sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(DESIRED_ADDRESS);

    if (bind(sock, (sockaddr *)&addr, sizeof(addr)) == -1)
    {
        if (debug)
            addLog("Error binding socket");
        close(sock);
        return;
    }

    if (listen(sock, 20) == -1)
    {
        if (debug)
            addLog("Error listening on socket");
        close(sock);
        return;
    }

    if (debug)
        addLog("Server listening for " + purpose + "s on " + DESIRED_ADDRESS + ":" + to_string(port));

    while (running)
    {
        sockaddr_in client_addr = {0};
        socklen_t client_addr_size = sizeof(client_addr);
        int client_sock = accept(sock, (sockaddr *)&client_addr, &client_addr_size);

        if (client_sock == -1)
        {
            if (debug)
                addLog("Error accepting client connection");
            continue;
        }
        getpeername(sock, (struct sockaddr *)&client_addr, &client_addr_size);

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        string ip = client_ip;
        int client_port = ntohs(client_addr.sin_port);
        if (debug)
            addLog("Connected to client with IP address: " + ip + " with port: " + to_string(client_port));

        clientData *pclient = new clientData;
        pclient->isServer = 1;
        pclient->socket = client_sock;
        allsockets.push_back(client_sock);
        if (purpose == "server")
            serverPool->enqueue([pclient]
                                { handle_server(pclient); });
        else
            clientPool->enqueue([pclient]
                                { handle_client(pclient); });
    }

    close(sock);
}

int main(int argc, char *argv[])
{
    string confFile = "bbserv.conf";
    if (argc > 2)
    {
        addLog("more then 1 argument is given");
        return 1;
    }
    else if (argc == 2)
    {
        confFile = argv[1];
    }
    signal(SIGINT, signalHandler);
    signal(SIGHUP, signalHandler);
    signal(SIGQUIT, signalHandler);
    config = readConfig(confFile);
    if (config.size() == 0)
    {
        addLog("Could not find configuration file or some error occured in configuration file");
        return 1;
    }
    if (config.find("BBFILE") == config.end())
    {
        if (debug)
            addLog("Could not find the BBFILE in the configuration. Add the BBFILE=<your BB file> in bbserv.conf file");
        return 1;
    }

    // indexes = createIndexes(config["BBFILE"]);
    indexes1 = createIndexes1(config["BBFILE"]);
    uint16_t bbport = config.find("BBPORT") == config.end() ? BBPORT : stoi(config["BBPORT"]);
    size_t thmax = config.find("THMAX") == config.end() ? THMAX : stoi(config["THMAX"]);
    daemon_ = config.find("DAEMON") == config.end() ? DAEMON : config["DAEMON"] == "true" ? true
                                                                                          : false;

    delay = config.find("DELAY") == config.end() ? DELAY : config["DELAY"] == "true" ? true
                                                                                     : false;
    debug = config.find("DEBUG") == config.end() ? DEBUG : config["DEBUG"] == "true" ? true
                                                                                     : false;
    int syncport = config.find("SYNCPORT") == config.end() ? SYNCPORT : stoi(config["SYNCPORT"]);

    if (daemon_)
    {
        daemonize();
    }

    clientPool = new ThreadPool(thmax);
    serverPool = new ThreadPool(serverAddresses.size());

    // Start server to listen on base port
    thread basePortThread(startServer, bbport, "client");

    // Start server to listen on sync port
    thread syncPortThread(startServer, syncport, "server");

    // Join threads to the main thread to keep the program running
    basePortThread.join();
    syncPortThread.join();

    return 0;
}