#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

NAME_MATCH=".*"
TEST_DIR="/usr/sbin/tests"
GCOV=0
CONTINUE_ON_ERROR=0
ERROR_LIST=""
OPENR=0

while [ $# -gt 0 ]
do
  case $1 in
  --help|-h)
    echo "Usage: run_tests.sh [--help] [--continue] [--gcov] [--openr] [--testdir <dir>] <filter>"
    echo "Run the terragraph unit tests."
    echo "  --help:     Show this message"
    echo "  --continue: Continue past test failures"
    echo "  --openr:    Run openr tests"
    echo "  --gcov:     Run gcoverage tests"
    echo "  --testdir:  Run the tests found in <dir>"
    echo "  <filter>:   Optional filter for which tests to run"
    exit 0
    ;;
  --testdir|-d)
    shift
    TEST_DIR=$1
    ;;
  --gcov|-g)
    GCOV=1
    ;;
  --openr)
    echo "Enabled OpenR tests due to CLI argument"
    OPENR=1
    ;;
  --continue|-c)
    CONTINUE_ON_ERROR=1
    ;;
  *)
    NAME_MATCH="$1"
    ;;
  esac
  shift
done

if [ "$GCOV" = "1" ] && [ -e /etc/gcov.env ]; then
  . /etc/gcov.env
fi

if ! [ -d "$TEST_DIR" ]; then
  echo "Unable to locate test directory $TEST_DIR."
  echo "Specify a valid directory for the tests with the --testdir option"
fi

# TODO: Enable Open/R's prefix_allocator_test if it's ever fixed
TESTS=$(find "$TEST_DIR" -name "*_test*" | grep "$NAME_MATCH" | grep -v 'prefix_allocator_test')

echo
echo "Found the following tests:"
echo
echo "$TESTS"

FAILCOUNT=0
for i in ${TESTS}
do
  # Only run OpenR tests if asked
  if echo "$i" | grep -iq openr && [ "${OPENR}" = "0" ]
  then
    echo "!-> Skipping OpenR test: ${i}"
    continue
  fi

  if [ -f "$i" ] && [ -x "$i" ]; then
    echo "====================================="
    echo "Running Test: $i"
    echo "====================================="
    echo
    $i
    if [ "$?" != "0" ]; then
      if [ "$CONTINUE_ON_ERROR" = "0" ]; then
        echo
        echo "RUN_TESTS STOPPED ON FAILURE OF $i"
        echo
        exit 1
      else
        FAILCOUNT=$((FAILCOUNT+1))
        ERROR_LIST="${ERROR_LIST} $i"
      fi
    fi
    echo
  fi
done
echo "====================================="
if [ "${FAILCOUNT}" = "0" ]; then
  echo "All Tests PASSED"
else
  echo "${FAILCOUNT} Tests FAILED"
  for e in ${ERROR_LIST}
  do
    echo "  $e"
  done
  exit 1
fi
echo "====================================="
echo
