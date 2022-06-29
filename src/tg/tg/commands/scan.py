#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import collections
import datetime
import json
import logging
import re
import time

import click
import tabulate
from dateutil.parser import parse
from terragraph_thrift.Controller import ttypes as ctrlTypes
from tg.commands import base, consts


_log = logging.getLogger(__name__)

TimeFmts = ["local", "unix", "gps", "bwgd", "sf"]


class ScanCli:

    ScanTypes = [x.lower() for x in ctrlTypes.ScanType._NAMES_TO_VALUES.keys()]
    ScanModes = [x.lower() for x in ctrlTypes.ScanMode._NAMES_TO_VALUES.keys()]
    ScanSubTypes = [x.lower() for x in ctrlTypes.ScanSubType._NAMES_TO_VALUES.keys()]

    def __init__(self):

        self.scan.add_command(self._start, name="start")
        self.scan.add_command(self._status, name="status")
        self.scan.add_command(self._reset, name="reset")
        self.scan.add_command(self._schedule, name="schedule")
        self.scan.add_command(self._slotmapconfig, name="slotmapconfig")
        self.scan.add_command(self._timeconv, name="timeconv")
        self.scan.add_command(self._cbfConfig, name="cbfconfig")
        self._cbfConfig.add_command(self._cbfConfigGet, name="get")
        self._cbfConfig.add_command(self._cbfConfigSet, name="set")
        self._cbfConfig.add_command(self._cbfConfigReset, name="reset")
        self.scan.add_command(self._rfState, name="rfstate")
        self._rfState.add_command(self._rfStateGet, name="get")
        self._rfState.add_command(self._rfStateSet, name="set")
        self._rfState.add_command(self._rfStateReset, name="reset")
        self._rfState.add_command(self._laTpcParamsSet, name="set_latpc_params")

    @click.group()
    @click.pass_obj
    def scan(cli_opts):
        """ Start/query/reset scans """
        pass

    @click.command()
    @click.option(
        "-t",
        "--scan_type",
        required=True,
        type=click.Choice(ScanTypes),
        help="Type of scan",
    )
    @click.option(
        "--tx", help="TX node MAC/name for scan. If missing, run scan on whole network"
    )
    @click.option(
        "--rx",
        multiple=True,
        help="RX node MAC/name for scan. Specify multiple --rx for "
        "IM scan. If missing, run scan on whole network",
    )
    @click.option(
        "--scan_mode",
        "-m",
        default="fine",
        type=click.Choice(ScanModes),
        help="Scan mode (default=fine)",
    )
    @click.option(
        "--delay",
        "-d",
        type=int,
        default=5,
        help="Start scan after this many seconds (default=5)",
    )
    @click.option(
        "-b",
        "--beams",
        multiple=True,
        help="If not set, use default beam indices. If set, must "
        "be repeated n+1 times, where n is the number of "
        "--rx nodes. First value is tx indices for tx node, "
        "the rest are rx indices for rx nodes. Each value is "
        "formatted as start_idx-end_idx, to use "
        "[start_idx, end_idx], inclusive",
    )
    @click.option(
        "--apply",
        is_flag=True,
        default=None,
        help="Apply new beam after scan procedure if true (default no apply)",
    )
    @click.option(
        "--sub_type",
        type=click.Choice(ScanSubTypes),
        help="Runtime calibration type or Nulling type",
    )
    @click.option("--bwgd_len", type=int, help="Calibration length in BWGDs (2-64)")
    @click.option(
        "--tx_power_index",
        type=int,
        default=[255],
        multiple=True,
        help="Tx power index used during scanning, default is current average power",
    )
    @click.option("--vtx", multiple=True, help="Victim tx node (CBF)")
    @click.option("--vrx", multiple=True, help="Victim rx node (CBF)")
    @click.option("--atx", multiple=True, help="Aggressor tx node (CBF)")
    @click.option("--arx", multiple=True, help="Aggressor rx node (CBF)")
    @click.option("--null_angle", type=int, default=None, help="Nulling angle (CBF)")
    @click.option(
        "--beam_idx", type=int, default=None, help="Beam index (CBF), optional"
    )
    @click.option(
        "--set_config",
        is_flag=True,
        default=None,
        help="Set CBF config for a link instead of executing one-time scan",
    )
    @click.pass_obj
    def _start(cli_opts, **kwargs):
        """ Start interference measurement scan """
        ScanStartCmd(cli_opts).run(**kwargs)

    @click.command()
    @click.option(
        "--format",
        "-f",
        default="table",
        type=click.Choice(["table", "raw", "json"]),
        help="Result formatting (default=table)",
    )
    @click.option(
        "--concise/--no-concise",
        default=False,
        help="Only return metadata (scan id, node names, times)",
    )
    @click.option(
        "-t",
        "--tokens",
        default=None,
        help="If not set, output all. If set as "
        "start_token[-end_token], output "
        "[start_token, end_token], inclusive. If no end_token "
        "provided, only output start_token",
    )
    @click.pass_obj
    def _status(cli_opts, format, concise, tokens):
        """ Show status of interference measurement scan """
        ScanStatusCmd(cli_opts).run(format, concise, tokens)

    @click.command()
    @click.pass_obj
    def _reset(cli_opts):
        """ Reset status of interference measurement scan """
        ScanStatusResetCmd(cli_opts).run()

    @click.command()
    @click.option(
        "-t",
        "--timer",
        default=None,
        type=click.Choice(["im", "combined"]),
        help="Select which periodic scan timer to configure (default=None)",
    )
    @click.option(
        "-p",
        "--period",
        type=int,
        default=None,
        help="Scan period in seconds (-1 to disable, 0 for one-time"
        " scan [combined scans only])",
    )
    @click.option(
        "--pbf", is_flag=True, default=False, help="Enable PBF during combined scan"
    )
    @click.option(
        "--rtcal", is_flag=True, default=False, help="Enable RTCAL during combined scan"
    )
    @click.option(
        "--cbf", is_flag=True, default=False, help="Enable CBF during combined scan"
    )
    @click.option(
        "--im", is_flag=True, default=False, help="Enable fast IM after combined scan"
    )
    @click.pass_obj
    def _schedule(cli_opts, timer, period, pbf, rtcal, cbf, im):
        """ Get/set configuration for periodic scans.

        Two types of periodic scans can be enabled in the network and each type
        is configured independently:

        \b
        - Periodic combined PBF/RTCAL/CBF scans
        - Periodic IM scans

        For combined scans PBF, RTCAL, CBF, and fast IM scans can be selectively
        enabled using flags given when the combined scan period is configured.
        At least one scan type must be enabled when periodic combined scans are
        enabled and all types are disabled by default. """
        ScanScheduleCmd(cli_opts).run(timer, period, pbf, rtcal, cbf, im)

    @click.command()
    @click.option(
        "--config",
        default=None,
        help="Set slot map config. The format is a single string "
        "(as returned by this command without `--config`) "
        "in quotes",
    )
    @click.pass_obj
    def _slotmapconfig(cli_opts, config):
        """ Get/set slot map config """
        SlotMapConfigCmd(cli_opts).run(config)

    @click.command()
    @click.option(
        "--from",
        "-f",
        "from_",
        default=None,
        type=click.Choice(TimeFmts),
        help="Format to convert from. Defaults to autodetection",
    )
    @click.option(
        "--to",
        "-t",
        default=None,
        type=click.Choice(TimeFmts),
        help="Format to convert to. Defaults to all formats",
    )
    @click.argument("timestamps", nargs=-1)
    @click.pass_obj
    def _timeconv(cli_opts, from_, to, timestamps):
        """ Convert timestamps to different format """
        TimeConvCmd(cli_opts).run(from_, to, timestamps)

    @click.group()
    @click.pass_obj
    def _cbfConfig(cli_opts):
        """ Get/set/reset CBF config """
        pass

    @click.command()
    @click.pass_obj
    def _cbfConfigGet(cli_opts):
        """ Get CBF configuration for all links """
        CbfConfigGetCmd(cli_opts).run()

    @click.command()
    @click.pass_obj
    def _cbfConfigSet(cli_opts):
        """ Set CBF configuration for all links.

        CBF configuration is generated using RF state information from IM
        scan data and latest link state from PBF scans (if available)."""
        CbfConfigSetCmd(cli_opts).run()

    @click.command()
    @click.pass_obj
    def _cbfConfigReset(cli_opts):
        """ Reset CBF configuration for all links """
        CbfConfigResetCmd(cli_opts).run()

    @click.group()
    @click.pass_obj
    def _rfState(cli_opts):
        """ Get/set/reset RF state """
        pass

    @click.command()
    @click.pass_obj
    def _rfStateGet(cli_opts):
        """ Get RF state (internal use only) """
        RfStateGetCmd(cli_opts).run()

    @click.command()
    @click.option(
        "-f",
        "--rfstate_file",
        type=click.Path(exists=True),
        required=True,
        help="RF state or scan result json file",
    )
    @click.pass_obj
    def _rfStateSet(cli_opts, rfstate_file):
        """ Set RF state (internal use only) """
        RfStateSetCmd(cli_opts).run(rfstate_file)

    @click.command()
    @click.pass_obj
    def _rfStateReset(cli_opts):
        """ Reset RF state """
        RfStateResetCmd(cli_opts).run()

    @click.command()
    @click.pass_obj
    def _laTpcParamsSet(cli_opts):
        """ Set LA/TPC params from RF state """
        LaTpcParamsSetCmd(cli_opts).run()


