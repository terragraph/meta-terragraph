#!/bin/bash

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

#
# Display watchdogs states from /var/volatile/progress directory
# refresh states every second and display content of data files.
#

# Get the configuration vars
. /etc/monit_config.sh

dir=$progress_dir watch -t -n 1 ' 
  echo "Monotonic seconds: $(monotonic-touch -t)"
  printf "%-40s %-12s [%s]\n" "file name" "last mod" "content"
  echo "---------------------------------------------------------------"
  for file in $(find $dir -type f | sort)
  do
    time=$(stat -c "%Y" "$file")
    short=${file#$dir/}
    test -s "$file" && data=$(head -c 10 "$file") || data=""
    printf "%-40s %-12d [%s]\n" "$short" $time "$data"
  done
'
