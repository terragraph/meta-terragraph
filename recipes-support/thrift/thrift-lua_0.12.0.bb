DESCRIPTION = "Apache Thrift Lua Libraries"
HOMEPAGE = "https://thrift.apache.org/"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=394465e125cffc0f133695ed43f14047 \
                    file://NOTICE;md5=42748ae4646b45fbfa5182807321fb6c"

SRC_URI = "https://archive.apache.org/dist/thrift/${PV}/thrift-${PV}.tar.gz \
           file://fix-lua-TCompactProtocol-readMapBegin.patch \
           file://fbthrift-TCompactProtocol-big-endian-doubles.patch \
           file://fix-lua-TCompactProtocol-readVarint64.patch \
           file://delete-print-in-TCompactProtocol-writeBool.patch \
           file://fix-lua-TCompactProtocol-readBool.patch \
           file://fix-lua-TCompactProtocol-writeStruct.patch \
           file://luasocket-ipv6-client-support.patch \
"
SRC_URI[md5sum] = "3deebbb4d1ca77dd9c9e009a1ea02183"
SRC_URI[sha256sum] = "c336099532b765a6815173f62df0ed897528a9d551837d627c1f87fadad90428"

DEPENDS = "thrift-native boost flex-native bison-native openssl lua5.2 lua5.2-native"

inherit pkgconfig autotools-brokensep lua-minify

S = "${WORKDIR}/thrift-${PV}"

EXTRA_OECONF = " \
    --with-lua="yes" \
    --enable-libs="yes" \
    --enable-tests="no" \
    --enable-tutorial="no" \
    --with-as3="no" \
    --with-cpp="no" \
    --with-c-glib="no" \
    --with-cl="no" \
    --with-java="no" \
    --with-csharp="no" \
    --with-python="no" \
    --with-py3="no" \
    --with-ruby="no" \
    --with-haskell="no" \
    --with-haxe="no" \
    --with-netstd="no" \
    --with-perl="no" \
    --with-php="no" \
    --with-php-extension="no" \
    --with-dart="no" \
    --with-erlang="no" \
    --with-go="no" \
    --with-d="no" \
    --with-nodejs="no" \
    --with-nodets="no" \
    --with-rs="no" \
    --with-swift="no" \
    --with-boost="no" \
    --with-c_glib="no" \
    --with-libevent="no" \
    --with-qt4="no" \
    --with-qt5="no" \
"

do_configure_prepend () {
    cd ${S}
    ./bootstrap.sh
}

do_configure () {
    oe_runconf
}

do_install () {
    autotools_do_install

    luadir="${libdir}/lua/5.2"

    # install lua libs
    install -m 0755 -d ${D}${luadir}/
    install -D -m 0755 ${S}/lib/lua/*.lua ${D}${luadir}/

    # install lua shared objects
    for f in ${D}${libdir}/liblua*.so; do
        filename="$(basename $f)"
        ln -sf "../../${filename}.0" "${D}${luadir}/${filename}"
    done

    # remove 'thrift' binary
    rm -rf ${D}${bindir}
}

FILES_${PN} = "${libdir}"
INSANE_SKIP_${PN} += "dev-so dev-deps"

BBCLASSEXTEND = "native nativesdk"
