#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

#
# Set encoding to UTF-8 for all modules as it is needed for click in python3
#
import inspect
import locale
import logging
import re
import subprocess
from random import randrange
from typing import Optional

import click
from terragraph_thrift.Controller import ttypes as ctrlTypes

from . import help, r2d2_commands as cmds


# TODO: Monkey patch click until a cleaner API is available
# Copied from the click.decorators module and does not .replace("_", "-")
#
# In Click>=7.1, delete this block and instead add in @click.group():
#
#  def normalize(name):
#    return name.replace("_", "-")
#
# @click.group(context_settings={"token_normalize_func": normalize})
# def your_group():
#    <...>
#
def _make_command(f, name, attrs, cls):
    if isinstance(f, click.Command):
        raise TypeError("Attempted to convert a callback into a command twice.")
    try:
        params = f.__click_params__
        params.reverse()
        del f.__click_params__
    except AttributeError:
        params = []
    help = attrs.get("help")
    if help is None:
        help = inspect.getdoc(f)
        if isinstance(help, bytes):
            help = help.decode("utf-8")
    else:
        help = inspect.cleandoc(help)
    attrs["help"] = help
    # We don't need to waste time checking for this - We're Python 3
    # _check_for_unicode_literals()
    # Do not replace _ with - like upstream does ...
    return cls(name=name or f.__name__.lower(), callback=f, params=params, **attrs)


if click.__version__[0] == "7":
    click.decorators._make_command = _make_command


def getpreferredencoding(do_setlocale=True):
    return "utf-8"


locale.getpreferredencoding = getpreferredencoding

BROADCAST_MAC = "FF:FF:FF:FF:FF:FF"
LOG = logging.getLogger(__name__)
MAC_RE = re.compile(r"^([0-9a-f]{2}[:-]){5}[0-9a-f]{2}$")


ScanTypes = [x.lower() for x in ctrlTypes.ScanType._NAMES_TO_VALUES.keys()]
ScanModes = [x.lower() for x in ctrlTypes.ScanMode._NAMES_TO_VALUES.keys()]
ScanSubTypes = [x.lower() for x in ctrlTypes.ScanSubType._NAMES_TO_VALUES.keys()]


# Checks if MAC address is in the right format XX:XX:XX:XX:XX:XX
# Returns the MAC address if successful or raises a click error if not
def validate_mac(ctx, param, mac) -> Optional[str]:
    mac = mac.lower()
    if MAC_RE.match(mac):
        return mac
    else:
        raise click.BadParameter(
            "Invalid MAC address {} format should be XX:XX:XX:XX:XX:XX".format(mac)
        )
    return None


# Checks if MAC is empty or valid
def validate_optional_mac(ctx, param, mac) -> Optional[str]:
    if mac:
        return validate_mac(ctx, param, mac)
    return mac


def get_bwgd(unix_offset):
    bwgd_idx = None
    bwgd_cmd = subprocess.Popen(
        [("r2d2 fw_get_params node | grep BWGD | egrep -o [0-9]+")],
        shell=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        universal_newlines=True,
    )
    result = bwgd_cmd.stdout.readlines()
    if result == []:
        error = bwgd_cmd.stderr.readlines()
        print("Error getting time with `r2d2 fw_get_params node`:", error)
    else:
        current_bwgd = float(result[0].rstrip("\n"))
        print("Current BWGD:", str(int(current_bwgd)))
        bwgd_idx = current_bwgd + unix_offset * 1000 // 25.6
    bwgd_cmd.stdout.close()
    bwgd_cmd.stderr.close()
    return bwgd_idx


class CliOptions:
    """ Object for holding CLI state information """

    def __init__(self, debug, driver_if_host, driver_if_pair_port, timeout, radio_mac):
        self.debug = debug
        self.driver_if_host = driver_if_host
        self.driver_if_pair_port = driver_if_pair_port
        self.timeout = timeout
        self.radio_mac = radio_mac


class NodeInitCli:
    @click.command()
    @click.option(
        "--config_file",
        "-f",
        default="/data/cfg/node_config.json",
        type=str,
        help="node config file",
    )
    @click.pass_obj
    def node_init(cli_opts, config_file):
        """ Initialize the node. At the end of this process all of the config
        parameters are set and the node is up in responder mode and looking for
        an initiator (if the node is given an r2d2 assoc command it will switch
        from responder to initiator). A node config file can be passed with
        this command. For the list of available config file parameters with
        correct format please refer to meta-terragraph/src/terragraph-e2e/
        e2e/config/config_metadata.json. The node_config file can be
        found on the image at /data/cfg/node_config.json"""
        cmds.NodeInitCmd(cli_opts, config_file).run()


