#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import argparse
import json
import os
import re


# Decode json objects and save them as array of dicts
FLAGS = re.VERBOSE | re.MULTILINE | re.DOTALL
WHITESPACE = re.compile(r"[ \t\n\r]*", FLAGS)


class ConcatJSONDecoder(json.JSONDecoder):
    def decode(self, s, _w=WHITESPACE.match):
        s_len = len(s)

        objs = []
        end = 0
        while end != s_len:
            obj, end = self.raw_decode(s, idx=_w(s, end).end())
            end = _w(s, end).end()
            objs.append(obj)
        return objs


def parse_args():
    parser = argparse.ArgumentParser(
        description="This script reformats the r2d2 scan output to be compatible with scan_verif.py and scan_plot.py."
    )
    parser.add_argument(
        "scan_file", help="Scan result json file to parse into e2e format", type=str
    )
    return parser.parse_args()


def main():
    args = parse_args()
    scan_file = args.scan_file
    output_file = os.path.splitext(scan_file)[0] + "_parsed.json"
    parse_text_output(scan_file, output_file)


def parse_text_output(scan_file, output_file):

    # capture only the data between between open and close braces
    with open(scan_file, "r") as myfile:
        data = myfile.readlines()
        my_array = []
        open_brace = 0
        close_brace = 0
        done_first_brace = False
        parent_dict = {}
        for line in data:
            if "{" in line:
                open_brace += line.count("{")
            elif "}" in line:
                close_brace += line.count("}")
            if open_brace >= 1 and close_brace < open_brace:
                if done_first_brace:
                    my_array.append(line)
                if open_brace == 1 and not done_first_brace:
                    # include open brace and everything from it on that line
                    my_array.append(line[line.index("{") :])
                    done_first_brace = True
            elif open_brace == close_brace and close_brace >= 1:
                # include everything upto and including the close brace
                my_array.append(line[: line.index("}") + 1])
                open_brace = close_brace = 0
                done_first_brace = False
        # create a file and write output to it
        with open(output_file, "w+") as f:
            for item in my_array:
                f.write(item)
            f.close()
    # Create json structure similar to e2e scan report
    data = open(output_file, "r").read()
    json_list = json.loads(data, cls=ConcatJSONDecoder)
    scan_type = json_list[0]["scanType"]
    im_scan = False
    if scan_type == 2:
        im_scan = True
    rx_node_mac = json_list[0]["rxNodeMac"]
    tx_node_mac = json_list[2]["txNodeMac"]
    start_bwgd = json_list[0]["startBwgdIdx"]
    scan_type = json_list[0]["scanType"]
    scan_mode = json_list[0]["scanMode"]
    apply = False
    # For IM scan handle multiple rx responses
    if im_scan:
        for i in range(3, len(json_list), 2):
            parent_dict["resp_node{}".format(i)] = json_list[i]
    else:
        parent_dict[rx_node_mac] = json_list[3]
        apply = json_list[0]["apply"]
    parent_dict[tx_node_mac] = json_list[1]
    token = parent_dict[tx_node_mac]["token"]
    final_dict = {}
    final_dict["scans"] = {}
    final_dict["scans"][token] = {}
    final_dict["scans"][token]["responses"] = {}
    final_dict["scans"][token]["responses"] = parent_dict
    final_dict["scans"][token]["txNode"] = tx_node_mac
    final_dict["scans"][token]["startBwgdIdx"] = start_bwgd
    final_dict["scans"][token]["type"] = scan_type
    final_dict["scans"][token]["mode"] = scan_mode
    final_dict["scans"][token]["apply"] = apply
    string = json.dumps(final_dict)
    with open(output_file, "w") as f:
        for item in string:
            f.write(item)
    return output_file


if __name__ == "__main__":
    main()
