#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# =============================================================================
#
#    This file contains all functions related to converting an input ini file
#    (w/ 120 non-zero TX/RX Beams)to generate an output.ini file with only 61
#    TX/RX beams which is a Boardfile compatible (Boardfile can only support up
#    to 61 TX/RX beams).
#
# =============================================================================

from __future__ import absolute_import, division, print_function, unicode_literals

import argparse
import os
import re

import numpy as np


DATA_KEY = ";;data only - rf"
SECTOR_KEY = ";;sector"
NUM_FB_BEAMS = 128
OP_BEAMS = 120
PAD_BEAM = 120


def parse_args():
    parser = argparse.ArgumentParser(
        description="This is to convert the input.ini files (w/ 120 non-zero TX/RX Beams)"
        "to a new ini file with Boardfile formate, the input ini file format is preserved"
        "(i.e. fields and registers in the input file will be preserved in the output.ini"
        "file except the beams sections)"
    )
    parser.add_argument(
        "input_file",
        help="Input ini file Pathname, it has to be w/ 120 non-zero TX/RX Beams",
        type=str,
    )
    parser.add_argument("output_file", help="output.ini file Pathname", type=str)
    parser.add_argument(
        "-m",
        required=True,
        choices=["massive", "diversity"],
        help="Mode of operation",
        type=str,
    )
    return parser.parse_args()


def selected_beams(mode, num_sectors):
    # Assumptions for input.ini file:
    #       - Contains 128 beams
    #       - TX Beam[126] is used for TX Power Calibration
    #       - RX Beam[127] is used for RX Noise Calibration
    #       - Original 120 Beams are stored in ini file from Beam_orig[0 : 119]
    #       - Boresight [0 AZ, 0 EL] is Beam_orig[60]
    # Beams Format for output.ini file:
    #       - For Massive: w/ 61 selected beams
    #              Beam[0 : 60]                 = Beam_orig[0 : 2 : 118] + Beam_orig[118]
    #              Beam[64]                     = Beam[38]
    #              Beam[38] is not used (is set to ZERO beam)
    #       - For Divesity: w/ 30 selected beams
    #              Beam[0 : 29]                 = Beam_orig[0 : 4 : 116]
    #              Beam[(0 + 30) : (29 + 30)]   = Beam[30 : 59] = Beam[0 : 29]
    #              Beam[60]                     = Beam[59]
    #              Beam[64]                     = Beam[38]
    #              Beam[38] is not used (is set to ZERO beam, QCOM limitation)
    # NOTE: Beam_orig[120] is used to pad output.ini file to 128 beams
    if mode == "massive":
        step = 2
        if num_sectors < NUM_FB_BEAMS // step:
            print(
                "Error: not enough input sectors! "
                + "\n"
                + "number of sectors = "
                + str(num_sectors)
            )
            exit(0)
        az_idx = list(np.arange(0, OP_BEAMS, step))
        az_idx = np.append(az_idx, az_idx[-1])
        az_idx = np.append(
            np.append(az_idx, np.repeat(PAD_BEAM, OP_BEAMS - len(az_idx))),
            list(range(OP_BEAMS, NUM_FB_BEAMS)),
        )
        az_idx[64] = az_idx[38]
        az_idx[38] = PAD_BEAM
    elif mode == "diversity":
        step = 4
        if num_sectors < NUM_FB_BEAMS // step:
            print(
                "Error: not enough input sectors! "
                + "\n"
                + "number of sectors = "
                + str(num_sectors)
            )
            exit(0)
        az_idx = list(np.arange(0, OP_BEAMS, step))
        az_idx = np.append(np.append(az_idx, az_idx), az_idx[-1])
        az_idx = np.append(
            np.append(az_idx, np.repeat(PAD_BEAM, OP_BEAMS - len(az_idx))),
            list(range(OP_BEAMS, NUM_FB_BEAMS)),
        )
        az_idx[64] = az_idx[38]
        az_idx[38] = PAD_BEAM
    else:
        print("Invalid mode: " + mode)
        exit(0)

    return az_idx


def process_block(block, mode):

    # Split block into [header, sector0, sector1, ...]
    data = re.split(SECTOR_KEY + ".*\n", block)
    print("Processing block: " + data[0].strip())
    new_block = data[0]
    sectors = data[1:]
    az_idx = selected_beams(mode, len(sectors))
    print("Selected Beam Indices out of 120 Beams: " + "\n", az_idx)
    # Picking either 2 beam or every 4 beams depending on if input was massive or diversity
    for n in range(0, NUM_FB_BEAMS):
        new_block = new_block + SECTOR_KEY + " " + str(n) + "\n"
        new_block = new_block + sectors[az_idx[n]]

    return new_block


def main():
    args = parse_args()
    # Load ini file
    input_file = args.input_file
    try:
        with open(input_file, "r") as fin:
            data = fin.read()
    except Exception:
        print("Error reading ini file: " + input_file)
        exit(0)

    out = ""
    # Split input data into blocks
    section_validity = 0
    blocks = re.compile("\n\n").split(data + "\n")
    for block_idx, block in enumerate(blocks):
        # Assumption is the Sub-section validity block is 3 blocks ahead
        # of the first required block for the first RFIC.
        # "section_validity" flag pointing to the sub-section validity
        # block from the current RFIC data block.
        # "0D" and "0E" are the  "unique" identifiers in the section validity
        # block corresponding to either rx sectors or tx sectors.
        # This will avoid trying to downsampling the TSSI sector data block
        #  which contains < 61 sectors
        if DATA_KEY in block and (
            "0E" in blocks[block_idx - (3 + section_validity)]
            or "0D" in blocks[block_idx - (3 + section_validity)]
        ):
            block = process_block(block, args.m)
            section_validity = section_validity + 1
        else:
            section_validity = 0
        out = out + block + "\n\n"

    # Write output
    output_file = args.output_file
    print(output_file)
    if os.path.exists(output_file):
        os.remove(output_file)

    try:
        with open(output_file, "w") as fout:
            fout.write(out)
            print("Generated 61 beam ini file for", str(args.m), "mode: ", output_file)
    except Exception:
        print("Error writing output file: " + output_file)
        exit(0)


if __name__ == "__main__":
    main()
