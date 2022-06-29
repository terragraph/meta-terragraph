DESCRIPTION = "A tool for linting and static analysis of Lua code"
HOMEPAGE = "https://github.com/mpeterv/luacheck"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=78c1d9d1063a2e3c98965716ac8494f0"

SRCREV = "7360cfb4cf2c7dd8c73adf45e31a04811a745250"
SRC_URI = "git://github.com/mpeterv/luacheck.git;protocol=https;branch=master"

S = "${WORKDIR}/git"

RDEPENDS_${PN} = "lua5.2 lua-argparse lua-luafilesystem"

do_install () {
    luadir="${libdir}/lua/5.2"

    # move 'luacheck' to lua module path
    install -m 0755 -d ${D}${luadir}/
    cp -r ${S}/src/luacheck ${D}${luadir}/

    # install 'bin/luacheck.lua' to bindir
    install -m 0755 -d ${D}${bindir}/
    install -D -m 0755 ${S}/bin/luacheck.lua ${D}${bindir}/luacheck
}

FILES_${PN} = "${bindir} ${libdir}"
