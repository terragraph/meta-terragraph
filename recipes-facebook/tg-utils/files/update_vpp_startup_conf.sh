#!/bin/sh

# Create vpp startup configuration
# By default uses /var/run/node_info so gen_node_info_file.sh must run first
case "$1" in
	start)
		/usr/sbin/config_get_env.sh
		/usr/sbin/update_vpp_startup_conf
		;;
	*)
		/usr/sbin/update_vpp_startup_conf --help
		;;
esac
