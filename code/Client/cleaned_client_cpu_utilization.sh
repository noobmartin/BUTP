#!/bin/sh
cat client_cpu_utilization.log |grep 'client' | cut -c48-53> cleaned_client_cpu_utilization.log
