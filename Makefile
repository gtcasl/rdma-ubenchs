CXX:=g++
CXXFLAGS:=-Wall -Werror

all:
	$(CXX) $(CXXFLAGS) server.cpp -o server -lrdmacm
	$(CXX) $(CXXFLAGS) client.cpp -o client -lrdmacm

.PHONY: clean
clean:
	rm server client
