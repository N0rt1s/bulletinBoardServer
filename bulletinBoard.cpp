#include <cstring>
#include <iostream>

using namespace std;

class bulletinBoard
{
private:
    string name;

public:
    bulletinBoard();
    void setName(string name);
    ~bulletinBoard();
};

bulletinBoard::bulletinBoard()
{
    this->name = "Anonymous";
}

void bulletinBoard::setName(string name)
{
    this->name = name;
}

bulletinBoard::~bulletinBoard()
{
}
