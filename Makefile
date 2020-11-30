CC = g++
DBGFLAGS = -g -Wall
CCFLAGS = -std=c++14 -pedantic -pthread -lboost_system -O
CC_INCLUDE_DIRS = /usr/local/include
CC_INCLUDE_PARAMS = $(addprefix -I , $(CC_INCLUDE_DIRS))
CC_LIB_DIRS = /usr/local/lib
CC_LIB_PARAMS = $(addprefix -L , $(CC_LIB_DIRS))
WCCFLAGS = -std=c++14 -lws2_32 -lwsock32
WCC_INCLUDE_DIRS = C:\\MinGW\\include
WCC_INCLUDE_PARAMS = $(addprefix -I , $(WCC_INCLUDE_DIRS))
WCC_LIB_DIRS = C:\\MinGW\\lib
WCC_LIB_PARAMS = $(addprefix -L , $(WCC_LIB_DIRS))

all: build

build: clean part1 part2

part1: clean http_server.cpp console.cpp
	$(CC) http_server.cpp $(CCFLAGS) $(CC_INCLUDE_PARAMS) $(CC_LIB_PARAMS) -o http_server
	$(CC) console.cpp $(CCFLAGS) $(CC_INCLUDE_PARAMS) $(CC_LIB_PARAMS) -o console.cgi

part2: cgi_server.cpp
	$(CC) cgi_server.cpp $(WCCFLAGS) -o cgi_server.exe

build-dbg: clean part1 part2

part1-dbg: clean http_server.cpp console.cpp
	$(CC) http_server.cpp $(DBGFLAGS) $(CCFLAGS) $(CC_INCLUDE_PARAMS) $(CC_LIB_PARAMS) -o http_server
	$(CC) console.cpp $(DBGFLAGS) $(CCFLAGS) $(CC_INCLUDE_PARAMS) $(CC_LIB_PARAMS) -o console.cgi

part2-dbg: cgi_server.cpp
	$(CC) cgi_server.cpp $(DBGFLAGS) $(WCCFLAGS) -o cgi_server.exe

clean:
	rm -rf http_server cgi_server.exe

appendix: commands/noop.cpp commands/number.cpp commands/removetag.cpp commands/removetag0.cpp
	mkdir -p bin
	$(CC) commands/noop.cpp -o bin/noop
	$(CC) commands/number.cpp -o bin/number
	$(CC) commands/removetag.cpp -o bin/removetag
	$(CC) commands/removetag0.cpp -o bin/removetag0
	$(CC) commands/delayedremovetag.cpp -o bin/delayedremovetag

clean-appendix:
	rm -rf bin
