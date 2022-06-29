DESCRIPTION = "Facebook Thrift"
HOMEPAGE = "https://github.com/facebook/fbthrift"
SECTION = "core"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=fa259dfc0c8b1f07957c1760a7553d1c"

include fbthrift_src.inc

SRC_URI += "file://cmake-allow-compiler-override.patch \
            file://cmake-do-not-expose-full-lib-paths.patch \
            file://Fix-ThriftLibrary.cmake-path-in-CMakeLists.txt.patch \
            file://Disable-Python.patch \
            file://Disable-shared-library-versioning.patch \
           "


S = "${WORKDIR}/git"

inherit cmake

DEPENDS += "snappy wangle folly gflags cyrus-sasl python3 fbthrift-compiler-native zstd fmt"

CXXFLAGS += "-fPIC"
EXTRA_OECMAKE_append = " -Dlib_only=ON"
EXTRA_OECMAKE_append = " -DBUILD_SHARED_LIBS=ON"
EXTRA_OECMAKE_append = " -DCMAKE_SKIP_RPATH=TRUE"
EXTRA_OECMAKE_append = " -DCMAKE_CXX_STANDARD=17"
FILES_SOLIBSDEV = ""
FILES_${PN} += "${libdir}"
