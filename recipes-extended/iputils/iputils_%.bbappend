# Upstream iputils have folded ping6 and tracepath6 into main
# binaries, but our testbed will used ping6 actively. Provide
# these binaries for compatibility

do_install_append() {
	install -m 4555 ping ${D}${base_bindir}/ping6
	install -m 7555 tracepath ${D}${base_bindir}/tracepath6
}

ALTERNATIVE_${PN}-ping6 = "ping6"
ALTERNATIVE_LINK_NAME[ping6] = "${base_bindir}/ping6"

PACKAGES += "${PN}-ping6 ${PN}-tracepath6"

RDEPENDS_${PN} += "${PN}-ping6 ${PN}-tracepath6"

FILES_${PN}-ping6 = "${base_bindir}/ping6.${BPN}"
FILES_${PN}-tracepath6 = "${base_bindir}/tracepath6"
