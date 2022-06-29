inherit image_types

# Base this image on core-image-minimal
require recipes-core/images/core-image-minimal.bb

DEPENDS += "mkupdateimg-native"

# Include modules in rootfs
# Specific platforms append a wireless-fw and routing packagegroup.
IMAGE_INSTALL += " \
    packagegroup-tg-e2e-node \
    packagegroup-tg-facebook \
    packagegroup-tg-utils \
    os-release \
    "
IMAGE_FEATURES += "ssh-server-openssh package-management"

IMAGE_FSTYPES_append_armada39x += "ubi"
IMAGE_FSTYPES_append_qoriq += "ext4.zst squashfs-zstd"
ZSTD_COMPRESSION_LEVEL = "-19"
# Override default overhead factor 1.3 -> 1.0
IMAGE_OVERHEAD_FACTOR_qoriq = "1"
# Extra space in KB for the rootfs filesystem and post-process steps. 20MB
IMAGE_ROOTFS_EXTRA_SPACE_qoriq = "20480"
# Set filesystem journal size to 8MB
EXTRA_IMAGECMD_ext4="-J size=8"

ROOTFS_PKGMANAGE_armada39x = "rpm"

# Zstd support for squashfs
do_image_squashfs_zstd[depends] += "squashfs-tools-native:do_populate_sysroot"
IMAGE_CMD_squashfs-zstd = "mksquashfs ${IMAGE_ROOTFS} ${IMGDEPLOYDIR}/${IMAGE_NAME}${IMAGE_NAME_SUFFIX}.squashfs-zstd ${EXTRA_IMAGECMD} -noappend -comp zstd"

# mkfs.ubifs
# -e = logical erase block size
# -m = minimum I/O unit size
# -c = maximum logical erase block count
# -F = file-system free space has to be fixed up on first mount
#
# NAND page size  =   4k
#      block size = 256k
# PEB (Physical Erase Block) = 256k
# LEB (Logical Erase Block)  = PEB - (PG_SIZE * 2)
# (256 * 1024) - (4 * 1024 * 2) = 253952
MKUBIFS_ARGS += "-e 253952 -m 4096 -c 3072 -F"

# ubinize
# -p = size of the physical eraseblock of the flash this UBI image
#      is created for
# -m = minimum input/output unit size of the flash in bytes
UBINIZE_ARGS += "-p 256KiB -m 4096"

hostname_pn-base-files = "terragraph"

# oe_mkimage() was defined in image_types_uboot. Howver, it does not consider
# the image load address and entry point. Override it here.
oe_mkimage () {
    mkimage -A ${UBOOT_ARCH} -O linux -T ramdisk -C $2 -n ${IMAGE_NAME} \
        -a ${UBOOT_IMAGE_LOADADDRESS} -e ${UBOOT_IMAGE_ENTRYPOINT} \
        -d ${DEPLOY_DIR_IMAGE}/$1 ${DEPLOY_DIR_IMAGE}/$1.u-boot
}

UBOOT_IMAGE_ENTRYPOINT ?= "0x50000000"
UBOOT_IMAGE_LOADADDRESS ?= "${UBOOT_IMAGE_ENTRYPOINT}"

TG_HOSTNAME = "terragraph"

DATETIME_COPY="${DATETIME}"
DATETIME_COPY[vardepsexclude] = "DATETIME"

# Final touch of rootfs for the version #
ROOTFS_POSTPROCESS_COMMAND_append += "terragraph_rootfs_fixup ; "
terragraph_rootfs_fixup() {
    # Fix the hostname
    echo ${TG_HOSTNAME} > ${IMAGE_ROOTFS}/etc/hostname

    . ${IMAGE_ROOTFS}/etc/os-release
    echo "${PRETTY_NAME} \\n \\l " > ${IMAGE_ROOTFS}/etc/issue
    echo >> ${IMAGE_ROOTFS}/etc/issue
    echo "${PRETTY_NAME} %h " > ${IMAGE_ROOTFS}/etc/issue.net
    echo >> ${IMAGE_ROOTFS}/etc/issue.net

    # Write build date to /etc/timestamp
    # (default is ${REPRODUCIBLE_TIMESTAMP_ROOTFS} from rootfs_update_timestamp)
    echo "${DATETIME_COPY}" > ${IMAGE_ROOTFS}/etc/timestamp

    # Force resolvconf update
    echo "# Update resolvers" >> ${IMAGE_ROOTFS}/etc/network/if-up.d/000resolvconf
    echo "resolvconf -u" >> ${IMAGE_ROOTFS}/etc/network/if-up.d/000resolvconf

    # runsv needs to write to /etc/sv and since /etc/sv is on read only
    # partition we can't do this.  So instead put the scripts in /etc/sv.bak.
    # At run time we copy /etc/sv.bak to /var/run/sv and
    # symbolic link below from /etc/sv to /var/run/sv makes it writable
    /bin/mv ${IMAGE_ROOTFS}/etc/sv ${IMAGE_ROOTFS}/etc/sv.bak
    /bin/ln -s /var/run/sv ${IMAGE_ROOTFS}/etc/sv

    # we need to modify the hosts file to add hostnames to work without DNS
    # So we back /etc/hosts at /etc/hosts.bak as it is read only partition
    # At run time we copy /etc/hosts.bak to /var/run/hosts and
    # symbolic link below from /etc/hosts to /var/run/hosts makes it writable
    /bin/mv ${IMAGE_ROOTFS}/etc/hosts ${IMAGE_ROOTFS}/etc/hosts.bak
    /bin/ln -s /var/run/hosts ${IMAGE_ROOTFS}/etc/hosts
}

