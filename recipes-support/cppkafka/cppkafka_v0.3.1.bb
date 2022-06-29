DESCRIPTION = "Modern C++ Apache Kafka client library (wrapper for librdkafka)"
HOMEPAGE = "https://github.com/mfontanini/cppkafka"

LICENSE = "BSD"
LIC_FILES_CHKSUM = "file://LICENSE;md5=d8b4ca15d239dc1485ef495c8f1bcc72"

# v0.3.1
SRCREV = "872ee0442b1a5009064379d4b89821a319c1bbbb"
SRC_URI = "git://github.com/mfontanini/cppkafka.git;protocol=https;branch=master \
           file://0001-remove_include_system.patch \
           "

S = "${WORKDIR}/git"

inherit cmake

DEPENDS += "librdkafka boost"

EXTRA_OECMAKE_append = " -DRDKAFKA_INCLUDE_DIR=${STAGING_INCDIR}/librdkafka"

BBCLASSEXTEND = "native nativesdk"
