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
    string getName();
    int writeMessage(string message, string filename);
    string readMessage(int messageId, string filename);
    bool replaceMessage(int messageId, string message, string filename);
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
string bulletinBoard::getName()
{
    return this->name;
}

int bulletinBoard::writeMessage(string message, string filename)
{
    ofstream outfile;

    struct stat buffer;
    if (stat(filename.c_str(), &buffer) != 0)
    {
        // File doesn't exist, create it
        ofstream createFile(filename);
        createFile.close();
    }
    int id = countLines(filename) + 1;

    outfile.open(filename, ios_base::app); // append instead of overwrite
    outfile << id << "," << this->name << ",\"" << message << "\"" << "\n";
    outfile.close();
    return id;
}

bool bulletinBoard::replaceMessage(int messageId, string message, string filename)
{
    try
    {
        std::ifstream file(filename);
        std::string line;
        std::ofstream temp("temp.txt");
        int lineNumber = 1;

        while (std::getline(file, line))
        {
            if (lineNumber == messageId)
            {
                line = to_string(messageId) + "," + this->name + ",\"" + message + "\"" + "\n";
            }
            temp << line << std::endl;
            lineNumber++;
        }

        temp.close();
        file.close();

        std::remove(filename.c_str());
        std::rename("temp.txt", filename.c_str());
        return true;
    }
    catch (const std::exception &e)
    {
        return false;
    }
}

int bulletinBoard::countLines(const string &filename)
{
    ifstream file(filename);
    int lineCount = 0;

    if (file.is_open())
    {
        string line;
        while (getline(file, line))
        {
            lineCount++;
        }
        file.close();
    }
    else
    {
        cerr << "Unable to open file" << endl;
    }

    return lineCount;
}

bulletinBoard::~bulletinBoard()
{
}
