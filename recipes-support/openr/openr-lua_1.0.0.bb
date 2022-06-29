DESCRIPTION = "Facebook Open/R routing libraries and tools."
HOMEPAGE = "https://github.com/facebook/openr"
SECTION = "core"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

include openr_src.inc

S = "${WORKDIR}/git"

inherit lua-minify

DEPENDS = "thrift-native openr fbzmq"

RDEPENDS_${PN} = "thrift-lua fbzmq-lua"

do_configure () {
        :
}

do_compile () {
    THRIFTCMD="thrift --gen lua -I ${S} \
    -I ${STAGING_DIR_TARGET}/usr/include/thrift-files \
    -I ${STAGING_DIR_TARGET}/usr/include/"

        $THRIFTCMD "${S}/openr/if/BgpConfig.thrift"
        $THRIFTCMD "${S}/openr/if/KvStore.thrift"
        $THRIFTCMD "${S}/openr/if/Network.thrift"
        $THRIFTCMD "${S}/openr/if/OpenrConfig.thrift"
        $THRIFTCMD "${S}/openr/if/OpenrCtrl.thrift"
    # NOTE: omitting OpenrCtrlCpp.thrift
        $THRIFTCMD "${S}/openr/if/Platform.thrift"
        $THRIFTCMD "${S}/openr/if/Types.thrift"
}

do_install () {
    luadir="${libdir}/lua/5.2"

    # move Thrift generated code to lua module path
    install -m 0755 -d ${D}${luadir}/openr/
    for f in ${S}/gen-lua/openr.*.lua; do
        filename="$(basename $f)"
        filename=${filename#openr.}
        install -m 0755 "${f}" "${D}${luadir}/openr/${filename}"
    done
}

FILES_${PN} = "${libdir}"
