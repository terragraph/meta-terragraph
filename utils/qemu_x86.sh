#!/bin/bash

# Run the x86 terragraph image in a QEMU VM

# shellcheck disable=SC1090
. "$(dirname "$0")/qemu_common.sh"

IMAGES_PATH="build-x86/tmp/deploy/images/tgx86"

parse_args "$@"

IMAGE="$IMAGES_PATH/terragraph-image-x86-tgx86.ext4"
KERNEL="$IMAGES_PATH/bzImage"

# Check if qemu is installed
if ! type qemu-system-x86_64 >/dev/null 2>&1; then
  echo "Can't find qemu. Please install via 'sudo yum install qemu-system-x86'" 1>&2
  exit 1
fi

load_kvm

check_files_exist

filter=$(get_image_filter "$IMAGE")

tmpimage=$(mktemp)
trap 'rm -f $tmpimage' EXIT

# Uncompress/copy image to temp file
$filter "$IMAGE" > "$tmpimage" || exit $?

# Host port for forwarding into guest port 22
port=$(find_host_port)

qemu-system-x86_64 \
    -M pc,accel=kvm:tcg \
    -m 2G \
    -kernel "$KERNEL" \
    -append "console=ttyS0 root=/dev/sda rw loglevel=5" \
    -nographic \
    -netdev user,id=oob,hostfwd=tcp:127.0.0.1:"${port}"-:22 \
    -device e1000,netdev=oob,mac=52:54:00:00:00:0b,romfile= \
    -drive id=disk,file="$tmpimage",format=raw,if=none \
    -device ahci,id=ahci -device ide-drive,drive=disk,bus=ahci.0
