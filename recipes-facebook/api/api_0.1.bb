SUMMARY = "Facebook API service"
DESCRIPTION = "Facebook API service"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${WORKDIR}/LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

FILESEXTRAPATHS_append := ":${META_TERRAGRAPH_DIR}/src/terragraph-api"

SRC_URI = "file://src \
           file://apidoc \
           file://systemd \
           file://LICENSE \
          "

S = "${WORKDIR}/src"

inherit cmake tgdev

do_install_append () {
  # copy apidoc
  install -D -m 0644 -d ${D}${sysconfdir}/apidoc/
  cp -r ${WORKDIR}/apidoc/* ${D}${sysconfdir}/apidoc/
  # copy systemd files
  install -D -m 0644 -d ${D}${sysconfdir}/tg_systemd_config/
  cp ${WORKDIR}/systemd/*.service ${D}${sysconfdir}/tg_systemd_config/
  install -m 0755 ${WORKDIR}/systemd/*.start ${D}${sysconfdir}/tg_systemd_config/
}

DEPENDS = "e2e-if \
           api-if \
           fbthrift \
           folly \
           gflags \
           glog \
           fbzmq \
           gtest \
           stats-if \
           e2e-common \
           stats-common \
           proxygen \
           cpp-jwt \
           "

RDEPENDS_${PN} = "stats-if e2e-if"
