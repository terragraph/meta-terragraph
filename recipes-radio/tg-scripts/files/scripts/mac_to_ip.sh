#!/bin/sh
node_name=`echo node-$1 | sed 's/:/./g'`
node_ip=`breeze kvstore prefixes --nodes $node_name | grep fc00 | sed 's/\/.*/1/'`
echo $node_ip
