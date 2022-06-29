SUMMARY = "Facebook API Service Thrift Interface"
DESCRIPTION = "Facebook API Service Thrift Interface"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${WORKDIR}/LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

FILESEXTRAPATHS_append := ":${META_TERRAGRAPH_DIR}/src/terragraph-api"

SRC_URI = "file://src/if \
           file://LICENSE \
          "

S = "${WORKDIR}/src/if"

DEPENDS = "fbthrift fbthrift-compiler-native"

inherit cmake tgdev
