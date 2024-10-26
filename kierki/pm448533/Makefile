CXX = g++
CXXFLAGS = -Wall -Wextra -O2 -std=c++17 

all: kierki-serwer kierki-klient

kierki-serwer: Server.o main_server.o common.o
	$(CXX) $(CXXFLAGS) -o kierki-serwer Server.o main_server.o common.o 

kierki-klient: Client.o main_client.o common.o
	$(CXX) $(CXXFLAGS) -o kierki-klient Client.o main_client.o common.o

Server.o: Server.cpp Server.hpp common.h
	$(CXX) $(CXXFLAGS) -c Server.cpp

Client.o: Client.cpp Client.hpp common.h
	$(CXX) $(CXXFLAGS) -c Client.cpp

main_server.o: main_server.cpp Server.hpp common.h
	$(CXX) $(CXXFLAGS) -c main_server.cpp

main_client.o: main_client.cpp Client.hpp common.h
	$(CXX) $(CXXFLAGS) -c main_client.cpp

common.o: common.cpp common.h
	$(CXX) $(CXXFLAGS) -c common.cpp

clean:
	rm -f *.o kierki-serwer kierki-klient
