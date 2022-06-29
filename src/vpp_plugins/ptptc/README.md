This plugin implements support for Precision Time Protocol Transparent Clocks
in Terragraph networks.  The plugin attaches a node to the VPP graph which
records incoming PTP packets and hardware timestamps, and then corrects for
network delay when forwarding these packets out of the TG network.
