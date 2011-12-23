#!/bin/sh
gcc -Wall -Os -march=native server.c -pg -o server -lrt -g -finline-functions -fno-strict-aliasing -lm
