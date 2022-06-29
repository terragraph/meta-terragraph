# Base this image on core-image-minimal
require recipes-core/images/core-image-minimal.bb

# Enable systemd
DISTRO_FEATURES_append = " systemd"
VIRTUAL-RUNTIME_init_manager = "systemd"
DISTRO_FEATURES_BACKFILL_CONSIDERED = "sysvinit"
VIRTUAL-RUNTIME_initscripts = "systemd-compat-units"

# Include modules in rootfs
IMAGE_INSTALL += " \
    packagegroup-tg-e2e-controller \
    coreutils \
    os-release \
    "

require os-release-utils.inc

# Final touch of rootfs for the version #
ROOTFS_POSTPROCESS_COMMAND += "terragraph_build_info ; "
terragraph_build_info() {
    # Load the version information
    . ${IMAGE_ROOTFS}/etc/os-release

    # Create METADATA file name
    echo "Terragraph E2E Controller Rootfs Build Information" > ${IMAGE_ROOTFS}/METADATA
    echo >> ${IMAGE_ROOTFS}/METADATA

    # Add localhost name
    echo "build-user: ${USER}" >> ${IMAGE_ROOTFS}/METADATA
    echo "build-host: $(hostname)" >> ${IMAGE_ROOTFS}/METADATA
    echo "build-time: ${DATETIME_ISOFORMAT}" >> ${IMAGE_ROOTFS}/METADATA
    echo "commit-hash: ${VERSION}" >> ${IMAGE_ROOTFS}/METADATA

    # Force resolvconf update
    mkdir -p ${IMAGE_ROOTFS}/etc/network/if-up.d
    echo "# Update resolvers" >> ${IMAGE_ROOTFS}/etc/network/if-up.d/000resolvconf
    echo "resolvconf -u" >> ${IMAGE_ROOTFS}/etc/network/if-up.d/000resolvconf

    # Fix shell for systemd-nspawn
    if [ ! -f ${IMAGE_ROOTFS}/etc/inittab ]; then
        touch ${IMAGE_ROOTFS}/etc/inittab
    fi
    sed -i 's|tty1|/dev/console|' ${IMAGE_ROOTFS}/etc/inittab

    # Prevent constant respawning
    sed -i 's|^S0|#S0|' ${IMAGE_ROOTFS}/etc/inittab

    # Remove the init scripts we don't need in x86
    rm -f ${IMAGE_ROOTFS}/etc/rcS.d/{S04flash_mtd,S04udev,S05modutils.sh}
    rm -f ${IMAGE_ROOTFS}/etc/rcS.d/{S06checkroot.sh,S07bootlogd}
    rm -f ${IMAGE_ROOTFS}/etc/rcS.d/{S29read-only-rootfs-hook.sh,S36udev-cache}
    rm -f ${IMAGE_ROOTFS}/etc/rcS.d/{S37populate-volatile.sh,S38dmesg.sh}
    rm -f ${IMAGE_ROOTFS}/etc/rcS.d/{S55bootmisc.sh,S02sysfs.sh}
    rm -f ${IMAGE_ROOTFS}/etc/rc5.d/{S20hwclock.sh,S20ubootversion,S67oob_netns}
    rm -f ${IMAGE_ROOTFS}/etc/rc5.d/{S25tps23861-daemon}

    # Hack to remove the tests from the controller image.
    rm -rf ${IMAGE_ROOTFS}/usr/sbin/tests/

    # Create volatile folders
    mkdir -p ${IMAGE_ROOTFS}/var/volatile/{log,tmp}
    # Don't create an extra tmpfs for logs
    sed -i '/volatile/d' ${IMAGE_ROOTFS}/etc/fstab

    # install /data dirctory
    mkdir -p ${IMAGE_ROOTFS}/data
}

# Set en_US as the default locale
IMAGE_LINGUAS += "en-us"
