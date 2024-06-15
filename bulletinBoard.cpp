#include <cstring>
#include <iostream>
#include <fstream>
#include <sys/stat.h>

using namespace std;

class bulletinBoard
{
private:
    string name;
    int client_sock;
    static int countLines(const std::string &filename);

public:
    bulletinBoard();
    bulletinBoard(int client_socket);
    void setName(string name);
    int WriteMessage(string message, string filename);
    ~bulletinBoard();
};

bulletinBoard::bulletinBoard(int client_socket)
{
    this->name = "Anonymous";
    this->client_sock = client_socket;
}

void bulletinBoard::setName(string username)
{
    this->name = username;
}

int bulletinBoard::WriteMessage(string message, string filename)
{
    ofstream outfile;

    struct stat buffer;
    if (stat(filename.c_str(), &buffer) != 0)
    {
        // File doesn't exist, create it
        std::ofstream createFile(filename);
        createFile.close();
    }
    int id = countLines(filename) + 1;

    outfile.open(filename, ios_base::app); // append instead of overwrite
    outfile << "\"" << message << "\"," << id << "," << this->name << "\n";
    outfile.close();
    return id;
}

int bulletinBoard::countLines(const std::string &filename)
{
    std::ifstream file(filename);
    int lineCount = 0;

    if (file.is_open())
    {
        std::string line;
        while (std::getline(file, line))
        {
            lineCount++;
        }
        file.close();
    }
    else
    {
        std::cerr << "Unable to open file" << std::endl;
    }

    return lineCount;
}

bulletinBoard::~bulletinBoard()
{
}