# Rocko uses IMGDEPLOYDIR but Krogoth does not.  Set it conditionally so both work.
IMGDEPLOYDIR ?= "${DEPLOY_DIR_IMAGE}"

### auto calculate md5 checksum on flash image ###

IMAGE_POSTPROCESS_COMMAND_append_armada39x = " flash_ubi_image_md5_generate ; flash_ubi_image_meta_generate ; flash_ubi_update_image_generate ;"
IMAGE_POSTPROCESS_COMMAND_append_qoriq  = " flash_ext4_image_md5_generate ; flash_ext4_image_meta_generate ; flash_ext4_update_image_generate ;"

flash_update_image_generate() {
  image_ext=$1
  image_type=$2

  image_link="${IMGDEPLOYDIR}/${PN}-${MACHINE}.${image_ext}"
  image_file="${IMGDEPLOYDIR}/"$(readlink ${image_link})

  image_prefix=$(echo ${PN} | sed 's/terragraph-image/tg-update/')
  update_image_link="${IMGDEPLOYDIR}/${image_prefix}-${MACHINE}.bin"
  update_image_file=$(echo ${image_file} | sed 's,'"${image_ext}"'$,bin,')

  mkimg="mkupdateimg.sh"
  "${mkimg}" -t "${image_type}" -i "${image_file}" -o "${update_image_file}"

  # Add symlink
  ln -fs $(basename "${update_image_file}") "${update_image_link}"
}

flash_ext4_update_image_generate() {
  flash_update_image_generate "ext4.zst" "ext4fs"
}

flash_ubi_update_image_generate() {
  flash_update_image_generate "ubi" "ubifs"
}

flash_image_md5_generate() {
  image_ext=$1

  # Get symbolic link to the flash image (ubi file)
  image_link="${IMGDEPLOYDIR}/${PN}-${MACHINE}.${image_ext}"
  image_link_md5="${image_link}.md5"

  # Find the current image pointed to by the symbolic link
  image_file="${IMGDEPLOYDIR}/"$(readlink ${image_link})
  image_file_md5="${image_file}.md5"

  # Calculate md5 checksum on the current image
  md5sum ${image_file} | awk '{print $1}' > ${image_file_md5}

  # Point md5 symbolic link to this checksum
  ln -fs $(basename ${image_file_md5}) ${image_link_md5}
}

flash_ext4_image_md5_generate() {
  flash_image_md5_generate "ext4.zst"
}

flash_ubi_image_md5_generate() {
  flash_image_md5_generate "ubi"
}

flash_image_meta_generate() {
  image_ext=$1
  meta_model=$2
  meta_board_ids=$3

  # Get symbolic link to the flash image (ubi file)
  image_link="${IMGDEPLOYDIR}/${PN}-${MACHINE}.${image_ext}"
  image_link_meta="${image_link}.meta"

  # Find the current image pointed to by the symbolic link
  image_file="${IMGDEPLOYDIR}/"$(readlink ${image_link})
  image_file_meta="${image_file}.meta"

  # Generate meta file in json format

  # Calculate md5 checksum on the current image
  meta_md5=$(md5sum ${image_file} | awk '{print $1}')
  # Get version of current image
  meta_version=$(cat ${IMAGE_ROOTFS}/etc/tgversion)

  echo "{" > ${image_file_meta}
  echo "\"version\":\"${meta_version}\"," >> ${image_file_meta}
  echo "\"md5\":\"${meta_md5}\"," >> ${image_file_meta}
  echo "\"model\":\"${meta_model}\"," >> ${image_file_meta}
  echo "\"hardwareBoardIds\":[${meta_board_ids}]" >> ${image_file_meta}
  echo "}" >> ${image_file_meta}

  # Point meta symbolic link to meta file
  ln -fs $(basename ${image_file_meta}) ${image_link_meta}
}

flash_ext4_image_meta_generate() {
  flash_image_meta_generate \
    "ext4.zst" \
    "NXP TG Board" \
    '"NXP_LS1048A_PUMA"'
}

flash_ubi_image_meta_generate() {
  flash_image_meta_generate \
    "ubi" \
    "Facebook Terragraph Digital Board EVT" \
    '"MVL_ARMADA39X_P","MVL_ARMADA39X_S"'
}

# Set en_US as the default locale
IMAGE_LINGUAS += "en-us"