class ScanStartCmd(base.BaseCmd):
    def run(
        self,
        scan_type,
        tx,
        rx,
        scan_mode,
        delay,
        beams,
        sub_type,
        bwgd_len,
        tx_power_index,
        vtx,
        vrx,
        atx,
        arx,
        null_angle,
        apply,
        beam_idx,
        set_config,
    ):
        startScan = ctrlTypes.StartScan()
        startScan.startTime = int(time.time()) + delay
        startScan.apply = apply

        if scan_type == "topo":
            if not tx:
                self._my_exit(False, "Topology scan requires --tx")
        elif (tx and not rx) or (not tx and rx):
            self._my_exit(
                False, "Either both or neither of --tx and --rx must be specified"
            )
        if tx:
            startScan.txNode = tx
        if rx:
            startScan.rxNodes = rx

        if scan_type == "rtcal":
            if sub_type is None and tx:
                self._my_exit(
                    False,
                    "sub_type needed for rtcal scan type "
                    "(unless whole network scan)",
                )

        if scan_type == "im" and scan_mode == "relative" and bwgd_len is None:
            bwgd_len = 4

        startScan.scanType = ctrlTypes.ScanType._NAMES_TO_VALUES[scan_type.upper()]
        startScan.scanMode = ctrlTypes.ScanMode._NAMES_TO_VALUES[scan_mode.upper()]

        if scan_type == "pbf" or scan_type == "im" or scan_type == "rtcal":
            if beams:
                if not tx:
                    self._my_exit(False, "--beams requires --tx and --rx")
                if len(beams) != 1 + len(rx):
                    self._my_exit(
                        False,
                        "There must be as many --beams as the sum of --tx and --rx",
                    )
                startScan.beams = []
                for b in beams:
                    match = re.match(r"(\d+)-(\d+)$", b)
                    if not match:
                        self._my_exit(False, "Bad --beams argument: %s" % b)
                    indices = ctrlTypes.BeamIndices()
                    try:
                        indices.low = int(match.group(1))
                        indices.high = int(match.group(2))
                    except ValueError:
                        self._my_exit(False, "Bad --beams argument, must be ints")
                    startScan.beams.append(indices)
            if not (bwgd_len is None or 2 <= bwgd_len <= 64):
                self._my_exit(False, "--bwgd_len must be in the range 2-64")
            if (tx_power_index[0] > 31 or tx_power_index[0] < 0) and tx_power_index[
                0
            ] != 255:
                self._my_exit(
                    False,
                    "tx_power_index is optional, "
                    "should be within limits of [0..31, 255]",
                )
            startScan.txPwrIndex = tx_power_index[0]
            if sub_type is not None:
                startScan.subType = ctrlTypes.ScanSubType._NAMES_TO_VALUES[
                    sub_type.upper()
                ]

            startScan.bwgdLen = bwgd_len
        elif scan_type == "cbf_rx" or scan_type == "cbf_tx":
            if scan_type == "cbf_rx":
                if (
                    len(vtx) != 1
                    or len(vrx) != 1
                    or len(atx) != len(arx)
                    or len(atx) == 0
                ):
                    self._my_exit(
                        False,
                        "--scan_type cbf_rx requires one of "
                        "--vtx, --vrx, and an equal number (>0) of "
                        "--atx, --arx",
                    )
                (startScan.mainTxNode, startScan.mainRxNode) = (vtx[0], vrx[0])
                (startScan.auxTxNodes, startScan.auxRxNodes) = (atx, arx)
            else:
                if (
                    len(atx) != 1
                    or len(arx) != 1
                    or len(vtx) != len(vrx)
                    or len(vtx) == 0
                ):
                    self._my_exit(
                        False,
                        "--scan_type cbf_tx requires one of "
                        "--atx, --arx, and an equal number (>0) of "
                        "--vtx, --vrx",
                    )
                (startScan.mainTxNode, startScan.mainRxNode) = (atx[0], arx[0])
                (startScan.auxTxNodes, startScan.auxRxNodes) = (vtx, vrx)
            if (len(tx_power_index) != 1 or tx_power_index[0] != 255) and len(
                tx_power_index
            ) != 1 + len(startScan.auxTxNodes):
                self._my_exit(
                    False,
                    "--tx_power_index is optional, but if "
                    "specified must be given for all nodes",
                )
            if null_angle is None:
                self._my_exit(False, "--null_angle is required for CBF")
            if not (bwgd_len is None or 4 <= bwgd_len <= 64):
                self._my_exit(False, "--bwgd_len must be in the range 4-64 for CBF")
            startScan.txPwrIndex = tx_power_index[0]
            if len(tx_power_index) > 1:
                startScan.auxTxPwrIndex = tx_power_index[1:]
            startScan.nullAngle = null_angle
            startScan.cbfBeamIdx = beam_idx
            startScan.bwgdLen = bwgd_len
            startScan.setConfig = set_config
        elif scan_type == "topo":
            if delay == 0:
                startScan.startTime = 0  # start immediately
            if (tx_power_index[0] > 31 or tx_power_index[0] < 0) and tx_power_index[
                0
            ] != 255:
                self._my_exit(
                    False,
                    "tx_power_index is optional, "
                    "should be within limits of [0..31, 255]",
                )
            startScan.txPwrIndex = tx_power_index[0]

        self._connect_to_controller(recv_timeout=10000)
        self._send_to_ctrl(
            ctrlTypes.MessageType.START_SCAN, startScan, consts.SCAN_APP_CTRL_ID
        )
        startScanResp = ctrlTypes.StartScanResp()
        self._recv_from_ctrl(
            ctrlTypes.MessageType.START_SCAN_RESP,
            startScanResp,
            consts.SCAN_APP_CTRL_ID,
        )
        self._my_exit(startScanResp.success, startScanResp.message)


