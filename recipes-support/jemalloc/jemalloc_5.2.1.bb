DESCRIPTION = "Awesome memory management library"
HOMEPAGE = "https://github.com/jemalloc"

LICENSE = "BSD"
LIC_FILES_CHKSUM = "file://COPYING;md5=ea061f8731d5e6a5761dfad951ef5f5f"

SRC_URI = "https://github.com/jemalloc/jemalloc/releases/download/${PV}/jemalloc-${PV}.tar.bz2 \
           "

SRC_URI[md5sum] = "3d41fbf006e6ebffd489bdb304d009ae"
SRC_URI[sha256sum] = "34330e5ce276099e2e8950d9335db5a875689a4c6a56751ef3b1d8c537f887f6"

inherit autotools

S = "${WORKDIR}/jemalloc-${PV}"
B = "${S}"

DEPENDS = "libunwind"

do_configure () {
  ${S}/configure --prefix=/usr \
                 --enable-prof \
                 --enable-prof-libunwind \
                 --htmldir=${datadir}/html \
                 --host="${TARGET_ARCH}-linux"
}

FILES_${PN} += " *.so*"

BBCLASSEXTEND = "native nativesdk"