class LinkAssocCli:
    @click.command()
    @click.option(
        "--config_file",
        "-f",
        default="/data/cfg/node_config.json",
        type=str,
        help="node config file",
    )
    @click.option(
        "--responder_mac",
        "-m",
        type=str,
        required=True,
        callback=validate_mac,
        help="responder node MAC address",
    )
    @click.option(
        "--backwards_compatible/--not-backwards_compatible",
        default=True,
        help="enable/disable backward compatibility patch",
    )
    @click.pass_obj
    def assoc(cli_opts, config_file, responder_mac, backwards_compatible):
        """Bring a link up. This command returns the interface on which assoc
        happened. A node config file can be passed with this command. For the
        list of available config file parameters with correct format please
        refer to meta-terragraph/src/terragraph-e2e/e2e/config/
        config_metadata.json. The node_config file can be found on the image at
        /data/cfg/node_config.json """
        cmds.LinkAssocCmd(
            cli_opts, config_file, responder_mac, backwards_compatible
        ).run()


class LinkDissocCli:
    @click.command()
    @click.option(
        "--responder_mac",
        "-m",
        type=str,
        required=True,
        callback=validate_mac,
        help="responder node MAC address",
    )
    @click.pass_obj
    def dissoc(cli_opts, responder_mac):
        """Bring a link down"""
        cmds.LinkDissocCmd(cli_opts, responder_mac).run()


class AirtimeAllocationCli:
    @click.command()
    @click.option(
        "--airtime_alloc_file",
        "-f",
        default="",
        type=click.Path(exists=True),
        required=True,
        help="dynamic airtime allocation configuration file",
    )
    @click.pass_obj
    def airtime_alloc(cli_opts, airtime_alloc_file):
        """ Allocate airtime in dynamic manner. Requires a dynamic airtime
        allocation configuration file. An example airtime allocation file can
        be found at meta-terragraph/src/terragraph-e2e/e2e/config/
        airtimes_r2d2_1dn_4cn.json or on the image at /etc/e2e_config/
        airtimes_r2d2_1dn_4cn.json. The airtime allocation in the file is a
        percentage scaled to 10000 """
        cmds.AirtimeAllocateCmd(cli_opts, airtime_alloc_file).run()


class FwStatsCli:
    @click.command()
    @click.option(
        "--driver_if_pub_port",
        "-d",
        default=18990,
        type=int,
        help="driver-if pub port to connect to (default=18990)",
    )
    @click.option(
        "--radio_mac",
        "-m",
        default="",
        type=str,
        callback=validate_optional_mac,
        help="filter stats by the provided radio MAC address",
    )
    @click.option(
        "--poll_time", "-t", type=int, help="exit after the given number of seconds"
    )
    @click.pass_obj
    def fw_stats(cli_opts, driver_if_pub_port, radio_mac, poll_time):
        """Dump Fw stats"""
        cmds.NodeFwStatsCmd(cli_opts, driver_if_pub_port, radio_mac, poll_time).run()


class FwStatsConfigCli:
    @click.command()
    @click.option(
        "--config_file", "-f", default="", type=str, help="firmware stats config file"
    )
    @click.pass_obj
    def fw_stats_config(cli_opts, config_file):
        """ Configure which firmware stats are dumped in r2d2 fw_stats command.
        A config file can be passed with this command to select the set of
        stats to be enabled. An example file may be found in the repo at
        meta-terragraph/src/terragraph-e2e/e2e/config/fw_stats_cfg.json
        or on the image at /etc/e2e_config/fw_stats_cfg.json. Each radio
        interface can have a different set of stats enabled.
        In addition to the stats listed in the example config file, vendors
        can add their own stats to be enabled or disabled. """
        cmds.FwStatsConfigCmd(cli_opts, config_file).run()


