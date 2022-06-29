FILESEXTRAPATHS_prepend := "${THISDIR}/udev:"

SRC_URI += "file://10-local.rules"

do_install_append () {
	install -d ${D}${sysconfdir}/udev/rules.d/
	install -m 0644 ${WORKDIR}/10-local.rules ${D}${sysconfdir}/udev/rules.d/10-local.rules
}

FILES_${PN} += "${sysconfdir}/udev/rules.d/"
