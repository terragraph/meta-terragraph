SUMMARY = "Facebook STATS Thrift Interface"
DESCRIPTION = "Facebook STATS Thrift Interface"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${WORKDIR}/LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

FILESEXTRAPATHS_append := ":${META_TERRAGRAPH_DIR}/src/terragraph-stats"

SRC_URI = "file://src/if \
           file://LICENSE \
          "

S = "${WORKDIR}/src/if"

inherit cmake tgdev python3native lua-minify

DEPENDS = "fbthrift fbthrift-compiler-native fbzmq thrift-native"

do_install_append() {
  ${PYTHON} -m compileall ${D}${PYTHON_SITEPACKAGES_DIR}/terragraph_thrift/
}

FILES_${PN} += "${libdir}/lua/5.2/terragraph_thrift/"

PACKAGES += "${PN}-py3"
RDEPENDS_${PN}-py3 += "${PN} fbthrift-py3"
FILES_${PN}-py3 += "${PYTHON_SITEPACKAGES_DIR}/terragraph_thrift/"
