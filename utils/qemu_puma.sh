#!/bin/bash

# Run the ARM64 Puma terragraph image in a QEMU VM

# shellcheck disable=SC1090
. "$(dirname "$0")/qemu_common.sh"

IMAGES_PATH="build-qca/tmp/deploy/images/qoriq"

parse_args "$@"

IMAGE="$IMAGES_PATH/terragraph-image-qoriq.ext4.zst"
KERNEL="$IMAGES_PATH/fitImage-linux.bin-qoriq"

# Check if qemu is installed
if ! type qemu-system-aarch64 >/dev/null 2>&1; then
  echo "Can't find qemu. Please install via 'sudo dnf install gdisk \
       qemu-system-aarch64'" 1>&2
  exit 1
fi

check_files_exist

filter=$(get_image_filter "$IMAGE")

if [ "$KEEP_IMAGE" = "1" ]; then
  qemuimage="$IMAGES_PATH/qemu_puma.img"
else
  qemuimage=$(mktemp)
  trap 'rm -f $qemuimage' EXIT
fi

# mktemp creates a 0-byte file, so create a disk image if file doesn't exist
# or has size 0
if [ ! -s "$qemuimage" ]; then
  # Create disk image
  truncate -s +550M "$qemuimage"
  parted -s "$qemuimage" \
    mklabel GPT \
    mkpart rootfs ext4 2048s 500M \
    mkpart data ext4 500M 100%
fi
offset=$(parted -sm "$qemuimage" unit s print \
       | awk -F":" "/1:/{print substr(\$2, 1, length(\$2)-1)}")

# Uncompress/copy image into a partition in disk image
$filter "$IMAGE" | dd of="$qemuimage" bs=1M seek=$((offset * 512)) \
  oflag=seek_bytes conv=notrunc status=none || exit $?

# Host port for forwarding into guest port 22
port=$(find_host_port)

qemu-system-aarch64 \
  -M virt \
  -cpu cortex-a53 \
  -smp 4 \
  -m 3G \
  -kernel "$KERNEL" \
  -append "console=ttyS0 root=/dev/mmcblk0p1 rw loglevel=5 rootdelay=5" \
  -nographic \
  \
  -chardev stdio,id=console,mux=on,signal=off \
  -mon chardev=console,mode=readline \
  -serial chardev:console \
  -device pci-serial,chardev=console \
  \
  -netdev user,id=oob,hostfwd=tcp:127.0.0.1:"${port}"-:22 \
  -device virtio-net,netdev=oob,mac=52:54:00:00:00:0b,romfile= \
  \
  -drive id=disk,file="$qemuimage",format=raw,if=none \
  -device sdhci-pci -device sd-card,drive=disk,bus=sd-bus \
  \
  -device virtio-rng-pci,max-bytes=1024,period=1000
