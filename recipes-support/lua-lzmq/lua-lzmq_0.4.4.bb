DESCRIPTION = "Lua binding to ZeroMQ"
HOMEPAGE = "https://github.com/zeromq/lzmq"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENCE.txt;md5=3d9656bbe55b3d9c26903ed12fb6d4db"

SRCREV = "fe979b59707915a7935b831d41fae7805360fc70"
SRC_URI = "git://github.com/zeromq/lzmq.git;protocol=https;branch=master \
"

S = "${WORKDIR}/git"

inherit cmake lua-minify

DEPENDS = "lua5.2 zeromq"

do_install () {
    cmake_do_install

    luadir="${libdir}/lua/5.2"

    # remove unwanted files
    rm -rf ${D}${datadir}
    rm -rf ${D}${libdir}/lua/lzmq/ffi
    rm -f ${D}${libdir}/lua/lzmq/ffi.lua

    # move 'lua' to 'lua/<VER>'
    install -m 0755 -d ${D}${luadir}/
    mv ${D}${libdir}/lua/lzmq ${D}${luadir}/
    mv ${D}${libdir}/lua/lzmq.so ${D}${luadir}/
}

FILES_${PN} = "${libdir}"
