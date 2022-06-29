#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import argparse
import json
import re
import time
from array import array
from collections import namedtuple

import numpy as np


# Constants
NUM_BEAMS = 64
NUM_BEAMS_RTCAL = 64
NUM_BEAMS_VBS = 18
NUM_BEAMS_CBF = 152
SUBFRAME_DURATION_US = 200
SUPERFRAME_DURATION_US = 1600
MIN_VALID_SNR = -30
DATE = "2018-08-07"

ScanTypes = ["INVALID_SCAN_TYPE", "PBF", "IM", "RTCAL", "CBF_TX", "CBF_RX"]

SubTypes = [
    "NO_CAL",
    "TOP_RX_CAL",
    "TOP_TX_CAL",
    "BOT_RX_CAL",
    "BOT_TX_CAL",
    "VBS_RX_CAL",
    "VBS_TX_CAL",
    "RX_CBF_AGGRESSOR",
    "RX_CBF_VICTIM",
    "TX_CBF_AGGRESSOR",
    "TX_CBF_VICTIM",
]

ScanStatus = [
    "SCAN_PROCEDURE_COMPLETE",
    "SCAN_PROCEDURE_INVALID_TYPE",
    "SCAN_PROCEDURE_INVALID_START_TSF",
    "SCAN_PROCEDURE_INVALID_STA",
    "SCAN_PROCEDURE_AWV_IN_PROG",
    "SCAN_PROCEDURE_STA_NOT_ASSOC",
    "SCAN_PROCEDURE_REQ_BUFFER_FULL",
    "SCAN_PROCEDURE_LINK_SHUT_DOWN",
    "SCAN_PROCEDURE_UNKNOWN_ERROR",
    "SCAN_PROCEDURE_UNEXPECTED_ERROR",
    "SCAN_PROCEDURE_EXPIRED_TSF",
]

ScanMode = ["INVALID_BFSCAN", "COARSE", "FINE", "SELECTIVE", "RELATIVE"]


def parse_args():
    parser = argparse.ArgumentParser(description="Verify e2e and r2d2 scan results.")
    parser.add_argument("scan_file", help="Scan result json file to verify", type=str)
    parser.add_argument(
        "--rf",
        action="store_true",
        help="Assume RF scan results (allow for missed packets)",
    )
    parser.add_argument(
        "--routes",
        action="store_true",
        help="Compute average for each route and print results",
    )

    return parser.parse_args()


def bwgdToTs(bwgd):
    realGpsTime = bwgd * int(256 / 10)
    gpsTime = realGpsTime - 18000
    unixTimeMs = gpsTime + 315964800000
    str = time.strftime(
        "%Y-%m-%d %H:%M:%S", time.localtime(unixTimeMs / 1000)
    ) + ".{:03.0f}".format(unixTimeMs % 1000)
    return str


def log_subtest(res, test_str, subres):
    if subres == "FAIL":
        res = subres
    test_str = test_str + " " + "." * (120 - len(test_str) - len(subres)) + " " + subres
    print(test_str)
    return res


def get_relative_beams(beam):
    if beam == -1:
        beam_list = [-1]
    elif beam == 32:
        beam_list = [33, 32, 0]
    elif beam == 0:
        beam_list = [32, 0, 1]
    else:
        beam_list = [beam - 1, beam, beam + 1]
    return beam_list


def verif_status(res, scan, tag, node):
    # Check presence of response
    if node == "" or node not in list(scan["responses"].keys()):
        subres = "FAIL"
        status = "?"
    else:
        subres = "PASS"
        status = ScanStatus[scan["responses"][node]["status"]]
    res = log_subtest(res, "{:s} response present ({:s})".format(tag, node), subres)

    # Check status of response
    if status == "?":
        subres = "IGNORE"
    elif status == "SCAN_PROCEDURE_COMPLETE":
        subres = "PASS"
    else:
        subres = "FAIL"
    res = log_subtest(res, "{:s} response status ({:s})".format(tag, status), subres)

    return res


