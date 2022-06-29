DESCRIPTION = "Feature-rich command line parser for Lua"
HOMEPAGE = "https://github.com/luarocks/argparse"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=121fa4cef6513eb48482998ce7805bad"

# NOTE: Using a fork of original: https://github.com/mpeterv/argparse
# The original is no longer maintained.

SRCREV = "27967d7b52295ea7885671af734332038c132837"
SRC_URI = "git://github.com/luarocks/argparse.git;protocol=https;branch=master \
"

S = "${WORKDIR}/git"

inherit lua-minify

RDEPENDS_${PN} = "lua5.2"

do_install () {
    luadir="${libdir}/lua/5.2"

    # move 'argparse.lua' to lua module path
    install -m 0755 -d ${D}${luadir}/
    install -D -m 0755 ${S}/src/argparse.lua ${D}${luadir}/
}

FILES_${PN} = "${libdir}"