class FwSetGolayParamsCli:
    @click.command()
    @click.option(
        "--unix_offset",
        type=int,
        help="Offset in seconds from current time for bwgd start index",
        default=5,
    )
    @click.option(
        "--peers",
        "-p",
        type=str,
        multiple=True,
        help="Peer mac address for the cmd. "
        "May be repeated for if there are multiple peers",
    )
    @click.option(
        "--ip_addrs",
        "-ip",
        type=str,
        multiple=True,
        help="ipv6 address for other nodes to run the cmd."
        "May be repeated for if there are multiple peers",
    )
    @click.option(
        "--tx_golay_idx",
        "-tx",
        type=int,
        help="Value to set for the tx Golay index",
        required=True,
    )
    @click.option(
        "--rx_golay_idx",
        "-rx",
        type=int,
        help="Value to set for the rx Golay index",
        required=True,
    )
    @click.pass_obj
    def fw_set_golay(
        cli_opts, unix_offset, peers, ip_addrs, tx_golay_idx, rx_golay_idx
    ):
        """Requires `ssh -A` to forward ssh credentials from your devserver"""
        if (len(peers) - len(ip_addrs)) != 1:
            print(
                "Error: Incorrect number of peers and ip_addrs. Expects 1 "
                "more peer mac than ip addr"
            )
            exit(0)

        bwgd_idx = get_bwgd(unix_offset)
        fw_set_cmds = []

        parameters = ["txGolayIdx", str(tx_golay_idx), "rxGolayIdx", str(rx_golay_idx)]
        responder_parameters = [
            "txGolayIdx",
            str(rx_golay_idx),
            "rxGolayIdx",
            str(tx_golay_idx),
        ]

        print("Starting cmds")
        for idx, peer in enumerate(peers):
            cmd = []
            if idx > 0:
                cmd = ["ssh", "root@" + ip_addrs[idx - 1]]

            cmd.extend(["r2d2", "fw_set_params", "-b", str(int(bwgd_idx)), "-m", peer])

            if idx > 0:
                cmd.extend(responder_parameters)
                print("Starting command on node at ip", ip_addrs[idx - 1])
            else:
                cmd.extend(parameters)
                print("Starting command on this node")

            fw_set_cmds.append(
                subprocess.Popen(
                    cmd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    universal_newlines=True,
                )
            )

        for fw_set_cmd in fw_set_cmds:
            print(fw_set_cmd.stdout.read())
            print(fw_set_cmd.stderr.read())
            fw_set_cmd.stdout.close()
            fw_set_cmd.stderr.close()


class FwSetParamsCli:
    @click.command()
    @click.argument("parameters", nargs=-1, metavar="[paramName] [paramValue] ..")
    @click.option(
        "--responder_mac",
        "-m",
        type=str,
        default=cmds.EMPTY_MAC_ADDRESS,
        callback=validate_mac,
        help="responder node MAC address ,required for link \
                  specific parameters",
    )
    @click.option(
        "--bwgdIdx",
        "-b",
        type=int,
        help="BWGD index of execution start."
        + " If not set the command will be executed immediately",
    )
    @click.option(
        "--show_list",
        "-s",
        is_flag=True,
        is_eager=True,
        help="Show list of parameters that can be set",
    )
    @click.pass_obj
    def fw_set_params(cli_opts, parameters, responder_mac, bwgdidx, show_list):
        """ Setting runtime firmware params """
        cmds.FwSetParamsCmd(
            cli_opts, parameters, responder_mac, bwgdidx, show_list
        ).run()


class FwGetParamsCli:
    @click.group()
    def fw_get_params():
        """ Getting runtime firmware parameters """
        pass

    _node_params = [
        "nodeFwCfg",
        # example_1 , example_2 , etc..
    ]

    _link_params = [
        "linkFwCfg",
        # 'example_1', 'example_2'
    ]

    @click.command()
    @click.option(
        "--param_type",
        "-t",
        type=click.Choice(_node_params),
        default="nodeFwCfg",
        required=True,
        help="Type of Node parameters to get",
    )
    @click.pass_obj
    def _node(cli_opts, param_type):
        """ Get FW parameters associated with the node. """
        cmds.FwGetParamsCmd(
            cli_opts, param_type, cmds.EMPTY_MAC_ADDRESS
        ).getNodeParams()

    @click.command()
    @click.option(
        "--param_type",
        "-t",
        type=click.Choice(_link_params),
        default="linkFwCfg",
        required=True,
        help="Type of Link parameters to get",
    )
    @click.option(
        "--responder_mac",
        "-m",
        type=str,
        required=True,
        callback=validate_mac,
        help="Responder node MAC address",
    )
    @click.pass_obj
    def _link(cli_opts, param_type, responder_mac):
        """ Get current setting of FW Link parameters """
        cmds.FwGetParamsCmd(cli_opts, param_type, responder_mac).getLinkParams()

    fw_get_params.add_command(_node, name="node")
    fw_get_params.add_command(_link, name="link")


class PhyLAConfigCli:
    @click.command()
    @click.option(
        "--config_file", "-f", default="", type=str, help="PHY LA Config file"
    )
    @click.option(
        "--responder_mac",
        "-m",
        type=str,
        required=True,
        callback=validate_mac,
        help="responder node MAC address",
    )
    @click.pass_obj
    def phyla_config(cli_opts, config_file, responder_mac):
        """ Configure PHY Link Adaptation at run time. A config file can be
        passed with this command. An example comfig file can be found at
        meta-terragraph/src/terragraph-e2e/e2e/config/fw_phyla_cfg.json
        or in the image at /etc/e2e_config/fw_phyla_cfg.json """
        cmds.PhyLAConfigCmd(cli_opts, config_file, responder_mac).run()


