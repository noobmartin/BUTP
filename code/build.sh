#!/bin/sh
gcc -Wall butp.c client.c -o Client/client -lrt -g
gcc -Wall butp.c server.c -o Server/server -lrt -g
