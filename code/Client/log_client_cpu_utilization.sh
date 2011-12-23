#!/bin/sh
pidstat -C "client" -I -u 1 > client_cpu_utilization.log
