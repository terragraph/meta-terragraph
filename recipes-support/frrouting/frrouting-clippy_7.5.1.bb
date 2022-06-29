SUMMARY = "FRrouting clippy"
DESCRIPTION = "CLI cross-compiling tool for building FRrouting"
HOMEPAGE = "https://frrouting.org/"
SECTION = "net"
LICENSE = "GPL-2.0 & LGPL-2.0"
LIC_FILES_CHKSUM = "file://COPYING;md5=b234ee4d69f5fce4486a80fdaf4a4263"

SRCREV = "df7ab485bde1a511f131f7ad6b70cb43c48c8e6d"
SRC_URI = "git://github.com/FRRouting/frr.git;protocol=https;branch=stable/7.5"

S = "${WORKDIR}/git"

inherit autotools pkgconfig python3native

DEPENDS += "bison flex"

EXTRA_OECONF = "--enable-clippy-only"

do_install() {
    install -d ${D}${bindir}
    install -m 755 lib/clippy ${D}${bindir}
}

BBCLASSEXTEND = "native"
