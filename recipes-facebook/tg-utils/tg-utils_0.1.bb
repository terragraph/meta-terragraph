SUMMARY = "Terragraph Utilities"
DESCRIPTION = "Terragraph Utilities"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

# /data_startup.sh is used run startup scripts from /data/startup
SRC_URI = " \
           file://bgp_softshut \
           file://config_migrate.sh \
           file://core_dump.sh \
           file://core_partition.sh \
           file://data_startup.sh \
           file://empty.dtb \
           file://firewall.sh \
           file://gen_node_info_file.sh \
           file://get_fw_version \
           file://get_hw_info \
           file://get_wlanmac \
           file://initscript \
           file://log_boot_flash_info.sh \
           file://openr_hostnames.sh \
           file://persist_event_cache.sh \
           file://persist_reboot_history.sh \
           file://persist_remaining_gps_timeout.sh \
           file://pop_unreachable_cmd.py \
           file://restart_e2e_minion \
           file://testcode \
           file://tg_shutdown \
           file://update_vpp_startup_conf.sh \
           file://LICENSE \
          "

S = "${WORKDIR}"

UTILS = " \
    core_dump.sh \
    get_fw_version \
    get_hw_info \
    get_wlanmac \
    pop_unreachable_cmd.py \
    restart_e2e_minion \
    testcode \
    "

# To add to sysdump scripts please use bbappend file to add the script
# file to this variable.  In order to dump a log file append the absolute
# path to SYSDUMP_LOGS again in the bbappend file
SYSDUMP_SCRIPTS := "${THISDIR}/files/stats_dump"
SYSDUMP_LOGS = " \
    /data/cfg/* \
    /data/log/reboot_history* \
    /data/upgrade.log* \
    /var/log/auth.log* \
    /var/log/daemon.log* \
    /var/log/dmesg* \
    /var/log/kern.log* \
    /var/log/messages* \
    /var/log/wdog_repair_history* \
    /var/volatile/cores \
    "

do_install() {
    set -f  # No globbing in case SYSDUMP_LOGS has '*' in it
    install -d ${D}/usr/sbin
    for f in ${UTILS}; do
        install -m 755 $f ${D}/usr/sbin
    done

    dst=${D}${sysconfdir}/default
    install -d ${dst}
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${S}/empty.dtb ${D}${sysconfdir}/empty.dtb

    # Get node HW information into a shared file
    install -m 755 ${S}/gen_node_info_file.sh ${D}${sysconfdir}/init.d/gen_node_info_file.sh
    update-rc.d -r ${D} gen_node_info_file.sh start 41 S .

    # Make vpp startup configuration file, run after gen_node_info_file.sh
    install -m 755 ${S}/update_vpp_startup_conf.sh ${D}${sysconfdir}/init.d/update_vpp_startup_conf.sh
    update-rc.d -r ${D} update_vpp_startup_conf.sh start 44 S .

    # Run script to merge config on outdated nodes
    install -m 755 ${S}/config_migrate.sh ${D}${sysconfdir}/init.d/config_migrate.sh
    update-rc.d -r ${D} config_migrate.sh start 42 S .

    # Fix /etc/hosts files link. We do this before networking starts
    install -m 755 ${S}/openr_hostnames.sh ${D}${sysconfdir}/init.d/openr_hostnames.sh
    update-rc.d -r ${D} openr_hostnames.sh start 64 S .

    # Log boot flash info. Do this after syslog starts.
    install -m 755 ${S}/log_boot_flash_info.sh ${D}${sysconfdir}/init.d/log_boot_flash_info.sh
    update-rc.d -r ${D} log_boot_flash_info.sh start 21 5 .

    # Persist reboot history in /data
    install -m 755 ${S}/persist_reboot_history.sh ${D}${sysconfdir}/init.d/persist_reboot_history.sh
    update-rc.d -r ${D} persist_reboot_history.sh start 21 5 .

    # install all sysdump scripts
    install -d ${D}/etc/sysdump
    for f in ${SYSDUMP_SCRIPTS}; do
        install -m 755 $f ${D}/etc/sysdump
    done
    for f in ${SYSDUMP_LOGS}; do
        echo $f >> ${D}/etc/sysdump/sysdump_log_files
    done

    # Perform persistent startup scripts under /data/startup/
    install -m 755 ${S}/data_startup.sh ${D}${sysconfdir}/init.d/data_startup.sh
    update-rc.d -r ${D} data_startup.sh defaults 1

    # Persist remaining gps timeout in flash
    install -m 755 ${S}/persist_remaining_gps_timeout.sh ${D}${sysconfdir}/init.d/persist_remaining_gps_timeout.sh

    # Save/load event cache to/from flash
    install -m 755 ${S}/persist_event_cache.sh ${D}${sysconfdir}/init.d/persist_event_cache.sh

    # Perform soft BGP shutdown before reboot
    install -m 755 ${S}/bgp_softshut ${D}${sysconfdir}/init.d/bgp_softshut
    update-rc.d -r ${D} bgp_softshut stop 11 0 6 .

    # Perform tg specific shutdown actions (log rotation etc.)
    install -m 755 ${S}/tg_shutdown ${D}${sysconfdir}/init.d/tg_shutdown
    update-rc.d -r ${D} tg_shutdown start 25 0 6 .

    # Create partition /var/volatile/cores.
    install -m 755 ${S}/core_partition.sh ${D}${sysconfdir}/init.d/core_partition.sh
    update-rc.d -r ${D} core_partition.sh start 38 S . # After S37populate-volatile.sh
    install -m 755 ${S}/initscript ${D}/etc/initscript

    # Configure the firewall right after networking starts
    install -m 755 ${S}/firewall.sh ${D}${sysconfdir}/init.d/firewall.sh
    update-rc.d -r ${D} firewall.sh start 02 2 3 5 .
}

DEPENDS = "update-rc.d-native"

RDEPENDS_${PN} = " \
    bash \
    python3-click \
    python3-core \
    python3-datetime \
    python3-logging \
    python3-threading \
    monotonic-touch \
    "
