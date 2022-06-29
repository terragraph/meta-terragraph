#!/bin/bash

set -e
source utils/sync_yocto_utils.sh

sync_yocto \
  "https://git.yoctoproject.org/git/poky" \
  "https://github.com/openembedded/meta-openembedded.git"

# The new kernel fitimage unit patch patch in Dunfell breaks Puma uboot.
# Just revert it for now to pick up all of the other CVE fixes.
(cd yocto/poky && patch -p1 < ../../utils/0001-Revert-kernel-fitimage-Don-t-use-unit-addresses-on-F.patch)
