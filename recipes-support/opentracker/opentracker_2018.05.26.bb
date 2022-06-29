DESCRIPTION = "An open bittorrent tracker"
HOMEPAGE = "http://erdgeist.org/arts/software/opentracker/"
LICENSE = "Beerware-opentracker"
LIC_FILES_CHKSUM = "file://README;beginline=30;endline=30;md5=2c68905715a12d6815dea2d5d90d54a1"

SRCREV = "6411f1567f64248b0d145493c2e61004d2822623"
SRC_URI = "git://erdgeist.org/opentracker;branch=master \
           file://0001-Disable-binary-strip-ipv6.patch \
           "

S = "${WORKDIR}/git"

DEPENDS = "libowfat zlib"

inherit autotools-brokensep

EXTRA_OEMAKE += "LIBOWFAT_HEADERS=${STAGING_INCDIR}/libowfat "
EXTRA_OEMAKE += "LIBOWFAT_LIBRARY=${STAGING_LIBDIR}/libowfat "

do_install () {
  install -d ${D}${bindir}
  install -m 755 opentracker ${D}${bindir}
}
