#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import socket
import sys
import time

import click
import serial
import zmq
from terragraph_thrift.Controller import ttypes as ctrlTypes
from terragraph_thrift.DriverMessage import ttypes as drvrTypes
from terragraph_thrift.PassThru import ttypes as passThruTypes
from thrift.protocol import fastproto
from thrift.transport import TTransport


class DriverIfError(Exception):
    pass


def timestr_to_secs(ddmmyy_hhmmss):
    tm = time.strptime(ddmmyy_hhmmss, "%d%m%y %H%M%S")
    return time.mktime(tm)


def process_nmea_message(opts, msg):
    msg = msg.decode("utf-8")
    if not msg.startswith("$"):
        return
    fields = msg.split(",")
    if fields[0] != "$GPRMC":
        return

    timeofday = fields[1]
    datestr = fields[9]

    # drop decimal millis
    timeofday = timeofday.split(".")[0]

    pps_time = timestr_to_secs(datestr + " " + timeofday)
    upload_time_to_firmware(opts, pps_time)


def upload_time_to_firmware(opts, gps_time):

    gpsTimeValue = passThruTypes.GpsTimeValue()
    gpsTimeValue.unixTimeSecs = gps_time
    gpsTimeValue.unixTimeNsecs = 0

    sys.stderr.write("Sending gpstime: %d" % gps_time)

    try:
        fw_ack = send_driver_if_message(
            opts, ctrlTypes.MessageType.GPS_SEND_TIME, gpsTimeValue
        )
    except zmq.ZMQError as e:
        raise DriverIfError("could not upload gps time (%s)" % e)

    finally:
        pass

    if not fw_ack.success:
        raise DriverIfError("could not upload gps time (%s)" % fw_ack)
    sys.stderr.write("... ok\r")


def deserialize(inByteArray, outThriftStruct):
    fastproto.decode(
        outThriftStruct,
        TTransport.TMemoryBuffer(inByteArray),
        [outThriftStruct.__class__, outThriftStruct.thrift_spec, False],
        utf8strings=0,
        protoid=2,
    )  # compact=2, binary=0


def serialize(inThriftStruct):
    return fastproto.encode(
        inThriftStruct,
        [inThriftStruct.__class__, inThriftStruct.thrift_spec, False],
        utf8strings=0,
        protoid=2,
    )


def send_driver_if_message(opts, msg_type, msg_data):
    dr_msg = drvrTypes.DriverMessage()
    dr_msg.radioMac = opts["radio_mac"]
    if msg_data is not None:
        dr_msg.value = serialize(msg_data)
    msg = ctrlTypes.Message(msg_type, serialize(dr_msg))
    ret = _send_if_message(opts, serialize(msg))

    deser_ret = ctrlTypes.Message()
    deserialize(ret, deser_ret)

    dret_msg = drvrTypes.DriverMessage()
    deserialize(deser_ret.value, dret_msg)

    fw_ack = drvrTypes.FwAck()
    deserialize(dret_msg.value, fw_ack)

    return fw_ack


def _send_if_message(opts, msg):
    sock = zmq.Context().socket(zmq.PAIR)
    sock.SNDTIMEO = 5000
    sock.RCVTIMEO = 5000
    sock.linger = 250
    poller = zmq.Poller()
    poller.register(sock, zmq.POLLIN)

    try:
        sock.set(zmq.IPV6, 1)

        pair_sock_url = "tcp://%s" % (opts["driver_if_host_port"])
        sock.connect(pair_sock_url)
        sock.send(msg)
        return sock.recv()
    finally:
        sock.close()


class GpsModule:
    def __init__(self, use_uart, gpsd_port, gpsd_host, uart_dev):
        self._use_uart = use_uart
        self.gpsd_port = int(gpsd_port)
        self.gpsd_host = gpsd_host
        self.uart_dev = uart_dev
        self._ser = None
        self._sock = None

    def __enter__(self):
        if self._use_uart:
            self._ser = serial.Serial(self.uart_dev, 115200, timeout=1)
            self._ser.__enter__()
        else:
            self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._sock.__enter__()
            self._sock.connect((self.gpsd_host, self.gpsd_port))
            self._sock.sendall(b'?WATCH={"enable":true,"nmea":true,"raw":1};\n')
        return self

    def recv(self):
        if self._use_uart:
            return self._ser.readline()
        else:
            return self._sock.recv(1600)

    def __exit__(self, *exc):
        if self._use_uart:
            self._ser.__exit__()
        else:
            self._sock.__exit__()


@click.command()
@click.option(
    "--uart", default=False, is_flag=True, help="bypass gpsd and access uart directly"
)
@click.option("--uart_dev", show_default=True, default="/dev/ttyS1")
@click.option("--gpsd_host_port", default="localhost:2947")
@click.option("--driver_if_host_port", default="localhost:17989")
@click.option("--radio_mac", default="")
def main(gpsd_host_port, driver_if_host_port, radio_mac, uart, uart_dev):

    gpsd_host, gpsd_port = gpsd_host_port.split(":")
    with GpsModule(uart, gpsd_port, gpsd_host, uart_dev) as gps:

        while True:
            msg = gps.recv()
            try:
                process_nmea_message(
                    {
                        "driver_if_host_port": driver_if_host_port,
                        "radio_mac": radio_mac,
                    },
                    msg,
                )
            except DriverIfError as e:
                sys.stderr.write("...error: %s\r" % e)


if __name__ == "__main__":
    main()