class PhyAgcConfigCli:
    @click.command()
    @click.option(
        "--config_file", "-f", default="", type=str, help="PHY AGC Config file"
    )
    @click.option(
        "--responder_mac",
        "-m",
        type=str,
        required=True,
        callback=validate_mac,
        help="responder node MAC address",
    )
    @click.pass_obj
    def phyagc_config(cli_opts, config_file, responder_mac):
        """ Configure PHY max AGC tracking. A config file can be passed with
        this command. An example config file can be found here: meta-terragraph/
        src/terragraph-e2e/e2e/config/fw_phyagc_cfg.json or on the image
        at /etc/e2e_config/fw_phyagc_cfg.json. Note: The node parameters are
        for all links on a node the link parameters are per link.
        The last config file programmed will be the node parameters used. """
        cmds.PhyAgcConfigCmd(cli_opts, config_file, responder_mac).run()


class PhyTpcConfigCli:
    @click.command()
    @click.option(
        "--config_file", "-f", default="", type=str, help="PHY TPC Config file"
    )
    @click.option(
        "--responder_mac",
        "-m",
        type=str,
        required=True,
        callback=validate_mac,
        help="responder node MAC address",
    )
    @click.pass_obj
    def phytpc_config(cli_opts, config_file, responder_mac):
        """ Configure PHY transmit power control. A config file can be passed
        with this command. An example file can be found at meta-terragraph/
        src/terragraph-e2e/e2e/config/fw_phytpc_cfg.json or on the
        image at /etc/e2e_config/fw_phytpc_cfg.json. All radios attached to the
        same primary use a common file."""
        cmds.PhyTpcConfigCmd(cli_opts, config_file, responder_mac).run()


class PhyTpcAdjTblConfigCli:
    @click.command()
    @click.option(
        "--config_file",
        "-f",
        default="",
        type=str,
        help="PHY TxPower Adjustment Table Config",
    )
    @click.pass_obj
    def phy_tpc_adj_tbl_config(cli_opts, config_file):
        """ Configure PHY TxPower Adjustment Table """
        cmds.PhyTpcAdjustmentTblConfigCmd(cli_opts, config_file).run()


class PhyAntWgtCodeBookConfigCli:
    @click.command()
    @click.option(
        "--config_file",
        "-f",
        default="",
        type=str,
        help="PHY Antenna Weight Table Config",
    )
    @click.pass_obj
    def phy_ant_wgt_code_book_config(cli_opts, config_file):
        """ Configure PHY Antenna CodeBook """
        cmds.PhyAntWgtCodeBookConfigCmd(cli_opts, config_file).run()


class PhyGolaySequenceConfigCli:
    @click.command()
    @click.option(
        "--config_file", "-f", default="", type=str, help="PHY Golay Sequences Config"
    )
    @click.pass_obj
    def phy_golay_sequence_config(cli_opts, config_file):
        """ Configure PHY Golay Sequences """
        cmds.PhyGolaySequenceConfigCmd(cli_opts, config_file).run()


class GetGpsPosCli:
    @click.command()
    @click.pass_obj
    def get_gps_pos(cli_opts):
        """ Get position of node from GPS """
        cmds.GetGpsPosCmd(cli_opts).run()


class SetGpsPosCli:
    @click.command()
    @click.option("--latitude", "-t", default=37.4847215, type=float, help="Latitude")
    @click.option(
        "--longitude", "-g", default=-122.1472362, type=float, help="Longitude"
    )
    @click.option("--height", "-h", default=17.92, type=float, help="Height in meters")
    @click.option("--accuracy", "-e", default=50, type=float, help="Accuracy in meters")
    @click.pass_obj
    def set_gps_pos(cli_opts, latitude, longitude, height, accuracy):
        """ Set position of node for GPS single satellite mode, default=MPK """
        cmds.SetGpsPosCmd(cli_opts, latitude, longitude, height, accuracy).run()


class GpsEnableCli:
    @click.command()
    @click.pass_obj
    def gps_enable(cli_opts):
        """ Enable GPS """
        cmds.GpsEnableCmd(cli_opts).run()


class GpsSendTimeCli:
    @click.command()
    @click.argument("gps_time")
    @click.pass_obj
    def gps_send_time(cli_opts, gps_time):
        """ Set current GPS time """
        cmds.GpsSendTimeCmd(cli_opts, gps_time).run()


class PolarityConfigCli:
    @click.command()
    @click.option(
        "--polarity",
        "-p",
        default=255,
        type=int,
        help="Node polarity (1: ODD; 2: EVEN; 3: HYBRID_ODD; 4:HYBRID_EVEN)",
    )
    @click.pass_obj
    def polarity_config(cli_opts, polarity):
        """Configure node polarity. This must be done before any assoc commands.
        This polarity is applied to all links that will be established."""
        cmds.PolarityConfigCmd(cli_opts, polarity).run()