def verif_rxstart(res, response):
    # Verify packet timing

    routes = (
        []
        if "routeInfoList" not in list(response.keys())
        else response["routeInfoList"]
    )
    if "startSuperframeNum" not in list(response.keys()) or len(routes) == 0:
        return res

    # Unwrap 16 bit rxStart to get 64 bit rxStart mod 200us frame boundary
    startSF = response["startSuperframeNum"]
    rxStartUpper = (startSF * SUPERFRAME_DURATION_US) & 0xFFFFFFFFFFFF0000
    rxStartFrame = [array("i"), array("i")]
    rxStart16 = -1
    for r in routes:
        pkt = r["packetIdx"]
        if r["rxStart"] <= rxStart16:
            rxStartUpper = rxStartUpper + pow(2, 16)
        rxStart16 = r["rxStart"]
        rxStart64 = rxStartUpper + rxStart16
        rxStartFrame[pkt].append(rxStart64 % SUBFRAME_DURATION_US)

    for pkt in range(0, 2):
        res = log_subtest(
            res,
            "Check RX start TSF mod subframe boundary (pkt{:d} min/avg/max/var: "
            "[{:0.1f}, {:0.1f}, {:0.1f}, {:0.1f}])".format(
                pkt,
                np.min(rxStartFrame[pkt]),
                np.mean(rxStartFrame[pkt]),
                np.max(rxStartFrame[pkt]),
                np.var(rxStartFrame[pkt]),
            ),
            "PASS",
        )

    return res


def average_routes(scanType, subType, routes, tx_list, rx_list):
    res = "PASS"

    if scanType == "CBF_TX" or scanType == "CBF_RX":
        num_beams = NUM_BEAMS_CBF
    else:
        num_beams = NUM_BEAMS

    # Initialize array
    snr = []
    for _tx in range(0, num_beams):
        for _rx in range(0, num_beams):
            snr.append(array("f"))

    # Populate array
    for r in routes:
        txb = r["route"]["tx"]
        rxb = r["route"]["rx"]
        txidx = txb
        rxidx = rxb
        # Adjust indexing for RTCAL
        if subType in ["TOP_TX_CAL", "BOT_TX_CAL", "VBS_TX_CAL"]:
            txidx = txb - 64
            rxidx = 0
        elif subType in ["TOP_RX_CAL", "BOT_RX_CAL", "VBS_RX_CAL"]:
            rxidx = rxb - 64
            txidx = 0
        # Adjust indexing for CBF
        if scanType == "CBF_TX" or scanType == "CBF_RX":
            txidx = txidx if txidx < 220 else txidx - 220
            txidx = txidx if txidx < 64 else txidx - 64
            rxidx = rxidx if rxidx < 220 else rxidx - 220
            rxidx = rxidx if rxidx < 64 else rxidx - 64
        idx = txidx + num_beams * rxidx
        snr[idx].append(r["snrEst"])

    # Compute average for each beam pair and pick best beam
    best_tx = -1
    best_rx = -1
    # best_idx = -1
    best_snr = MIN_VALID_SNR
    snr_mat = np.ones([num_beams, num_beams]) * MIN_VALID_SNR
    for tx in range(0, num_beams):
        for rx in range(0, num_beams):
            idx = tx + num_beams * rx
            if len(snr[idx]) > 0:
                snr_avg = np.mean(snr[idx])
            elif tx in tx_list and rx in rx_list:
                snr_avg = MIN_VALID_SNR + 1
            else:
                snr_avg = MIN_VALID_SNR
            snr_mat[tx][rx] = snr_avg
            if snr_avg > best_snr:
                best_snr = snr_avg
                # best_idx = idx
                best_rx = rx
                best_tx = tx
            # Print all measurements for debugging
            # if len(snr[idx]) > 0:
            #     print tx, rx, len(snr[idx]), snr_avg, best_tx, best_rx

    if scanType == "PBF":
        beam_range = list(range(63, 31, -1)) + list(range(0, 32))
    else:
        beam_range = list(range(0, num_beams))
    # Print results
    for tx in beam_range:
        for rx in beam_range:
            if snr_mat[tx][rx] > MIN_VALID_SNR:
                str = "({:2d}, {:2d}): {:5.1f}".format(tx, rx, snr_mat[tx][rx])
                if show_routes:
                    print(str)

    # TODO: Check that newBeam matches best_idx

    # If tx_list and rx_list have 3 elements, assume relative scan and use middle beams as baseline
    # Else use first beam as baseline
    if len(tx_list) == 3 and len(rx_list) == 3:
        ref_tx = tx_list[1]
        ref_rx = rx_list[1]
    else:
        ref_tx = 0
        ref_rx = 0
    ref_snr = snr_mat[ref_tx][ref_rx]
    subres = "FAIL" if best_snr < ref_snr else "PASS"
    str = "Base SNR: {:0.1f} dB, best SNR: {:0.1f} dB, SNR gain: {:0.1f} dB".format(
        ref_snr, best_snr, best_snr - ref_snr
    )

    # Add best beam info for relative PBF
    if subType == "NO_CAL":
        change_tx = 1 if best_tx != ref_tx and best_tx != -1 else 0
        change_rx = 1 if best_rx != ref_rx and best_rx != -1 else 0
        str = str + ", changeTX:{:d}, changeRX:{:d}".format(change_tx, change_rx)

    # Add (w1, w2) info for RTCAL
    rtcal_idx = -1
    if subType in ["TOP_TX_CAL", "BOT_TX_CAL", "VBS_TX_CAL"]:
        rtcal_idx = best_tx
    elif subType in ["TOP_RX_CAL", "BOT_RX_CAL", "VBS_RX_CAL"]:
        rtcal_idx = best_rx
    if rtcal_idx >= 0:
        str = str + ", bestIdx:{:}".format(rtcal_idx)
    if rtcal_idx >= 0 and "VBS" not in subType:
        w1 = int(rtcal_idx / 8)
        w2 = int(rtcal_idx % 8)
        w1 = ((w1 + 4) % 8) - 4
        w2 = ((w2 + 4) % 8) - 4
        str = str + ", w1:{:}, w2:{:}".format(w1, w2)

    log_subtest(res, str, subres)

    return res


