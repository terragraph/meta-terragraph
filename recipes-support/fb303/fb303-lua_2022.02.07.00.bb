DESCRIPTION = "Facebook fb303 Lua"
HOMEPAGE = "https://github.com/facebook/fb303"
SECTION = "core"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=2ee41112a44fe7014dce33e26468ba93"

SRCREV = "5a28f8792d77689302ecdae75b71afaf233b8f4c"
SRC_URI = "git://github.com/facebook/fb303.git;protocol=https;branch=main \
           file://Add-lua-namespace-to-fb303-thrift.patch \
          "

S = "${WORKDIR}/git"

inherit lua-minify

DEPENDS += "fb303 thrift-native"

do_configure () {
    :
}

do_compile () {
    thrift --gen lua "${S}/fb303/thrift/fb303_core.thrift"
}

do_install () {
    luadir="${libdir}/lua/5.2"

    # move Thrift generated code to lua module path
    install -m 0755 -d ${D}${luadir}/fb303/
    for f in ${S}/gen-lua/fb303.*.lua; do
        filename="$(basename $f)"
        filename=${filename#fb303.}
        install -m 0755 "${f}" "${D}${luadir}/fb303/${filename}"
    done
}

FILES_${PN} += "${libdir}"
