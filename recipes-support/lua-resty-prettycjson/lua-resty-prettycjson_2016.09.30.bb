DESCRIPTION = "Lua cJSON Pretty Formatter"
HOMEPAGE = "https://github.com/bungle/lua-resty-prettycjson"
LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://LICENSE;md5=89c619e8d778c498640104d70143609c"

SRCREV = "f4d3e94e85ed02c7ebba1c05a445531ad9bef52d"
SRC_URI = "git://github.com/bungle/lua-resty-prettycjson.git;protocol=https;branch=master \
           file://fix-encoding-bug.patch \
"

S = "${WORKDIR}/git"

inherit lua-minify

RDEPENDS_${PN} = "lua5.2 lua-cjson"

do_install () {
    luadir="${libdir}/lua/5.2"

    # move 'prettycjson.lua' to lua module path
    install -m 0755 -d ${D}${luadir}/
    install -D -m 0755 ${S}/lib/resty/prettycjson.lua ${D}${luadir}/
}

FILES_${PN} = "${libdir}"
