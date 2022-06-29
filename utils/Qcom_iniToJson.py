#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# =============================================================================
#
#    This file contains all functions related to converting an input ini file
#    (w/ 120 non-zero Beams) for all channels to per-channel JSON Codebooks.
#
# =============================================================================

from __future__ import absolute_import, division, print_function, unicode_literals

import argparse
import os
import re
import shutil
import time
from datetime import datetime

from pathlib2 import Path


def parse_args():
    parser = argparse.ArgumentParser(
        description="This is to convert the input ini files (w/ 120 non-zero TX/RX Beams)"
        " to json codebooks for the 4 channels"
    )
    parser.add_argument("input_file", help="Input ini file Pathname", type=str)
    parser.add_argument(
        "output_files_pathname",
        help="output json codebook files Pathname",
        type=str,
        default="./",
    )
    parser.add_argument(
        "-m",
        required=True,
        choices=["massive2", "massive4", "diversity", "single"],
        help="Mode of operation to comply with the FW Codebook requirements",
        type=str,
        default="massive4",
    )
    parser.add_argument("-s", required=True, help="Serial Number of Node", type=str)
    parser.add_argument(
        "-b",
        required=True,
        choices=[1, 4, 16, 64, 5, 17, 65, 20, 68, 80, 10, 34, 130, 40, 136, 160, 85],
        help="Activated RFIC bitmap in decimal; Massive4 (85),"
        " Massive2 (5, 17, 65, 20, 68, 80), Diversity (10, 34, 130, 40, 136, 160),"
        " Single Tile (1, 4, 16, 64)",
        type=int,
        default=85,
    )
    parser.add_argument(
        "-c",
        required=True,
        choices=["1", "4"],
        help="Per-channel calibration or Single-channel calibration",
        type=str,
        default="4",
    )
    return parser.parse_args()


def findWholeWord(w):
    return re.compile(r"\b({0})\b".format(w), flags=re.IGNORECASE).search


def myLog(x, b):
    if x < b:
        return 0
    return 1 + myLog(x / b, b)


