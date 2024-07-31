#ifndef BULLETINBOARD_H
#define BULLETINBOARD_H

#include <string>

class bulletinBoard
{
private:
    std::string name;
    // int client_sock;
    static int countLines(const std::string &filename);

public:
    bulletinBoard();
    // bulletinBoard(int client_socket);
    void setName(std::string name);
    std::string getName();
    void writeMessage(std::string message, std::string filename);
    static std::pair<std::string, std::string> readMessage(int pos, int length, std::string filename);
    bool replaceMessage(int startpos, int MessageLength, std::string message, std::string filename);
    ~bulletinBoard();
};

#endif // BULLETINBOARD_H
