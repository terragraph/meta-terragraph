DESCRIPTION = "Facebook Thrift Compiler"
HOMEPAGE = "https://github.com/facebook/fbthrift"
SECTION = "core"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=fa259dfc0c8b1f07957c1760a7553d1c"

include fbthrift_src.inc

SRC_URI += "file://Add-instructions-to-use-_ref-in-cpp2-mustache.patch \
            file://Find-Boost-Filesystem.patch \
           "

S = "${WORKDIR}/git"

inherit cmake

DEPENDS += "boost bison-native flex-native zlib zstd"

EXTRA_OECMAKE_append = " -Dcompiler_only=ON"
EXTRA_OECMAKE_append = " -DCMAKE_CXX_STANDARD=17"
EXTRA_OECMAKE_append = " -DFS_EXPERIMENTAL=1"

BBCLASSEXTEND = "native nativesdk"
