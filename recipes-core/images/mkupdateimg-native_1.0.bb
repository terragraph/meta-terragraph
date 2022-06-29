SUMMARY = "Make the terragraph self-installing image"
DESCRIPTION = "Make the terragraph self-installing image"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${META_TERRAGRAPH_DIR}/licenses/Meta-MIT;md5=ae79e563b8a09c8fc37978f18dbaa640"

SRC_URI = "file://mkupdateimg.sh \
           file://preamble.template \
           "

S = "${WORKDIR}"

inherit native

do_install() {
    install -D -m 0755 ${WORKDIR}/mkupdateimg.sh ${D}${bindir}/mkupdateimg.sh
    install -D -m 0644 ${WORKDIR}/preamble.template ${D}${bindir}/preamble.template
}
