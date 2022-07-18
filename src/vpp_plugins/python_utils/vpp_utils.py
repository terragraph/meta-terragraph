#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from pathlib import Path
from typing import List


API_FILE_EXT = ".api.json"
# Directory containing all the JSON API files.
# If vpp is installed woth development packages on the system,
# these will be in /usr/share/vpp/api/
DEFAULT_JSON_BASE_PATH = Path("/usr/share/vpp/api")


def load_api_json(
    jsonfiles: List[Path], json_base_path: Path = DEFAULT_JSON_BASE_PATH
) -> None:
    """Recursive filesystem walker to get all the API JSON files"""
    for d in json_base_path.iterdir():
        if d.is_dir():
            load_api_json(jsonfiles, d)

        if d.is_file() and d.name.endswith(API_FILE_EXT):
            jsonfiles.append(d)