class GolayConfigCli:
    @click.command()
    @click.option(
        "--tx_index", "-t", default=255, type=int, help="Tx Golay code index (0 to 7)"
    )
    @click.option(
        "--rx_index", "-r", default=255, type=int, help="Rx Golay code index (0 to 7)"
    )
    @click.pass_obj
    def golay_config(cli_opts, tx_index, rx_index):
        """Configure Golay code indices. This has to be run before assoc
        commands are run. This command must only be run on the initiator node."""
        cmds.GolayConfigCmd(cli_opts, tx_index, rx_index).run()


class BfSlotExclusionReqCli:
    @click.command()
    @click.option(
        "--bwgdIdx", "-b", type=int, help="BWGD index of slot exclusion start"
    )
    @click.pass_obj
    def bf_slot_exclusion_req(cli_opts, bwgdidx):
        """Configure Bf Slot Exclusion. This used to make sure that scans and
        BF don't collide. Not available yet. """
        cmds.BfSlotExclusionReqCmd(cli_opts, bwgdidx).run()


class DebugCli:
    @click.command()
    @click.option("--command", "-c", type=str, help="Debug Command")
    @click.option("--value", "-v", type=int, help="Command value")
    @click.pass_obj
    def debug(cli_opts, command, value):
        """ Send debug command to firmware. Takes in the string specified by
        the -c option and the value specified by the -v option and prints out
        a debug cmd message in kern log. """
        cmds.DebugCmd(cli_opts, command, value).run()


class SynchronizedScanCli:
    @click.command()
    @click.option(
        "--unix_offset",
        type=int,
        help="Offset in seconds from current time for bwgd start index",
        default=5,
    )
    @click.option(
        "--token",
        type=int,
        help="Token to associate response with request (default=random)",
    )
    @click.option(
        "--scan_type",
        "-t",
        type=click.Choice(ScanTypes),
        help="Scan type (enum defined in Controller.thrift)",
        required=True,
    )
    @click.option(
        "--scan_mode",
        "-m",
        type=click.Choice(ScanModes),
        help="Scan mode (enum defined in Controller.thrift)",
        required=True,
    )
    @click.option(
        "--bf_scan_invert_polarity/--no-bf_scan_invert_polarity",
        "-bf",
        help="Invert polarity if both nodes have the same polarity, default=false",
        default=False,
    )
    @click.option(
        "--beams",
        "-b",
        type=int,
        multiple=True,
        help="If not set, use default beam indices. If set, must "
        "be repeated n+1 times, where n is the number of "
        "--rx nodes. First value is tx indices for tx node, "
        "the rest are rx indices for rx nodes. Each value is "
        "formatted as start_idx-end_idx, to use "
        "[start_idx, end_idx], inclusive."
        "For relative PBF end_idx is used as the one-sided codebook range to "
        "use relative to the current beam, e.g. specifying `-b 0 -b 5` means "
        "sweep current codebook beam +/- 5.",
    )
    @click.option(
        "--apply/--no-apply",
        help="Apply new beam after scan procedure if new beams are selected, "
        "default=True)",
        default=True,
    )
    @click.option(
        "--sub_type",
        type=click.Choice(ScanSubTypes),
        help="Scan subtype, default=no_cal (enum defined in Controller.thrift)",
        default="no_cal",
    )
    @click.option(
        "--bwgd_len",
        type=int,
        help="BWGD duration for which scan is active, default=64",
        default=64,
    )
    @click.option(
        "--tx_power_index",
        type=int,
        help="Tx Power Index used during scanning, default=16",
        default=16,
    )
    @click.option(
        "--peers",
        "-p",
        type=str,
        multiple=True,
        help="Peer mac address for the scan. "
        "May be repeated if there are multiple peers",
    )
    @click.option(
        "--ip_addrs",
        "-ip",
        type=str,
        multiple=True,
        help="Peer ipv6 address for the scan."
        "May be repeated if there are multiple peers",
    )
    # Use choice option instead of boolean flag for backward compatibility
    @click.option(
        "--is_initiator",
        type=click.Choice(["True", "False"]),
        help="Is node initiator or not",
        default="False",
    )
    @click.option("--null_angle", type=int, help="Null angle for CBF", default=0)
    @click.option("--cbf_beam_idx", type=int, help="Beam index for CBF", default=0)
    @click.option(
        "--aggressor/--no-aggressor", help="Is aggressor for CBF", default=False
    )
    @click.pass_obj
    def sync_scan(
        cli_opts,
        unix_offset,
        token,
        scan_type,
        scan_mode,
        peers,
        ip_addrs,
        beams,
        is_initiator,
        **kwargs
    ):
        """Command to execute bwgd synchronized r2d2 scan from a node. This command
        requires `ssh -A` when you ssh into this node to forward ssh credentials
        from your devserver. The node on which this command is run is assumed
        to be tx for the scan."""
        if token is None:
            token = randrange(2 ** 31)
        if (
            ctrlTypes.ScanType._NAMES_TO_VALUES[scan_type.upper()]
            == ctrlTypes.ScanType.IM
        ):
            if len(peers) != 1:
                print("Error: For IM scan, only 1 peer mac address is expected")
                exit(0)
            if len(ip_addrs) < 1:
                print("Error: For IM scan, at least 1 peer ip address is expected")
                exit(0)
            peers = [BROADCAST_MAC] + [peers[0]] * len(ip_addrs)

        if (len(peers) - len(ip_addrs)) != 1:
            print(
                "Error: Incorrect number of peers and ip_addrs. Expects 1 "
                "more peer mac than ip addr"
            )
            exit(0)

        if (
            ctrlTypes.ScanMode._NAMES_TO_VALUES[scan_mode.upper()]
            == ctrlTypes.ScanMode.SELECTIVE
            and not beams
        ):
            print("Error: Selective scan mode requires beam indices")
            exit(0)

        if is_initiator == "True":
            self_tx_rx = "--tx"
            peer_tx_rx = "--rx"
        else:
            self_tx_rx = "--rx"
            peer_tx_rx = "--tx"

        bwgd_idx = get_bwgd(unix_offset)
        scan_cmds = []
        print("Starting scans")
        for idx, peer in enumerate(peers):
            cmd = []
            if idx > 0:
                cmd = ["ssh", "root@" + ip_addrs[idx - 1]]

            cmd.extend(
                [
                    "r2d2",
                    "scan",
                    "--bwgd_idx",
                    str(int(bwgd_idx)),
                    "--peer",
                    peer,
                    "--scan_type",
                    scan_type,
                    "--scan_mode",
                    scan_mode,
                    "--token",
                    str(token),
                ]
            )

            if beams:
                cmd.append("--beams")
                cmd.append(str(beams[0]))
                cmd.append("--beams")
                cmd.append(str(beams[1]))

            for (name, value) in kwargs.items():
                if isinstance(value, bool):
                    cmd.append("--" + name if value else "--no-" + name)
                else:
                    cmd.append("--" + name)
                    cmd.append(str(value))

            if idx == 0:
                cmd.append(self_tx_rx)
            else:
                cmd.append(peer_tx_rx)

            if idx > 0:
                print("Starting command on node at ip", ip_addrs[idx - 1])
            else:
                print("Starting command on this node")

            scan_cmds.append(
                subprocess.Popen(
                    cmd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    universal_newlines=True,
                )
            )

        for scan_cmd in scan_cmds:
            print(scan_cmd.stdout.read())
            print(scan_cmd.stderr.read())
            scan_cmd.stdout.close()
            scan_cmd.stderr.close()