def parser_ini_file(args):
    # This function is to parse the input ini file to 4 per-channel ini files
    # to be converted to per-channel JSON codebook. It takes into consideration
    # the mode of operation; either massive2, massive4, diversity, or single tile modes
    # Assumptions:
    #   - FB Firmware expects to see 4 RFIC modules data in the JSON codebook "regardless" of
    #     the mode of operation.
    #   - For Massive4 mode,
    #        - The input ini file will have the required 4 TX/RX RFIC modules' data
    #        - The output JSON codebook file will have the required 4 TX/RX RFIC modules' data
    #   - For Diversity and Massive2 modes,
    #        - The input ini file will have ONLY 2 TX/RX RFIC modules' data
    #        - The output JSON codebook file will have the required 4 TX/RX RFIC modules'
    #          data with 2 appended "Dummy" TX/RX RFIC module data
    #   - For Single Tile mode,
    #        - The input ini file will have ONLY 1 TX/RX RFIC module's data
    #        - The output JSON codebook file will have the required 4 TX/RX RFIC modules'
    #          data with 3 appended "Dummy" TX/RX RFIC module data
    #   - The "dummy" TX/RX RFIC module data, used for the appending, is just the first
    #     RX RFIC module in the input ini file using the RFICbitmap. For example,
    #        - For Massive2 with input ini file which has TX/RX RFIC 0 and TX/RX RFIC 2,
    #          the "dummy" TX/RX RFIC module will be RX RFIC 0
    #        - For Diversity with input ini file which has TX/RX RFIC 1 and TX/RX RFIC 5,
    #          the "dummy" TX/RX RFIC module will be RX RFIC 1
    #   - FB Firmware will discard the inactive TX/RX RFIC modules in the JSON codebook
    #     depends on the mode of operation

    input_file = args.input_file
    out_dirname = args.output_files_pathname
    mode = args.m
    per_or_sing_cal = args.c
    padding_rfic_idx = myLog(args.b & ~(args.b - 1), 2)
    if mode == "single":
        append_factor = 3
    elif mode == "massive2":
        append_factor = 2
    elif mode == "massive4":
        append_factor = 0
    elif mode == "diversity":
        append_factor = 2
    else:
        print("Invalid mode: " + mode)
        exit(0)

    Boardfile_ini_name = input_file
    for i in range(4):
        if os.path.exists(out_dirname + "fw_cfg_ant_codebook_chn_%d.ini" % (i + 1)):
            os.remove(out_dirname + "fw_cfg_ant_codebook_chn_%d.ini" % (i + 1))
    time.sleep(1)
    for chn in range(1, 5):
        with open(Boardfile_ini_name) as infile, open(
            Path(out_dirname, "fw_cfg_ant_codebook_chn_%d" % chn).with_suffix(".ini"),
            "w",
        ) as outfile:
            copy = False
            chn_str = str(chn)
            chn_str_plusOne = str(chn + 1)
            for line in infile:
                if "Channel" in line.strip():
                    if findWholeWord(chn_str)(line.strip()) is not None:
                        if "rx" in line.strip():
                            copy = True
                if per_or_sing_cal == "1":
                    if "Channel" in line.strip():
                        if "1" in line.strip():
                            if "tx" in line.strip():
                                copy = False
                else:
                    if chn != 4:
                        if "Channel" in line.strip():
                            if chn_str_plusOne in line.strip():
                                if "rx" in line.strip():
                                    copy = False
                    if chn == 4:
                        if "Channel" in line.strip():
                            if "1" in line.strip():
                                if "tx" in line.strip():
                                    copy = False
                if copy:
                    outfile.write(line)
            outfile.close()
        # Appending the per-channel .ini file to comply with the FB Firmware requirement of
        # 4 "TX/RX" RFIC modules' data in json codebook. Appending the per-channel ini file
        # uses the first RX RFIC module data provided as dummy
        if append_factor != 0:
            for _i in range(0, append_factor):
                with open(
                    Path(out_dirname, "fw_cfg_ant_codebook_chn_%d" % chn).with_suffix(
                        ".ini"
                    ),
                    "r",
                ) as outfile_to_append_rx:
                    data = outfile_to_append_rx.read()
                    outfile_to_append_rx.close()
                match = re.search(
                    r"\n(;;data only - rf%d\n.*?\n)\n" % padding_rfic_idx,
                    data,
                    re.M | re.S,
                )
                if match:
                    with open(
                        Path(
                            out_dirname, "fw_cfg_ant_codebook_chn_%d" % chn
                        ).with_suffix(".ini"),
                        "a+",
                    ) as outfile_to_append_rx:
                        outfile_to_append_rx.write(match.group(1))
                        outfile_to_append_rx.write("\n")
                outfile_to_append_rx.close()
        with open(Boardfile_ini_name) as infile, open(
            Path(out_dirname, "fw_cfg_ant_codebook_chn_%d" % chn).with_suffix(".ini"),
            "a+",
        ) as outfile:
            copysecond = False
            copysubsection = False
            for line in infile:
                if "Channel" in line.strip():
                    if findWholeWord(chn_str)(line.strip()) is not None:
                        line1 = next(infile)
                        line2 = next(infile)
                        copysubsection = True
                        # Skipping the TSSI sector in the parser
                        if "tx" in line.strip() and "0B" not in line2.strip():
                            copysecond = True
                else:
                    copysubsection = False

                if per_or_sing_cal == "1":
                    if "Channel" in line.strip():
                        if "RfChannelConfig" in line.strip():
                            copysecond = False
                else:
                    if chn != 4:
                        if "Channel" in line.strip():
                            if chn_str_plusOne in line.strip():
                                if "tx" in line.strip():
                                    copysecond = False
                    if chn == 4:
                        if "Channel" in line.strip():
                            if "RfChannelConfig" in line.strip():
                                copysecond = False
                if copysecond:
                    outfile.write(line)
                    if copysubsection:
                        outfile.write(line1)
                        outfile.write(line2)
            outfile.close()
        # Appending the per-channel .ini file to comply with the FB Firmware requirement of
        # 4 "TX/RX" RFIC modules' data in json codebook. Appending the per-channel ini file
        # uses the first RX RFIC module data provided as dummy
        if append_factor != 0:
            for _i in range(0, append_factor):
                with open(
                    Path(out_dirname, "fw_cfg_ant_codebook_chn_%d" % chn).with_suffix(
                        ".ini"
                    ),
                    "r",
                ) as outfile_to_append_rx:
                    data = outfile_to_append_rx.read()
                    outfile_to_append_rx.close()
                match = re.search(
                    r"\n(;;data only - rf%d\n.*?\n)\n" % padding_rfic_idx,
                    data,
                    re.M | re.S,
                )
                if match:
                    with open(
                        Path(
                            out_dirname, "fw_cfg_ant_codebook_chn_%d" % chn
                        ).with_suffix(".ini"),
                        "a+",
                    ) as outfile_to_append_rx:
                        outfile_to_append_rx.write(match.group(1))
                        outfile_to_append_rx.write("\n")
                outfile_to_append_rx.close()

    print("Finished parsing ini files!!")


