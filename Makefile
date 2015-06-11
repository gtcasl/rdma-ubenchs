CXX:=g++
CXXFLAGS:=-Wall -Werror -std=c++11 -O2 -DREL
LDFLAGS:=-lrdmacm -libverbs -lpthread
BIN:=server3 client3

all: $(BIN)

server3: server3.cpp common3.h
	$(CXX) $(CXXFLAGS) server3.cpp -g -o server3 $(LDFLAGS)

client3: client3.cpp common3.h
	$(CXX) $(CXXFLAGS) client3.cpp -g -o client3 $(LDFLAGS)

.PHONY: clean
clean:
	rm -f server3 client3
