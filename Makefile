CC=gcc
CFLAGS=-O3 -flto -std=c11 -fPIE -Wall
LDFLAGS=-lcrypto -lssl -lm -pthread
DEBUGFLAGS=-DDEBUG

main: src/*.c
	$(CC) $(CFLAGS) $? $(LDFLAGS) -o bin/czarrapo

debug: src/*.c
	$(CC) $(CFLAGS) $(DEBUGFLAGS) $? $(LDFLAGS) -o bin/czarrapo

testfile:
	bash test/generate_test_file.bash

clean:
	rm -f bin/czarrapo
	rm -f test/czarrapo_rsa test/czarrapo_rsa.pub
	rm -f test/test.txt test/test.crypt test/test.decrypt