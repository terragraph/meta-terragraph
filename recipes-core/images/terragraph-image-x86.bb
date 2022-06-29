# Base this image on core-image-minimal
require recipes-core/images/core-image-minimal.bb

# Include modules in rootfs
IMAGE_INSTALL += " \
    packagegroup-tg-e2e-node \
    packagegroup-tg-e2e-controller \
    packagegroup-tg-facebook \
    packagegroup-tg-testing \
    packagegroup-tg-utils \
    os-release \
    "

require os-release-utils.inc

# Final touch of rootfs for the version #
ROOTFS_POSTPROCESS_COMMAND += "terragraph_build_info ; "
terragraph_build_info() {
    # Load the version information
    . ${IMAGE_ROOTFS}/etc/os-release

    # Create METADATA file name
    echo "Terragraph Rootfs Build Information" > ${IMAGE_ROOTFS}/METADATA
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

    # install /data dirctory
    mkdir -p ${IMAGE_ROOTFS}/data
}

# Set en_US as the default locale
IMAGE_LINGUAS += "en-us"