def thrift_to_dict(obj):
    if isinstance(obj, list):
        return [thrift_to_dict(x) for x in obj]
    elif isinstance(obj, set):
        return [thrift_to_dict(x) for x in obj]
    elif isinstance(obj, dict):
        return {k: thrift_to_dict(v) for (k, v) in obj.items()}
    elif hasattr(obj, "thrift_spec"):
        # It's a thrift struct
        return {
            k: thrift_to_dict(v) for (k, v) in obj.__dict__.items() if v is not None
        }
    else:
        # Simple value (int, str, etc.)
        return obj


class ScanStatusCmd(base.BaseCmd):
    def run(self, format, concise, tokens):
        getScanStatus = ctrlTypes.GetScanStatus()
        getScanStatus.isConcise = concise
        if tokens is not None:
            # start_token[-end_token]
            match = re.match(r"(\d+)(-(\d+))?$", tokens)
            if not match:
                self._my_exit(False, "Bad --tokens argument")
            try:
                getScanStatus.tokenFrom = int(match.group(1))
                if match.group(3) is not None:
                    getScanStatus.tokenTo = int(match.group(3))
            except ValueError:
                self._my_exit(False, "Bad --tokens argument, must be integers")
        # Scan results are large and may take some time for the
        # controller to generate, use a larger timeout
        self._connect_to_controller(recv_timeout=10000)
        self._send_to_ctrl(
            ctrlTypes.MessageType.GET_SCAN_STATUS,
            getScanStatus,
            consts.SCAN_APP_CTRL_ID,
        )
        status = ctrlTypes.ScanStatus()
        self._recv_from_ctrl(
            ctrlTypes.MessageType.SCAN_STATUS, status, consts.SCAN_APP_CTRL_ID
        )
        if format == "raw":
            print(status.scans)
            return
        elif format == "json":
            print(json.dumps(thrift_to_dict(status), indent=2))
            return
        # table
        for scanid in status.scans:
            curScan = status.scans[scanid]
            for minion in curScan.responses:
                print(
                    "Scan Id {}, type {}, tx node {}, rx node {}, "
                    "start bwgd {}, response bwgd {}, status {}".format(
                        scanid,
                        ctrlTypes.ScanType._VALUES_TO_NAMES[curScan.type],
                        curScan.txNode,
                        minion,
                        curScan.startBwgdIdx,
                        curScan.responses[minion].curSuperframeNum // 16,
                        curScan.responses[minion].status,
                    ),
                    end="",
                )
                if curScan.responses[minion].txPwrIndex:
                    print(", tx power {}".format(curScan.responses[minion].txPwrIndex))
                else:
                    print("")
                routes = curScan.responses[minion].routeInfoList
                # dict: txidx -> (rxidx -> data)
                table = collections.defaultdict(dict)
                txIndexes = set()
                rxIndexes = set()
                for r in routes:
                    txIndexes.add(r.route.tx)
                    rxIndexes.add(r.route.rx)
                    table[r.route.tx][r.route.rx] = (
                        r.rssi,
                        r.snrEst,
                        r.postSnr,
                        r.rxStart,
                        r.packetIdx,
                    )
                txIndexes = sorted(list(txIndexes))
                rxIndexes = sorted(list(rxIndexes))
                data = [
                    ["" for r in range(len(rxIndexes))] for t in range(len(txIndexes))
                ]
                for (tIdx, t) in enumerate(txIndexes):
                    for (rIdx, r) in enumerate(rxIndexes):
                        try:
                            d = table[t][r]
                        except KeyError:
                            d = ("",) * 5
                        data[tIdx][rIdx] = ", ".join(str(x) for x in d)

                # tabulate got showindex in version in 0.7.7, we got 0.7.5,
                # so prepend txIndexes ourselves.
                # print(tabulate.tabulate(
                #     data, headers=rxIndexes, showindex=txIndexes))
                for (tIdx, t) in enumerate(txIndexes):
                    data[tIdx].insert(0, str(t))
                if data:
                    print(tabulate.tabulate(data, headers=rxIndexes))
                    print("")