def main():
    args = parse_args()

    # Parsing the input ini file into 4 per-channel ini files
    parser_ini_file(args)

    out_dirname = args.output_files_pathname
    serial_num = args.s
    if len(serial_num) > 25:
        print("Serial number too large!")
        return

    dtobj = datetime.now()
    tmstamp = dtobj.strftime("%d-%b-%Y %H:%M:%S")
    desc = serial_num + " " + tmstamp
    time.sleep(2)

    for i in range(4):
        path = Path(out_dirname, "fw_cfg_ant_codebook_chn_%d" % (i + 1)).with_suffix(
            ".ini"
        )
        text = path.read_text()
        text = text.replace(" = ", "=")
        path.write_text(text)
        time.sleep(1)

    for i in (1, 3):
        path = Path(out_dirname, "fw_cfg_ant_codebook_chn_%d" % i).with_suffix(".ini")
        text = path.read_text()
        text = text.replace("Channel", "Channels")
        path.write_text(text)
        time.sleep(1)

    for i in range(11):
        if os.path.exists(out_dirname + "fw_cfg_ant_codebook_chn_%d.json" % (i + 1)):
            os.remove(out_dirname + "fw_cfg_ant_codebook_chn_%d.json" % (i + 1))

    for i in range(4):
        path = Path("conv_qcom_cb.py")
        text = path.read_text()
        text = text.replace(
            "fw_cfg_ant_codebook_chn_%d" % i, "fw_cfg_ant_codebook_chn_%d" % (i + 1)
        )
        path.write_text(text)

        os.system(
            "python3 conv_qcom_cb.py "
            + "-d "
            + '"'
            + desc
            + '"'
            + " -f "
            + out_dirname
            + "/fw_cfg_ant_codebook_chn_%d.ini" % (i + 1)
            + " -o "
            + out_dirname
            + "/fw_cfg_ant_codebook_chn_%d.json" % (i + 1)
        )
        time.sleep(2)

    text = path.read_text()
    text = text.replace("fw_cfg_ant_codebook_chn_4", "fw_cfg_ant_codebook_chn_1")
    path.write_text(text)

    os.chdir(out_dirname)
    if os.path.exists("fw_cfg_ant_codebook_chn_2.json"):
        shutil.copy("fw_cfg_ant_codebook_chn_2.json", "fw_cfg_ant_codebook_chn_5.json")
        shutil.copy("fw_cfg_ant_codebook_chn_2.json", "fw_cfg_ant_codebook_chn_6.json")
        shutil.copy("fw_cfg_ant_codebook_chn_2.json", "fw_cfg_ant_codebook_chn_7.json")
        shutil.copy("fw_cfg_ant_codebook_chn_2.json", "fw_cfg_ant_codebook_chn_8.json")
        shutil.copy("fw_cfg_ant_codebook_chn_2.json", "fw_cfg_ant_codebook_chn_10.json")

    if os.path.exists("fw_cfg_ant_codebook_chn_1.json"):
        shutil.copy("fw_cfg_ant_codebook_chn_1.json", "fw_cfg_ant_codebook_chn_9.json")

    if os.path.exists("fw_cfg_ant_codebook_chn_3.json"):
        shutil.copy("fw_cfg_ant_codebook_chn_3.json", "fw_cfg_ant_codebook_chn_11.json")

    for i in range(11):
        if os.path.exists("fw_cfg_ant_codebook_chn_%d.json" % (i + 1)):
            num_lines = sum(
                1 for line in open("fw_cfg_ant_codebook_chn_%d.json" % (i + 1))
            )
            if 2085 == num_lines:
                print(
                    "fw_cfg_ant_codebook_chn_%d.json created successfully!!" % (i + 1)
                )
            else:
                print("Failed to create fw_cfg_ant_codebook_chn_%d.json!!" % (i + 1))
                if os.path.exists("fw_cfg_ant_codebook_chn_%d.json" % (i + 1)):
                    os.remove("fw_cfg_ant_codebook_chn_%d.json" % (i + 1))

    time.sleep(1)


if __name__ == "__main__":
    main()
    print("Done!")
