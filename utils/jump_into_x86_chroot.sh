#!/bin/sh

COMMON_LIB="$(dirname "$0")/chroot_common.sh"
# shellcheck source=/dev/null
. "$COMMON_LIB"

setup_chroot_nods
run_in_chroot "$SHELL"
