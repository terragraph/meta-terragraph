DESCRIPTION = "Parallel Ping"
HOMEPAGE = "http://noping.cc/"

LICENSE = "LGPLv2.1+"
LIC_FILES_CHKSUM = "file://COPYING;md5=4fbd65380cdd255951079008b364516c"

SRC_URI = "https://noping.cc/files/liboping-${PV}.tar.bz2 \
           file://errmsg-length.patch \
           file://strings-and-ni-maxhost.patch \
          "

SRC_URI[md5sum] = "54e0f5a1aaf9eabf3f412d2fdc9c6831"
SRC_URI[sha256sum] = "eb38aa93f93e8ab282d97e2582fbaea88b3f889a08cbc9dbf20059c3779d5cd8"

S = "${WORKDIR}/liboping-${PV}"

inherit autotools-brokensep pkgconfig perlnative

DEPENDS += "ncurses"

EXTRA_OECONF += "--without-perl-bindings"

FILES_${PN} = "/usr"
