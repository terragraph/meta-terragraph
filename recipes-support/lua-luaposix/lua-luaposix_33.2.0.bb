DESCRIPTION = "Lua bindings for POSIX APIs"
HOMEPAGE = "https://github.com/luaposix/luaposix"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://COPYING;md5=0dedf8962dbce44c66c3312ccee647d0"

# The release versions contain generated automake scripts which aren't present in the
# original git source and are non-trivial to generate. The release tarball is available
# from github, but that source causes a qa[build-deps] warning from yocto since github
# archive urls are unstable. Instead, we can fetch the source from luarocks.org; note
# however that this comes as a .rock file, which is a zip within a zip. This requires
# a custom unpack step and naturally changes the hashes.
SRC_URI = "https://luarocks.org/manifests/gvvaughan/luaposix-${PV}-1.src.rock;downloadfilename=luaposix-${PV}.src.rock.zip"
SRC_URI[md5sum] = "4c79987abb66090f2e4efcff5eed209c"
SRC_URI[sha256sum] = "d4d9219ac9e9a28f0d6316c53a5c0b4011281ec9ebaf7e6acf93f0e8b35c2d5f"

# NOTE: We are using this older version because:
#   version 34.0 replaces autotools with "luke", which does not work with Yocto
#   versions [33.2.1, 34.0) contain a configure bug when creating 'luadir' and
#     'luaexecdir' variables, which causes bogus install paths

S = "${WORKDIR}/luaposix-release-v${PV}"

python do_unpack() {
    bb.build.exec_func('base_do_unpack', d)
    src_uri = d.getVar('SRC_URI', False)
    d.setVar('SRC_URI', 'file://${WORKDIR}/release-v${PV}.zip')
    bb.build.exec_func('base_do_unpack', d)
    d.setVar('SRC_URI', src_uri)
}
do_unpack[depends] += "unzip-native:do_populate_sysroot"

inherit autotools pkgconfig lua-minify

DEPENDS = "lua5.2 lua5.2-native ncurses"

do_install_append () {
    luadir="${libdir}/lua/5.2"

    # move *.lua files from datadir to libdir for consistency
    install -m 0755 -d ${D}${luadir}/
    mv ${D}${datadir}/lua/5.2/* ${D}${luadir}/
    rmdir ${D}${datadir}/lua/5.2
    rmdir ${D}${datadir}/lua
}

FILES_${PN} = "${libdir}"

BBCLASSEXTEND = "native"
