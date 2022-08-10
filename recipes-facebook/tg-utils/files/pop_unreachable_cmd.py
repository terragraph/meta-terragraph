#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import datetime

#
# Set encoding to UTF-8 for all modules as it is needed for click in python3
#
import locale
import logging
import os
import socket
import subprocess
from logging.handlers import RotatingFileHandler
from threading import Timer

import click


def getpreferredencoding(do_setlocale=True):
    return "utf-8"


locale.getpreferredencoding = getpreferredencoding

"""
This script collects traceroute logs for unreachable pop nodes.
It is invoked by watchdog when no pop nodes are reachable for several minutes
"""


class LogOutput(object):
    """
    LogOutput class to log output into the provided file name or class
    defined file
    """

    _log_file = "/var/log/openr/openr_debug.log"
    _max_file_size = 0x40000
    _bkup_count = 2
    _logger = None

    def _get_logger(self):
        """ Get logger handle """

        if LogOutput._logger is not None:
            return LogOutput._logger

        hndlr = RotatingFileHandler(
            LogOutput._log_file,
            mode="a",
            maxBytes=LogOutput._max_file_size,
            backupCount=LogOutput._bkup_count,
        )

        formatter = logging.Formatter("%(message)s")
        hndlr.setFormatter(formatter)
        hndlr.setLevel(logging.INFO)

        LogOutput._logger = logging.getLogger(__name__)
        LogOutput._logger.addHandler(hndlr)
        LogOutput._logger.setLevel(logging.INFO)
        return LogOutput._logger

    def log_output(self, lines, log_file):
        """ Output to log file if provided else output to logger """

        timestamp = datetime.datetime.now().strftime(
            "\n----- %A, %d. %B %Y %I:%M %p -----"
        )
        lines.insert(0, timestamp)

        if log_file:
            with open(log_file, "a") as logfile:
                logfile.write("\n".join([line + "\n" for line in lines]))
            return

        mylogger = self._get_logger()
        for line in lines:
            mylogger.info(line)


class RunCmd(object):
    """
    Run a command and return the output as list
    """

    def __init__(self):
        """ Command to execute """

    def run(self, cmd, timeout):
        """ execute the command """
        lines = []
        lines.append("Running {}, timeout: {}".format(" ".join(cmd), timeout))
        stdout = None
        stderr = None
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        timer = Timer(timeout, proc.kill)
        try:
            timer.start()
            stdout, stderr = proc.communicate()
        except Exception as e:
            lines.append("Failed to run {}".format(" ".join(cmd)))
            lines.append(" error: {}".format(e))
        finally:
            timer.cancel()
            if not stdout and not stderr:
                return [
                    "Command {}, timed out after {} seconds".format(
                        " ".join(cmd), timeout
                    )
                ]

        lines.append(stdout)
        lines.append(stderr)

        return lines


class TracerouteCmd(RunCmd):
    """ traceroute command """

    _timeout = 30
    _max_ttl = 30

    def __init__(self):
        super(TracerouteCmd, self).__init__()

    def _check_ip(self, ipaddr):
        """ Determine if IP is a valid v4 or v6 """

        isv4 = None
        try:
            socket.inet_aton(ipaddr)
            isv4 = True
        except socket.error:
            try:
                socket.inet_pton(socket.AF_INET6, ipaddr)
                isv4 = False
            except socket.error:
                pass
        return isv4

    def run_cmd(self, ipaddr, log_file, timeout, host=None, isv4=None):
        """ Determine v4 or v6 traceroute """

        timeout = TracerouteCmd._timeout if timeout is None else timeout

        if host is not None:
            ipaddr = host
        else:
            isv4 = self._check_ip(ipaddr)

        if isv4 is not None:
            cmd = "traceroute" if isv4 else "traceroute6"
            self.cmdstr = [cmd, "-m {}".format(TracerouteCmd._max_ttl), ipaddr]
            lines = super(TracerouteCmd, self).run(self.cmdstr, timeout)
        else:
            lines = ["Not a valid IP address: {}".format(ipaddr)]

        LogOutput().log_output(lines, log_file)


class TracerouteCli(object):
    @click.command(name="traceroute")
    @click.option("--ipaddr", default=None, type=str, help="v4/v6 address")
    @click.option(
        "--file_name",
        default=None,
        type=str,
        help="File with list of v4 or v6 addresses",
    )
    @click.option("--log_file", default=None, help="file name to log")
    @click.option("--host", default=None, help="hostname")
    @click.option(
        "--isv4/--no-isv4", default=True, help="v4 or v6 traceroute for given host"
    )
    @click.option("--timeout", default=None, type=int, help="command timeout value")
    @click.pass_context
    def tracepath(self, ipaddr, file_name, log_file, host, isv4, timeout):
        """ Traceroute host or ip address or both """

        if ipaddr:
            TracerouteCmd().run_cmd(ipaddr, log_file, timeout, None, None)
        if host:
            TracerouteCmd().run_cmd(None, log_file, timeout, host, isv4)
        if file_name:
            if os.path.isfile(file_name):
                if os.stat(file_name).st_size == 0:
                    LogOutput().log_output(
                        ["File {} is empty".format(file_name)], log_file
                    )
                    return
                with open(file_name, "r") as f:
                    for line in f:
                        TracerouteCmd().run_cmd(line.strip(), log_file, timeout)
            else:
                LogOutput().log_output(
                    ["File {} not found".format(file_name)], log_file
                )


@click.group()
@click.pass_context
def cli(self):
    """ Commands to run """


def main():
    """ entry point """

    cli.add_command(TracerouteCli().tracepath)
    cli()


if __name__ == "__main__":
    main()
