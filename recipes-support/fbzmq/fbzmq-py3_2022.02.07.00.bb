DESCRIPTION = "Async framework and C++ wrapper over libzmq"
HOMEPAGE = "https://github.com/facebookincubator/fbzmq"
SECTION = "core"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${S}/../../LICENSE;md5=57a79768a3b1e4d0f3a3787048a96356"

include fbzmq_src.inc

S = "${WORKDIR}/git/fbzmq/py"

inherit setuptools3

DEPENDS += "fbthrift-compiler-native fbzmq"

RDEPENDS_${PN} += "fbzmq fbthrift-py3"
