SUMMARY = "ptr script to download and run"
DESCRIPTION = "Include ptr venv wrapper in x86 chroot"
SECTION = "testing"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${WORKDIR}/LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

SRC_URI = "file://run_ptr.sh \
           file://LICENSE \
          "

do_install() {
     install -D -m 0755 ${WORKDIR}/run_ptr.sh ${D}${sbindir}/run_ptr.sh
}

# flake8 needs doctest
# exaconf need python3-aioexabgp + python3-exabgp
# mypy needs sqlite3
RDEPENDS_${PN} = "bash \
                 gcc \
                 make \
                 python3-aioexabgp \
                 python3-doctest \
                 python3-exabgp \
                 python3-pip \
                 python3-pexpect \
                 python3-regex \
                 python3-sqlite3 \
                 python3-venv \
                 "
