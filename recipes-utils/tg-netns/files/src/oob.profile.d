#!/bin/sh

alias oobip='ip netns exec oob ip addr show dev nic0'
alias oobipglobal='ip netns exec oob ip addr show dev nic0 | grep global'
