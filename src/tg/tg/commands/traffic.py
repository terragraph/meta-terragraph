#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import logging

import click
from terragraph_thrift.Controller import ttypes as ctrlTypes
from tg.commands import base, consts


_log = logging.getLogger(__name__)
_IPERF_FORMATS = ["k", "m", "g", "K", "M", "G"]


class TrafficCli(object):
    def __init__(self):
        self.traffic.add_command(self._iperf, name="iperf")
        self.traffic.add_command(self._ping, name="ping")
        self._iperf.add_command(self._startIperf, name="start")
        self._iperf.add_command(self._stopIperf, name="stop")
        self._iperf.add_command(self._statusIperf, name="status")
        self._ping.add_command(self._startPing, name="start")
        self._ping.add_command(self._stopPing, name="stop")
        self._ping.add_command(self._statusPing, name="status")

    @click.group()
    def traffic():
        """Run traffic measurements"""
        pass

    @click.group()
    @click.pass_obj
    def _iperf(cli_opts):
        """Start an iperf session"""
        pass

    @click.group()
    @click.pass_obj
    def _ping(cli_opts):
        """Start a ping session"""
        pass

    @click.command()
    @click.option("--src-id", "-s", type=str, required=True, help="Source node id")
    @click.option("--dst-id", "-d", type=str, required=True, help="Destination node id")
    @click.option("--dst-ip", "-ip", type=str, help="Destination node IPv6 address")
    @click.option(
        "--link-local", is_flag=True, help="Use the link-local IP address and interface"
    )
    @click.option("--no-wait", is_flag=True, help="Do not wait for the output")
    @click.option("--bitrate", "-b", type=int, help="Target traffic bitrate (bps)")
    @click.option("--time", "-t", type=int, help="Measurement duration (in seconds)")
    @click.option(
        "--proto", "-p", type=click.Choice(["tcp", "udp"]), help="Transport protocol"
    )
    @click.option(
        "--interval",
        "-i",
        type=int,
        help="Interval between bandwidth reports (in seconds)",
    )
    @click.option("--window", "-w", type=int, help="Window size (in bytes)")
    @click.option(
        "--mss", "-M", type=int, help="TCP maximum segment size (MTU - 40 bytes)"
    )
    @click.option("--nodelay", "-N", is_flag=True, help="Disable Nagle's Algorithm")
    @click.option("--omit", "-O", type=int, help="Omit the first n seconds")
    @click.option("--verbose", "-V", is_flag=True, help="Show more detailed output")
    @click.option("--json", "-J", is_flag=True, help="Output in JSON format")
    @click.option("--buffer", "-l", type=int, help="Length of buffer to read or write")
    @click.option(
        "--fmt", "-f", type=click.Choice(_IPERF_FORMATS), help="Format to report"
    )
    @click.option("--vpp", "-vpp", is_flag=True, help="Bind to vpp stack")
    @click.option(
        "--client-delay", "-D", type=int, help="Server-client delay (in milliseconds)"
    )
    @click.pass_obj
    def _startIperf(
        cli_opts,
        src_id,
        dst_id,
        dst_ip,
        link_local,
        no_wait,
        bitrate,
        time,
        proto,
        interval,
        window,
        mss,
        nodelay,
        omit,
        verbose,
        json,
        buffer,
        fmt,
        vpp,
        client_delay,
    ):
        """Start an iperf session"""
        if fmt:
            fmt = _IPERF_FORMATS.index(fmt) + 1
        IperfCmd(cli_opts)._start(
            src_id,
            dst_id,
            dst_ip,
            link_local,
            no_wait,
            bitrate,
            time,
            proto,
            interval,
            window,
            mss,
            nodelay,
            omit,
            verbose,
            json,
            buffer,
            fmt,
            vpp,
            client_delay,
        )

    @click.command()
    @click.option("--id", type=str, required=True, help="Session id")
    @click.pass_obj
    def _stopIperf(cli_opts, id):
        """Stop an iperf session"""
        IperfCmd(cli_opts)._stop(id)

    @click.command()
    @click.pass_obj
    def _statusIperf(cli_opts):
        """Get all running iperf sessions"""
        IperfCmd(cli_opts)._status()

    @click.command()
    @click.option("--src-id", "-s", type=str, required=True, help="Source node id")
    @click.option("--dst-id", "-d", type=str, help="Destination node id")
    @click.option("--dst-ip", "-ip", type=str, help="Destination node IPv6 address")
    @click.option(
        "--link-local", is_flag=True, help="Use the link-local IP address and interface"
    )
    @click.option("--no-wait", is_flag=True, help="Do not wait for the output")
    @click.option("--adaptive", "-A", is_flag=True, help="Adaptive ping")
    @click.option(
        "--count", "-c", type=int, help="Stop after sending count ECHO_REQUEST packets"
    )
    @click.option(
        "--timestamp", "-D", is_flag=True, help="Print timestamp before each line"
    )
    @click.option("--flood", "-f", is_flag=True, help="Flood ping")
    @click.option(
        "--interval",
        "-i",
        type=int,
        help="Wait interval seconds between sending each packet",
    )
    @click.option(
        "--preload",
        "-l",
        type=int,
        help="Sends this many packets not waiting for a reply",
    )
    @click.option("--numeric", "-n", is_flag=True, help="Numeric output only")
    @click.option(
        "--outstanding",
        "-O",
        is_flag=True,
        help="Report outstanding ICMP ECHO reply before sending " "next packet",
    )
    @click.option("--quiet", "-q", is_flag=True, help="Quiet output")
    @click.option(
        "--packet_size", type=int, help="Specifies the number of data bytes to be sent"
    )
    @click.option("--sndbuf", "-S", type=int, help="Set socket sndbuf")
    @click.option("--ttl", "-t", type=int, help="Set the IP time-to-live")
    @click.option("--verbose", "-v", is_flag=True, help="Verbose output")
    @click.option(
        "--deadline",
        "-w",
        type=int,
        help="Seconds before exit regardless of how many packets " "sent or received",
    )
    @click.option(
        "--timeout", "-W", type=int, help="Time to wait for a response, in seconds"
    )
    @click.pass_obj
    def _startPing(
        cli_opts,
        src_id,
        dst_id,
        dst_ip,
        link_local,
        no_wait,
        adaptive,
        count,
        timestamp,
        flood,
        interval,
        preload,
        numeric,
        outstanding,
        quiet,
        packet_size,
        sndbuf,
        ttl,
        verbose,
        deadline,
        timeout,
    ):
        """Start a ping session"""
        PingCmd(cli_opts)._start(
            src_id,
            dst_id,
            dst_ip,
            link_local,
            no_wait,
            adaptive,
            count,
            timestamp,
            flood,
            interval,
            preload,
            numeric,
            outstanding,
            quiet,
            packet_size,
            sndbuf,
            ttl,
            verbose,
            deadline,
            timeout,
        )

    @click.command()
    @click.option("--id", type=str, required=True, help="Session id")
    @click.pass_obj
    def _stopPing(cli_opts, id):
        """Stop a ping session"""
        PingCmd(cli_opts)._stop(id)

    @click.command()
    @click.pass_obj
    def _statusPing(cli_opts):
        """Get all running ping sessions"""
        PingCmd(cli_opts)._status()


