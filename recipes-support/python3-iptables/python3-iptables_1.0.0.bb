DESCRIPTION = "Python-iptables provides a pythonesque wrapper via python bindings to iptables under Linux"
HOMEPAGE = "https://github.com/ldx/python-iptables"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://NOTICE;md5=a5053747d02fe1f4fd00ad25869b095b"

SRCREV = "30cd7ce19d0eb8e8d96ad3b03a9073e80b98bdd0"
SRC_URI = "git://github.com/ldx/python-iptables.git;protocol=https;branch=master"

FILES_${PN} += "${libdir}/${PYTHON_DIR}/lib-dynload/libc"

S = "${WORKDIR}/git"

inherit setuptools3

do_install_append() {
    ${PYTHON} setup.py install_lib
    mkdir -p ${D}${libdir}/${PYTHON_DIR}/lib-dynload/
    ln -s /lib/libc.so.6 ${D}${libdir}/${PYTHON_DIR}/lib-dynload/libc
}
