#!/bin/sh
# We try TG rootfs first then OpenR rootfs

export TG_ROOTFS_PATH='tmp/work/tgx86-poky-linux/terragraph-image-x86/1.0-r0/rootfs/'
export E2E_ROOTFS_PATH='tmp/work/tgx86-poky-linux/e2e-image/1.0-r0/rootfs/'

if [ -z "${MY_BUILD_DIR}" ]
then
  echo "!!! Cannot find the chroot base ..."
  echo "Set MY_BUILD_DIR to your x86 build dir and re-run."
  echo "e.g export MY_BUILD_DIR=~/meta-terragraph/build-x86/"
  exit 1
else
  echo "MY_BUILD_DIR is set to '$MY_BUILD_DIR'"
fi

if [ -d "${MY_BUILD_DIR}/${TG_ROOTFS_PATH}" ]; then
  export ROOTFS_PATH="${TG_ROOTFS_PATH}"
elif [ -d "${MY_BUILD_DIR}/${E2E_ROOTFS_PATH}" ]; then
  export ROOTFS_PATH="${E2E_ROOTFS_PATH}"
else
    echo "!!! Cannot find a root fs in ${MY_BUILD_DIR} ..."
    exit 2
fi

# Python multiprocess requires /dev/shm
setup_chroot_nods() {
  sudo chroot "${MY_BUILD_DIR}/${ROOTFS_PATH}" /bin/sh -c 'if [ ! -e /dev/null ]; then mknod -m 444 /dev/null c 1 3;fi'
  sudo chroot "${MY_BUILD_DIR}/${ROOTFS_PATH}" /bin/sh -c 'if [ ! -e /dev/random ]; then mknod -m 444 /dev/random c 1 8;fi'
  sudo chroot "${MY_BUILD_DIR}/${ROOTFS_PATH}" /bin/sh -c 'if [ ! -e /dev/urandom ]; then mknod -m 444 /dev/urandom c 1 9;fi'
  sudo chroot "${MY_BUILD_DIR}/${ROOTFS_PATH}" /bin/sh -c 'if [ ! -d /tmp ]; then mkdir /tmp;fi'
  sudo chroot "${MY_BUILD_DIR}/${ROOTFS_PATH}" /bin/sh -c 'if [ ! -d /dev/shm ]; then mkdir -p /dev/shm;fi'

  echo "Using rootfs : ${MY_BUILD_DIR}/${ROOTFS_PATH}"
}

run_in_chroot() {
  sudo mount --bind /dev/shm "${MY_BUILD_DIR}/${ROOTFS_PATH}/dev/shm"
  sudo mount --bind /proc "${MY_BUILD_DIR}/${ROOTFS_PATH}/proc"
  # Run passed in command
  sudo -H chroot "${MY_BUILD_DIR}/${ROOTFS_PATH}" "$@"
  exit_code=$?
  sleep 1  # Give time for resources to be released
  sudo umount -f "${MY_BUILD_DIR}/${ROOTFS_PATH}/dev/shm"
  sudo umount -f "${MY_BUILD_DIR}/${ROOTFS_PATH}/proc"
  exit ${exit_code}
}
