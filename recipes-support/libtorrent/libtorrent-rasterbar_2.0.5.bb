DESCRIPTION = "An embeddable library for bittorrent clients"
HOMEPAGE = "http://www.libtorrent.org"
LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://LICENSE;md5=a4256be0ec4fe8b82bce76871bf99393"

SRC_URI = " \
	https://github.com/arvidn/libtorrent/releases/download/v${PV}/${BP}.tar.gz \
"

SRC_URI[md5sum] = "98e0464cabbf73f56bfbfe6dfaaef4c0"
SRC_URI[sha256sum] = "e965c2e53170c61c0db3a2d898a61769cb7acd541bbf157cbbef97a185930ea5"

S = "${WORKDIR}/${BP}"

inherit cmake

DEPENDS += "boost openssl"

EXTRA_OECMAKE_append = " -DBUILD_SHARED_LIBS=ON"
EXTRA_OECMAKE_append = " -DCMAKE_CXX_STANDARD=17"