def verif_routes(scanType, subType, routes, expected_num_routes):
    # Helper function used to verify content of 'routeInfoList' from a single response
    res = "PASS"

    if isinstance(expected_num_routes, list) is False:
        expected_num_routes = [expected_num_routes, expected_num_routes]

    # Check route count
    if scanType == "CBF_TX" or scanType == "CBF_RX":
        num_beams = NUM_BEAMS_CBF
    else:
        num_beams = NUM_BEAMS
    beam_cnt = np.zeros(shape=(num_beams, num_beams, 2))
    min_snr = 999
    low_snr_cnt = 0
    for r in routes:
        pkt = r["packetIdx"]
        txb = r["route"]["tx"]
        rxb = r["route"]["rx"]
        txidx = txb
        rxidx = rxb
        # Adjust indexing for RTCAL
        if subType in ["TOP_TX_CAL", "BOT_TX_CAL", "VBS_TX_CAL"]:
            txidx = txb - 64
        elif subType in ["TOP_RX_CAL", "BOT_RX_CAL", "VBS_RX_CAL"]:
            rxidx = rxidx - 64
        # Adjust indexing for CBF
        if scanType == "CBF_TX" or scanType == "CBF_RX":
            txidx = txidx if txidx < 220 else txidx - 220
            txidx = txidx if txidx < 64 else txidx - 64
            rxidx = rxidx if rxidx < 220 else rxidx - 220
            rxidx = rxidx if rxidx < 64 else rxidx - 64
        if r["snrEst"] < min_snr:
            min_snr = r["snrEst"]
        if r["snrEst"] < MIN_VALID_SNR:
            low_snr_cnt = low_snr_cnt + 1
        if pkt > 1 or txidx >= num_beams or rxidx >= num_beams:
            res = log_subtest(
                res,
                "Out of bounds route (pktIdx:{:d}, tx:{:d}, rx:{:d})!".format(
                    pkt, txb, rxb
                ),
                "FAIL",
            )
            return res, beam_cnt
        beam_cnt[txidx][rxidx][pkt] = beam_cnt[txidx][rxidx][pkt] + 1

    # Check STF SNR in valid range
    subres = "IGNORE" if min_snr < -30 else "PASS"
    res = log_subtest(
        res,
        "Min STF SNR in valid range (min:{:0.1f}, {:d} below {:d})".format(
            min_snr, low_snr_cnt, MIN_VALID_SNR
        ),
        subres,
    )

    # Check unique route count
    pkt_cnt = [int(0), int(0)]
    route_cnt = [int(0), int(0)]
    for pkt in range(0, 2):
        for txb in range(0, num_beams):
            for rxb in range(0, num_beams):
                pkt_cnt[pkt] = pkt_cnt[pkt] + int(beam_cnt[txb][rxb][pkt])
                if beam_cnt[txb][rxb][pkt] > 0:
                    route_cnt[pkt] = route_cnt[pkt] + 1
        if route_cnt[pkt] < expected_num_routes[pkt]:
            subres = "IGNORE" if scanType == "IM" else "FAIL"
        elif (
            (scanType == "CBF_TX" or scanType == "CBF_RX")
            and route_cnt[pkt] > 0
            and expected_num_routes[pkt] == 0
        ):
            subres = "FAIL"
        else:
            subres = "PASS"
        res = log_subtest(
            res,
            "Unique route count for pktIdx {:d} (expect:{:d}, actual:{:d})".format(
                pkt, expected_num_routes[pkt], route_cnt[pkt]
            ),
            subres,
        )
        res = "FAIL" if subres == "IGNORE" else res

    # Check for almost same number of packets per double packet index
    if abs(pkt_cnt[1] - pkt_cnt[0]) <= thresh.double_pkt_diff:
        subres = "PASS"
    elif (
        min(pkt_cnt) > 0 and 100 * min(pkt_cnt) / max(pkt_cnt) >= thresh.double_pkt_rel
    ):
        subres = "PASS"
    elif scanType == "IM":
        subres = "IGNORE"
    else:
        subres = "FAIL"
    if (scanType == "CBF_TX" or scanType == "CBF_RX") and (
        expected_num_routes[1] == 0 or thresh.is_rf
    ):
        # Skip for CBF aux RX node
        res = log_subtest(res, "Skipping double packet check for CBF", "IGNORE")
    else:
        res = log_subtest(
            res,
            "Double packet match to {:d} packets or {:d}% ({:d}, {:d})".format(
                thresh.double_pkt_diff, thresh.double_pkt_rel, pkt_cnt[0], pkt_cnt[1]
            ),
            subres,
        )
        res = "FAIL" if subres == "IGNORE" else res

    return res, beam_cnt


