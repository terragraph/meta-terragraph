DESCRIPTION = "A LuaDoc-compatible documentation generation system"
HOMEPAGE = "https://github.com/lunarmodules/LDoc"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://COPYRIGHT;md5=4183d221633f1fc26c238fbb9e020e1f"

# Jun 24 2021
SRCREV = "bbd498ab39fa49318b36378430d3cdab571f8ba0"
SRC_URI = "git://github.com/lunarmodules/LDoc.git;protocol=https;branch=master \
           file://fix-new-css.patch \
"

S = "${WORKDIR}/git"

RDEPENDS_${PN} = "lua5.2 lua-penlight"

do_configure () {
	:
}

do_install () {
    luadir="${libdir}/lua/5.2"

    # move 'ldoc' to lua module path
    install -m 0755 -d ${D}${luadir}/
    cp -r ${S}/ldoc ${D}${luadir}/

    # install 'ldoc.lua' to bindir
    install -m 0755 -d ${D}${bindir}/
    install -D -m 0755 ${S}/ldoc.lua ${D}${bindir}/ldoc
}

FILES_${PN} = "${bindir} ${libdir}"
