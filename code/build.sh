#!/bin/sh
gcc -Wall butp.c client.c -pg -o Client/client -lrt -g -finline-functions
gcc -Wall butp.c server.c -pg -o Server/server -lrt -g -finline-functions
