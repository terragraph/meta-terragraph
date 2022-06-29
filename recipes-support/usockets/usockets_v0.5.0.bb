DESCRIPTION = "Cross-platform async networking and eventing foundation library used by µWebSockets."
HOMEPAGE = "https://github.com/uNetworking/uSockets"
SECTION = "core"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=86d3f3a95c324c9479bd8986968f4327"

SRCREV = "577c822ac2e27d297eb6ea8712c84db5bbfe3b44"
SRC_URI = "git://github.com/uNetworking/uSockets.git;protocol=https;branch=master \
          "

S = "${WORKDIR}/git"

DEPENDS += "openssl"
RDEPENDS_${PN} += "openssl"

# include openssl support during build
EXTRA_OEMAKE_append = " WITH_OPENSSL=1"

do_install () {
    # install the uSockets objects that uWebSockets relies on
    # and the header files
    install -d ${D}${includedir}/uSockets
    install -d ${D}${libdir}
    # copy uSockets source/library into system include dir
    # using cp - install doesn't do recursive copies and there's no make
    # install in the Makefile
    cp -R ${S}/src/* ${D}${includedir}/uSockets
    # copy uSockets static lib into system lib dir
    install -D ${S}/uSockets.a ${D}${libdir}
}

FILES_${PN} += "${includedir} ${libdir}"