def verif_pbf_rtcal(scan):
    res = "PASS"
    scanType = ScanTypes[scan["type"]]
    subType = (
        "NO_CAL" if "subType" not in list(scan.keys()) else SubTypes[scan["subType"]]
    )
    scanMode = ScanMode[scan["mode"]]

    # Check presence of response from TX and RX nodes
    tx = ""
    rx = ""
    for node in list(scan["responses"]):
        if node == scan["txNode"]:
            tx = node
        else:
            rx = node
    res = verif_status(res, scan, "TX", tx)
    res = verif_status(res, scan, "RX", rx)
    tx_status = "?" if tx == "" else ScanStatus[scan["responses"][tx]["status"]]
    rx_status = "?" if rx == "" else ScanStatus[scan["responses"][rx]["status"]]

    # Check route response info
    if scanType == "PBF" and scanMode == "RELATIVE":
        expected_num_routes = thresh.pbf_rel_route_cnt
    elif scanType == "PBF" and scanMode == "FINE":
        expected_num_routes = thresh.pbf_fine_route_cnt
    elif scanType == "RTCAL":
        if "VBS" in subType:
            expected_num_routes = thresh.vbs_route_cnt
        else:
            expected_num_routes = thresh.rtcal_route_cnt
    else:  # Unexpected
        expected_num_routes = NUM_BEAMS * NUM_BEAMS

    routes = [] if rx == "" else scan["responses"][rx]["routeInfoList"]
    subres, beam_cnt = verif_routes(scanType, subType, routes, expected_num_routes)
    res = "FAIL" if subres == "FAIL" else res

    # Check that correct routes were detected
    tx_list = []
    rx_list = []
    txb = (
        -1
        if tx == "" or "oldBeam" not in list(scan["responses"][tx].keys())
        else scan["responses"][tx]["oldBeam"]
    )
    rxb = (
        -1
        if rx == "" or "oldBeam" not in list(scan["responses"][rx].keys())
        else scan["responses"][rx]["oldBeam"]
    )
    if scanType == "PBF" and scanMode == "RELATIVE":
        tx_list = get_relative_beams(txb)
        rx_list = get_relative_beams(rxb)
        for pkt in range(0, 2):
            tx_good = 0
            tx_bad = 0
            rx_good = 0
            rx_bad = 0
            for txidx in range(0, NUM_BEAMS):
                for rxidx in range(0, NUM_BEAMS):
                    if beam_cnt[txidx][rxidx][pkt] > 0:
                        if txidx in tx_list:
                            tx_good = tx_good + 1
                        else:
                            tx_bad = tx_bad + 1
                        if rxidx in rx_list:
                            rx_good = rx_good + 1
                        else:
                            rx_bad = rx_bad + 1
            # Skip route verfication if no packets detected
            if tx_good == 0 and tx_bad == 0 and rx_good == 0 and rx_bad == 0:
                continue

            # Check TX
            subres = (
                "PASS" if tx_good >= expected_num_routes and tx_bad == 0 else "FAIL"
            )
            res = log_subtest(
                res,
                "Check if TX beam correct for packet index {:d} "
                "({:d} correct and {:d} wrong of {:d} expected)".format(
                    pkt, tx_good, tx_bad, expected_num_routes
                ),
                subres,
            )
            # Check RX
            subres = (
                "PASS" if rx_good >= expected_num_routes and rx_bad == 0 else "FAIL"
            )
            res = log_subtest(
                res,
                "Check if RX beam correct for packet index {:d} "
                "({:d} correct and {:d} wrong of {:d} expected)".format(
                    pkt, rx_good, rx_bad, expected_num_routes
                ),
                subres,
            )

    subres = average_routes(scanType, subType, routes, tx_list, rx_list)
    res = "FAIL" if subres == "FAIL" else res

    # Check for constant beam on one end of link for RTCAL
    if subType != "NO_CAL" and routes != []:
        subres = "PASS"
        key = "rx" if subType in ["TOP_TX_CAL", "BOT_TX_CAL", "VBS_TX_CAL"] else "tx"
        beam = routes[0]["route"][key]
        for r in routes:
            if r["route"][key] != beam:
                subres = "FAIL"
        res = log_subtest(
            res,
            "Constant beam check for subType {:s} (key:{:s}, beam:{:d})".format(
                subType, key, beam
            ),
            subres,
        )

    # Check apply and new beam
    apply = scan["apply"] if "apply" in list(scan.keys()) else False
    new_tx = (
        -1
        if tx == "" or "newBeam" not in list(scan["responses"][tx].keys())
        else scan["responses"][tx]["newBeam"]
    )
    new_rx = (
        -1
        if rx == "" or "newBeam" not in list(scan["responses"][rx].keys())
        else scan["responses"][rx]["newBeam"]
    )
    if tx_status != "SCAN_PROCEDURE_COMPLETE" or rx_status != "SCAN_PROCEDURE_COMPLETE":
        subres = "IGNORE"
    elif apply is True:
        if (
            subType == "NO_CAL"
            and new_tx in range(0, NUM_BEAMS)
            and new_rx in range(0, NUM_BEAMS)
        ):
            subres = "PASS"
        elif (
            subType in ["TOP_TX_CAL", "BOT_TX_CAL", "VBS_TX_CAL"]
            and new_tx in range(0, NUM_BEAMS)
            and new_rx == 255
        ):
            subres = "PASS"
        elif (
            subType in ["TOP_RX_CAL", "BOT_RX_CAL", "VBS_RX_CAL"]
            and new_tx == 255
            and new_rx in range(0, NUM_BEAMS)
        ):
            subres = "PASS"
        else:
            subres = "FAIL"
    elif apply is False and new_tx == 255 and new_rx == 255:
        subres = "PASS"
    else:
        subres = "FAIL"
    res = log_subtest(
        res,
        "Apply flag and new beam ({}, TX:{:d}, RX:{:d})".format(apply, new_tx, new_rx),
        subres,
    )

    # if rx != '':
    #    res = verif_rxstart(res, scan['responses'][rx])

    return res


