DESCRIPTION = "Vector Packet Processing Plugin Example"
HOMEPAGE = "http://fd.io"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://sample/sample.c;beginline=1;endline=14;md5=fd1ae1cb14755554d5a20a602585a5db"

FILESEXTRAPATHS_append := ":${META_TERRAGRAPH_DIR}/src/vpp_plugins"

DEPENDS = "vpp vpp-native python3-ply-native"
SRC_URI = "file://example"

S = "${WORKDIR}/example"

inherit cmake python3native

FILES_${PN} = "${libdir} ${datadir}"
