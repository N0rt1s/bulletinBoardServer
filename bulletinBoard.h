#ifndef BULLETINBOARD_H
#define BULLETINBOARD_H

#include <string>

class bulletinBoard
{
private:
    std::string name;
    bool isServer;
    // int client_sock;
    static int countLines(const std::string &filename);

public:
    bulletinBoard(bool isServer);
    // bulletinBoard(int client_socket);
    void setName(std::string name);
    std::string getName();
    bool getIsServer();
    void writeMessage(std::string message, std::string filename);
    std::pair<std::string, std::string> readMessage(int pos, int length, std::string filename);
    bool replaceMessage(int startpos, int MessageLength, std::string message, std::string filename);
    ~bulletinBoard();
};

#endif // BULLETINBOARD_H
