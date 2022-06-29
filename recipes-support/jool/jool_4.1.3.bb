DESCRIPTION = "Jool is an Open Source IPv4/IPv6 Translator"
HOMEPAGE = "https://github.com/NICMx/Jool"
SECTION = "networking"
LICENSE = "GPLv2"
LIC_FILES_CHKSUM = "file://COPYING;md5=b234ee4d69f5fce4486a80fdaf4a4263"

require jool.inc

DEPENDS = "libnl iptables"

S = "${WORKDIR}/jool-4.1.3"

inherit pkgconfig autotools-brokensep

do_configure[depends] += "virtual/kernel:do_shared_workdir"

FILES_${PN} += "/usr/share/bash-completion \
                /usr/share/bash-completion/completions \
                /usr/share/bash-completion/completions/jool_siit.bash \
                /usr/share/bash-completion/completions/jool.bash \
                /usr/lib/xtables/libxt_JOOL_SIIT.so \
                /usr/lib/xtables/libxt_JOOL.so"
