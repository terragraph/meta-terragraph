DESCRIPTION = "Lua CJSON is a fast JSON encoding/parsing module for Lua"
HOMEPAGE = "https://www.kyne.com.au/~mark/software/lua-cjson.php"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=b1fee3afe4f4a4b26c13016123b2d08a"

SRCREV = "e8972ac754788d3ef10a57a36016d6c3e85ba20d"
SRC_URI = "git://github.com/mpx/lua-cjson.git;protocol=https;branch=master \
           file://set-cmake-dest-dir.patch \
           file://do-not-escape-slash.patch \
           file://encode-sort-objects-by-key.patch \
"

S = "${WORKDIR}/git"

inherit cmake

DEPENDS = "lua5.2"

do_install_append () {
    luadir="${libdir}/lua/5.2"

    # move 'cjson.so' to lua module path
    install -m 0755 -d ${D}${luadir}/
    mv ${D}${prefix}/cjson.so ${D}${luadir}/
}

FILES_${PN} = "${libdir}"
