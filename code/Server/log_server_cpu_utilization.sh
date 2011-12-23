#!/bin/sh
pidstat -C "server" -I -u 1 > server_cpu_utilization.log
