DESCRIPTION = "Simple, secure & standards compliant web server for the most demanding of applications."
HOMEPAGE = "https://github.com/uNetworking/uWebSockets"
SECTION = "core"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=86d3f3a95c324c9479bd8986968f4327"

SRCREV = "8dfabd559d4a54f29bd04d397223da0140610c8e"
SRC_URI = "git://github.com/uNetworking/uWebSockets.git;protocol=https;branch=master \
          "

S = "${WORKDIR}/git"

DEPENDS += "usockets"

# include openssl support during build
EXTRA_OEMAKE_append = " WITH_OPENSSL=1"
# include path to uSockets library during build
EXTRA_OEMAKE_append = " -I${includedir}/uSockets"

do_install () {
    oe_runmake install DESTDIR=${D}
}
