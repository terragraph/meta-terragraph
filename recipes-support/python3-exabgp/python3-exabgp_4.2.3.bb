DESCRIPTION = "BGP Daemon"
HOMEPAGE = "https://pypi.python.org/p/exabgp"
SECTION = "devel/python"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://setup.cfg;md5=54803014adfbe0352b5978ca91dbeae4"

SRC_URI[md5sum] = "e8514a1c911545e681af90ae764db988"
SRC_URI[sha256sum] = "564f459b3dcb90021ec78bfc9d71e594734f8fc9cc4a81edcf19470dec6950f2"

FILESEXTRAPATHS_prepend := "${THISDIR}/files:"
SRC_URI += "file://069-CPU-sleeptime-ms.patch \
            file://070-add-show-stats-command-to-exabgpcli.patch \
            file://exabgp.conf.template \
            file://exabgp.env \
            file://sv \
            file://systemd \
            "

inherit pypi setuptools3 systemd

do_install_append() {
    # We do not need the exabgp unittests etc.
    rm -rf ${D}/usr/share

    # env + conf template
    install -D -m 0640 ${WORKDIR}/exabgp.conf.template ${D}${sysconfdir}/exabgp/exabgp.conf.template
    install -D -m 0640 ${WORKDIR}/exabgp.env ${D}${sysconfdir}/exabgp/exabgp.env

    if ${@bb.utils.contains('DISTRO_FEATURES','systemd','true','false',d)}; then
        install -Dm 0644 ${WORKDIR}/systemd/exabgp.service ${D}${systemd_system_unitdir}/exabgp.service
    else
        # Default to down and enable via NodeConfig for POP nodes + generate config
        install -d 0755 ${D}${sysconfdir}/sv/exabgp/
        install -d 0755 ${D}${sysconfdir}/sv/exabgp/log
        install -m 0640 ${WORKDIR}/sv/exabgp.down ${D}${sysconfdir}/sv/exabgp/down
        install -m 0750 ${WORKDIR}/sv/exabgp.log ${D}${sysconfdir}/sv/exabgp/log/run
        install -m 0750 ${WORKDIR}/sv/exabgp.run ${D}${sysconfdir}/sv/exabgp/run
    fi
}

RDEPENDS_${PN} += " \
    exaconf \
    python3-mmap \
    python3-setuptools \
    python3-syslog \
    python3-zstandard \
    "

SYSTEMD_SERVICE_${PN} = "exabgp.service"
SYSTEMD_AUTO_ENABLE = "disable"
