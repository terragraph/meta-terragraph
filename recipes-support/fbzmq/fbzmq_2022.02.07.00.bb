DESCRIPTION = "Async framework and C++ wrapper over libzmq"
HOMEPAGE = "https://github.com/facebookincubator/fbzmq"
SECTION = "core"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=57a79768a3b1e4d0f3a3787048a96356"

include fbzmq_src.inc

SRC_URI += "file://cmake-do-not-expose-full-lib-paths.patch"

S = "${WORKDIR}/git"

inherit cmake tgdev

DEPENDS += "\
  fbthrift \
  fbthrift-compiler-native \
  folly \
  gflags \
  glog \
  gtest \
  sigar \
  zeromq \
  zstd \
  "

CXXFLAGS += "-fPIC"
EXTRA_OECMAKE_append = " -Dlib_only=ON"
EXTRA_OECMAKE_append = " -DBUILD_SHARED_LIBS=ON"
EXTRA_OECMAKE_append = " -DBUILD_TESTS=OFF"
EXTRA_OECMAKE_append = " -DCMAKE_CXX_STANDARD=17"
EXTRA_OECMAKE_append = " -DFBTHRIFT_COMPILER=${RECIPE_SYSROOT_NATIVE}/usr/bin/thrift1"
EXTRA_OECMAKE_append = " -DFBTHRIFT_TEMPLATES_DIR=${RECIPE_SYSROOT_NATIVE}/usr/include/thrift/templates"
# fbzmq is currently building the Monitor files in the wrong place.
# Just move them out of the image for now as we don't use them either way.
FILES_${PN}-dev += "${libdir}/Monitor*"
