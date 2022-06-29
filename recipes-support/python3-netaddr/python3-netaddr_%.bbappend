# Add a cli package and put netaddr there.
PACKAGES =+ "${PN}-cli"
RDEPENDS_${PN}-cli = "${PN} "
FILES_${PN}-cli = " \
    ${bindir}/netaddr \
"
