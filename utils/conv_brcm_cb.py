#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from __future__ import absolute_import, division, print_function, unicode_literals

import copy
import io
import json
import os

import click


# Usage:
#   for all 3 channels: `python conv_brcm_cb.py -f chscan_vbs/`
#   for a specified channel: `python conv_brcm_cb.py -f chscan_vbs/ -ch 1`
# Help: `python conv_brcm_cb.py --help`

BOTTOM_KEY = "BOTTOMSLAVE"
MASTER_KEY = "MASTER"
TOP_KEY = "TOPSLAVE"

NUM_ANT_ELEM_PER_BEAM = 16
NUM_BEAMS_PER_TABLE_PART = 16
MAX_NUM_CHANNELS = 3
TOP_SLAVE_ID_NUM = 22
MASTER_ID_NUM = 29
BOTTOM_SLAVE_ID_NUM = 25
NUM_COPIES_OF_SLAVE = 2


@click.command()
@click.option(
    "--folder_path",
    "-f",
    type=str,
    required=True,
    help="folder containing files to convert to json",
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
    default="brcm_output.json",
    type=str,
    help="name of file to output to",
)
@click.option(
    "--channel",
    "-ch",
    default=0,
    type=int,
    help="Channel to use, 0 to parse channels 1-3 in folder",
)
@click.pass_obj
def process_cli(cli_opts, folder_path, description, output_file, channel):
    if not folder_path.endswith("/"):
        folder_path += "/"
    if not output_file.endswith(".json"):
        output_file += ".json"

    # Parse all 3 channels if channel is not specified
    if channel <= 0:
        for ch in range(1, MAX_NUM_CHANNELS + 1):
            output_filename = output_file.rstrip(".json") + "_ch" + str(ch) + ".json"
            config_to_json(folder_path, description, output_filename, ch)
    else:
        config_to_json(folder_path, description, output_file, channel)


def config_to_json(folder_path, description, output_file, channel):
    filenames = os.listdir(folder_path)
    topslave_files = []
    master_files = []
    bottomslave_files = []
    module = 0

    # Based on channel, collect the files to be parsed
    for filename in filenames:
        if "CH" + str(channel) in filename:
            if BOTTOM_KEY in filename:
                bottomslave_files.append(folder_path + filename)
            elif MASTER_KEY in filename:
                master_files.append(folder_path + filename)
            elif TOP_KEY in filename:
                topslave_files.append(folder_path + filename)

    # Parse files and order them appropriately
    (top_code_book_tables, module) = create_slaves_code_book_tables(
        topslave_files, module, TOP_SLAVE_ID_NUM
    )

    master_files.sort(reverse=True)
    master_code_book_tables = create_code_book_tables(master_files, module)
    module += 1

    (bottom_code_book_tables, module) = create_slaves_code_book_tables(
        bottomslave_files, module, BOTTOM_SLAVE_ID_NUM
    )

    code_book_tables = top_code_book_tables
    code_book_tables.extend(master_code_book_tables)
    code_book_tables.extend(bottom_code_book_tables)

    data = {}
    data["Description"] = description + " for Channel " + str(channel)
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


def create_slaves_code_book_tables(files, module, start_num):
    # Sort files to parse TX then RX
    files.sort(reverse=True)
    code_book_tables = create_code_book_tables(files, module)
    module += 1

    # Make two copies of the code_book_tables for Slave 23/24 or Slave 26/27
    table_copies = []
    for i in range(1, NUM_COPIES_OF_SLAVE + 1):
        code_book_tables_copy = copy.deepcopy(code_book_tables)
        for table_parts in code_book_tables_copy:
            for table_part in table_parts["tableParts"]:
                if "TX" in table_part["rfModuleName"]:
                    module_name = "SLAVE " + str(start_num + i) + " TX"
                elif "RX" in table_part["rfModuleName"]:
                    module_name = "SLAVE " + str(start_num + i) + " RX"
                table_part["module"] = module
                table_part["rfModuleName"] = module_name
        table_copies.append(code_book_tables_copy)
        module += 1
    for table in table_copies:
        code_book_tables.extend(table)
    return (code_book_tables, module)


def create_code_book_tables(filenames, module):
    code_book_tables = []
    # Create tables from each file
    for filename in filenames:
        try:
            with open(filename, "r") as content:
                buff = content.read()
        except Exception:
            print("Error reading config file: " + filename)
            continue

        buff = io.StringIO(buff)
        buff.seek(0, os.SEEK_END)
        buff_len = buff.tell()
        buff.seek(0, os.SEEK_SET)

        table_parts = get_table_parts(filename, buff, buff_len, module)
        code_book_tables.append(table_parts)
    return code_book_tables


def get_table_parts(filename, buff, buff_len, module):
    table_parts = {}
    table_parts["tableParts"] = []
    module_name = ""
    is_RX = 0

    # Determine the rfModuleName and isRx
    if "TX_" + BOTTOM_KEY in filename:
        module_name = "SLAVE " + str(BOTTOM_SLAVE_ID_NUM) + " TX"
    elif "RX_" + BOTTOM_KEY in filename:
        module_name = "SLAVE " + str(BOTTOM_SLAVE_ID_NUM) + " RX"
        is_RX = 1
    elif "TX_" + TOP_KEY in filename:
        module_name = "SLAVE " + str(TOP_SLAVE_ID_NUM) + " TX"
    elif "RX_" + TOP_KEY in filename:
        module_name = "SLAVE " + str(TOP_SLAVE_ID_NUM) + " RX"
        is_RX = 1
    elif "TX_" + MASTER_KEY in filename:
        module_name = "MASTER " + str(MASTER_ID_NUM) + " TX"
    elif "RX_" + MASTER_KEY in filename:
        module_name = "MASTER " + str(MASTER_ID_NUM) + " RX"
        is_RX = 1

    ant_wgt_code = []
    table_part_id = 0

    # Read through buff to parse table parts
    while buff.tell() < buff_len:
        table_part = {}
        table_part["rfModuleName"] = module_name
        table_part["module"] = module
        table_part["tablePartId"] = table_part_id
        table_part["isRx"] = is_RX

        # Once ant_wgt_code is full, create a new table part
        while len(ant_wgt_code) < NUM_BEAMS_PER_TABLE_PART:
            line = buff.readline()
            code = list(map(int, line.split(",")))
            ant_wgt_code.append(code)
            if buff.tell() >= buff_len:
                break
        table_part["antWgtCode"] = ant_wgt_code
        table_part_id += 1
        ant_wgt_code = []
        table_parts["tableParts"].append(table_part)
    return table_parts


if __name__ == "__main__":
    process_cli()
