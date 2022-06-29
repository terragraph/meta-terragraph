SUMMARY = "Terragraph95"
DESCRIPTION = "Terragraph95 Local Web UI"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

FILESEXTRAPATHS_append := ":${META_TERRAGRAPH_DIR}/src"

SRC_URI = "file://tg95/build/ \
           file://tg95/LICENSE \
          "

S = "${WORKDIR}/tg95"

do_install() {
    tar -C ${S}/build -czf tg95.tar.gz .
    install -d ${D}${sysconfdir}/webui/
    install -m 644 ${S}/tg95.tar.gz ${D}${sysconfdir}/webui/
}

FILES_${PN} += "${sysconfdir}"
