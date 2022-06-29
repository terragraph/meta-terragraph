#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from __future__ import absolute_import, division, print_function, unicode_literals

import io
import json
import os
import re

import click


# Usage: `python conv_intel_cb.py -f intel_input.txt`
# Help: `python conv_intel_cb.py --help`

RFEM_KEY = "RFEM"
VAL_KEY = "{.val = "

NUM_ANT_ELEM_PER_BEAM = 16
NUM_BEAMS_PER_TABLE = 64
NUM_BEAMS_PER_TABLE_PART = 32
NUM_TABLE_PARTS = NUM_BEAMS_PER_TABLE / NUM_BEAMS_PER_TABLE_PART


@click.command()
@click.option(
    "--config_file",
    "-f",
    type=str,
    help="txt file name to convert to json",
    required=True,
)
@click.option(
    "--description",
    "-d",
    default="Default Codebook",
    type=str,
    help="description of the codebook",
)
@click.option(
    "--output_file",
    "-o",
    default="intel_output.json",
    type=str,
    help="name of file to output to",
)
@click.pass_obj
def config_to_json(cli_opts, config_file, description, output_file):
    if not output_file.endswith(".json"):
        output_file += ".json"

    try:
        with open(config_file, "r") as content:
            buff = content.read()
    except Exception:
        print("Error reading config file: " + config_file)
        exit(0)

    data = {}
    data["Description"] = description
    code_book_tables = []
    buff = io.StringIO(buff)
    buff.seek(0, os.SEEK_END)
    buff_len = buff.tell()
    buff.seek(0, os.SEEK_SET)

    code_book_tables = create_code_book_tables(buff, buff_len)
    data["codeBookTables"] = code_book_tables

    # Format json data
    json_data = json.dumps(data, sort_keys=False, indent=2)
    json_data = json_data.replace("\n              ", "")
    json_data = json_data.replace("            [", "            [ ")
    json_data = json_data.replace("\n            ]", " ]")

    try:
        with open(output_file, "w") as output:
            output.write(json_data)
            print("Outputted to file:", output_file)
    except Exception:
        print("Error writing json file: " + output_file)
        exit(0)


def create_code_book_tables(buff, buff_len):
    code_book_tables = []
    module = 0
    module_name = ""
    type = ""
    is_Rx = 0
    while buff.tell() < buff_len:
        line = buff.readline()
        # Skip over all commented lines
        if "/*" in line:
            while "*/" not in line:
                line = buff.readline()
                if buff.tell() >= buff_len:
                    break
            continue

        # RFEM indicates new table
        if RFEM_KEY in line:
            # Determine TX/RX by using the RFEM number
            rfem_num = int(re.search(RFEM_KEY + "(.*)", line).group(1).strip())
            if rfem_num % 2:
                type = "RX"
                is_Rx = 1
                module -= 1
            else:
                type = "TX"
                is_Rx = 0
            module_name = RFEM_KEY + str(rfem_num) + " " + type
            table_parts = get_table_parts(buff, buff_len, module, module_name, is_Rx)
            module += 1
            code_book_tables.append(table_parts)

    return code_book_tables


def get_table_parts(buff, buff_len, module, module_name, is_Rx):
    table_parts = {}
    table_parts["tableParts"] = []
    table_part_id = 0
    ant_wgt_code = []

    # Create 4 table parts from [64] beams
    while len(table_parts["tableParts"]) < NUM_TABLE_PARTS:
        while len(ant_wgt_code) < NUM_BEAMS_PER_TABLE_PART:
            line = buff.readline()

            # The input file provides 61 beams, fill last 3 beams with 0s
            if (
                len(table_parts["tableParts"]) == NUM_TABLE_PARTS - 1
                and len(ant_wgt_code) == NUM_BEAMS_PER_TABLE_PART - 3
            ):
                code = []
                for _ in range(0, NUM_ANT_ELEM_PER_BEAM):
                    code.append(0)
                for _ in range(0, 3):
                    ant_wgt_code.append(code)

            # Parse antenna elements
            elif VAL_KEY in line:
                code = re.search(VAL_KEY + "{(.*)}, },", line).group(1).strip()
                # Input has 17 elements, only use 16
                code = list(map(int, code.split(",")))[0:NUM_ANT_ELEM_PER_BEAM]
                ant_wgt_code.append(code)

            if buff.tell() >= buff_len:
                break

        table_part = {}
        table_part["rfModuleName"] = module_name
        table_part["module"] = module
        table_part["tablePartId"] = table_part_id
        table_part["isRx"] = is_Rx
        table_part["antWgtCode"] = ant_wgt_code

        table_parts["tableParts"].append(table_part)
        table_part_id += 1
        ant_wgt_code = []

        if buff.tell() >= buff_len:
            break

    return table_parts


if __name__ == "__main__":
    config_to_json()
