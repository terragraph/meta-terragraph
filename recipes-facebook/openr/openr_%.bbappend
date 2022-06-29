FILESEXTRAPATHS_prepend := "${THISDIR}/files:"

SRC_URI += "file://sv/openr.run \
            file://sv/openr.cfg \
            file://sv/openr.log \
            file://sv/fib_linux.run \
            file://sv/fib_linux.log \
            file://sv/squire_linux.run \
            file://sv/squire_linux.log \
            file://sv/radvd.run \
            file://sv/radvd.log \
            file://sv/radvd.down \
            file://sv/dhcp.run \
            file://sv/dhcp.log \
            file://sv/dhcp.down \
            file://openr_config_template.txt \
            file://run_openr.sh \
            "

do_install_append () {
  install -D -m 0755 ${WORKDIR}/sv/openr.run ${D}${sysconfdir}/sv/openr/run
  install -D -m 0755 ${WORKDIR}/sv/openr.cfg ${D}${sysconfdir}/sysconfig/openr
  install -D -m 0755 ${WORKDIR}/sv/openr.log ${D}${sysconfdir}/sv/openr/log/run
  install -D -m 0755 ${WORKDIR}/sv/fib_linux.run ${D}${sysconfdir}/sv/fib_linux/run
  install -D -m 0755 ${WORKDIR}/sv/fib_linux.log ${D}${sysconfdir}/sv/fib_linux/log/run
  install -D -m 0755 ${WORKDIR}/sv/squire_linux.run ${D}${sysconfdir}/sv/squire_linux/run
  install -D -m 0755 ${WORKDIR}/sv/squire_linux.log ${D}${sysconfdir}/sv/squire_linux/log/run
  install -D -m 0755 ${WORKDIR}/sv/radvd.run ${D}${sysconfdir}/sv/radvd/run
  install -D -m 0755 ${WORKDIR}/sv/radvd.log ${D}${sysconfdir}/sv/radvd/log/run
  install -D -m 0755 ${WORKDIR}/sv/radvd.down ${D}${sysconfdir}/sv/radvd/down
  install -D -m 0755 ${WORKDIR}/sv/dhcp.run ${D}${sysconfdir}/sv/dhcp/run
  install -D -m 0755 ${WORKDIR}/sv/dhcp.log ${D}${sysconfdir}/sv/dhcp/log/run
  install -D -m 0755 ${WORKDIR}/sv/dhcp.down ${D}${sysconfdir}/sv/dhcp/down
  install -D -m 0755 ${WORKDIR}/openr_config_template.txt ${D}${sysconfdir}/sysconfig/openr_config_template.txt
  install -D -m 0755 ${WORKDIR}/run_openr.sh ${D}/usr/sbin/run_openr.sh
}
