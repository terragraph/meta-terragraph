DESCRIPTION = "Facebook Fizz"
HOMEPAGE = "https://github.com/facebookincubator/fizz"
SECTION = "core"
LICENSE = "BSD"
LIC_FILES_CHKSUM = "file://${S}/../LICENSE;md5=dc30e04d5f7401819dca09f17c682e51"

SRCREV = "a17f834c956e5100c652dc2d738cff6e87adc502"
SRC_URI = "git://github.com/facebookincubator/fizz;protocol=https;branch=main \
           file://remove-references-to-recipe-sysroot-from-INTERFACE.patch;striplevel=2 \
          "

DEPENDS = "folly libsodium"

inherit cmake

S = "${WORKDIR}/git/fizz"

FILES_${PN}-dbg = "${libdir}/libfizz_test_support.so*"
FILES_SOLIBSDEV = "${libdir}/libfizz.so ${libdir}/libfizz.so.1 "

BBCLASSEXTEND = "native nativesdk"

EXTRA_OECMAKE_append = " -DBUILD_SHARED_LIBS=ON"
EXTRA_OECMAKE_append = " -DCMAKE_SKIP_RPATH=TRUE"
EXTRA_OECMAKE_append = " -DCMAKE_CXX_STANDARD=17"
EXTRA_OECMAKE_append = " -DBUILD_EXAMPLES=OFF"