class ScanStatusResetCmd(base.BaseCmd):
    def run(self):
        self._connect_to_controller()
        self._send_to_ctrl(
            ctrlTypes.MessageType.RESET_SCAN_STATUS,
            ctrlTypes.ResetScanStatus(),
            consts.SCAN_APP_CTRL_ID,
        )
        print("Resetting scan status ...")


class CbfConfigGetCmd(base.BaseCmd):
    def run(self):
        self._connect_to_controller()
        self._send_to_ctrl(
            ctrlTypes.MessageType.GET_CBF_CONFIG,
            ctrlTypes.GetCbfConfig(),
            consts.SCAN_APP_CTRL_ID,
        )
        config = ctrlTypes.CbfConfig()
        self._recv_from_ctrl(
            ctrlTypes.MessageType.CBF_CONFIG, config, consts.SCAN_APP_CTRL_ID
        )
        if len(config.config) == 0:
            print("CBF config empty")
        for key in config.config:
            linkCfg = config.config[key]
            print(
                "{}: Type:{}, MainTx:{}, MainRx:{}, NullAngle:{}".format(
                    key,
                    ctrlTypes.ScanType._VALUES_TO_NAMES[linkCfg.scanType],
                    linkCfg.mainTxNode,
                    linkCfg.mainRxNode,
                    linkCfg.nullAngle,
                ),
                end="",
            )
            if linkCfg.txPwrIndex and linkCfg.txPwrIndex != 255:
                print(", MainTxPwr:{}".format(linkCfg.txPwrIndex), end="")
            for n in range(len(linkCfg.auxTxNodes)):
                print(
                    ", AuxTx[{}]:{}, AuxRx[{}]:{}".format(
                        n, linkCfg.auxTxNodes[n], n, linkCfg.auxRxNodes[n]
                    ),
                    end="",
                )
                if linkCfg.auxTxPwrIndex:
                    print(
                        ", AuxTxPwr[{}]:{}".format(n, linkCfg.auxTxPwrIndex[n]), end=""
                    )
            if linkCfg.cbfBeamIdx:
                print(", cbfBeamIdx:{}".format(linkCfg.cbfBeamIdx), end="")
            print(", Apply:{}".format(linkCfg.apply))


