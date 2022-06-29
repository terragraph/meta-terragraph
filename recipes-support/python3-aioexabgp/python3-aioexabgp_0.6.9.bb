DESCRIPTION = "Exabgp asyncio API client base library"
HOMEPAGE = "https://github.com/cooperlees/aioexabgp"
SECTION = "neworking"
LICENSE = "BSD"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ffabb2a9ad2054c9114c20625e2bfc10"

SRCREV = "096777146b7cee862a220b390dc182b80e405952"
SRC_URI = "git://github.com/cooperlees/aioexabgp.git;protocol=https;branch=master \
           file://Auto-advertisement-of-dynamically-changing-prefixes.patch \
           "

S = "${WORKDIR}/git"

inherit setuptools3

do_install_append() {
  install -m 0755 -d ${D}${sysconfdir}/exabgp
  install -D -m 0644 ${S}/aioexabgp/announcer/sample_announcer.json ${D}${sysconfdir}/exabgp/aioexabgp.template
}

RDEPENDS_${PN} += " \
  python3-asyncio \
  python3-core \
  python3-datetime \
  python3-json \
  python3-typing \
  "
