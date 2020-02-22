CXX = g++
CXXFLAGS = -Wall -Wextra -pedantic -std=c++2a -g -o3
#-fsanitize=address -fsanitize=leak
#-fsanitize=thread
SRC = test.cc
EXEC = test

all: $(EXEC)

$(EXEC): test.cc lockfree_linkedlist.h HazardPointer/reclaimer.h
	$(CXX) $(CXXFLAGS) -o $(EXEC) test.cc

HazardPointer/reclaimer.h:
	git submodule update --remote

.Phony : clean

clean:
	rm -rf $(EXEC)