class ScanCli:
    @click.command()
    @click.option(
        "--token",
        type=int,
        help="Token to associate response with request (default=random)",
    )
    @click.option(
        "--scan_type",
        "-t",
        type=click.Choice(ScanTypes),
        help="Scan type (enum defined in Controller.thrift)",
        required=True,
    )
    @click.option(
        "--scan_mode",
        "-m",
        default="fine",
        type=click.Choice(ScanModes),
        help="Scan mode (enum defined in Controller.thrift)",
    )
    @click.option(
        "--bwgd_idx", default=0, type=int, help="BWGD index of scan start (default=0)"
    )
    @click.option(
        "--tx/--rx", help="Device is transmitter or receiver in scan", required=True
    )
    @click.option(
        "--bf_scan_invert_polarity/--no-bf_scan_invert_polarity",
        "-bf",
        help="Invert polarity if both nodes have the same polarity, default=false",
        default=False,
    )
    @click.option(
        "--beams",
        "-b",
        type=int,
        multiple=True,
        help="If not set, use default beam indices. If set, must "
        "be repeated n+1 times, where n is the number of "
        "--rx nodes. First value is tx indices for tx node, "
        "the rest are rx indices for rx nodes. Each value is "
        "formatted as start_idx-end_idx, to use "
        "[start_idx, end_idx], inclusive."
        "For relative PBF end_idx is used as the one-sided codebook range to "
        "use relative to the current beam, e.g. specifying `-b 0 -b 5` means "
        "sweep current codebook beam +/- 5.",
    )
    @click.option(
        "--apply/--no-apply",
        help="Apply new beam after scan procedure if new beams are selected, default=apply)",
        default=True,
    )
    @click.option(
        "--sub_type",
        type=click.Choice(ScanSubTypes),
        help="Scan subtype, default=no_cal (enum defined in Controller.thrift)",
        default="no_cal",
    )
    @click.option(
        "--bwgd_len",
        type=int,
        help="BWGD duration for which scan is active, default=64",
        default=64,
    )
    @click.option(
        "--tx_power_index",
        type=int,
        help="Tx Power Index used during scanning, default is average current power",
        default=16,
    )
    @click.option(
        "--peer",
        "-p",
        type=str,
        help="Peer mac address for the scan.",
    )
    @click.option("--null_angle", type=int, help="Null angle for CBF")
    @click.option("--cbf_beam_idx", type=int, help="Beam index for CBF")
    @click.option(
        "--aggressor/--no-aggressor", help="Is aggressor for CBF", default=False
    )
    @click.pass_obj
    def scan(
        cli_opts,
        token,
        scan_type,
        scan_mode,
        bwgd_idx,
        tx,
        bf_scan_invert_polarity,
        beams,
        apply,
        sub_type,
        bwgd_len,
        tx_power_index,
        peer,
        null_angle,
        cbf_beam_idx,
        aggressor,
    ):
        """Start scan. Note: Running scans requires coordination
        between devices. There is a device that transmits and one or more
        devices that receive. The r2d2 sync_scan command can start scans in a
        coordinated manner."""
        if token is None:
            token = randrange(2 ** 31)
        cmds.ScanCmd(
            cli_opts,
            token,
            scan_type,
            scan_mode,
            bwgd_idx,
            tx,
            bf_scan_invert_polarity,
            beams,
            apply,
            sub_type,
            bwgd_len,
            tx_power_index,
            peer,
            null_angle,
            cbf_beam_idx,
            aggressor,
        ).run()


