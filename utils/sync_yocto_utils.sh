#!/bin/bash

POKY_COMMIT="bba323389749ec3e306509f8fb12649f031be152"
OE_COMMIT="ec978232732edbdd875ac367b5a9c04b881f2e19"
BRANCH="dunfell"

sync_yocto() {
  POKY_REPO_URI=$1
  OE_REPO_URI=$2

  if [ ! -d ./yocto ]; then
    mkdir ./yocto
  fi

  # Remove the previous copies.
  if [ -d ./yocto/poky ]; then
    rm -rf ./yocto/poky
  fi
  if [ -d ./yocto/meta-openembedded ]; then
    rm -rf ./yocto/meta-openembedded
  fi

  # Clone the new ones
  git clone -b "${BRANCH}" "${POKY_REPO_URI}" yocto/poky
  (cd yocto/poky && git reset --hard "${POKY_COMMIT}")

  git clone -b "${BRANCH}" "${OE_REPO_URI}" yocto/meta-openembedded
  (cd yocto/meta-openembedded && git reset --hard "${OE_COMMIT}")

  # Make a blank source_mirrors for any extra blobs required
  if [ ! -e ./yocto/source_mirrors ]; then
    mkdir ./yocto/source_mirrors
  fi
}