class CbfConfigSetCmd(base.BaseCmd):
    def run(self):
        self._connect_to_controller()
        self._send_to_ctrl(
            ctrlTypes.MessageType.SET_CBF_CONFIG,
            ctrlTypes.SetCbfConfig(),
            consts.SCAN_APP_CTRL_ID,
        )
        e2e_ack = self._recv_e2e_ack(consts.SCAN_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)


class CbfConfigResetCmd(base.BaseCmd):
    def run(self):
        self._connect_to_controller()
        self._send_to_ctrl(
            ctrlTypes.MessageType.RESET_CBF_CONFIG,
            ctrlTypes.ResetCbfConfig(),
            consts.SCAN_APP_CTRL_ID,
        )
        e2e_ack = self._recv_e2e_ack(consts.SCAN_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)


class RfStateGetCmd(base.BaseCmd):
    def run(self):
        self._connect_to_controller(recv_timeout=10000)
        self._send_to_ctrl(
            ctrlTypes.MessageType.GET_RF_STATE,
            ctrlTypes.GetRfState(),
            consts.SCAN_APP_CTRL_ID,
        )
        rfstate = ctrlTypes.RfState()
        self._recv_from_ctrl(
            ctrlTypes.MessageType.RF_STATE, rfstate, consts.SCAN_APP_CTRL_ID
        )
        print(base.serialize_to_json(rfstate).decode("utf-8"))


