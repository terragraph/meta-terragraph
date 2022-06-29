SUMMARY = "FRrouting"
DESCRIPTION = "FRrouting"
HOMEPAGE = "https://frrouting.org/"
SECTION = "net"
LICENSE = "GPL-2.0 & LGPL-2.0"
LIC_FILES_CHKSUM = "file://COPYING;md5=b234ee4d69f5fce4486a80fdaf4a4263"

SRCREV = "df7ab485bde1a511f131f7ad6b70cb43c48c8e6d"
SRC_URI = "git://github.com/FRRouting/frr.git;protocol=https;branch=stable/7.5"

S = "${WORKDIR}/git"

inherit autotools pkgconfig python3native useradd

DEPENDS += "bison-native frrouting-clippy-native json-c libyang openssl readline"

EXTRA_OECONF = "--sysconfdir=${sysconfdir}/frrouting \
                --localstatedir=${localstatedir}/run/frr \
                --enable-exampledir=${docdir}/frrouting/examples \
                --enable-vtysh \
                --enable-user=frr \
                --enable-group=frr \
                --enable-shared=yes \
                --disable-capabilities \
                --disable-doc \
                --with-clippy=${RECIPE_SYSROOT_NATIVE}/usr/bin/clippy"

# Parallel make of frrouting seems to be broken
PARALLEL_MAKE = ""

# Add FRR's user and group
USERADD_PACKAGES = "${PN}"
GROUPADD_PARAM_${PN} = "--system frr; --system frrvty"
USERADD_PARAM_${PN} = "--system --home-dir ${localstatedir}/run/frr/ -M -g frr -G frrvty --shell /sbin/nologin --comment 'FRR suite' frr"

do_install_append() {
  # remove python scripts
  rm -vf ${D}${sbindir}/*.py
  rm -vf ${D}${sbindir}/frr-reload

  # remove ospf client
  rm -vf ${D}${libdir}/libfrrospfapiclient.so*

  # install example config files
  install -d ${D}${sysconfdir}/frrouting
  install -m 0644 ${S}/tools/etc/frr/daemons ${D}${sysconfdir}/frrouting/
  install -m 0644 ${S}/tools/etc/frr/frr.conf ${D}${sysconfdir}/frrouting/
  install -m 0644 ${S}/tools/etc/frr/vtysh.conf ${D}${sysconfdir}/frrouting/
  chown -R frr:frrvty ${D}${sysconfdir}/frrouting/
  chown -R frr:frr ${D}${sysconfdir}/frrouting/*.conf
  chmod -R 750 ${D}${sysconfdir}/frrouting/
  chmod -R 640 ${D}${sysconfdir}/frrouting/*.conf
}

# Split into a main package and separate per-protocol packages
PACKAGE_BEFORE_PN = "${PN}-tools ${PN}-bgpd ${PN}-ospfd ${PN}-ospf6d \
                     ${PN}-ripd ${PN}-ripngd ${PN}-isisd ${PN}-staticd \
                     ${PN}-fabricd ${PN}-babeld ${PN}-ldpd ${PN}-bfdd \
                     ${PN}-eigrpd ${PN}-pimd ${PN}-pbrd ${PN}-vrrpd \
                    "
RDEPENDS_${PN} += "${PN}-tools ${PN}-bgpd ${PN}-ospfd ${PN}-ospf6d \
                   ${PN}-ripd ${PN}-ripngd ${PN}-isisd ${PN}-staticd \
                   ${PN}-fabricd ${PN}-babeld ${PN}-ldpd ${PN}-bfdd \
                   ${PN}-eigrpd ${PN}-pimd ${PN}-pbrd ${PN}-vrrpd \
                  "
RDEPENDS_${PN}-tools += "bash"

FILES_${PN}-tools       = "${sbindir}/frr \
                           ${sbindir}/frrinit.sh \
                           ${sbindir}/frrcommon.sh \
                           ${sbindir}/ssd \
                           ${sbindir}/watchfrr \
                           ${sbindir}/watchfrr.sh \
                           ${bindir}/mtracebis \
                           ${sysconfdir} \
                          "
FILES_${PN}-bgpd        = "${sbindir}/bgpd"
FILES_${PN}-ospfd       = "${sbindir}/ospfd"
FILES_${PN}-ospf6d      = "${sbindir}/ospf6d"
FILES_${PN}-ripd        = "${sbindir}/ripd"
FILES_${PN}-ripngd      = "${sbindir}/ripngd"
FILES_${PN}-isisd       = "${sbindir}/isisd"
FILES_${PN}-staticd     = "${sbindir}/staticd"
FILES_${PN}-fabricd     = "${sbindir}/fabricd"
FILES_${PN}-babeld      = "${sbindir}/babeld"
FILES_${PN}-ldpd        = "${sbindir}/ldpd"
FILES_${PN}-bfdd        = "${sbindir}/bfdd"
FILES_${PN}-eigrpd      = "${sbindir}/eigrpd"
FILES_${PN}-pimd        = "${sbindir}/pimd"
FILES_${PN}-pbrd        = "${sbindir}/pbrd"
FILES_${PN}-vrrpd       = "${sbindir}/vrrpd"

FILES_${PN} = "${bindir} ${libdir} ${sbindir}"
FILES_${PN}-dev += "${datadir}/yang"
