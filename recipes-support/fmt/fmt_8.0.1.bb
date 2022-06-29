DESCRIPTION = "fmt"
HOMEPAGE = "https://github.com/fmtlib/fmt"
SECTION = "core"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE.rst;md5=af88d758f75f3c5c48a967501f24384b"

# 8.0.1
SRCREV = "d141cdbeb0fb422a3fb7173b285fd38e0d1772dc"
SRC_URI = "git://github.com/fmtlib/fmt;protocol=https;branch=master"

inherit cmake

S = "${WORKDIR}/git"

FILES_${PN} = "/usr"
SOLIBS = ".so"
FILES_SOLIBSDEV = ""
FILES_${PN}-dev = "${includedir}"

INSANE_SKIP_${PN} = "dev-so"

EXTRA_OECMAKE_append = " -DBUILD_SHARED_LIBS=ON"
EXTRA_OECMAKE_append = " -DFMT_DOC=OFF"
EXTRA_OECMAKE_append = " -DFMT_TEST=OFF"
EXTRA_OECMAKE_append = " -DCMAKE_CXX_STANDARD=17"

BBCLASSEXTEND = "native nativesdk"
