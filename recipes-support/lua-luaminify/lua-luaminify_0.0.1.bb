DESCRIPTION = "Lua source code minifier"
HOMEPAGE = "https://github.com/oyooyo/LuaMinify"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE.md;md5=77afe6e8e783bbdfb5ed4028aeb9e39d"

# NOTE: Using a fork of original: https://github.com/stravant/LuaMinify

SRCREV = "8406394585457ce24fc6a921d22fa44912e26b6d"
SRC_URI = "git://github.com/oyooyo/LuaMinify.git;protocol=https;branch=master \
           file://add-main-function.patch \
"

S = "${WORKDIR}/git"

RDEPENDS_${PN} = "lua5.2"

do_install () {
    # install 'Minify.lua' to bindir
    install -m 0755 -d ${D}${bindir}/
    install -D -m 0755 ${S}/RobloxPlugin/Minify.lua ${D}${bindir}/lua-minify
}

FILES_${PN} = "${bindir}"

BBCLASSEXTEND = "native"
