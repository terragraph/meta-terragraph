# Dynamically compute fw_env.conf on the Puma platform

FILESEXTRAPATHS_prepend := "${THISDIR}/files:"
SRC_URI += "file://setup_fw_env.sh"

# Used by setup_fw_env
RDEPENDS_${PN} += "bc util-linux"

inherit update-rc.d
INITSCRIPT_NAME = "setup_fw_env"
INITSCRIPT_PARAMS = "start 39 S ."

do_install_append () {
	install -d ${D}${sysconfdir}
	install -D -m 0755 ${WORKDIR}/setup_fw_env.sh ${D}${sysconfdir}/init.d/setup_fw_env

	# Actual file will be created by setup_fw_env
	ln -sf /var/run/fw_env.config ${D}${sysconfdir}/fw_env.config
}
