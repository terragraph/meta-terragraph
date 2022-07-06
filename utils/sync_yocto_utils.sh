#!/bin/bash

POKY_COMMIT="bba323389749ec3e306509f8fb12649f031be152"
OE_COMMIT="ec978232732edbdd875ac367b5a9c04b881f2e19"
BRANCH="dunfell"

# Meta-internal source_mirrors location, change this as needed
SOURCE_MIRRORS_DIR="/mnt/gvfs/yocto/source_mirrors/04d5c868a31bbd7ea0a0795aeb65acf2202e8008"

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

  # Make source_mirrors for any extra blobs required.
  # If $SOURCE_MIRRORS_DIR exists, symlink to it, otherwise create an empty dir.
  if [ -L ./yocto/source_mirrors ]; then
    rm -v ./yocto/source_mirrors
  fi
  if [ -d "$SOURCE_MIRRORS_DIR" ]; then
    ln -v -s "$SOURCE_MIRRORS_DIR" ./yocto/source_mirrors
  fi
  if [ ! -e ./yocto/source_mirrors ]; then
    mkdir -v ./yocto/source_mirrors
  fi

  # On Meta-internal servers, also install "facebook.enabled" symlink
  if [ -d "$SOURCE_MIRRORS_DIR" ] && [ ! -e ./facebook.enabled ]; then
    ln -v -s ./facebook ./facebook.enabled
  fi
}
