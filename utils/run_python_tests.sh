#!/bin/sh

if [ "$MY_BUILD_DIR" = "" ]; then
    if [ "${BOX_DIR}" = "" ]; then
        PWD=$(pwd)
        export MY_BUILD_DIR="${PWD}/build-x86"
    else
        export MY_BUILD_DIR="${BOX_DIR}/build-x86"
    fi
    echo "Setting MY_BUILD_DIR to ${MY_BUILD_DIR}"
fi

COMMON_LIB="$(dirname "$0")/chroot_common.sh"
TLS_CERTS="/etc/pki"
# shellcheck source=/dev/null
. "$COMMON_LIB"

# Copy system /etc/resolv.conf so DNS works
sudo cp -pv /etc/resolv.conf "${MY_BUILD_DIR}/${ROOTFS_PATH}/etc/"

if [ "$PYPI_DOMAIN" = "" ] && [ -s /etc/fbwhoami ] ; then
    echo "Since we're a FB Host lets use use a proxy"
    export https_proxy="http://fwdproxy:8080"
fi

# If we have custom pki certs, lets include them
if [ -d ${TLS_CERTS} ]; then
    echo "Copying custom TLS certs to ${MY_BUILD_DIR}/${ROOTFS_PATH}"
    sudo cp -rp "${TLS_CERTS}" "${MY_BUILD_DIR}/${ROOTFS_PATH}/etc/"
fi

# Copy source code into the chroot for ptr to run over
CHROOT_REPO_COPY="${MY_BUILD_DIR}/${ROOTFS_PATH}/repo"
if [ ! -d "$CHROOT_REPO_COPY" ]; then
    mkdir -p "$CHROOT_REPO_COPY"
fi

REPO_BASE="$(dirname "$(dirname "$0")")"
for adir in "$REPO_BASE/.flake8" "$REPO_BASE/.ptrconfig" "$REPO_BASE/facebook" "$REPO_BASE/meta*" \
            "$REPO_BASE/recipes*" "$REPO_BASE/src" "$REPO_BASE/utils"
do
    cp -r "$adir" "$CHROOT_REPO_COPY"
done

setup_chroot_nods
run_in_chroot "/usr/sbin/run_ptr.sh" "$1"
