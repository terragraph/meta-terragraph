SUMMARY = "TG r2d2 for radio cli + utilities"
DESCRIPTION = "Scripts to give access to firmware and radio commands"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

# Source for this is in the toplevel src/ directory.
FILESEXTRAPATHS_append := ":${META_TERRAGRAPH_DIR}/src"

SRC_URI = "file://r2d2/"

S="${WORKDIR}/r2d2"

inherit setuptools3

RDEPENDS_${PN} += " \
    e2e-if-py3 \
    fbthrift-py3 \
    fbzmq-py3 \
    python3-click \
    python3-core \
    python3-json \
    python3-logging \
    python3-pyzmq \
    python3-setuptools \
    "
