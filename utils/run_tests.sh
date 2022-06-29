#!/bin/sh

COMMON_LIB="$(dirname "$0")/chroot_common.sh"
# shellcheck source=/dev/null
. "$COMMON_LIB"

setup_chroot_nods

echo "Ensuring /sbin and /bin are in PATH"
export PATH="$PATH:/sbin:/bin"

RUN_TEST_ARGS=""
# Look at `git show` output and if OpenR is mentioned run it's tests
git show -s | grep -Eiq 'open/?r'
# shellcheck disable=SC2181
if [ $? -eq 0 ]; then
    echo "Found OpenR mentioned in latest diff - Running tests ..."
    RUN_TEST_ARGS="--openr"
fi

run_in_chroot "/usr/sbin/run_tests.sh" "$RUN_TEST_ARGS" "$@"
