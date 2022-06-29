DESCRIPTION = "Facebook Open/R routing libraries and tools."
HOMEPAGE = "https://github.com/facebook/openr"
SECTION = "core"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

include openr_src.inc

S = "${WORKDIR}/git"

DEPENDS += "fbthrift-compiler-native fb303 fbzmq fbthrift fb303-py3"

RDEPENDS_${PN} += " \
    python3-bunch \
    python3-jsondiff \
    python3-six \
    python3-tabulate \
    fb303-py3 \
    "

do_configure () {
    :
}

do_compile () {
    THRIFTCMD="thrift1 --gen py -I ${S} \
        -I ${STAGING_DIR_TARGET}/usr/include/thrift-files \
        -I ${STAGING_DIR_TARGET}/usr/include/"

        $THRIFTCMD "${S}/openr/if/BgpConfig.thrift"
        $THRIFTCMD "${S}/openr/if/KvStore.thrift"
        $THRIFTCMD "${S}/openr/if/Network.thrift"
        $THRIFTCMD "${S}/openr/if/OpenrConfig.thrift"
        $THRIFTCMD "${S}/openr/if/OpenrCtrl.thrift"
        $THRIFTCMD "${S}/openr/if/OpenrCtrlCpp.thrift"
        $THRIFTCMD "${S}/openr/if/Platform.thrift"
        $THRIFTCMD "${S}/openr/if/Types.thrift"
        $THRIFTCMD "${S}/configerator/structs/neteng/config/routing_policy.thrift"
        $THRIFTCMD "${S}/configerator/structs/neteng/config/vip_service_config.thrift"
}

OPENR_PY3_MODS = "cli clients utils"

do_install () {
    pydir="${libdir}/python3.8/site-packages"

    # Move Thrift generated code to openr python module in site-packages
    install -m 0755 -d ${D}${pydir}/openr/
    files=$(find ./gen-py -type f)
    for f in $files; do
        f="$( echo ${f} | sed 's/\/gen-py//' )"
        folder="$( dirname ${f} )"
        install -m 0755 -d ${D}${pydir}/${folder}
        install -m 0755 "${S}/gen-py/${f}" "${D}${pydir}/${f}"
    done

    # Install Open/R pure-python modules in site-packages
    for mod in ${OPENR_PY3_MODS}; do
        install -m 0755 -d ${D}${pydir}/openr/${mod}
        files=$(find ./openr/py/openr/${mod} -type f)
        for f in $files; do
            f="$( echo ${f} | sed 's/\/openr\/py//' )"
            folder="$( dirname ${f} )"
            install -m 0755 -d ${D}${pydir}/${folder}
            install -m 0755 "${S}/openr/py/${f}" "${D}${pydir}/${f}"
        done
    done
}

FILES_${PN} += "${libdir}"
