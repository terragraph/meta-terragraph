DESCRIPTION = "Facebook Wangle"
HOMEPAGE = "https://github.com/facebook/wangle"
SECTION = "core"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://${S}/../LICENSE;md5=2ee41112a44fe7014dce33e26468ba93"

SRCREV = "9cc79258f8d0be0dd297f1c69b0c77e34d6f53b4"
SRC_URI = "git://github.com/facebook/wangle.git;protocol=https;branch=main \
           file://cmake-hide-found-libraries-from-public-interface.patch;striplevel=2 \
          "

DEPENDS = "folly fizz"

inherit cmake

S = "${WORKDIR}/git/wangle"

FILES_${PN} = "/usr"

BBCLASSEXTEND = "native nativesdk"

EXTRA_OECMAKE_append = " -DBUILD_SHARED_LIBS=ON"
EXTRA_OECMAKE_append = " -DCMAKE_SKIP_RPATH=TRUE"
EXTRA_OECMAKE_append = " -DCMAKE_CXX_STANDARD=17"
