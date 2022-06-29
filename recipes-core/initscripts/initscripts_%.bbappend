FILESEXTRAPATHS_prepend := "${THISDIR}/files:"

SRC_URI += "\
	file://checkroot.sh.diff \
	file://sysfs.sh.diff \
	"

do_install_append() {
	install -d -m 0755 ${D}/debugfs
}

FILES_${PN} += "/debugfs"
