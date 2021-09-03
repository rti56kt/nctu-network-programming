CC = g++
DBGFLAGS = -g -Wall

all: build

build: clean server1 server2

server1: clean np_simple.cpp
	$(CC) np_simple.cpp -O -o np_simple

server2: clean np_single_proc.cpp
	$(CC) np_single_proc.cpp -O -o np_single_proc

build-dbg: clean server1 server2

server1-dbg: clean np_simple.cpp
	$(CC) np_simple.cpp $(DBGFLAGS) -o np_simple

server2-dbg: clean np_single_proc.cpp
	$(CC) np_single_proc.cpp $(DBGFLAGS) -o np_single_proc

clean:
	rm -rf np_single np_single_proc

appendix: commands/noop.cpp commands/number.cpp commands/removetag.cpp commands/removetag0.cpp
	mkdir -p bin
	$(CC) commands/noop.cpp -o bin/noop
	$(CC) commands/number.cpp -o bin/number
	$(CC) commands/removetag.cpp -o bin/removetag
	$(CC) commands/removetag0.cpp -o bin/removetag0

clean-appendix:
	rm -rf bin
