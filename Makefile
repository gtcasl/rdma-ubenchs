CXX:=g++
CXXFLAGS:=-Wall -Werror

all:
	$(CXX) $(CXXFLAGS) server.cpp -o server -lrdmacm

.PHONY: clean
clean:
	rm server
