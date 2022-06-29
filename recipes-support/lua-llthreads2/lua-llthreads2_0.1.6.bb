DESCRIPTION = "Low-Level threads(pthreads or WIN32 threads) for Lua"
HOMEPAGE = "https://github.com/moteus/lua-llthreads2"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=dd3e6b98ba10c848c0688b32f8430339"

SRCREV = "fe0789496a6acec04421e2a86caf437d98f1e178"
SRC_URI = "git://github.com/moteus/lua-llthreads2.git;protocol=https;branch=master \
"

S = "${WORKDIR}/git"

inherit cmake lua-minify

DEPENDS = "lua5.2"

do_install () {
    cmake_do_install

    luadir="${libdir}/lua/5.2"

    # move 'lua' to 'lua/<VER>'
    install -m 0755 -d ${D}${luadir}/
    mv ${D}${libdir}/lua/llthreads2 ${D}${luadir}/
    # install as llthreads replacement (i.e. lua-llthreads2-compat)
    mv ${D}${libdir}/lua/llthreads2.so ${D}${luadir}/llthreads.so
}

FILES_${PN} = "${libdir}"
FILES_${PN}-dev = "${datadir}"
