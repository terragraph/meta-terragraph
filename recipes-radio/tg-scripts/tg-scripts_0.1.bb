SUMMARY = "Terragraph helper scripts"
DESCRIPTION = "Terragraph helper scripts"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

SRC_URI = "file://scripts \
           file://init/ubootversion \
           file://LICENSE \
           "

S = "${WORKDIR}"

INITSCRIPT_NAME = "ubootversion"
INITSCRIPT_PARAMS = "defaults"
inherit update-rc.d

do_install() {
     install -d ${D}${bindir}
     install -m 0755 scripts/* ${D}${bindir}/
     install -D -m 0755 init/ubootversion ${D}${sysconfdir}/init.d/ubootversion
}

RDEPENDS_${PN} = "\
    e2e-if-py3 \
    python3-core \
    python3-datetime \
    python3-json \
    python3-logging \
    python3-math \
    python3-pyserial \
    "