def verif_cbf(scan):
    res = "PASS"
    scanType = ScanTypes[scan["type"]]
    subType = (
        "NO_CAL" if "subType" not in list(scan.keys()) else SubTypes[scan["subType"]]
    )

    # Check for at least 4 responses for scan; 2 responses for apply with beam from controller
    if "cbfBeamIdx" in list(scan.keys()):
        subres = "PASS" if len(scan["responses"]) == 2 else "FAIL"
        res = log_subtest(
            res,
            "Response from 2 nodes ({:d} responses present)".format(
                len(scan["responses"])
            ),
            subres,
        )
    else:
        subres = "PASS" if len(scan["responses"]) >= 4 else "FAIL"
        res = log_subtest(
            res,
            "Response from at least 4 nodes ({:d} responses present)".format(
                len(scan["responses"])
            ),
            subres,
        )

    # Get main and aux nodes
    if "auxTxNodes" in list(scan.keys()):
        aux_tx_nodes = scan["auxTxNodes"]
        aux_rx_nodes = scan["auxRxNodes"]
    if "mainTxNode" in list(scan.keys()):
        main_tx = scan["mainTxNode"]
        main_rx = scan["mainRxNode"]
    else:
        # Legacy reports don't have node roles so must infer from scan responses
        main_tx = ""
        main_rx = ""
        aux_tx = ""
        aux_rx = ""
        for node in list(scan["responses"]):
            if node == scan["txNode"]:
                main_tx = node
            elif "txPwrIndex" in list(scan["responses"][node].keys()):
                aux_tx = node
            else:
                routes = scan["responses"][node]["routeInfoList"]
                for r in routes:
                    if r["packetIdx"] == 1:
                        main_rx = node
                if len(routes) > 0 and node != main_rx:
                    aux_rx = node
        aux_tx_nodes = [aux_tx]
        aux_rx_nodes = [aux_rx]

    # Check presence of response from main nodes
    res = verif_status(res, scan, "Main TX", main_tx)
    res = verif_status(res, scan, "Main RX", main_rx)

    # If cbfBeamIdx specified skip route check and return
    if "cbfBeamIdx" in list(scan.keys()):
        return res

    # Check presence of response from aux nodes
    for aux_tx in aux_tx_nodes:
        res = verif_status(res, scan, "Aux TX", aux_tx)
    for aux_rx in aux_rx_nodes:
        res = verif_status(res, scan, "Aux RX", aux_rx)

    # Loop through again and display status of responses from nodes with unknown role
    for node in list(scan["responses"]):
        if node not in [main_tx, main_rx] + aux_tx_nodes + aux_rx_nodes:
            res = verif_status(res, scan, "Unknown", node)

    # Check routes for main_rx
    routes = [] if main_rx == "" else scan["responses"][main_rx]["routeInfoList"]
    subres, main_beam_cnt = verif_routes(
        scanType, subType, routes, thresh.cbf_route_cnt
    )
    res = "FAIL" if subres == "FAIL" else res

    # Check routes for aux_rx
    routes = [] if aux_rx == "" else scan["responses"][aux_rx]["routeInfoList"]
    subres, aux_beam_cnt = verif_routes(scanType, subType, routes, [1, 0])
    res = "FAIL" if subres == "FAIL" else res

    return res


