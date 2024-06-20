#include "bulletinBoard.h"
#include <fstream>
#include <iostream>
#include <sys/stat.h>

using namespace std;

bulletinBoard::bulletinBoard() : name("Anonymous") {}

// bulletinBoard::bulletinBoard(int client_socket) : name("Anonymous"), client_sock(client_socket) {}

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
        ifstream file(filename);
        string line;
        ofstream temp("temp.txt");
        int lineNumber = 1;

        while (getline(file, line))
        {
            if (lineNumber == messageId)
            {
                line = to_string(messageId) + "," + this->name + ",\"" + message + "\"" + "\n";
            }
            temp << line << endl;
            lineNumber++;
        }

        temp.close();
        file.close();

        remove(filename.c_str());
        rename("temp.txt", filename.c_str());
        return true;
    }
    catch (const exception &e)
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

bulletinBoard::~bulletinBoard() {}
