DESCRIPTION = "Vector Packet Processing TG Config Plugin"
HOMEPAGE = "http://fd.io"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

FILESEXTRAPATHS_append := ":${META_TERRAGRAPH_DIR}/src/vpp_plugins"

DEPENDS = "vpp vpp-native python3-ply-native"
SRC_URI = "file://tgcfg"

S = "${WORKDIR}/tgcfg"

inherit cmake python3native

# Enable extra safety checks in debug builds
DEBUG_OPTIMIZATION += "-DCLIB_DEBUG"

FILES_${PN} = "${libdir} ${datadir}"
