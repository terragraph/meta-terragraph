#!/bin/sh
# Usage: some_cmd | add_timestamp.sh
# adds timestamp, e.g. `ping6 localhost | add_timestamp.sh` outputs
# `1514935626:  64 bytes from localhost (127.0.0.1): icmp_seq=26 ttl=96 time=0.043 ms`
awk '{ print strftime("%s: "), $0; fflush(); }'

