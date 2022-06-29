DESCRIPTION = "Compatibility module providing Lua-5.2-style APIs for Lua 5.1"
HOMEPAGE = "https://github.com/keplerproject/lua-compat-5.2"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=d3bf3c07c9585e444c8010120cf66618"

SRCREV = "c164c8f339b95451b572d6b4b4d11e944dc7169d"
SRC_URI = "git://github.com/keplerproject/lua-compat-5.2.git;protocol=https;branch=master \
           file://0001-Makefile.patch \
"

S = "${WORKDIR}/git"

inherit pkgconfig lua-minify

DEPENDS = "lua5.2"

EXTRA_OEMAKE = 'LUAVER="5.2" PREFIX=${D}/usr CROSS_COMPILE=${TARGET_PREFIX} CC="${CC} -fpic" LDFLAGS="${LDFLAGS}"'

do_configure () {
	:
}

do_compile () {
	oe_runmake
}

do_install () {
	oe_runmake install
}

do_install_append () {
    luadir="${libdir}/lua/5.2"

    # move 'compat52' modules to lua module path
    install -m 0755 -d ${D}${luadir}/
    install -D -m 0755 ${S}/compat52.lua ${D}${luadir}/
    install -m 0755 -d ${D}${luadir}/compat52/
    install -D -m 0755 ${S}/compat52/*.lua ${D}${luadir}/compat52/
}

FILES_${PN} = "${libdir}"
