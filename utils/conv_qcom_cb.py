#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# =============================================================================
#
#    This file contains all functions related to converting a per channel input
#    ini file to per channel JSON Codebook file
#
# =============================================================================

from __future__ import absolute_import, division, print_function, unicode_literals

import io
import json
import os

import click


brdName = "fw_cfg_ant_codebook_chn_1"
iniName = brdName + ".ini"
jsonName = brdName + ".json"


CHANNELS_KEY = ";;;;;;;;;;;;;;;Channels"
DATA_KEY = ";;data only"
SECTOR_KEY = ";;sector "

NUM_ANT_ELEM_PER_BEAM = 32
NUM_BEAMS_PER_TABLE_PART = 8


@click.command()
@click.option(
    "--config_file",
    "-f",
    default=iniName,
    type=str,
    help="ini file name to convert to json",
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
    "--output_file", "-o", default=jsonName, type=str, help="name of file to output to"
)
@click.pass_obj
def config_to_json(cli_opts, config_file, description, output_file):
    try:
        with open(config_file, "r") as content:
            buff = content.read()
    except Exception:
        print("Error reading config file: " + config_file)
        exit(0)

    data = {}
    module = 0
    module_name = ""
    type = ""
    data["Description"] = description
    code_book_tables = []
    tables = []
    buff = io.StringIO(buff)
    buff.seek(0, os.SEEK_END)
    buff_len = buff.tell()
    buff.seek(0, os.SEEK_SET)

    # Read through buffer and create json structure
    while True:
        line = buff.readline().rstrip()
        # Channels keyword indicates new RX/TX sector, determine RX/TX
        if CHANNELS_KEY in line:
            type = ""
            # Reset module number
            module = 0
            if "tx" in line:
                type = "TX"
            elif "rx" in line:
                type = "RX"
            else:
                print("Error: RX/TX not specified")
            module_name = get_module_name(buff, type)

        # Data keyword indicates a new table - create table and tables parts
        if DATA_KEY in line:
            module_name = line.replace(DATA_KEY + " - ", "") + " " + type
            is_RX = 1 if "RX" in type else 0
            table_parts = create_table_parts(buff, buff_len, module_name, module, is_RX)
            module += 1
            tables.append(table_parts)

        if buff.tell() >= buff_len:
            break

    # Sort it in order of rf0 TX, rf0 RX, rf1 TX, rf1 RX ...
    tables.sort(key=lambda x: x["tableParts"][0]["isRx"])
    tables.sort(key=lambda x: x["tableParts"][0]["module"])

    for table in tables:
        code_book_tables.append(table)
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


def get_module_name(buff, type):
    module_name = ""

    # Read through channel header
    while True:
        line = buff.readline().rstrip()
        if ";;section header" in line:
            module_name = buff.readline().rstrip()
            module_name = module_name.split(" = ")[-1].lstrip("0x")
            print(module_name)
            module_name = list(bytes.fromhex(module_name))
            module_name = str(sum(module_name)) + " " + type
            break
        if not line:
            break
    return module_name


def create_table_parts(buff, buff_len, module_name, module, is_RX):
    table_parts = {}
    table_part = {}
    ant_wgt_code = []
    codes = []

    table_parts["tableParts"] = []
    table_part["rfModuleName"] = module_name
    table_part["module"] = module
    table_part_id = 0
    table_part["tablePartId"] = table_part_id
    table_part["isRx"] = is_RX

    # Read through buffer to create table parts
    while True:
        pos = buff.tell()
        line = buff.readline().rstrip()
        line = line.replace("=", " = ")

        # Parse bytes from hex values
        if line.startswith("0x"):
            # Remove sector definition from the line
            if SECTOR_KEY in line:
                line = line.split(SECTOR_KEY)[0].rstrip()

            # Get the two hex strings from each side of equality
            hex_strings = line.split(" = ")
            for hex_str in hex_strings:
                hex_str = hex_str.lstrip("0x")
                while len(hex_str) < 8:
                    hex_str = "0" + hex_str

                bytes_from_hex = list(bytes.fromhex(hex_str))
                codes.extend(bytes_from_hex)

            if len(codes) >= NUM_ANT_ELEM_PER_BEAM:
                ant_wgt_code.append(codes)
                codes = []

        # Once ant_wgt_code is full, create a new table part
        if len(ant_wgt_code) >= NUM_BEAMS_PER_TABLE_PART:
            table_part["antWgtCode"] = ant_wgt_code
            table_parts["tableParts"].append(table_part)
            table_part_id += 1

            table_part = {}
            table_part["rfModuleName"] = module_name
            table_part["module"] = module
            table_part["tablePartId"] = table_part_id
            table_part["isRx"] = is_RX
            ant_wgt_code = []
            codes = []

        # If a new data key or channel key is found, the table is complete
        if DATA_KEY in line or CHANNELS_KEY in line:
            buff.seek(pos, os.SEEK_SET)
            break

        if buff.tell() >= buff_len:
            break
    return table_parts


if __name__ == "__main__":
    config_to_json()

print("Done")
