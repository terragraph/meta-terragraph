DESCRIPTION = "RE2 is a software library for regular expressions"
HOMEPAGE = "https://github.com/google/re2"
SECTION = "core"
LICENSE = "BSD"
LIC_FILES_CHKSUM = "file://LICENSE;md5=3b5c31eb512bdf3cb11ffd5713963760"

SRCREV = "cd026c5bfe9f9d713d9302ede331fa2696018b26"
SRC_URI = "git://github.com/google/re2.git;protocol=https;branch=main"
S = "${WORKDIR}/git"

inherit cmake

CXXFLAGS += "-fPIC"
EXTRA_OECMAKE_append = " -DBUILD_SHARED_LIBS=ON"
FILES_SOLIBSDEV = ""
FILES_${PN} += "${libdir}"

BBCLASSEXTEND = "native nativesdk"