class RfStateSetCmd(base.BaseCmd):
    def run(self, rfstate_file):
        setRfState = ctrlTypes.SetRfState()
        try:
            with open(rfstate_file, "r") as f:
                thrift_json = f.read()
            # Try reading as RfState
            rfstate = ctrlTypes.RfState()
            rfstate.readFromJson(thrift_json)
            if rfstate.im is not None:
                setRfState.rfState = rfstate
            else:
                # Try reading as ScanStatus instead
                status = ctrlTypes.ScanStatus()
                status.readFromJson(thrift_json)
                if len(status.scans):
                    setRfState.scanStatus = status
                else:
                    print("No RF state or scan data in {}".format(rfstate_file))
                    return
        except Exception:
            print("Empty file or invalid content in {}".format(rfstate_file))
            return

        self._connect_to_controller(recv_timeout=10000)
        self._send_to_ctrl(
            ctrlTypes.MessageType.SET_RF_STATE, setRfState, consts.SCAN_APP_CTRL_ID
        )
        e2e_ack = self._recv_e2e_ack(consts.SCAN_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)


class RfStateResetCmd(base.BaseCmd):
    def run(self):
        self._connect_to_controller()
        self._send_to_ctrl(
            ctrlTypes.MessageType.RESET_RF_STATE,
            ctrlTypes.ResetRfState(),
            consts.SCAN_APP_CTRL_ID,
        )
        e2e_ack = self._recv_e2e_ack(consts.SCAN_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)


class LaTpcParamsSetCmd(base.BaseCmd):
    def run(self):
        self._connect_to_controller()
        self._send_to_ctrl(
            ctrlTypes.MessageType.SET_LATPC_PARAMS,
            ctrlTypes.SetLaTpcParams(),
            consts.SCAN_APP_CTRL_ID,
        )
        e2e_ack = self._recv_e2e_ack(consts.SCAN_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)


class ScanScheduleCmd(base.BaseCmd):
    def run(self, timer, period, pbf, rtcal, cbf, im):
        if (
            period is None
            and pbf is False
            and rtcal is False
            and cbf is False
            and im is False
        ):
            # Get schedule
            self._connect_to_controller()
            self._send_to_ctrl(
                ctrlTypes.MessageType.GET_SCAN_SCHEDULE,
                ctrlTypes.GetScanSchedule(),
                consts.SCAN_APP_CTRL_ID,
            )
            resp = ctrlTypes.GetScanScheduleResp()
            self._recv_from_ctrl(
                ctrlTypes.MessageType.SCAN_SCHEDULE, resp, consts.SCAN_APP_CTRL_ID
            )

            for (scantype, timeout) in (
                ("IM", resp.scanSchedule.imScanTimeoutSec),
                ("combined", resp.scanSchedule.combinedScanTimeoutSec),
            ):
                if timeout is not None and timeout > 0:
                    print(
                        "Periodic {} scans occur every {} seconds".format(
                            scantype, timeout
                        )
                    )
                else:
                    print("Periodic {} scans are disabled".format(scantype))

            for (scantype, enabled) in (
                ("PBF", resp.scanSchedule.pbfEnable),
                ("RTCAL", resp.scanSchedule.rtcalEnable),
                ("CBF", resp.scanSchedule.cbfEnable),
                ("IM", resp.scanSchedule.imEnable),
            ):
                print(
                    "For combined scans, {} is {}".format(
                        scantype, "enabled" if enabled else "disabled"
                    )
                )

            if resp.nextBwgdIdx > 0:
                print(
                    "Combined scans already scheduled finish at BWGD {}".format(
                        resp.nextBwgdIdx
                    )
                )
            else:
                print("No combined scans have been scheduled")
            return

        if timer is None or period is None:
            self._my_exit(False, "Both --timer and --period must be specified")
        if timer == "im" and (pbf is True or rtcal is True or cbf is True):
            self._my_exit(False, "Scan types cannot be specified for periodic IM scans")
        if (
            timer == "combined"
            and period >= 0
            and not pbf
            and not rtcal
            and not cbf
            and not im
        ):
            self._my_exit(
                False,
                "At least one scan type must be enabled using --pbf, --rtcal, "
                "--cbf, or --im when periodic combined scans are enabled",
            )

        # Set schedule
        self._connect_to_controller()
        setSchedule = ctrlTypes.ScanSchedule()
        if timer == "im":
            setSchedule.imScanTimeoutSec = period
        elif timer == "combined":
            setSchedule.combinedScanTimeoutSec = period
        setSchedule.pbfEnable = pbf
        setSchedule.rtcalEnable = rtcal
        setSchedule.cbfEnable = cbf
        setSchedule.imEnable = im

        self._send_to_ctrl(
            ctrlTypes.MessageType.SET_SCAN_SCHEDULE,
            setSchedule,
            consts.SCAN_APP_CTRL_ID,
        )
        e2e_ack = self._recv_e2e_ack(consts.SCAN_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)


