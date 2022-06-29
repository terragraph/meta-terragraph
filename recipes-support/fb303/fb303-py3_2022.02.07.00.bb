DESCRIPTION = "Facebook fb303 Python Modules"
HOMEPAGE = "https://github.com/facebook/fb303"
SECTION = "core"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=2ee41112a44fe7014dce33e26468ba93"

SRCREV = "5a28f8792d77689302ecdae75b71afaf233b8f4c"
SRC_URI = "git://github.com/facebook/fb303.git;protocol=https;branch=main"

S = "${WORKDIR}/git"
B = "${WORKDIR}/build"

DEPENDS += " \
    fbthrift-compiler-native \ 
    fbthrift-py3 \
    python3-native \
"

RDEPENDS_${PN} += " \
    python3-async \
    python3-threading \
    python3-six \
"

do_compile() {
    THRIFTCMD="thrift1 --gen py --out ${B}"
    $THRIFTCMD ${S}/fb303/thrift/fb303_core.thrift
}
    
do_install() {
    pydir=${D}/usr/lib/python3.8/site-packages/fb303_core
    install -m 0755 -d ${pydir}

    # Install thrift-generated python3 
    cd ${B}/fb303_core
    files=$(find . -type f)
    for f in $files; do
        install -m 0755 "${f}" ${pydir}/${f}
    done
}

FILES_${PN} += "${libdir}"

BBCLASSEXTEND = "native nativesdk"
