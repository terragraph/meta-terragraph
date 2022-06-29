FILESEXTRAPATHS_prepend := "${THISDIR}/files:"

SRC_URI+= " \
	file://ltc3884_prog_v2.sh \
	file://fuse_read \
	file://fuse_write \
	file://check_qspi_versions \
	file://wriop_stats \
	"

S = "${WORKDIR}"

do_install_append() {
	# NXP core voltage manipulation script
	install -d ${D}${sbindir}
	install -m 755 ${WORKDIR}/ltc3884_prog_v2.sh ${D}${sbindir}
	install -m 755 ${WORKDIR}/fuse_read ${D}${sbindir}
	install -m 755 ${WORKDIR}/fuse_write ${D}${sbindir}
	install -m 755 ${WORKDIR}/check_qspi_versions ${D}${sbindir}
	install -m 755 ${WORKDIR}/wriop_stats ${D}${sbindir}
}

