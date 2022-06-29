SUMMARY = "Facebook stats publisher/agent"
DESCRIPTION = "Facebook stats publisher/agent"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${WORKDIR}/LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

FILESEXTRAPATHS_append := ":${META_TERRAGRAPH_DIR}/src/terragraph-stats"
SRC_URI = "file://src \
           file://sv \
           file://systemd \
           file://LICENSE \
          "

S = "${WORKDIR}/src"

inherit cmake tgdev

do_install_append () {
  install -D -m 0755 ${WORKDIR}/sv/stats_agent.run ${D}${sysconfdir}/sv/stats_agent/run
  install -D -m 0755 ${WORKDIR}/sv/stats_agent.log ${D}${sysconfdir}/sv/stats_agent/log/run
  install -D -m 0755 ${WORKDIR}/sv/logtail.run ${D}${sysconfdir}/sv/logtail/run
  install -D -m 0755 ${WORKDIR}/sv/logtail.log ${D}${sysconfdir}/sv/logtail/log/run
  install -D -m 0755 ${WORKDIR}/sv/nms_aggregator.run ${D}${sysconfdir}/sv/nms_aggregator/run
  install -D -m 0755 ${WORKDIR}/sv/nms_aggregator.log ${D}${sysconfdir}/sv/nms_aggregator/log/run
  install -D -m 0755 ${WORKDIR}/sv/snmp_agent.run ${D}${sysconfdir}/sv/snmp_agent/run
  install -D -m 0755 ${WORKDIR}/sv/snmp_agent.log ${D}${sysconfdir}/sv/snmp_agent/log/run
  install -D -m 0755 ${WORKDIR}/sv/down ${D}${sysconfdir}/sv/nms_aggregator/down
  # copy config files
  install -D -m 0644 -d ${D}${sysconfdir}/stats_config/
  cp ${WORKDIR}/src/config/* ${D}${sysconfdir}/stats_config/
  # copy systemd files
  install -D -m 0644 -d ${D}${sysconfdir}/tg_systemd_config/
  cp ${WORKDIR}/systemd/*.service ${D}${sysconfdir}/tg_systemd_config/
  install -m 0755 ${WORKDIR}/systemd/*.start ${D}${sysconfdir}/tg_systemd_config/
  install -m 0755 ${WORKDIR}/systemd/*.stop ${D}${sysconfdir}/tg_systemd_config/
}

DEPENDS = "cppkafka \
           curl \
           e2e-clients \
           e2e-common \
           e2e-if \
           lmsensors \
           net-snmp \
           stats-common \
           stats-if \
           "

# Node package
PACKAGES += "${PN}-logtail"
RDEPENDS_${PN}-logtail += "${PN}"
FILES_${PN}-logtail = " \
    /etc/sv/logtail \
    /usr/sbin/logtail \
    "

# Controller package
PACKAGES += "${PN}-aggregator"
RDEPENDS_${PN}-aggregator += "${PN}"
FILES_${PN}-aggregator = " \
    /etc/sv/nms_aggregator \
    /etc/stats_config/aggregator_config_default.json \
    /etc/stats_config/aggregator_config_metadata.json \
    /etc/tg_systemd_config/nms_aggregator.service \
    /etc/tg_systemd_config/nms_aggregator.start \
    /etc/tg_systemd_config/nms_aggregator.docker.start \
    /etc/tg_systemd_config/nms_aggregator.stop \
    /usr/sbin/nms_aggregator \
    /usr/sbin/nms_perf \
    "

# SNMP package
PACKAGES += "${PN}-snmp"
RDEPENDS_${PN}-snmp += "${PN}"
FILES_${PN}-snmp = " \
    /etc/sv/snmp_agent \
    /usr/sbin/snmp_agent \
    "

FILES_${PN} = " \
    /etc/sv/stats_agent \
    /etc/tg_systemd_config/stats_agent.service \
    /etc/tg_systemd_config/stats_agent.start \
    /etc/tg_systemd_config/stats_agent.docker.start \
    /usr/sbin/stats_agent \
    "

FILES_${PN}-dbg += "/usr/sbin/tests/nms/.debug"

RDEPENDS_${PN} = "e2e-if stats-if lmsensors-sensors"
