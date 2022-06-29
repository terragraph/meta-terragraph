SUMMARY = "QTI WIL6210 helper scripts for Terragraph"
DESCRIPTION = "Scripts specific to systems with wil6210 chip"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${META_TERRAGRAPH_DIR}/licenses/Meta-MIT;md5=ae79e563b8a09c8fc37978f18dbaa640"

SRC_URI = "file://scripts \
           file://sv \
           file://init \
           "
S = "${WORKDIR}"

# After S42config_migrate.sh
INITSCRIPT_NAME = "fw_brd_update"
INITSCRIPT_PARAMS = "start 43 S ."
inherit update-rc.d

do_install() {
  install -d ${D}${bindir}
  install -d ${D}${sysconfdir}/init.d
  install -m 0755 scripts/* ${D}${bindir}/

  install -D -m 0755 ${S}/sv/wiltest.run ${D}${sysconfdir}/sv/wiltest/run
  install -D -m 0755 ${S}/sv/wiltest.log ${D}${sysconfdir}/sv/wiltest/log/run
  install -D -m 0755 ${S}/sv/wiltest.down ${D}${sysconfdir}/sv/wiltest/down

  install -D -m 0755 ${S}/sv/host_manager_11ad.run ${D}${sysconfdir}/sv/host_manager_11ad/run
  install -D -m 0755 ${S}/sv/host_manager_11ad.log ${D}${sysconfdir}/sv/host_manager_11ad/log/run

  install -D -m 0755 ${S}/sv/start_host_manager_fw_log.run ${D}${sysconfdir}/sv/start_host_manager_fw_log/run
  install -D -m 0755 ${S}/sv/start_host_manager_fw_log.log ${D}${sysconfdir}/sv/start_host_manager_fw_log/log/run
  install -D -m 0755 ${S}/sv/start_host_manager_fw_log.down ${D}${sysconfdir}/sv/start_host_manager_fw_log/down

  # Create /data/firmware/wil6210 directory
  # Update native board file for each baseband EEPROM (if needed)
  install -m 755 ${S}/init/fw_brd_update ${D}${sysconfdir}/init.d/fw_brd_update
}