def verif_im(scan):
    res = "PASS"

    # Check presence and status of response from TX node
    tx = scan["txNode"]
    if tx in list(scan["responses"].keys()):
        status = ScanStatus[scan["responses"][tx]["status"]]
    else:
        status = "?"
    subres = "PASS" if status == "SCAN_PROCEDURE_COMPLETE" else "FAIL"
    res = log_subtest(
        res, "TX response status ({:s} from {:s})".format(status, tx), subres
    )

    # Check that each response has no routes or all routes present
    num_resp_no_routes = 0
    num_resp_with_routes = 0
    num_resp_exp_routes = 0
    for node in list(scan["responses"]):
        if node == tx:
            continue
        routes = scan["responses"][node]["routeInfoList"]
        status = ScanStatus[scan["responses"][node]["status"]]
        subres = "PASS" if status == "SCAN_PROCEDURE_COMPLETE" else "FAIL"
        res = log_subtest(
            res,
            "Status of response from {:s} with {:d} packets ({:s})".format(
                node, len(routes), status
            ),
            subres,
        )
        if len(routes) == 0:
            num_resp_no_routes = num_resp_no_routes + 1
        else:
            num_resp_with_routes = num_resp_with_routes + 1
            subres, beam_cnts = verif_routes(
                "IM", "NO_CAL", routes, thresh.im_route_cnt
            )
            if subres == "PASS":
                num_resp_exp_routes = num_resp_exp_routes + 1
            # res = verif_rxstart(res, scan['responses'][node])

    # Check for at least 1 response with expected number of routes
    subres = "PASS" if num_resp_exp_routes > 0 else "FAIL"
    res = log_subtest(
        res,
        "Have response with all routes ({:d} responses, "
        "{:d} with routes, {:d} with expected num routes)".format(
            num_resp_no_routes + num_resp_with_routes,
            num_resp_with_routes,
            num_resp_exp_routes,
        ),
        subres,
    )

    return res


