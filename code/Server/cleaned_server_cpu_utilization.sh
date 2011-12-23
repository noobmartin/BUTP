#!/bin/sh
cat server_cpu_utilization.log |grep 'server' | cut -c48-53> cleaned_server_cpu_utilization.log
