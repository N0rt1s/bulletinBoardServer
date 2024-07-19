# Compiler
CXX = g++

# Compiler flags
CXXFLAGS = -pthread

# Target executable
TARGET = bbserv

# Source files
SRCS = server.cpp bulletinBoard.cpp threadPool.cpp

# Default target
all: $(TARGET)

# Rule to compile and link in one step
$(TARGET):
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS)

# Clean up
clean:
	rm -f $(TARGET)