class SlotMapConfigCmd(base.BaseCmd):
    def run(self, configStr):
        if configStr is None:

            def intToThriftEnum(EnumClass, x):
                class EnumInt(int):
                    def __new__(cls, val, *args, **kwargs):
                        return super(EnumInt, cls).__new__(cls, val)

                    def __repr__(self):
                        if int(self) in EnumClass._VALUES_TO_NAMES:
                            return "{}.{}".format(
                                EnumClass.__name__,
                                EnumClass._VALUES_TO_NAMES[int(self)],
                            )
                        else:
                            return super(EnumInt, self).__repr__()

                return EnumInt(x)

            # Get config
            self._connect_to_controller()
            self._send_to_ctrl(
                ctrlTypes.MessageType.GET_SLOT_MAP_CONFIG,
                ctrlTypes.GetSlotMapConfig(),
                consts.SCHEDULER_APP_CTRL_ID,
            )
            config = ctrlTypes.SlotMapConfig()
            self._recv_from_ctrl(
                ctrlTypes.MessageType.SLOT_MAP_CONFIG,
                config,
                consts.SCHEDULER_APP_CTRL_ID,
            )
            # Prettify config by changing the mapping key (int) to the
            # corresponding SlotPurpose enum field name
            pretty_config = config
            pretty_config.mapping = {
                intToThriftEnum(ctrlTypes.SlotPurpose, k): v
                for (k, v) in config.mapping.items()
            }
            print(pretty_config)
            return

        # Set config
        thrift_types = {key: getattr(ctrlTypes, key) for key in dir(ctrlTypes)}
        config = eval(configStr, thrift_types)

        self._connect_to_controller()
        self._send_to_ctrl(
            ctrlTypes.MessageType.SET_SLOT_MAP_CONFIG,
            config,
            consts.SCHEDULER_APP_CTRL_ID,
        )
        e2e_ack = self._recv_e2e_ack(consts.SCHEDULER_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)


