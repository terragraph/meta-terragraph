SUMMARY = "Facebook E2E config files"
DESCRIPTION = "Facebook E2E config files"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

FILESEXTRAPATHS_append := ":${META_TERRAGRAPH_DIR}/src/terragraph-e2e"

SRC_URI = "file://e2e \
           file://sv \
           file://scripts \
           file://systemd \
           file://tests \
           file://LICENSE \
          "

S = "${WORKDIR}"

inherit tgdev

do_install () {
  install -D -m 0755 ${S}/sv/down ${D}${sysconfdir}/sv/driver_if_daemon/down
  install -D -m 0755 ${S}/sv/driver_if_daemon.run ${D}${sysconfdir}/sv/driver_if_daemon/run
  install -D -m 0755 ${S}/sv/driver_if_daemon.log ${D}${sysconfdir}/sv/driver_if_daemon/log/run
  install -D -m 0755 ${S}/sv/e2e_minion.run ${D}${sysconfdir}/sv/e2e_minion/run
  install -D -m 0755 ${S}/sv/e2e_minion.log ${D}${sysconfdir}/sv/e2e_minion/log/run
  install -D -m 0755 ${S}/sv/e2e_controller.run ${D}${sysconfdir}/sv/e2e_controller/run
  install -D -m 0755 ${S}/sv/e2e_controller.log ${D}${sysconfdir}/sv/e2e_controller/log/run
  install -D -m 0755 ${S}/sv/down ${D}${sysconfdir}/sv/e2e_controller/down
  install -D -m 0755 ${S}/sv/udp_ping_server.run ${D}${sysconfdir}/sv/udp_ping_server/run
  install -D -m 0755 ${S}/sv/udp_ping_server.log ${D}${sysconfdir}/sv/udp_ping_server/log/run
  install -D -m 0755 ${S}/sv/pop_config.run ${D}${sysconfdir}/sv/pop_config/run
  install -D -m 0755 ${S}/sv/pop_config.finish ${D}${sysconfdir}/sv/pop_config/finish
  install -D -m 0755 ${S}/sv/pop_config.log ${D}${sysconfdir}/sv/pop_config/log/run
  install -D -m 0755 ${S}/sv/frr_bgp_healthcheck.run ${D}${sysconfdir}/sv/frr_bgp_healthcheck/run
  install -D -m 0755 ${S}/sv/frr_bgp_healthcheck.log ${D}${sysconfdir}/sv/frr_bgp_healthcheck/log/run
  install -D -m 0755 ${S}/sv/down ${D}${sysconfdir}/sv/frr_bgp_healthcheck/down
  install -D -m 0755 ${S}/sv/frr_openr_sync.run ${D}${sysconfdir}/sv/frr_openr_sync/run
  install -D -m 0755 ${S}/sv/frr_openr_sync.log ${D}${sysconfdir}/sv/frr_openr_sync/log/run
  install -D -m 0755 ${S}/sv/down ${D}${sysconfdir}/sv/frr_openr_sync/down
  install -D -m 0755 ${S}/sv/tunnel_monitor.run ${D}${sysconfdir}/sv/tunnel_monitor/run
  install -D -m 0755 ${S}/sv/tunnel_monitor.log ${D}${sysconfdir}/sv/tunnel_monitor/log/run
  install -D -m 0755 ${S}/sv/down ${D}${sysconfdir}/sv/tunnel_monitor/down
  install -m 0755 -d ${D}/usr/sbin/tests/
  install -m 0755 ${S}/scripts/* ${D}/usr/sbin/
  install -m 0755 ${S}/tests/* ${D}/usr/sbin/tests/
  # copy config files
  install -D -m 0644 -d ${D}${sysconfdir}/e2e_config/
  install -D -m 0644 -d ${D}${sysconfdir}/e2e_config/base_versions
  cp -r ${S}/e2e/config/* ${D}${sysconfdir}/e2e_config/
  cp ${S}/e2e/controller/topology/conf/* ${D}${sysconfdir}/e2e_config/
  # copy systemd files
  install -D -m 0644 -d ${D}${sysconfdir}/tg_systemd_config/
  cp ${S}/systemd/*.service ${D}${sysconfdir}/tg_systemd_config/
  install -m 0755 ${S}/systemd/*.start ${D}${sysconfdir}/tg_systemd_config/
  install -m 0755 ${S}/systemd/*.stop ${D}${sysconfdir}/tg_systemd_config/
  cp ${S}/systemd/README.md ${D}${sysconfdir}/tg_systemd_config/
  # create a link
  ln -sfn /data/firmware/wil6210 ${D}${sysconfdir}/e2e_config/wil6210
}

# Package for both controller and node
PACKAGES += "${PN}-both"
RDEPENDS_${PN}-both += "${PN}"
FILES_${PN}-both += " \
    /etc/e2e_config/base_versions/ \
    /etc/e2e_config/config_metadata.json \
    "

# Node package
PACKAGES += "${PN}-minion"
RDEPENDS_${PN}-minion += "${PN}-both"
# sv files
FILES_${PN}-minion = " \
    /etc/sv/driver_if_daemon/ \
    /etc/sv/e2e_minion/ \
    /etc/sv/pop_config/ \
    /etc/sv/udp_ping_server/ \
    /etc/sv/frr_bgp_healthcheck/ \
    /etc/sv/frr_openr_sync/ \
    /etc/sv/tunnel_monitor/ \
    "
# r2d2 files
FILES_${PN}-minion += " \
    /etc/e2e_config/airtimes_e2e_1dn_2cn.json \
    /etc/e2e_config/airtimes_e2e_1dn_3cn.json \
    /etc/e2e_config/airtimes_e2e_1dn_4cn.json \
    /etc/e2e_config/airtimes_r2d2_1dn_2cn.json \
    /etc/e2e_config/airtimes_r2d2_1dn_3cn.json \
    /etc/e2e_config/airtimes_r2d2_1dn_4cn.json \
    /etc/e2e_config/fw_cfg.json \
    /etc/e2e_config/fw_phyagc_cfg.json \
    /etc/e2e_config/fw_phyla_cfg.json \
    /etc/e2e_config/fw_phytpc_cfg.json \
    /etc/e2e_config/fw_stats_cfg.json \
    /etc/e2e_config/broadcom/fw_cfg.json \
    /etc/e2e_config/qualcomm/fw_cfg.json \
    /etc/e2e_config/qualcomm/fw_cfg_cb2.json \
    "
# driver-if files
FILES_${PN}-minion += " \
    /etc/e2e_config/wil6210 \
    /etc/e2e_config/broadcom/fw_cfg_ant_codebook_chn_1.json \
    /etc/e2e_config/broadcom/fw_cfg_ant_codebook_chn_2.json \
    /etc/e2e_config/broadcom/fw_cfg_ant_codebook_chn_3.json \
    /etc/e2e_config/broadcom/fw_cfg_ant_codebook_oneelement_cb.json \
    /etc/e2e_config/qualcomm/fw_cfg_ant_codebook_chn_1.json \
    /etc/e2e_config/qualcomm/fw_cfg_ant_codebook_chn_2.json \
    /etc/e2e_config/qualcomm/fw_cfg_ant_codebook_chn_3.json \
    /etc/e2e_config/qualcomm/fw_cfg_ant_codebook_chn_4.json \
    /etc/e2e_config/qualcomm/fw_cfg_ant_codebook_massive_1D_chn_1.json \
    /etc/e2e_config/qualcomm/fw_cfg_ant_codebook_massive_1D_chn_2.json \
    /etc/e2e_config/qualcomm/fw_cfg_ant_codebook_massive_1D_chn_3.json \
    /etc/e2e_config/qualcomm/fw_cfg_ant_codebook_massive_1D_chn_4.json \
    /etc/e2e_config/qualcomm/fw_cfg_ant_codebook_massive_1D_chn_9.json \
    /etc/e2e_config/qualcomm/fw_cfg_ant_codebook_massive_1D_chn_10.json \
    /etc/e2e_config/qualcomm/fw_cfg_ant_codebook_massive_1D_chn_11.json \
    /etc/e2e_config/qualcomm/fw_cfg_ant_codebook_massive_2D_chn_1.json \
    /etc/e2e_config/qualcomm/fw_cfg_ant_codebook_massive_2D_chn_2.json \
    /etc/e2e_config/qualcomm/fw_cfg_ant_codebook_massive_2D_chn_3.json \
    /etc/e2e_config/qualcomm/fw_cfg_ant_codebook_massive_2D_chn_4.json \
    /etc/e2e_config/qualcomm/fw_cfg_ant_codebook_massive_2D_chn_9.json \
    /etc/e2e_config/qualcomm/fw_cfg_ant_codebook_massive_2D_chn_10.json \
    /etc/e2e_config/qualcomm/fw_cfg_ant_codebook_massive_2D_chn_11.json \
    /etc/e2e_config/qualcomm/fw_cfg_ant_codebook_diversity_1D_chn_1.json \
    /etc/e2e_config/qualcomm/fw_cfg_ant_codebook_diversity_1D_chn_2.json \
    /etc/e2e_config/qualcomm/fw_cfg_ant_codebook_diversity_1D_chn_3.json \
    /etc/e2e_config/qualcomm/fw_cfg_ant_codebook_diversity_1D_chn_4.json \
    /etc/e2e_config/qualcomm/fw_cfg_ant_codebook_diversity_2D_chn_1.json \
    /etc/e2e_config/qualcomm/fw_cfg_ant_codebook_diversity_2D_chn_2.json \
    /etc/e2e_config/qualcomm/fw_cfg_ant_codebook_diversity_2D_chn_3.json \
    /etc/e2e_config/qualcomm/fw_cfg_ant_codebook_diversity_2D_chn_4.json \
    /etc/e2e_config/tpc_tables/fw_phy_txpower_adj_tbl_massive_1D_chn_1.json \
    /etc/e2e_config/tpc_tables/fw_phy_txpower_adj_tbl_massive_1D_chn_2.json \
    /etc/e2e_config/tpc_tables/fw_phy_txpower_adj_tbl_massive_1D_chn_3.json \
    /etc/e2e_config/tpc_tables/fw_phy_txpower_adj_tbl_massive_1D_chn_4.json \
    /etc/e2e_config/tpc_tables/fw_phy_txpower_adj_tbl_massive_1D_chn_9.json \
    /etc/e2e_config/tpc_tables/fw_phy_txpower_adj_tbl_massive_1D_chn_10.json \
    /etc/e2e_config/tpc_tables/fw_phy_txpower_adj_tbl_massive_1D_chn_11.json \
    /etc/e2e_config/tpc_tables/fw_phy_txpower_adj_tbl_massive_2D_chn_1.json \
    /etc/e2e_config/tpc_tables/fw_phy_txpower_adj_tbl_massive_2D_chn_2.json \
    /etc/e2e_config/tpc_tables/fw_phy_txpower_adj_tbl_massive_2D_chn_3.json \
    /etc/e2e_config/tpc_tables/fw_phy_txpower_adj_tbl_massive_2D_chn_4.json \
    /etc/e2e_config/tpc_tables/fw_phy_txpower_adj_tbl_massive_2D_chn_9.json \
    /etc/e2e_config/tpc_tables/fw_phy_txpower_adj_tbl_massive_2D_chn_10.json \
    /etc/e2e_config/tpc_tables/fw_phy_txpower_adj_tbl_massive_2D_chn_11.json \
    /etc/e2e_config/tpc_tables/fw_phy_txpower_adj_tbl_diversity_1D_chn_1.json \
    /etc/e2e_config/tpc_tables/fw_phy_txpower_adj_tbl_diversity_1D_chn_2.json \
    /etc/e2e_config/tpc_tables/fw_phy_txpower_adj_tbl_diversity_1D_chn_3.json \
    /etc/e2e_config/tpc_tables/fw_phy_txpower_adj_tbl_diversity_1D_chn_4.json \
    /etc/e2e_config/tpc_tables/fw_phy_txpower_adj_tbl_diversity_1D_chn_9.json \
    /etc/e2e_config/tpc_tables/fw_phy_txpower_adj_tbl_diversity_1D_chn_10.json \
    /etc/e2e_config/tpc_tables/fw_phy_txpower_adj_tbl_diversity_1D_chn_11.json \
    /etc/e2e_config/tpc_tables/fw_phy_txpower_adj_tbl_diversity_2D_chn_1.json \
    /etc/e2e_config/tpc_tables/fw_phy_txpower_adj_tbl_diversity_2D_chn_2.json \
    /etc/e2e_config/tpc_tables/fw_phy_txpower_adj_tbl_diversity_2D_chn_3.json \
    /etc/e2e_config/tpc_tables/fw_phy_txpower_adj_tbl_diversity_2D_chn_4.json \
    /etc/e2e_config/tpc_tables/fw_phy_txpower_adj_tbl_diversity_2D_chn_9.json \
    /etc/e2e_config/tpc_tables/fw_phy_txpower_adj_tbl_diversity_2D_chn_10.json \
    /etc/e2e_config/tpc_tables/fw_phy_txpower_adj_tbl_diversity_2D_chn_11.json \
    "

# one-off scripts
FILES_${PN}-minion += " \
    /usr/sbin/bgp_wrapper.sh \
    /usr/sbin/config_get_env.sh \
    /usr/sbin/config_pop.sh \
    /usr/sbin/config_tunnel.sh \
    /usr/sbin/run_vpp_chaperone_and_monitor.sh \
    /usr/sbin/driver_if_daemon \
    /usr/sbin/driver_if_start.sh \
    /usr/sbin/e2e_minion_wrapper.sh \
    /usr/sbin/fb_tg_load_common.sh \
    /usr/sbin/fb_tg_restart.sh \
    /usr/sbin/hostapd_stop.sh \
    /usr/sbin/start_cpe_security.sh \
    /usr/sbin/reload_topology_names.sh \
    "

# Controller package
PACKAGES += "${PN}-controller"
RDEPENDS_${PN}-controller += "${PN}-both"
# sv files
FILES_${PN}-controller = " \
    /etc/sv/e2e_controller/ \
    "
# systemd files
FILES_${PN}-controller += " \
    /etc/tg_systemd_config/ \
    "
# config management files
FILES_${PN}-controller += " \
    /etc/e2e_config/controller_config_default.json \
    /etc/e2e_config/controller_config_metadata.json \
    /etc/e2e_config/network_config_overrides_sample.json \
    /etc/e2e_config/node_config_overrides_sample.json \
    /etc/e2e_config/pop_node_config_overrides_sample.json \
    /etc/e2e_config/bgp_config.sample \
    "
# example topology files
FILES_${PN}-controller += " \
    /etc/e2e_config/topology_1dn_1dn.conf \
    /etc/e2e_config/topology_1dn_2cn.conf \
    /etc/e2e_config/topology_1dn_4cn.conf \
    /etc/e2e_config/topology_figure0.conf \
    /etc/e2e_config/topology_half_butterfly.conf \
    /etc/e2e_config/topology_lab_f8_b.conf \
    /etc/e2e_config/topology_lab_f8_c.conf \
    /etc/e2e_config/topology_lab_f8_d.conf \
    "
# one-off scripts
FILES_${PN}-controller += " \
    /usr/sbin/e2e_controller_init.sh \
    /usr/sbin/set_prefixes.py \
    "
RDEPENDS_${PN}-controller += " \
    python3-click \
    python3-core \
    python3-json \
    "

# These are currently unused and should probably be
# removed from the sources.  For now just drop them
# in a separate package and don't use them.
PACKAGES += "${PN}-unused"
FILES_${PN}-unused += " \
    /etc/e2e_config/intel/fw_cfg.json \
    /etc/e2e_config/intel/fw_cfg_ant_codebook_chn_1.json \
    /etc/e2e_config/intel/fw_cfg_ant_codebook_chn_2.json \
    /etc/e2e_config/intel/fw_cfg_ant_codebook_chn_3.json \
    /etc/e2e_config/qualcomm/PHY_Algorithms_QCOM.md \
    "

FILES_${PN}-tests += "/usr/sbin/run_tests.sh"

FILES_${PN} = ""

RDEPENDS_${PN} += "update-firewall"
