SUMMARY = "Facebook E2E Lua files"
DESCRIPTION = "Facebook E2E Lua files"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://lua/LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

FILESEXTRAPATHS_append := ":${META_TERRAGRAPH_DIR}/src/terragraph-e2e"

SRC_URI = "file://lua"

S = "${WORKDIR}"

inherit tgdev

do_install () {
  # install scripts to /usr/sbin/ without .lua extension
  install -m 0755 -d ${D}/usr/sbin/
  for f in ${S}/lua/*.lua; do
    filename="$(basename $f .lua)"
    install -m 0755 "${f}" "${D}/usr/sbin/${filename}"
  done

  # install tests to /usr/sbin/tests/
  luatestdir=/usr/sbin/tests/lua
  install -m 0755 -d ${D}${luatestdir}
  install -m 0755 ${S}/lua/tests/*.lua ${D}${luatestdir}

  # install libs to luadir
  luadir="${libdir}/lua/5.2"
  install -m 0755 -d ${D}${luadir}/tg/
  install -m 0755 ${S}/lua/tg/*.lua ${D}${luadir}/tg/

  # install link from "breeze" to "puff"
  install -m 0755 -d ${D}/usr/bin/
  ln -sf "../sbin/puff" "${D}/usr/bin/breeze"
}

# Files for both controller and minion
FILES_${PN} = " \
    ${libdir} \
    ${sbindir}/tg2 \
    "

# For compatibility, optionally override "breeze" path with a link to "puff"
PACKAGES =+ "openr-puff"
RDEPENDS_openr-puff += "${PN}"
FILES_openr-puff += " \
    ${bindir}/breeze \
    "

PACKAGES += "${PN}-minion"
RDEPENDS_${PN}-minion += "${PN}"
FILES_${PN}-minion = " \
    ${sbindir}/config_get_base \
    ${sbindir}/config_get_cpe \
    ${sbindir}/config_migrate \
    ${sbindir}/config_read_env \
    ${sbindir}/config_set \
    ${sbindir}/config_set_kvstore \
    ${sbindir}/cpe_operations \
    ${sbindir}/diff_node_config \
    ${sbindir}/export_security_config \
    ${sbindir}/frr_bgp_healthcheck \
    ${sbindir}/frr_openr_sync \
    ${sbindir}/frr_reload \
    ${sbindir}/generate_fluentbit_config \
    ${sbindir}/get_pop_ip \
    ${sbindir}/migrate_e2e_data \
    ${sbindir}/puff \
    ${sbindir}/reload_ssh_ca_keys \
    ${sbindir}/sys_dump \
    ${sbindir}/time_set \
    ${sbindir}/tunnel_monitor \
    ${sbindir}/update_bgpd_conf \
    ${sbindir}/update_chrony_conf \
    ${sbindir}/update_resolvconf \
    ${sbindir}/update_snmpd_conf \
    ${sbindir}/update_vpp_startup_conf \
    "

PACKAGES += "${PN}-controller"
RDEPENDS_${PN}-controller += "${PN}"
FILES_${PN}-controller = " \
    ${sbindir}/config_print_flags \
    "

RDEPENDS_${PN} = " \
    fb303-lua \
    fbzmq-lua \
    lua5.2 \
    lua-argparse \
    lua-llthreads2 \
    lua-luafilesystem \
    lua-lzmq \
    lua-cjson \
    lua-resty-prettycjson \
    lua-penlight \
    lua-luaposix \
    openr-lua \
    thrift-lua \
    "

# Pull the test library into the emulation build.
RDEPENDS_${PN}_append_tgx86 = " lua-luaunit"