class TimeConverter(object):
    class X(object):
        @staticmethod
        def sf_to_bwgd(x):
            return float(x) / 16

        @staticmethod
        def bwgd_to_sf(x):
            return float(x) * 16

        @staticmethod
        def gps_to_bwgd(x):
            return float(x) / 0.0256

        @staticmethod
        def bwgd_to_gps(x):
            return float(x) * 0.0256

        # See ScanScheduler.cpp for algorithm
        @staticmethod
        def gps_to_unix(x):
            return float(x) + 315964800 - 18

        @staticmethod
        def unix_to_gps(x):
            return float(x) - 315964800 + 18

        @staticmethod
        def unix_to_local(x):
            return datetime.datetime.fromtimestamp(float(x)).isoformat(" ")

        @staticmethod
        def local_to_unix(x):
            try:
                res = parse(x, ignoretz=True)
                return time.mktime(res.timetuple())
            except ValueError:
                raise Exception("Unable to parse date in format 'local'")

    CONV = {
        ("sf", "bwgd"): X.sf_to_bwgd,
        ("bwgd", "sf"): X.bwgd_to_sf,
        ("bwgd", "gps"): X.bwgd_to_gps,
        ("gps", "bwgd"): X.gps_to_bwgd,
        ("gps", "unix"): X.gps_to_unix,
        ("unix", "gps"): X.unix_to_gps,
        ("unix", "local"): X.unix_to_local,
        ("local", "unix"): X.local_to_unix,
    }

    FINAL = {"sf": int, "bwgd": int, "gps": int, "unix": int}

    def __init__(self):
        self.bounds = None

        self.CONV = {
            ("bwgd", "bwgd"): [],
            ("bwgd", "gps"): [self.X.bwgd_to_gps],
            ("bwgd", "local"): [
                self.X.bwgd_to_gps,
                self.X.gps_to_unix,
                self.X.unix_to_local,
            ],
            ("bwgd", "sf"): [self.X.bwgd_to_sf],
            ("bwgd", "unix"): [self.X.bwgd_to_gps, self.X.gps_to_unix],
            ("gps", "bwgd"): [self.X.gps_to_bwgd],
            ("gps", "gps"): [],
            ("gps", "local"): [self.X.gps_to_unix, self.X.unix_to_local],
            ("gps", "sf"): [self.X.gps_to_bwgd, self.X.bwgd_to_sf],
            ("gps", "unix"): [self.X.gps_to_unix],
            ("local", "bwgd"): [
                self.X.local_to_unix,
                self.X.unix_to_gps,
                self.X.gps_to_bwgd,
            ],
            ("local", "gps"): [self.X.local_to_unix, self.X.unix_to_gps],
            ("local", "local"): [],
            ("local", "sf"): [
                self.X.local_to_unix,
                self.X.unix_to_gps,
                self.X.gps_to_bwgd,
                self.X.bwgd_to_sf,
            ],
            ("local", "unix"): [self.X.local_to_unix],
            ("sf", "bwgd"): [self.X.sf_to_bwgd],
            ("sf", "gps"): [self.X.sf_to_bwgd, self.X.bwgd_to_gps],
            ("sf", "local"): [
                self.X.sf_to_bwgd,
                self.X.bwgd_to_gps,
                self.X.gps_to_unix,
                self.X.unix_to_local,
            ],
            ("sf", "sf"): [],
            ("sf", "unix"): [self.X.sf_to_bwgd, self.X.bwgd_to_gps, self.X.gps_to_unix],
            ("unix", "bwgd"): [self.X.unix_to_gps, self.X.gps_to_bwgd],
            ("unix", "gps"): [self.X.unix_to_gps],
            ("unix", "local"): [self.X.unix_to_local],
            ("unix", "sf"): [self.X.unix_to_gps, self.X.gps_to_bwgd, self.X.bwgd_to_sf],
            ("unix", "unix"): [],
        }

    def conv(self, from_, to, t):
        for f in self.CONV[(from_, to)]:
            t = f(t)
        if to in self.FINAL:
            t = self.FINAL[to](t)
        return t

    # Returns a tuple (fmt, time) of the format and possibly adjusted time
    def guess_format(self, t):
        if self.bounds is None:
            # Calculate bounds for t to be detected as a certain format
            # as a +- delta from now
            self.bounds = {}
            now = time.time()
            delta = 3600 * 24 * 365 * 5  # seconds in 5 years
            (lo, hi) = (now - delta, now + delta)
            for fmt in ("unix", "gps", "bwgd", "sf"):
                self.bounds[fmt] = (
                    self.conv("unix", fmt, lo),
                    self.conv("unix", fmt, hi),
                )
        try:
            f = float(t)
            for fmt in ("unix", "gps", "bwgd", "sf"):
                (lo, hi) = self.bounds[fmt]
                if lo <= f <= hi:
                    return (fmt, f)
            return (None, None)
        except ValueError:
            try:
                res = parse(t, ignoretz=True)
                return ("unix", time.mktime(res.timetuple()))
            except ValueError:
                return (None, None)


class TimeConvCmd(base.BaseCmd):
    def run(self, from_, to, timestamps):
        conv = TimeConverter()
        if len(timestamps) == 0:
            # If no timestamps are provided, use the current system time
            now = time.time()
            from_ = "unix"
            timestamps = (now,)
        for (idx, t) in enumerate(timestamps):
            if from_ is None:
                # Guess format
                (realfrom, t) = conv.guess_format(t)
                if realfrom is None:
                    self._my_exit(False, "Can't guess source format")
            else:
                realfrom = from_
            if to is None:
                # Convert to all formats
                if idx > 0:
                    print()
                for realto in TimeFmts:
                    print("{}: {}".format(realto, conv.conv(realfrom, realto, t)))
            else:
                print(conv.conv(realfrom, to, t))
