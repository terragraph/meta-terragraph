#!/bin/bash

usage() {
  exec 1>&2
  echo "Usage: $0 [options]"
  echo "  -p DIR   Path to images directory, containing kernel and image binaries. Defaults to"
  echo "           $IMAGES_PATH"
  echo "  -k       Keeps the image for reuse of the data partition. The rootfs is still"
  echo "           overwritten with each run. Defaults to off."
  echo ""
  echo "To SSH into the VM, use 'ssh -p2022 root@localhost'."
  echo "Note that a different port may be assigned if 2022 is already used,"
  echo "see message at VM boot time."
  echo ""
  echo "To exit from QEMU, use 'poweroff' to shut down the VM."
  echo "For forceful shutdown, use 'Ctrl+a x'. If running within screen/tmux"
  echo "with Ctrl+a as the command key, use 'Ctrl+a a x' instead."
  exit 1
}

# Find an unused host port for forwarding
find_host_port() {
  port=2022
  while [ $port -lt 65535 ]; do
    if ! netstat -4ant | awk '{print $4}' | grep -q ":$port"; then
      echo "Host port $port forwarded to VM port 22" 1>&2
      echo $port
      return
    fi
    port=$((port + 1000))
  done
  echo "Can't find an unused port, letting qemu choose." 1>&2
  echo "Run the following command to find it:" 1>&2
  echo "netstat -lntp | grep \$(pgrep -P $$ qemu)/" 1>&2
  echo 0
}

parse_args() {
  while getopts "p:k" arg; do
    case "$arg" in
      p)
        IMAGES_PATH="$OPTARG"
        ;;
      k)
        # shellcheck disable=SC2034
        KEEP_IMAGE=1
        ;;
      *)
        usage
        ;;
    esac
  done

  shift $((OPTIND-1))

  if [ "$#" -ne "0" ]; then
    usage
  fi
}

# Try loading KVM modules, don't fail if unsuccessful
load_kvm() {
  sudo modprobe kvm >/dev/null 2>&1
  sudo modprobe kvm_intel >/dev/null 2>&1
}

check_files_exist() {
  if [ ! -e "$IMAGE" ]; then
    echo "Image $IMAGE not found" 1>&2
    exit 1
  fi

  if [ ! -e "$KERNEL" ]; then
    echo "Kernel $KERNEL not found" 1>&2
    exit 1
  fi
}

# Decide whether and how to uncompress based on extension
get_image_filter() {
  if [ "${1%.gz}" != "$1" ]; then
    filter="zcat"
  elif [ "${1%.zst}" != "$1" ]; then
    filter="zstdcat"
  else
    filter="cat"
  fi
  echo "$filter"
}
