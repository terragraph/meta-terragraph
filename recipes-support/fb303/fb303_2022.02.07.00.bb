DESCRIPTION = "Facebook fb303"
HOMEPAGE = "https://github.com/facebook/fb303"
SECTION = "core"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=2ee41112a44fe7014dce33e26468ba93"

SRCREV = "5a28f8792d77689302ecdae75b71afaf233b8f4c"
SRC_URI = "git://github.com/facebook/fb303.git;protocol=https;branch=main \
           file://Add-lua-namespace-to-fb303-thrift.patch \
           file://do-not-expose-private-include-dirs.patch \
          "

S = "${WORKDIR}/git"

inherit cmake  

DEPENDS += " \
    gflags \
    glog \
    jemalloc \
    fbthrift \ 
    fbthrift-compiler-native \ 
    fbthrift-py3 \
    folly \
"

# Build shared library
CXXFLAGS += "-fPIC"
BUILD_CXXFLAGS += "-fPIC"
EXTRA_OECMAKE_append = " -DBUILD_SHARED_LIBS=ON"
EXTRA_OECMAKE_append = " -DCMAKE_CXX_STANDARD=17"
EXTRA_OECMAKE_append = " -DFBTHRIFT_COMPILER=${WORKDIR}/recipe-sysroot-native/usr/bin/thrift1"
EXTRA_OECMAKE_append = " -DFBTHRIFT_TEMPLATES_DIR=disable"
EXTRA_OECMAKE_append = " -DPYTHON_EXTENSIONS=false"
EXTRA_OECMAKE_append= "  -DPACKAGE_VERSION=''"
FILES_SOLIBSDEV = ""
FILES_${PN} += "${libdir}"

BBCLASSEXTEND = "native nativesdk"