class TrafficCmd(base.BaseCmd):
    def __init__(self, cli_opts):
        super(TrafficCmd, self).__init__(cli_opts)

    def _receive_ctrl_traffic_cmd_resp(self, resp_msg, resp_type):
        response1 = resp_msg
        response2 = ctrlTypes.E2EAck()
        recv_type = self._recv_multi_from_ctrl(
            [(resp_type, response1), (ctrlTypes.MessageType.E2E_ACK, response2)],
            consts.TRAFFIC_APP_CTRL_ID,
        )

        if recv_type == resp_type:
            return response1
        else:
            self._my_exit(False, response2.message)


class IperfCmd(TrafficCmd):
    def __init__(self, cli_opts):
        super(IperfCmd, self).__init__(cli_opts)

    def _start(
        self,
        src_id,
        dst_id,
        dst_ip,
        link_local,
        no_wait,
        bitrate,
        time,
        proto,
        interval,
        window,
        mss,
        nodelay,
        omit,
        verbose,
        json,
        buffer,
        fmt,
        vpp,
        client_delay,
    ):
        # set receive timeout
        if no_wait:
            self._connect_to_controller()
        else:
            recv_timeout = ((time if time else 10) + 10) * 1000
            self._connect_to_controller(recv_timeout=recv_timeout)

        # send request
        protocol = None
        if proto:
            proto = proto.upper()
            if proto not in ctrlTypes.IperfTransportProtocol._NAMES_TO_VALUES:
                self._my_exit(False, "Invalid transport protocol specified")
            protocol = ctrlTypes.IperfTransportProtocol._NAMES_TO_VALUES[proto]
        options = ctrlTypes.IperfOptions(
            bitrate,
            time,
            protocol,
            interval,
            window,
            mss,
            nodelay,
            omit,
            verbose,
            json,
            buffer,
            fmt,
            None,  # parallelStreams
            vpp,
            client_delay,
        )
        self._send_to_ctrl(
            ctrlTypes.MessageType.START_IPERF,
            ctrlTypes.StartIperf(src_id, dst_id, dst_ip, options, link_local),
            consts.TRAFFIC_APP_CTRL_ID,
        )
        start_iperf_resp = self._receive_ctrl_traffic_cmd_resp(
            ctrlTypes.StartIperfResp(), ctrlTypes.MessageType.START_IPERF_RESP
        )
        if no_wait:
            self._my_exit(True, start_iperf_resp)
            return

        # wait for responses
        _log.info(
            "Started iperf with id=%s. Waiting for responses..." % (start_iperf_resp.id)
        )
        for i in range(2):
            iperf_output = self._receive_ctrl_traffic_cmd_resp(
                ctrlTypes.IperfOutput(), ctrlTypes.MessageType.IPERF_OUTPUT
            )

            _log.info(
                "Received output from %s (%d/2):\n%s"
                % (
                    "server" if iperf_output.isServer else "client",
                    i + 1,
                    iperf_output.output,
                )
            )
        self._my_exit(True)

    def _stop(self, id):
        self._connect_to_controller()
        self._send_to_ctrl(
            ctrlTypes.MessageType.STOP_IPERF,
            ctrlTypes.StopIperf(id),
            consts.TRAFFIC_APP_CTRL_ID,
        )
        e2e_ack = self._recv_e2e_ack(consts.TRAFFIC_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)

    def _status(self):
        self._connect_to_controller()
        self._send_to_ctrl(
            ctrlTypes.MessageType.GET_IPERF_STATUS,
            ctrlTypes.GetIperfStatus(),
            consts.TRAFFIC_APP_CTRL_ID,
        )
        iperf_report = self._receive_ctrl_traffic_cmd_resp(
            ctrlTypes.IperfStatus(), ctrlTypes.MessageType.IPERF_STATUS
        )
        self._my_exit(True, iperf_report)


