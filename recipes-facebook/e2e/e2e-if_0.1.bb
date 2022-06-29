SUMMARY = "Facebook E2E Thrift Interface"
DESCRIPTION = "Facebook E2E Thrift Interface"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

FILESEXTRAPATHS_append := ":${META_TERRAGRAPH_DIR}/src/terragraph-e2e"

SRC_URI = "file://e2e/if"

S = "${WORKDIR}/e2e/if"

inherit cmake tgdev python3native lua-minify

DEPENDS += "fbthrift fbthrift-compiler-native openr thrift-native"

do_install_append() {
  ${PYTHON} -m compileall ${D}${PYTHON_SITEPACKAGES_DIR}/terragraph_thrift/
}

FILES_${PN} += "${libdir}/lua/5.2/terragraph_thrift/"

PACKAGES += "${PN}-py3"
RDEPENDS_${PN}-py3 += "${PN} fbthrift-py3"
FILES_${PN}-py3 += "${PYTHON_SITEPACKAGES_DIR}/terragraph_thrift/"
