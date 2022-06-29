DESCRIPTION = "A C++14 library for JSON Web Tokens(JWT)"
HOMEPAGE = "https://github.com/arun11299/cpp-jwt"
SECTION = "core"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=f1f530286256df2a319a5d85c4d866e5"

SRC_URI = "git://github.com/arun11299/cpp-jwt.git;protocol=https"
SRCREV = "1cbc5eb5a54c45b79399483c4357e7e47100f474"

S = "${WORKDIR}/git"

DEPENDS += "openssl"
RDEPENDS_${PN} += "openssl"

do_install() {
    install -d ${D}${includedir}/jwt
    cp -R ${S}/include/jwt ${D}${includedir}
}

FILES_${PN}-dev = "/usr/include/jwt"