class PingCmd(TrafficCmd):
    def __init__(self, cli_opts):
        super(PingCmd, self).__init__(cli_opts)

    def _start(
        self,
        src_id,
        dst_id,
        dst_ip,
        link_local,
        no_wait,
        adaptive,
        count,
        timestamp,
        flood,
        interval,
        preload,
        numeric,
        outstanding,
        quiet,
        packet_size,
        sndbuf,
        ttl,
        verbose,
        deadline,
        timeout,
    ):
        # set receive timeout
        if no_wait:
            self._connect_to_controller()
        else:
            count_default = count if count else 10
            timeout_default = timeout if timeout else 1
            deadline_default = (
                deadline if deadline else (count_default * timeout_default)
            )
            recv_timeout = (deadline_default + 2) * 1000
            self._connect_to_controller(recv_timeout=recv_timeout)

        # send request
        options = ctrlTypes.PingOptions(
            adaptive,
            count,
            timestamp,
            flood,
            interval,
            preload,
            numeric,
            outstanding,
            quiet,
            packet_size,
            sndbuf,
            ttl,
            verbose,
            deadline,
            timeout,
        )
        self._send_to_ctrl(
            ctrlTypes.MessageType.START_PING,
            ctrlTypes.StartPing(src_id, dst_id, dst_ip, options, link_local),
            consts.TRAFFIC_APP_CTRL_ID,
        )
        start_ping_resp = self._receive_ctrl_traffic_cmd_resp(
            ctrlTypes.StartPingResp(), ctrlTypes.MessageType.START_PING_RESP
        )
        if no_wait:
            self._my_exit(True, start_ping_resp)
            return

        # wait for response
        _log.info(
            "Started ping with id=%s. Waiting for response..." % (start_ping_resp.id)
        )
        ping_output = self._receive_ctrl_traffic_cmd_resp(
            ctrlTypes.PingOutput(), ctrlTypes.MessageType.PING_OUTPUT
        )
        _log.info("Received output:\n%s" % (ping_output.output))
        self._my_exit(True)

    def _stop(self, id):
        self._connect_to_controller()
        self._send_to_ctrl(
            ctrlTypes.MessageType.STOP_PING,
            ctrlTypes.StopPing(id),
            consts.TRAFFIC_APP_CTRL_ID,
        )
        e2e_ack = self._recv_e2e_ack(consts.TRAFFIC_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)

    def _status(self):
        self._connect_to_controller()
        self._send_to_ctrl(
            ctrlTypes.MessageType.GET_PING_STATUS,
            ctrlTypes.GetPingStatus(),
            consts.TRAFFIC_APP_CTRL_ID,
        )
        ping_report = self._receive_ctrl_traffic_cmd_resp(
            ctrlTypes.PingStatus(), ctrlTypes.MessageType.PING_STATUS
        )
        self._my_exit(True, ping_report)
