#!/bin/sh
gcc -Wall -Os -march=native butp.c client.c -pg -o Client/client -lrt -g -finline-functions -fno-strict-aliasing -lm
gcc -Wall -Os -march=native butp.c server.c -pg -o Server/server -lrt -g -finline-functions -fno-strict-aliasing -lm
