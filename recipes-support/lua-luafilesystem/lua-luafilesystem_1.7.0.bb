DESCRIPTION = "LuaFileSystem is a Lua library developed to complement the set \
of functions related to file systems offered by the standard Lua distribution."
HOMEPAGE = "https://github.com/keplerproject/luafilesystem"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=d9b7e441d51a96b17511ee3be5a75857"

SRCREV = "04bdaf9a1eb1406f83321e98daf08b0f06a8401b"
SRC_URI = "git://github.com/keplerproject/luafilesystem.git;protocol=https;branch=master \
           file://0001-Fix-for-OE.patch \
"

S = "${WORKDIR}/git"

inherit pkgconfig

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

FILES_${PN} = "${libdir}"
