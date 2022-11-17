#!/bin/sh
sv restart stats_agent
/etc/init.d/fluent-bit restart
