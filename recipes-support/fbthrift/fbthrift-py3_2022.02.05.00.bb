DESCRIPTION = "Facebook Thrift Python 3 Libraries"
HOMEPAGE = "https://github.com/facebook/fbthrift"
SECTION = "core"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://${S}/../../../LICENSE;md5=fa259dfc0c8b1f07957c1760a7553d1c"

include fbthrift_src.inc

DEPENDS += "fbthrift"
RDEPENDS_${PN} += "fbthrift python3-asyncio python3-threading python3-six"

inherit setuptools3

S = "${WORKDIR}/git/thrift/lib/py"
