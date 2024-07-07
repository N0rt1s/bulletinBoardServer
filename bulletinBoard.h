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
    int writeMessage(std::string message, std::string filename);
    bool replaceMessage(int messageId, std::string message, std::string filename);
    ~bulletinBoard();
};

#endif // BULLETINBOARD_H