class ChannelConfigCli:
    @click.command()
    @click.option("--channel", "-c", type=int, default=2, help="network channel (1-4)")
    @click.pass_obj
    def channel_config(cli_opts, channel):
        """ Send channel configuration to firmware. Default channel is 2
        (if no -c input) """
        cmds.ChannelConfigCmd(cli_opts, channel).run()


class BfRespScanCli:
    @click.command()
    @click.option("--cfg", "-c", type=bool, help="Enable/disable bf responder scan")
    @click.pass_obj
    def bf_resp_scan_config(cli_opts, cfg):
        """ Enable/Disable Bf responder scan mode. Used when the node has
        other links and allows the node to do other associations from
        Synchronous mode Initial Beam Forming."""
        cmds.BfRespScanConfigCmd(cli_opts, cfg).run()


class SynchronizedBfSlotExclusionReqCli:
    @click.command()
    @click.option(
        "--unix_offset",
        type=int,
        help="Offset in seconds from current time for bwgd start index",
        default=5,
    )
    @click.option(
        "--peers",
        "-p",
        type=str,
        multiple=True,
        help="Peer mac address for the scan. "
        "May be repeated if there are multiple peers",
    )
    @click.option(
        "--ip_addrs",
        "-ip",
        type=str,
        multiple=True,
        help="Peer ipv6 address for the scan."
        "May be repeated if there are multiple peers",
    )
    @click.pass_obj
    def sync_bf_slot_exclusion_req(cli_opts, unix_offset, peers, ip_addrs, **kwargs):
        """Command to execute bwgd synchronized r2d2 bf slot exclusion from a node. This command
        requires `ssh -A` when you ssh into this node to forward ssh credentials
        from your devserver. The node on which this command is run is assumed
        to be tx for the scan."""
        if (len(peers) - len(ip_addrs)) != 1:
            print(
                "Error: Incorrect number of peers and ip_addrs. Expects 1 "
                "more peer mac than ip addr"
            )
            exit(0)

        bwgd_idx = get_bwgd(unix_offset)
        bf_slot_exclusion_req_cmds = []
        print("Starting Slot Exclusion")
        for idx, _peer in enumerate(peers):
            cmd = []
            if idx > 0:
                cmd = ["ssh", "root@" + ip_addrs[idx - 1]]

            cmd.extend(
                ["r2d2", "bf_slot_exclusion_req", "--bwgdIdx", str(int(bwgd_idx))]
            )

            for (name, value) in kwargs.items():
                if isinstance(value, bool):
                    cmd.append("--" + name if value else "--no-" + name)
                else:
                    cmd.append("--" + name)
                    cmd.append(str(value))

            if idx > 0:
                print("Starting command on node at ip", ip_addrs[idx - 1])
            else:
                print("Starting command on this node")

            bf_slot_exclusion_req_cmds.append(
                subprocess.Popen(
                    cmd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    universal_newlines=True,
                )
            )

        for cmd in bf_slot_exclusion_req_cmds:
            print(cmd.stdout.read())
            print(cmd.stderr.read())
            cmd.stdout.close()
            cmd.stderr.close()


