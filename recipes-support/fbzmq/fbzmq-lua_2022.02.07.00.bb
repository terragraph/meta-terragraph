DESCRIPTION = "Async framework and C++ wrapper over libzmq"
HOMEPAGE = "https://github.com/facebookincubator/fbzmq"
SECTION = "core"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=57a79768a3b1e4d0f3a3787048a96356"

include fbzmq_src.inc

SRC_URI += "file://lua-thrift-interface.patch"

S = "${WORKDIR}/git"

inherit lua-minify

DEPENDS = "thrift-native fbzmq"

do_configure () {
    :
}

do_compile () {
    thrift --gen lua "${S}/fbzmq/service/if/Monitor.thrift"
}

do_install () {
    luadir="${libdir}/lua/5.2"

    # move Thrift generated code to lua module path
    install -m 0755 -d ${D}${luadir}/fbzmq/
    install -D -m 0755 ${S}/gen-lua/fbzmq.Monitor_ttypes.lua ${D}${luadir}/fbzmq/Monitor_ttypes.lua
    install -D -m 0755 ${S}/gen-lua/fbzmq.Monitor_constants.lua ${D}${luadir}/fbzmq/Monitor_constants.lua
}

FILES_${PN} = "${libdir}"
