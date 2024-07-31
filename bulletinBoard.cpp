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

pair<string, string> bulletinBoard::readMessage(int pos, int length, string filename)
{
    fstream file(filename, ios::in);
    pair<string, string> messagePair;

    if (!file.is_open())
    {
        return messagePair;
    }

    file.seekg(pos);
    string line;
    getline(file, line);

    if (line.empty())
    {
        return messagePair;
    }

    int start = 0;
    int commaCount = 0;
    bool inQuotes = false;
    string temp;

    for (char ch : line)
    {
        if (ch == '\"')
        {
            inQuotes = !inQuotes;
        }
        else if (ch == ',' && !inQuotes)
        {
            if (commaCount == 0)
            {
                temp.clear();
                // Skip the first comma (ID field)
            }
            else if (commaCount == 1)
            {
                messagePair.first = temp;
                temp.clear();
            }
            commaCount++;
        }
        else
        {
            temp += ch;
        }
    }

    messagePair.second = temp;

    file.close();

    return messagePair;
}

void bulletinBoard::writeMessage(string message, string filename)
{
    ofstream outfile;

    struct stat buffer;
    if (stat(filename.c_str(), &buffer) != 0)
    {
        // File doesn't exist, create it
        ofstream createFile(filename);
        createFile.close();
    }
    // int id = countLines(filename) + 1;

    outfile.open(filename, ios_base::app); // append instead of overwrite
    outfile << message;
    outfile.close();
}

bool bulletinBoard::replaceMessage(int startPos, int messageLength, string message, string fileName)
{
    try
    {
        fstream file(fileName, ios::in);

        if (!file.is_open())
        {
            return false;
        }
        file.seekg(0, ios::beg);
        string beforeContent(startPos, '\0');
        file.read(&beforeContent[0], startPos);
        // Read the remaining content after the line to be replaced
        file.seekg(startPos + messageLength);
        string remainingContent;
        getline(file, remainingContent, '\0');
        file.close();
        // Seek to the start position of the line to be replaced
        file.open(fileName, ios::out);

        if (!file.is_open())
        {
            return false;
        }
        file << beforeContent;
        file.seekp(startPos);
        file << message << remainingContent;
        file.close();
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
