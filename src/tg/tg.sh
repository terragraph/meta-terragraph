#!/bin/sh

# tg has been deployed in chroots in pre-docker builds
# This wrapper makes it easier to use ssh keys with the tg
# command while accessing the choot externally

. /etc/default/tg_services
COMMAND="$@"
 if [ -z "${SSH_AUTH_SOCK}" ]; then
  echo "SSH_AUTH_SOCK missing, this will probably fail, but trying anyway"
  chroot "${E2E_ROOTFS}" tg --no-log "${COMMAND}"
else
  DIR=$(dirname "${E2E_ROOTFS}/${SSH_AUTH_SOCK}")
  mkdir -p "${DIR}"
  ln "${SSH_AUTH_SOCK}" "${E2E_ROOTFS}/${SSH_AUTH_SOCK}"
  chroot "${E2E_ROOTFS}" /usr/bin/env -i SSH_AUTH_SOCK="${SSH_AUTH_SOCK}" tg --no-log "${COMMAND}"
  # could rm -rf, but I'd rather not delete the box in a weird edge case
  rm -f "${E2E_ROOTFS}/${SSH_AUTH_SOCK}"
  rmdir -p "${DIR}" > /dev/null 2>&1
fi