class FwSetLogConfigCli:
    @click.command()
    @click.option(
        "--module",
        "-m",
        type=str,
        multiple=True,
        help="Module name."
        " Can be used more than once for multiple modules."
        " To use all modules skip this option",
    )
    @click.option("--level", "-l", default="", type=str, help="logging level")
    @click.option(
        "--show_list",
        "-s",
        is_flag=True,
        help="Show list of all the modules and levels",
    )
    @click.pass_obj
    def fw_set_log_config(cli_opts, module, level, show_list):
        """ Set firmware verbosity logging level for specified modules. Each
        vendor needs to define the options to this command. These options
        including modules, and levels should be defined in the --help message. """
        cmds.FwSetLogConfigCmd(cli_opts, module, level, show_list).run()


# -- r2d2 Command Group -- #
@click.group()
@click.option("-d", "--debug", is_flag=True, help="Turn on debug logging")
@click.option(
    "--driver_if_host",
    "-c",
    default="localhost",
    type=str,
    help="driver-if IP to connect to (default=localhost)",
)
@click.option(
    "--driver_if_ip",
    default="",
    type=str,
    help="[DEPRECATED] driver-if IP to connect to",
)
@click.option(
    "--driver_if_pair_port",
    "-p",
    default=17989,
    type=int,
    help="""driver-if pair port to connect to
                                  (default=17989)""",
)
@click.option(
    "--timeout",
    "-t",
    default=30,
    type=int,
    help="timeout (in seconds) to expire the command (default=30)",
)
@click.option(
    "--radio_mac",
    default="",
    type=str,
    callback=validate_optional_mac,
    help="radio MAC address (for some commands with multi-radio DN)",
)
@click.pass_context
def r2d2(
    ctx,
    debug,
    driver_if_host,
    driver_if_ip,
    driver_if_pair_port,
    timeout,
    radio_mac,
):
    """ CLI to talk to driverIf daemon """
    log_level = logging.DEBUG if debug else logging.INFO
    logging.basicConfig(
        format="[%(asctime)s] %(levelname)s: %(message)s (%(filename)s:%(lineno)d)",
        level=log_level,
    )
    if driver_if_ip:
        driver_if_host = "[{}]".format(driver_if_ip)
    ctx.obj = CliOptions(debug, driver_if_host, driver_if_pair_port, timeout, radio_mac)


r2d2.add_command(NodeInitCli().node_init)
r2d2.add_command(LinkAssocCli().assoc)
r2d2.add_command(LinkDissocCli().dissoc)
r2d2.add_command(AirtimeAllocationCli().airtime_alloc)
r2d2.add_command(FwStatsCli().fw_stats)
r2d2.add_command(FwStatsConfigCli().fw_stats_config)
r2d2.add_command(FwSetParamsCli().fw_set_params)
r2d2.add_command(FwSetGolayParamsCli().fw_set_golay)
r2d2.add_command(FwGetParamsCli().fw_get_params)
r2d2.add_command(PhyLAConfigCli().phyla_config)
r2d2.add_command(PhyAgcConfigCli().phyagc_config)
r2d2.add_command(PhyTpcConfigCli().phytpc_config)
r2d2.add_command(PhyTpcAdjTblConfigCli().phy_tpc_adj_tbl_config)
r2d2.add_command(GetGpsPosCli().get_gps_pos)
r2d2.add_command(SetGpsPosCli().set_gps_pos)
r2d2.add_command(GpsEnableCli().gps_enable)
r2d2.add_command(GpsSendTimeCli().gps_send_time)
r2d2.add_command(PolarityConfigCli().polarity_config)
r2d2.add_command(GolayConfigCli().golay_config)
r2d2.add_command(PhyAntWgtCodeBookConfigCli().phy_ant_wgt_code_book_config)
r2d2.add_command(DebugCli.debug)
r2d2.add_command(ScanCli().scan)
r2d2.add_command(SynchronizedScanCli().sync_scan)
r2d2.add_command(BfSlotExclusionReqCli().bf_slot_exclusion_req)
r2d2.add_command(SynchronizedBfSlotExclusionReqCli().sync_bf_slot_exclusion_req)
r2d2.add_command(PhyGolaySequenceConfigCli().phy_golay_sequence_config)
r2d2.add_command(ChannelConfigCli.channel_config)
r2d2.add_command(BfRespScanCli.bf_resp_scan_config)
r2d2.add_command(help.help)
r2d2.add_command(FwSetLogConfigCli().fw_set_log_config)

if __name__ == "__main__":
    r2d2()