def atoi(text):
    return int(text) if text.isdigit() else text


def natural_keys(text):
    return [atoi(c) for c in re.split("(\d+)", text)]


def main():
    Threshold = namedtuple(
        "Threshhold",
        [
            "pbf_fine_route_cnt",
            "pbf_rel_route_cnt",
            "im_route_cnt",
            "rtcal_route_cnt",
            "vbs_route_cnt",
            "cbf_route_cnt",
            "double_pkt_diff",
            "double_pkt_rel",
            "is_rf",
        ],
    )
    global thresh
    global show_routes

    # Parse command line options
    args = parse_args()
    fname = args.scan_file
    print(("Version: %s" % DATE))
    print(("Using scan file: %s" % fname))
    if args.rf:
        print("Using thresholds for RF setup")
        thresh = Threshold(
            pbf_fine_route_cnt=50,
            pbf_rel_route_cnt=8,
            im_route_cnt=0,
            rtcal_route_cnt=50,
            vbs_route_cnt=10,
            cbf_route_cnt=100,
            double_pkt_diff=10,
            double_pkt_rel=80,
            is_rf=True,
        )
    else:
        print("Using thresholds for IF setup (add --rf for RF thresholds)")
        thresh = Threshold(
            pbf_fine_route_cnt=4096,
            pbf_rel_route_cnt=9,
            im_route_cnt=4096,
            rtcal_route_cnt=NUM_BEAMS_RTCAL,
            vbs_route_cnt=NUM_BEAMS_VBS,
            cbf_route_cnt=NUM_BEAMS_CBF,
            double_pkt_diff=1,
            double_pkt_rel=100,
            is_rf=False,
        )
    if args.routes:
        show_routes = True
    else:
        show_routes = False

    # Load scan results
    data = json.load(open(fname))
    tokens = list(data["scans"])
    tokens.sort(key=natural_keys)

    results = []
    for token in tokens:
        res = "PASS"
        try:
            scan = data["scans"][token]
            scanType = ScanTypes[scan["type"]]
            subType = (
                "NO_CAL"
                if "subType" not in list(scan.keys())
                else SubTypes[scan["subType"]]
            )
            mode = ScanMode[scan["mode"]]
            test_str = "Token %s: %s, %s, %s, TX:%s @ %d (%s), %d responses" % (
                token,
                scanType,
                subType,
                mode,
                scan["txNode"],
                scan["startBwgdIdx"],
                bwgdToTs(scan["startBwgdIdx"]),
                len(scan["responses"]),
            )
            print(("\n" + test_str))

            scanSlot = (scan["startBwgdIdx"] / 16) % 128
            res = log_subtest(res, "Scan slot: {:d}".format(int(scanSlot)), "IGNORE")

            if scanType == "PBF" or scanType == "RTCAL":
                res = verif_pbf_rtcal(scan)
            elif scanType == "IM":
                res = verif_im(scan)
            elif scanType == "CBF_RX" or scanType == "CBF_TX":
                res = verif_cbf(scan)
            else:
                res = "FAIL"
        except Exception as e:
            print("")
            test_str = "Token {:s}".format(token)
            res = log_subtest(
                res, "Token {:d}: Parse error!".format(int(token)), "FAIL"
            )
        log_subtest(res, "OVERALL", res)
        results.append([test_str, res])

    res = "PASS"
    print("\nTEST SUMMARY")
    for test in results:
        res = log_subtest(res, test[0], test[1])
    log_subtest(res, "OVERALL", res)


if __name__ == "__main__":
    main()
