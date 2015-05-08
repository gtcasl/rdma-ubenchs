CXX:=g++
CXXFLAGS:=-Wall -Werror
LDFLAGS:=-lrdmacm -libverbs -lpthread
BIN:=server client

all: $(BIN)

server: common.o
	$(CXX) $(CXXFLAGS) server.cpp common.o -g -o server $(LDFLAGS)

client: common.o
	$(CXX) $(CXXFLAGS) client.cpp common.o -g -o client $(LDFLAGS)

common.o:
	$(CXX) $(CXXFLAGS) common.cpp -g -c $(LDFLAGS)

.PHONY: clean
clean:
	rm -f server client *.o
